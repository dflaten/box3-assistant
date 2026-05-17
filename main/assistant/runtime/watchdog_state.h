#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"

/**
 * @brief Watchdog-visible heartbeat and timeout recovery state.
 */
typedef struct {
    /** Tick count updated by the audio feed task to prove it is still running. */
    volatile TickType_t audio_feed_heartbeat_tick;
    /** Tick count updated by the speech detect task to prove it is still running. */
    volatile TickType_t speech_detect_heartbeat_tick;
    /** Tick count updated by the presence clock task to prove it is still running. */
    volatile TickType_t presence_clock_heartbeat_tick;
    /** Most recent command id being executed, or 0 when idle. */
    volatile int current_command_id;
    /** True after the watchdog has requested timeout recovery for the active execution. */
    volatile bool execution_timeout_pending;
    /** Tick count captured when timeout recovery was first requested. */
    volatile TickType_t execution_timeout_tick;
} assistant_watchdog_state_t;
