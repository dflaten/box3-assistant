#pragma once

#include "assistant/command_handler.h"

/**
 * @brief Get the weather feature command handler registration.
 * @return Pointer to the static weather command handler descriptor.
 */
const assistant_command_handler_t *weather_command_handler_get(void);
