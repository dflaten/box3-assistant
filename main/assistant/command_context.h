#pragma once

#include "assistant_runtime.h"

/**
 * @brief Shared assistant context provided to feature-owned command handlers.
 */
typedef struct {
    /** Shared assistant runtime for the active command. */
    assistant_runtime_t *runtime;
    /** MultiNet command id being executed. */
    int command_id;
    /** Human-readable command text for logs and fallback UI details. */
    const char *command_text;
} assistant_command_context_t;
