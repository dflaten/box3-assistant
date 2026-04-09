#include "hue/hue_command_map.h"

/**
 * @brief Encode a Hue group index and power state into a runtime command ID.
 * @param command_base The first command ID reserved for dynamic Hue commands.
 * @param index The zero-based group index in the current runtime table.
 * @param on True for the "turn on" variant, false for the "turn off" variant.
 * @return The encoded runtime command ID for that group action.
 */
int hue_group_command_id(int command_base, size_t index, bool on) {
    return command_base + (int) (index * 2) + (on ? 0 : 1);
}

/**
 * @brief Decode a runtime command ID into a Hue group index and power state.
 * @param command_id The runtime command ID to decode.
 * @param command_base The first command ID reserved for dynamic Hue commands.
 * @param group_count The number of valid synced Hue groups in the runtime table.
 * @param group_index Optional output for the decoded group index.
 * @param on Optional output for the decoded target power state.
 * @return True if the command ID maps to a valid Hue group action, otherwise false.
 */
bool hue_decode_group_command_id(int command_id, int command_base, size_t group_count, size_t *group_index, bool *on) {
    if (command_id < command_base) {
        return false;
    }

    int offset = command_id - command_base;
    size_t index = (size_t) (offset / 2);
    if (index >= group_count) {
        return false;
    }

    if (group_index != NULL) {
        *group_index = index;
    }
    if (on != NULL) {
        *on = (offset % 2) == 0;
    }
    return true;
}
