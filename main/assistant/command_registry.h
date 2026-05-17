#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "assistant/command_handler.h"

/**
 * @brief Resolve a command id and locate the registered feature handler for it.
 * @param command_id Recognized MultiNet command id.
 * @param group_count Number of runtime Hue groups available for dynamic group commands.
 * @param out_dispatch Output dispatch description populated from the command id.
 * @param out_handler Output handler pointer for the resolved action, or NULL when no handler is registered.
 * @return True when a handler is registered for the resolved action, otherwise false.
 */
bool assistant_command_registry_lookup(int command_id,
                                       size_t group_count,
                                       assistant_command_dispatch_t *out_dispatch,
                                       const assistant_command_handler_t **out_handler);
