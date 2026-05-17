#pragma once

#include "freertos/FreeRTOS.h"

/**
 * @brief Presence sensor and display ownership state.
 */
typedef struct {
    /** Tick count of the most recent presence motion detection used to decide whether the idle clock should stay
     * visible. */
    volatile TickType_t last_presence_motion_tick;
} assistant_presence_state_t;
