#include <stdbool.h>
#include <stdio.h>

#include "commands/assistant_command_text.h"
#include "commands/assistant_commands.h"
#include "hue/hue_command_map.h"
#include "volume/volume_command_map.h"

/**
 * @brief Build a human-readable label for a recognized command id.
 * @param command_id Recognized MultiNet command id.
 * @param groups Runtime Hue groups used to label dynamic group commands.
 * @param group_count Number of valid entries in groups.
 * @param buffer Scratch buffer used for generated command labels.
 * @param buffer_size Size of the scratch buffer in bytes.
 * @return Static text or buffer when the command is recognized, otherwise "Unknown command".
 */
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
    if (command_id == ASSISTANT_CMD_TIME_NOW) {
        return "What time is it";
    }
    if (command_id == ASSISTANT_CMD_TIME_IN_LOCATION) {
        return "Current time in";
    }
    uint8_t volume_steps = 0;
    bool volume_increase = false;
    if (buffer != NULL && buffer_size > 0 &&
        volume_decode_command_id(command_id,
                                 ASSISTANT_CMD_VOLUME_UP_BASE,
                                 ASSISTANT_CMD_VOLUME_DOWN_BASE,
                                 ASSISTANT_CMD_VOLUME_STEP_COUNT,
                                 &volume_steps,
                                 &volume_increase)) {
        snprintf(buffer, buffer_size, "Volume %s by %u", volume_increase ? "up" : "down", volume_steps);
        return buffer;
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
