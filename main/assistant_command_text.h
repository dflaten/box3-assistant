#pragma once

#include <stddef.h>

#include "hue/hue_group.h"

const char *assistant_command_text(int command_id,
                                   const hue_group_t *groups,
                                   size_t group_count,
                                   char *buffer,
                                   size_t buffer_size);
