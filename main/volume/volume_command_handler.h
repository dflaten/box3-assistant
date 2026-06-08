#pragma once

#include "assistant/command_handler.h"

/**
 * @brief Get the speaker-volume command handler registration.
 * @return Pointer to the static volume command handler descriptor.
 */
const assistant_command_handler_t *volume_command_handler_get(void);
