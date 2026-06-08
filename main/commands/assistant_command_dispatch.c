#include "commands/assistant_command_dispatch.h"
#include "commands/assistant_commands.h"
#include "hue/hue_command_map.h"
#include "volume/volume_command_map.h"

/**
 * @brief Resolve a recognized command id into feature dispatch metadata.
 * @param command_id Recognized MultiNet command id.
 * @param group_count Number of runtime Hue groups available for dynamic commands.
 * @param out_dispatch Output dispatch metadata, reset to unknown when the id is unsupported.
 * @return This function does not return a value.
 */
void assistant_command_resolve(int command_id, size_t group_count, assistant_command_dispatch_t *out_dispatch) {
    if (out_dispatch == NULL) {
        return;
    }

    *out_dispatch = (assistant_command_dispatch_t) {
        .type = ASSISTANT_COMMAND_ACTION_UNKNOWN,
        .group_index = 0,
        .volume_steps = 0,
        .on = false,
    };

    if (command_id == ASSISTANT_CMD_SYNC_GROUPS) {
        out_dispatch->type = ASSISTANT_COMMAND_ACTION_SYNC_GROUPS;
        return;
    }
    if (command_id == ASSISTANT_CMD_WEATHER_TODAY) {
        out_dispatch->type = ASSISTANT_COMMAND_ACTION_WEATHER_TODAY;
        return;
    }
    if (command_id == ASSISTANT_CMD_WEATHER_TOMORROW) {
        out_dispatch->type = ASSISTANT_COMMAND_ACTION_WEATHER_TOMORROW;
        return;
    }
    if (command_id == ASSISTANT_CMD_SET_TIMER) {
        out_dispatch->type = ASSISTANT_COMMAND_ACTION_SET_TIMER;
        return;
    }
    if (command_id == ASSISTANT_CMD_STOP) {
        out_dispatch->type = ASSISTANT_COMMAND_ACTION_STOP;
        return;
    }
    if (command_id == ASSISTANT_CMD_TIME_NOW) {
        out_dispatch->type = ASSISTANT_COMMAND_ACTION_TIME_NOW;
        return;
    }
    if (command_id == ASSISTANT_CMD_TIME_IN_LOCATION) {
        out_dispatch->type = ASSISTANT_COMMAND_ACTION_TIME_IN_LOCATION;
        return;
    }
    uint8_t volume_steps = 0;
    bool volume_increase = false;
    if (volume_decode_command_id(command_id,
                                 ASSISTANT_CMD_VOLUME_UP_BASE,
                                 ASSISTANT_CMD_VOLUME_DOWN_BASE,
                                 ASSISTANT_CMD_VOLUME_STEP_COUNT,
                                 &volume_steps,
                                 &volume_increase)) {
        out_dispatch->type =
            volume_increase ? ASSISTANT_COMMAND_ACTION_VOLUME_UP : ASSISTANT_COMMAND_ACTION_VOLUME_DOWN;
        out_dispatch->volume_steps = volume_steps;
        return;
    }

    if (hue_decode_group_command_id(
            command_id, ASSISTANT_CMD_GROUP_BASE, group_count, &out_dispatch->group_index, &out_dispatch->on)) {
        out_dispatch->type = ASSISTANT_COMMAND_ACTION_HUE_GROUP;
    }
}
