#include "volume/volume_command_map.h"

#include <stddef.h>

/**
 * @brief Encode a volume step command into a runtime command ID.
 * @param command_base First command id in the selected volume command range.
 * @param steps Number of ten-percent steps from 1 through the configured step count.
 * @return Encoded command id, or command_base when steps is zero.
 */
int volume_command_id(int command_base, uint8_t steps) {
    return command_base + (int) steps - 1;
}

/**
 * @brief Decode a runtime command id into volume adjustment metadata.
 * @param command_id Runtime command id to decode.
 * @param volume_up_command_base First command id in the volume increase range.
 * @param volume_down_command_base First command id in the volume decrease range.
 * @param step_count Number of valid command ids in each volume range.
 * @param out_steps Optional output receiving the one-based step count.
 * @param out_increase Optional output set true for increase commands and false for decrease commands.
 * @return True when the command id maps to a valid volume adjustment command, otherwise false.
 */
bool volume_decode_command_id(int command_id,
                              int volume_up_command_base,
                              int volume_down_command_base,
                              uint8_t step_count,
                              uint8_t *out_steps,
                              bool *out_increase) {
    int base = 0;
    bool increase = false;

    if (command_id >= volume_up_command_base && command_id < volume_up_command_base + step_count) {
        base = volume_up_command_base;
        increase = true;
    } else if (command_id >= volume_down_command_base && command_id < volume_down_command_base + step_count) {
        base = volume_down_command_base;
        increase = false;
    } else {
        return false;
    }

    if (out_steps != NULL) {
        *out_steps = (uint8_t) (command_id - base + 1);
    }
    if (out_increase != NULL) {
        *out_increase = increase;
    }
    return true;
}
