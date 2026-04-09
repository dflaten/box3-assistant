#pragma once

#include <stdbool.h>
#include <stddef.h>

int hue_group_command_id(int command_base, size_t index, bool on);
bool hue_decode_group_command_id(int command_id, int command_base, size_t group_count, size_t *group_index, bool *on);
