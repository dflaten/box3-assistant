#include "assistant_state.h"

assistant_listen_step_t assistant_step_for_missing_fetch(bool assistant_awake, int fetch_failures, int max_fetch_failures)
{
    if (!assistant_awake) {
        return ASSISTANT_LISTEN_STEP_CONTINUE;
    }
    if (fetch_failures >= max_fetch_failures) {
        return ASSISTANT_LISTEN_STEP_RECOVER_FETCH_STALL;
    }
    return ASSISTANT_LISTEN_STEP_CONTINUE;
}

assistant_listen_step_t assistant_step_for_multinet(uint32_t elapsed_ms,
                                                    uint32_t command_window_ms,
                                                    uint32_t command_min_listen_ms,
                                                    assistant_mn_state_t mn_state,
                                                    bool have_results)
{
    if (elapsed_ms >= command_window_ms) {
        return ASSISTANT_LISTEN_STEP_RECOVER_COMMAND_TIMEOUT;
    }

    if (mn_state == ASSISTANT_MN_STATE_DETECTING) {
        return ASSISTANT_LISTEN_STEP_CONTINUE;
    }

    if (mn_state == ASSISTANT_MN_STATE_TIMEOUT) {
        if (elapsed_ms < command_min_listen_ms) {
            return ASSISTANT_LISTEN_STEP_IGNORE_EARLY_TIMEOUT;
        }
        return ASSISTANT_LISTEN_STEP_RECOVER_NO_COMMAND;
    }

    if (mn_state == ASSISTANT_MN_STATE_DETECTED && !have_results) {
        return ASSISTANT_LISTEN_STEP_RECOVER_EMPTY_RESULT;
    }

    return ASSISTANT_LISTEN_STEP_CONTINUE;
}

bool assistant_session_watchdog_expired(bool assistant_awake,
                                        bool have_awake_tick,
                                        uint32_t elapsed_ms,
                                        uint32_t session_timeout_ms)
{
    return assistant_awake && have_awake_tick && elapsed_ms >= session_timeout_ms;
}
