#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ASSISTANT_LISTEN_STEP_CONTINUE = 0,
    ASSISTANT_LISTEN_STEP_RECOVER_FETCH_STALL,
    ASSISTANT_LISTEN_STEP_RECOVER_COMMAND_TIMEOUT,
    ASSISTANT_LISTEN_STEP_IGNORE_EARLY_TIMEOUT,
    ASSISTANT_LISTEN_STEP_RECOVER_NO_COMMAND,
    ASSISTANT_LISTEN_STEP_RECOVER_EMPTY_RESULT,
} assistant_listen_step_t;

typedef enum {
    ASSISTANT_MN_STATE_DETECTING = 0,
    ASSISTANT_MN_STATE_DETECTED = 1,
    ASSISTANT_MN_STATE_TIMEOUT = 2,
    ASSISTANT_MN_STATE_OTHER = 3,
} assistant_mn_state_t;

assistant_listen_step_t assistant_step_for_missing_fetch(bool assistant_awake, int fetch_failures, int max_fetch_failures);
assistant_listen_step_t assistant_step_for_multinet(uint32_t elapsed_ms,
                                                    uint32_t command_window_ms,
                                                    uint32_t command_min_listen_ms,
                                                    assistant_mn_state_t mn_state,
                                                    bool have_results);
bool assistant_session_timed_out(bool assistant_awake,
                                 bool have_awake_tick,
                                 uint32_t elapsed_ms,
                                 uint32_t session_timeout_ms);
