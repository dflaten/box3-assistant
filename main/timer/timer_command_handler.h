#pragma once

#include "assistant/command_handler.h"

/**
 * @brief Get the timer feature command handler registration.
 * @return Pointer to the static timer command handler descriptor.
 */
const assistant_command_handler_t *timer_command_handler_get(void);
