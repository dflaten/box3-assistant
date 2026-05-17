#include "hue/hue_command_handler.h"

#include <stdio.h>

#include "esp_log.h"

#include "commands/assistant_commands.h"
#include "hue/hue_client.h"
#include "hue/hue_command_runtime.h"
#include "system/wifi_support.h"

static const char *TAG = "assistant-hue";

static esp_err_t hue_command_execute(const assistant_command_context_t *context,
                                     const assistant_command_dispatch_t *dispatch,
                                     assistant_command_result_t *out_result);

/**
 * @brief Get the Hue feature command handler registration.
 * @return Pointer to the static Hue command handler descriptor.
 */
const assistant_command_handler_t *hue_command_handler_get(void) {
    static const assistant_command_handler_t handler = {
        .action = ASSISTANT_COMMAND_ACTION_SYNC_GROUPS,
        .execute = hue_command_execute,
    };

    return &handler;
}

/**
 * @brief Convert a Hue bridge probe failure into a short UI detail message.
 * @param probe_err Error returned by Hue bridge probing.
 * @param detail Destination buffer for the user-facing status text.
 * @param detail_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
void hue_command_handler_format_probe_error(esp_err_t probe_err, char *detail, size_t detail_size) {
    if (detail == NULL || detail_size == 0) {
        return;
    }

    if (!wifi_is_connected() || probe_err == ESP_ERR_INVALID_STATE) {
        snprintf(detail, detail_size, "Hue Wi-Fi disconnected");
    } else if (hue_client_error_is_connectivity(probe_err) || probe_err == ESP_ERR_NOT_FOUND) {
        snprintf(detail, detail_size, "Wrong Hue bridge IP");
    } else {
        snprintf(detail, detail_size, "Hue bridge unavailable");
    }
}

/**
 * @brief Convert a Hue command failure into a short UI detail message.
 * @param fallback Default message to use when the bridge itself is reachable.
 * @param request_err Error returned by the Hue request.
 * @param detail Destination buffer for the user-facing status text.
 * @param detail_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
void hue_command_handler_format_request_error(const char *fallback,
                                              esp_err_t request_err,
                                              char *detail,
                                              size_t detail_size) {
    if (detail == NULL || detail_size == 0) {
        return;
    }

    if (!wifi_is_connected()) {
        hue_command_handler_format_probe_error(ESP_ERR_INVALID_STATE, detail, detail_size);
        return;
    }

    esp_err_t probe_err = hue_client_probe_bridge();
    if (probe_err != ESP_OK) {
        hue_command_handler_format_probe_error(probe_err, detail, detail_size);
        return;
    }

    snprintf(detail, detail_size, "%s", fallback != NULL ? fallback : "Hue command failed");
    if (hue_client_error_is_connectivity(request_err)) {
        ESP_LOGW(TAG, "Hue request failed even though bridge probe succeeded: %s", esp_err_to_name(request_err));
    }
}

/**
 * @brief Execute Hue-specific command actions.
 * @param context Assistant command context for the active command.
 * @param dispatch Resolved dispatch metadata describing the Hue action.
 * @param out_result Result structure to populate for assistant core.
 * @return ESP_OK after handling the command result structure, or an ESP error code on invalid input.
 */
static esp_err_t hue_command_execute(const assistant_command_context_t *context,
                                     const assistant_command_dispatch_t *dispatch,
                                     assistant_command_result_t *out_result) {
    if (context == NULL || context->runtime == NULL || dispatch == NULL || out_result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    assistant_runtime_t *rt = context->runtime;
    out_result->timeout_label = "Command";

    if (dispatch->type == ASSISTANT_COMMAND_ACTION_SYNC_GROUPS) {
        out_result->err = hue_command_runtime_sync_groups(rt,
                                                          ASSISTANT_CMD_SYNC_GROUPS,
                                                          ASSISTANT_CMD_WEATHER_TODAY,
                                                          ASSISTANT_CMD_WEATHER_TOMORROW,
                                                          ASSISTANT_CMD_SET_TIMER,
                                                          ASSISTANT_CMD_STOP,
                                                          ASSISTANT_CMD_GROUP_BASE);
        if (out_result->err != ESP_OK) {
            hue_command_handler_format_request_error(
                "Hue sync failed", out_result->err, out_result->detail, sizeof(out_result->detail));
        }
        return ESP_OK;
    }

    if (dispatch->type != ASSISTANT_COMMAND_ACTION_HUE_GROUP || dispatch->group_index >= rt->group_count) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Hue command unavailable");
        out_result->err = ESP_ERR_INVALID_ARG;
        return ESP_OK;
    }

    ESP_LOGI(TAG,
             "Hue action phase=start group_index=%u group_id=%s on=%s",
             (unsigned) dispatch->group_index,
             rt->groups[dispatch->group_index].id,
             dispatch->on ? "true" : "false");
    out_result->err = hue_client_set_group_by_id(rt->groups[dispatch->group_index].id, dispatch->on);
    ESP_LOGI(TAG, "Hue action phase=after_client err=%s", esp_err_to_name(out_result->err));
    if (out_result->err != ESP_OK) {
        hue_command_handler_format_request_error(
            "Hue command failed", out_result->err, out_result->detail, sizeof(out_result->detail));
        ESP_LOGI(TAG,
                 "Hue action phase=after_format_error_detail detail=\"%s\"",
                 out_result->detail[0] != '\0' ? out_result->detail : "<empty>");
    }

    return ESP_OK;
}
