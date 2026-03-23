#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    ASSISTANT_COMMAND_ACTION_UNKNOWN = 0,
    ASSISTANT_COMMAND_ACTION_SYNC_GROUPS,
    ASSISTANT_COMMAND_ACTION_WEATHER_TODAY,
    ASSISTANT_COMMAND_ACTION_WEATHER_TOMORROW,
    ASSISTANT_COMMAND_ACTION_HUE_GROUP,
} assistant_command_action_type_t;

typedef struct {
    assistant_command_action_type_t type;
    size_t group_index;
    bool on;
} assistant_command_dispatch_t;

void assistant_command_resolve(int command_id, size_t group_count, assistant_command_dispatch_t *out_dispatch);
