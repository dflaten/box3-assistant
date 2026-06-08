#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ASSISTANT_COMMAND_ACTION_UNKNOWN = 0,
    ASSISTANT_COMMAND_ACTION_SYNC_GROUPS,
    ASSISTANT_COMMAND_ACTION_WEATHER_TODAY,
    ASSISTANT_COMMAND_ACTION_WEATHER_TOMORROW,
    ASSISTANT_COMMAND_ACTION_SET_TIMER,
    ASSISTANT_COMMAND_ACTION_STOP,
    ASSISTANT_COMMAND_ACTION_TIME_NOW,
    ASSISTANT_COMMAND_ACTION_TIME_IN_LOCATION,
    ASSISTANT_COMMAND_ACTION_VOLUME_UP,
    ASSISTANT_COMMAND_ACTION_VOLUME_DOWN,
    ASSISTANT_COMMAND_ACTION_HUE_GROUP,
} assistant_command_action_type_t;

typedef struct {
    assistant_command_action_type_t type;
    size_t group_index;
    uint8_t volume_steps;
    bool on;
} assistant_command_dispatch_t;

void assistant_command_resolve(int command_id, size_t group_count, assistant_command_dispatch_t *out_dispatch);
