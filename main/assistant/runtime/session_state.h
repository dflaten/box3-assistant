#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"

#include "timer/timer_runtime.h"

typedef enum {
    ASSISTANT_STAGE_STANDBY = 0,
    ASSISTANT_STAGE_LISTENING,
    ASSISTANT_STAGE_EXECUTING,
} assistant_stage_t;

/**
 * @brief Session lifecycle state for the assistant wake/listen/execute flow.
 */
typedef struct {
    /** True while the assistant is inside an active wake/listen/execute session. */
    bool assistant_awake;
    /** Tick count captured when the current assistant session began. */
    TickType_t assistant_awake_tick;
    /** Tick count updated whenever the speech pipeline makes forward progress. */
    volatile TickType_t speech_progress_tick;
    /** High-level stage used for watchdog diagnostics. */
    volatile assistant_stage_t assistant_stage;
    /** True while direct command recognition should run without a wake word. */
    volatile bool direct_command_mode;
    /** True once direct command recognition has prepared MultiNet for the current direct-listen session. */
    volatile bool direct_command_prepared;
    /** Active timer countdown and alarm state. */
    timer_runtime_t timer;
} assistant_session_state_t;
