#include <stdbool.h>
#include <stdio.h>

#include "commands/assistant_command_text.h"
#include "commands/assistant_commands.h"
#include "hue/hue_command_map.h"

const char *assistant_command_text(
    int command_id, const hue_group_t *groups, size_t group_count, char *buffer, size_t buffer_size) {
    if (command_id == ASSISTANT_CMD_SYNC_GROUPS) {
        return "Update groups from Hue";
    }
    if (command_id == ASSISTANT_CMD_WEATHER_TODAY) {
        return "Weather today";
    }
    if (command_id == ASSISTANT_CMD_WEATHER_TOMORROW) {
        return "Weather tomorrow";
    }
    if (command_id == ASSISTANT_CMD_SET_TIMER) {
        return "Set a timer";
    }
    if (command_id == ASSISTANT_CMD_STOP) {
        return "Stop";
    }

    size_t index = 0;
    bool on = false;
    if (buffer != NULL && buffer_size > 0 &&
        hue_decode_group_command_id(command_id, ASSISTANT_CMD_GROUP_BASE, group_count, &index, &on)) {
        snprintf(buffer, buffer_size, "Turn %s %s", on ? "on" : "off", groups[index].name);
        return buffer;
    }

    return "Unknown command";
}
