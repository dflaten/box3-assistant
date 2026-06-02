#include "timer/timer_command_handler.h"

#include <stdio.h>
#include <stdlib.h>

#include "freertos/task.h"

#include "assistant/followup_audio.h"
#include "assistant/watchdog.h"
#include "board/ui_status.h"
#include "stt/local_stt_client.h"
#include "timer/timer_parse.h"
#include "timer/timer_runtime.h"

#define TIMER_HEARTBEAT_SLEEP_SLICE_MS    250
#define TIMER_FOLLOWUP_SETTLE_DELAY_MS    250
#define TIMER_AUDIO_PAUSE_WAIT_TIMEOUT_MS 500
#define TIMER_FOLLOWUP_SAMPLE_RATE_HZ     16000

static esp_err_t timer_command_execute(const assistant_command_context_t *context,
                                       const assistant_command_dispatch_t *dispatch,
                                       assistant_command_result_t *out_result);
static uint32_t monotonic_ms_now(void);

/**
 * @brief Get the timer feature command handler registration.
 * @return Pointer to the static timer command handler descriptor.
 */
const assistant_command_handler_t *timer_command_handler_get(void) {
    static const assistant_command_handler_t handler = {
        .action = ASSISTANT_COMMAND_ACTION_SET_TIMER,
        .execute = timer_command_execute,
    };

    return &handler;
}

/**
 * @brief Read monotonic uptime in milliseconds.
 * @return Milliseconds since boot truncated to 32 bits.
 */
static uint32_t monotonic_ms_now(void) {
    return (uint32_t) pdTICKS_TO_MS(xTaskGetTickCount());
}

/**
 * @brief Execute timer-specific command actions.
 * @param context Assistant command context for the active command.
 * @param dispatch Resolved dispatch metadata describing the timer action.
 * @param out_result Result structure to populate for assistant core.
 * @return ESP_OK after handling the command result structure, or an ESP error code on invalid input.
 */
static esp_err_t timer_command_execute(const assistant_command_context_t *context,
                                       const assistant_command_dispatch_t *dispatch,
                                       assistant_command_result_t *out_result) {
    if (context == NULL || context->runtime == NULL || dispatch == NULL || out_result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    assistant_runtime_t *rt = context->runtime;
    out_result->timeout_label = "Command";

    if (dispatch->type == ASSISTANT_COMMAND_ACTION_STOP) {
        if (!timer_runtime_stop(&rt->timer)) {
            snprintf(out_result->detail, sizeof(out_result->detail), "No timer is active");
            out_result->err = ESP_ERR_INVALID_STATE;
            return ESP_OK;
        }

        rt->direct_command_mode = false;
        rt->direct_command_prepared = false;
        snprintf(out_result->detail, sizeof(out_result->detail), "Timer stopped");
        out_result->err = ESP_OK;
        return ESP_OK;
    }

    if (dispatch->type != ASSISTANT_COMMAND_ACTION_SET_TIMER) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Timer command unavailable");
        out_result->err = ESP_ERR_NOT_SUPPORTED;
        return ESP_OK;
    }

    if (!local_stt_client_is_configured()) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Timer voice service unavailable");
        out_result->err = ESP_ERR_NOT_SUPPORTED;
        return ESP_OK;
    }

    ui_status_set(UI_STATUS_LISTENING, "Say duration now");
    assistant_watchdog_sleep_with_heartbeat(
        rt, pdMS_TO_TICKS(TIMER_FOLLOWUP_SETTLE_DELAY_MS), pdMS_TO_TICKS(TIMER_HEARTBEAT_SLEEP_SLICE_MS));

    TickType_t pause_wait_start = xTaskGetTickCount();
    while (!rt->audio_feed_paused) {
        if ((xTaskGetTickCount() - pause_wait_start) >= pdMS_TO_TICKS(TIMER_AUDIO_PAUSE_WAIT_TIMEOUT_MS)) {
            snprintf(out_result->detail, sizeof(out_result->detail), "Timer audio capture failed");
            out_result->err = ESP_ERR_TIMEOUT;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    uint8_t *pcm = NULL;
    size_t pcm_size = 0;
    esp_err_t err = assistant_followup_audio_capture_mono(
        rt, CONFIG_LOCAL_STT_CAPTURE_MS, TIMER_FOLLOWUP_SAMPLE_RATE_HZ, &pcm, &pcm_size);
    if (err != ESP_OK) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Timer audio capture failed");
        out_result->err = err;
        return ESP_OK;
    }

    ui_status_set(UI_STATUS_WORKING, "Understanding timer");
    char transcript[96];
    err =
        local_stt_client_transcribe(pcm, pcm_size, TIMER_FOLLOWUP_SAMPLE_RATE_HZ, 2, 1, transcript, sizeof(transcript));
    free(pcm);
    if (err != ESP_OK) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Timer voice service unavailable");
        out_result->err = err;
        return ESP_OK;
    }

    uint32_t duration_seconds = 0;
    if (!timer_parse_duration_text(transcript, CONFIG_TIMER_MAX_DURATION_SECONDS, &duration_seconds)) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Could not understand timer");
        out_result->err = ESP_ERR_INVALID_ARG;
        return ESP_OK;
    }

    timer_runtime_start(&rt->timer, duration_seconds, monotonic_ms_now());
    rt->direct_command_mode = false;
    rt->direct_command_prepared = false;
    timer_runtime_format_remaining(&rt->timer, monotonic_ms_now(), out_result->detail, sizeof(out_result->detail));
    out_result->err = ESP_OK;
    return ESP_OK;
}
