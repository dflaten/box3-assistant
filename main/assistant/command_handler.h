#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"

#include "esp_err.h"

#include "assistant/command_context.h"
#include "commands/assistant_command_dispatch.h"

#define ASSISTANT_COMMAND_RESULT_DETAIL_LEN 160

/**
 * @brief Result data produced by a feature-owned command handler.
 */
typedef struct {
    /** Final command status returned to assistant core. */
    esp_err_t err;
    /** User-facing detail text for success or failure UI. */
    char detail[ASSISTANT_COMMAND_RESULT_DETAIL_LEN];
    /** How long the result should remain visible before standby UI is restored. */
    TickType_t hold_time;
    /** Tick when a handler made its result visible before returning, or zero if not applicable. */
    TickType_t result_visible_start_tick;
    /** True when the handler already rendered the terminal UI state itself. */
    bool status_rendered;
    /** Timeout prefix used if watchdog cancellation forces a timeout result. */
    const char *timeout_label;
} assistant_command_result_t;

typedef esp_err_t (*assistant_command_handler_fn)(const assistant_command_context_t *context,
                                                  const assistant_command_dispatch_t *dispatch,
                                                  assistant_command_result_t *out_result);

/**
 * @brief Static registration record for a feature-owned command handler.
 */
typedef struct {
    /** High-level action type owned by this handler. */
    assistant_command_action_type_t action;
    /** Execution callback for the action type. */
    assistant_command_handler_fn execute;
} assistant_command_handler_t;
