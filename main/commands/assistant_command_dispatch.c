#include "commands/assistant_command_dispatch.h"
#include "commands/assistant_commands.h"
#include "hue/hue_command_map.h"

void assistant_command_resolve(int command_id, size_t group_count, assistant_command_dispatch_t *out_dispatch) {
    if (out_dispatch == NULL) {
        return;
    }

    *out_dispatch = (assistant_command_dispatch_t) {
        .type = ASSISTANT_COMMAND_ACTION_UNKNOWN,
        .group_index = 0,
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

    if (hue_decode_group_command_id(
            command_id, ASSISTANT_CMD_GROUP_BASE, group_count, &out_dispatch->group_index, &out_dispatch->on)) {
        out_dispatch->type = ASSISTANT_COMMAND_ACTION_HUE_GROUP;
    }
}
