#include <string.h>

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
 * @brief Convert two tick snapshots into elapsed milliseconds without wrapping on out-of-order reads.
 * @param now_tick The current scheduler tick snapshot.
 * @param then_tick The earlier scheduler tick snapshot to compare against.
 * @return Milliseconds elapsed between the two ticks, or zero when the compared tick is unset or newer than the current
 * snapshot.
 * @note Watchdog reads race with task heartbeat updates on another core, so future-looking tick values must be clamped.
 */
uint32_t assistant_elapsed_ms_since_tick(uint32_t now_tick, uint32_t then_tick) {
    if (then_tick == 0 || then_tick > now_tick) {
        return 0;
    }

    return now_tick - then_tick;
}

/**
 * @brief Decide whether the presence clock should redraw based on ownership, sync state, and displayed text.
 * @param display_owned_by_presence True when the presence clock currently owns the display.
 * @param last_clock_synced The previous clock-sync state shown on screen.
 * @param clock_synced The current clock-sync state to consider.
 * @param time_text The current formatted time string when clock_synced is true.
 * @param last_time_text The previously displayed time string.
 * @param date_text The current formatted date string when clock_synced is true.
 * @param last_date_text The previously displayed date string.
 * @return True when the clock UI should redraw, otherwise false.
 */
bool assistant_presence_clock_should_redraw(bool display_owned_by_presence,
                                            bool last_clock_synced,
                                            bool clock_synced,
                                            const char *time_text,
                                            const char *last_time_text,
                                            const char *date_text,
                                            const char *last_date_text) {
    if (!display_owned_by_presence || clock_synced != last_clock_synced) {
        return true;
    }

    if (!clock_synced) {
        return false;
    }

    return strcmp(time_text != NULL ? time_text : "", last_time_text != NULL ? last_time_text : "") != 0 ||
           strcmp(date_text != NULL ? date_text : "", last_date_text != NULL ? last_date_text : "") != 0;
}

/**
 * @brief Check whether a task heartbeat has exceeded its timeout.
 * @param have_heartbeat_tick True when a valid heartbeat timestamp is available.
 * @param stalled_ms Milliseconds elapsed since the last heartbeat update.
 * @param timeout_ms Maximum allowed time between heartbeat updates.
 * @return True if the task heartbeat has timed out, otherwise false.
 */
bool assistant_task_timed_out(bool have_heartbeat_tick, uint32_t stalled_ms, uint32_t timeout_ms) {
    return have_heartbeat_tick && stalled_ms >= timeout_ms;
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
