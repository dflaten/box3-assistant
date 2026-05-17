#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "commands/assistant_command_dispatch.h"

typedef struct assistant_command_context_t assistant_command_context_t;
typedef struct assistant_command_result_t assistant_command_result_t;

typedef esp_err_t (*assistant_command_handler_fn)(const assistant_command_context_t *context,
                                                  const assistant_command_dispatch_t *dispatch,
                                                  assistant_command_result_t *out_result);

typedef struct {
    assistant_command_action_type_t action;
    assistant_command_handler_fn execute;
} assistant_command_handler_t;
