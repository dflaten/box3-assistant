#include "assistant_state.h"

/**
 * @brief Decide how the assistant should react to repeated missing AFE fetch results.
 * @param assistant_awake True when the assistant is in an active command session.
 * @param fetch_failures The current count of consecutive missing fetches.
 * @param max_fetch_failures The threshold that triggers recovery.
 * @return The next listen-step action for the assistant state machine.
 */
assistant_listen_step_t
assistant_step_for_missing_fetch(bool assistant_awake, int fetch_failures, int max_fetch_failures) {
    if (!assistant_awake) {
        return ASSISTANT_LISTEN_STEP_CONTINUE;
    }
    if (fetch_failures >= max_fetch_failures) {
        return ASSISTANT_LISTEN_STEP_RECOVER_FETCH_STALL;
    }
    return ASSISTANT_LISTEN_STEP_CONTINUE;
}

/**
 * @brief Decide how the assistant should react to the current MultiNet detection state.
 * @param elapsed_ms Milliseconds since the current command session started.
 * @param command_window_ms Maximum allowed listening time before timeout recovery.
 * @param command_min_listen_ms Minimum listen time before timeout can count as no-command.
 * @param mn_state The current MultiNet detector state.
 * @param have_results True when MultiNet produced at least one command result.
 * @return The next listen-step action for the assistant state machine.
 */
assistant_listen_step_t assistant_step_for_multinet(uint32_t elapsed_ms,
                                                    uint32_t command_window_ms,
                                                    uint32_t command_min_listen_ms,
                                                    assistant_mn_state_t mn_state,
                                                    bool have_results) {
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

/**
 * @brief Check whether the current assistant session has exceeded its timeout.
 * @param assistant_awake True when the assistant is currently in an active session.
 * @param have_awake_tick True when a valid awake timestamp is available.
 * @param elapsed_ms Milliseconds elapsed since the awake timestamp.
 * @param session_timeout_ms Maximum allowed awake-session duration.
 * @return True if the active session has timed out, otherwise false.
 */
bool assistant_session_timed_out(bool assistant_awake,
                                 bool have_awake_tick,
                                 uint32_t elapsed_ms,
                                 uint32_t session_timeout_ms) {
    return assistant_awake && have_awake_tick && elapsed_ms >= session_timeout_ms;
}
