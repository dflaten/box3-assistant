#include "time/time_command_handler.h"

#include <stdio.h>
#include <stdlib.h>

#include "freertos/task.h"

#include "esp_log.h"

#include "assistant/followup_audio.h"
#include "assistant/watchdog.h"
#include "board/ui_status.h"
#include "stt/local_stt_client.h"
#include "system/time_support.h"
#include "time/time_client.h"
#include "time/time_format.h"
#include "tts/tts_player.h"

#define TIME_HEARTBEAT_SLEEP_SLICE_MS    250
#define TIME_FOLLOWUP_SETTLE_DELAY_MS    250
#define TIME_AUDIO_PAUSE_WAIT_TIMEOUT_MS 500
#define TIME_FOLLOWUP_SAMPLE_RATE_HZ     16000

static const char *TAG = "assistant-time";

static esp_err_t time_command_execute(const assistant_command_context_t *context,
                                      const assistant_command_dispatch_t *dispatch,
                                      assistant_command_result_t *out_result);

/**
 * @brief Get the time feature command handler registration.
 * @return Pointer to the static time command handler descriptor.
 */
const assistant_command_handler_t *time_command_handler_get(void) {
    static const assistant_command_handler_t handler = {
        .action = ASSISTANT_COMMAND_ACTION_TIME_NOW,
        .execute = time_command_execute,
    };

    return &handler;
}

/**
 * @brief Populate the result for the configured home time.
 * @param out_result Command result to populate.
 * @return ESP_OK after populating the result fields.
 */
static esp_err_t handle_home_time(assistant_command_result_t *out_result) {
    char time_text[TIME_TIME_TEXT_LEN];
    char date_text[TIME_DATE_TEXT_LEN];
    if (!time_support_format_now(time_text, sizeof(time_text), date_text, sizeof(date_text))) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Time is still syncing");
        out_result->err = ESP_ERR_INVALID_STATE;
        return ESP_OK;
    }

    time_report_t report = {0};
    snprintf(report.location, sizeof(report.location), "%s", CONFIG_ASSISTANT_HOME_LOCATION_NAME);
    snprintf(report.timezone, sizeof(report.timezone), "%s", CONFIG_ASSISTANT_HOME_TIMEZONE);
    snprintf(report.time_text, sizeof(report.time_text), "%s", time_text);
    snprintf(report.date_text, sizeof(report.date_text), "%s", date_text);
    time_format_detail(&report, out_result->detail, sizeof(out_result->detail));

    char spoken[TIME_SPOKEN_TEXT_LEN];
    time_format_spoken(&report, spoken, sizeof(spoken));
    if (tts_player_is_configured()) {
        esp_err_t tts_err = tts_player_speak(spoken);
        if (tts_err != ESP_OK) {
            ESP_LOGW(TAG, "Time speech playback failed: %s", esp_err_to_name(tts_err));
        }
    }

    out_result->hold_time = 0;
    out_result->status_rendered = true;
    out_result->err = ESP_OK;
    return ESP_OK;
}

/**
 * @brief Execute time-specific command actions.
 * @param context Assistant command context for the active command.
 * @param dispatch Resolved dispatch metadata describing the time action.
 * @param out_result Result structure to populate for assistant core.
 * @return ESP_OK after handling the command result structure, or an ESP error code on invalid input.
 */
static esp_err_t time_command_execute(const assistant_command_context_t *context,
                                      const assistant_command_dispatch_t *dispatch,
                                      assistant_command_result_t *out_result) {
    if (context == NULL || context->runtime == NULL || dispatch == NULL || out_result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    assistant_runtime_t *rt = context->runtime;
    out_result->timeout_label = "Time";

    if (dispatch->type == ASSISTANT_COMMAND_ACTION_TIME_NOW) {
        return handle_home_time(out_result);
    }
    if (dispatch->type != ASSISTANT_COMMAND_ACTION_TIME_IN_LOCATION) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Time command unavailable");
        out_result->err = ESP_ERR_NOT_SUPPORTED;
        return ESP_OK;
    }

    if (!local_stt_client_is_configured()) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Time voice service unavailable");
        out_result->err = ESP_ERR_NOT_SUPPORTED;
        return ESP_OK;
    }

    ui_status_set(UI_STATUS_LISTENING, "Say city now");
    assistant_watchdog_sleep_with_heartbeat(
        rt, pdMS_TO_TICKS(TIME_FOLLOWUP_SETTLE_DELAY_MS), pdMS_TO_TICKS(TIME_HEARTBEAT_SLEEP_SLICE_MS));

    TickType_t pause_wait_start = xTaskGetTickCount();
    while (!rt->audio_feed_paused) {
        if ((xTaskGetTickCount() - pause_wait_start) >= pdMS_TO_TICKS(TIME_AUDIO_PAUSE_WAIT_TIMEOUT_MS)) {
            snprintf(out_result->detail, sizeof(out_result->detail), "Time audio capture failed");
            out_result->err = ESP_ERR_TIMEOUT;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    uint8_t *pcm = NULL;
    size_t pcm_size = 0;
    esp_err_t err = assistant_followup_audio_capture_mono(
        rt, CONFIG_LOCAL_STT_CAPTURE_MS, TIME_FOLLOWUP_SAMPLE_RATE_HZ, &pcm, &pcm_size);
    if (err != ESP_OK) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Time audio capture failed");
        out_result->err = err;
        return ESP_OK;
    }

    ui_status_set(UI_STATUS_WORKING, "Understanding city");
    char transcript[TIME_QUERY_TEXT_LEN];
    err =
        local_stt_client_transcribe(pcm, pcm_size, TIME_FOLLOWUP_SAMPLE_RATE_HZ, 2, 1, transcript, sizeof(transcript));
    free(pcm);
    if (err != ESP_OK) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Time voice service unavailable");
        out_result->err = err;
        return ESP_OK;
    }

    char query[TIME_QUERY_TEXT_LEN];
    if (!time_format_normalize_location_query(transcript, query, sizeof(query))) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Could not understand city");
        out_result->err = ESP_ERR_INVALID_ARG;
        return ESP_OK;
    }

    ui_status_set(UI_STATUS_WORKING, "Checking time");
    time_report_t report = {0};
    err = time_client_fetch_location_time(query, &report);
    if (err == ESP_OK) {
        time_format_detail(&report, out_result->detail, sizeof(out_result->detail));
        char spoken[TIME_SPOKEN_TEXT_LEN];
        time_format_spoken(&report, spoken, sizeof(spoken));
        if (tts_player_is_configured()) {
            esp_err_t tts_err = tts_player_speak(spoken);
            if (tts_err != ESP_OK) {
                ESP_LOGW(TAG, "Time speech playback failed: %s", esp_err_to_name(tts_err));
            }
        }
        out_result->hold_time = 0;
        out_result->status_rendered = true;
    } else if (err == ESP_ERR_NOT_FOUND) {
        snprintf(out_result->detail, sizeof(out_result->detail), "City not found");
    } else if (err == ESP_ERR_INVALID_STATE) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Time unavailable");
    } else {
        snprintf(out_result->detail, sizeof(out_result->detail), "Time lookup failed");
    }

    out_result->err = err;
    return ESP_OK;
}
