#include "volume/volume_command_handler.h"

#include <stdio.h>

#include "board/board_audio.h"
#include "commands/assistant_commands.h"
#include "volume/volume_control.h"

static esp_err_t volume_command_execute(const assistant_command_context_t *context,
                                        const assistant_command_dispatch_t *dispatch,
                                        assistant_command_result_t *out_result);

/**
 * @brief Get the speaker-volume command handler registration.
 * @return Pointer to the static volume command handler descriptor.
 */
const assistant_command_handler_t *volume_command_handler_get(void) {
    static const assistant_command_handler_t handler = {
        .action = ASSISTANT_COMMAND_ACTION_VOLUME_UP,
        .execute = volume_command_execute,
    };

    return &handler;
}

/**
 * @brief Execute a speaker-volume increase or decrease command.
 * @param context Assistant command context for the active command.
 * @param dispatch Resolved volume direction and step count.
 * @param out_result Result structure to populate for assistant core.
 * @return ESP_OK after handling the command result structure, or an ESP error code on invalid input.
 */
static esp_err_t volume_command_execute(const assistant_command_context_t *context,
                                        const assistant_command_dispatch_t *dispatch,
                                        assistant_command_result_t *out_result) {
    if (context == NULL || dispatch == NULL || out_result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((dispatch->type != ASSISTANT_COMMAND_ACTION_VOLUME_UP &&
         dispatch->type != ASSISTANT_COMMAND_ACTION_VOLUME_DOWN) ||
        dispatch->volume_steps == 0 || dispatch->volume_steps > ASSISTANT_CMD_VOLUME_STEP_COUNT) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Volume command unavailable");
        out_result->err = ESP_ERR_INVALID_ARG;
        return ESP_OK;
    }

    uint8_t current_percent = board_audio_get_volume_percent();
    uint8_t adjusted_percent = volume_control_adjust(
        current_percent, dispatch->volume_steps, dispatch->type == ASSISTANT_COMMAND_ACTION_VOLUME_UP);
    out_result->err = board_audio_set_volume_percent(adjusted_percent);
    if (out_result->err != ESP_OK) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Volume change failed");
        return ESP_OK;
    }

    snprintf(out_result->detail, sizeof(out_result->detail), "Volume %u%%", (unsigned) adjusted_percent);
    return ESP_OK;
}
