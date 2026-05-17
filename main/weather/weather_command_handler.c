#include "weather/weather_command_handler.h"

#include <stdio.h>

#include "freertos/task.h"

#include "esp_log.h"
#include "esp_http_client.h"

#include "board/ui_status.h"
#include "tts/tts_player.h"
#include "weather/weather_client.h"

#define WEATHER_STATUS_HOLD_MS 30000

static const char *TAG = "assistant-weather";

static esp_err_t weather_command_execute(const assistant_command_context_t *context,
                                         const assistant_command_dispatch_t *dispatch,
                                         assistant_command_result_t *out_result);

/**
 * @brief Build the short loading text shown while waiting for the weather provider.
 * @param detail Destination buffer for the user-facing loading text.
 * @param detail_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
static void format_weather_loading_detail(char *detail, size_t detail_size) {
    if (detail == NULL || detail_size == 0) {
        return;
    }

    snprintf(detail, detail_size, "Checking %s", weather_client_provider_name());
}

/**
 * @brief Get the weather feature command handler registration.
 * @return Pointer to the static weather command handler descriptor.
 */
const assistant_command_handler_t *weather_command_handler_get(void) {
    static const assistant_command_handler_t handler = {
        .action = ASSISTANT_COMMAND_ACTION_WEATHER_TODAY,
        .execute = weather_command_execute,
    };

    return &handler;
}

/**
 * @brief Execute weather-specific command actions.
 * @param context Assistant command context for the active command.
 * @param dispatch Resolved dispatch metadata describing the weather action.
 * @param out_result Result structure to populate for assistant core.
 * @return ESP_OK after handling the command result structure, or an ESP error code on invalid input.
 */
static esp_err_t weather_command_execute(const assistant_command_context_t *context,
                                         const assistant_command_dispatch_t *dispatch,
                                         assistant_command_result_t *out_result) {
    if (context == NULL || dispatch == NULL || out_result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    out_result->timeout_label = "Weather";

    if (dispatch->type != ASSISTANT_COMMAND_ACTION_WEATHER_TODAY &&
        dispatch->type != ASSISTANT_COMMAND_ACTION_WEATHER_TOMORROW) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Weather command unavailable");
        out_result->err = ESP_ERR_NOT_SUPPORTED;
        return ESP_OK;
    }

    weather_report_t report = {0};
    format_weather_loading_detail(out_result->detail, sizeof(out_result->detail));
    ui_status_set(UI_STATUS_WEATHER_LOADING, out_result->detail);

    esp_err_t err = (dispatch->type == ASSISTANT_COMMAND_ACTION_WEATHER_TODAY) ? weather_client_fetch_today(&report)
                                                                               : weather_client_fetch_tomorrow(&report);
    if (err == ESP_OK) {
        char spoken_detail[WEATHER_SPOKEN_TEXT_LEN];
        weather_format_detail(&report, out_result->detail, sizeof(out_result->detail));
        out_result->hold_time = pdMS_TO_TICKS(WEATHER_STATUS_HOLD_MS);
        ui_status_set(UI_STATUS_SUCCESS, out_result->detail);
        out_result->result_visible_start_tick = xTaskGetTickCount();
        out_result->status_rendered = true;
        weather_format_spoken(&report, spoken_detail, sizeof(spoken_detail));
        if (tts_player_is_configured()) {
            esp_err_t tts_err = tts_player_speak(spoken_detail);
            if (tts_err != ESP_OK) {
                ESP_LOGW(TAG, "Weather speech playback failed: %s", esp_err_to_name(tts_err));
            }
        }
    } else if (err == ESP_ERR_HTTP_CONNECT || err == ESP_ERR_INVALID_STATE) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Weather network error");
    } else {
        snprintf(out_result->detail, sizeof(out_result->detail), "Weather unavailable");
    }

    out_result->err = err;
    return ESP_OK;
}
