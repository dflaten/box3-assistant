#pragma once

#include "assistant_runtime.h"

/**
 * @brief Render the active timer countdown or alarm state on screen.
 * @param rt Shared assistant runtime state whose timer should be shown.
 * @return This function does not return a value.
 */
void assistant_timer_status_show(assistant_runtime_t *rt);
