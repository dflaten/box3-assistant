#include <stdbool.h>

#include "assistant_state.h"
#include "test_support.h"

static bool test_missing_fetch_recovers_after_limit(void) {
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_CONTINUE, assistant_step_for_missing_fetch(false, 99, 50));
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_CONTINUE, assistant_step_for_missing_fetch(true, 49, 50));
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_RECOVER_FETCH_STALL, assistant_step_for_missing_fetch(true, 50, 50));
    return true;
}

static bool test_command_timeout_forces_recovery(void) {
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_CONTINUE,
                  assistant_step_for_multinet(9999, 10000, 3000, ASSISTANT_MN_STATE_DETECTING, true));
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_RECOVER_COMMAND_TIMEOUT,
                  assistant_step_for_multinet(10000, 10000, 3000, ASSISTANT_MN_STATE_DETECTING, true));
    return true;
}

static bool test_timeout_rules_match_listening_behavior(void) {
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_IGNORE_EARLY_TIMEOUT,
                  assistant_step_for_multinet(2000, 10000, 3000, ASSISTANT_MN_STATE_TIMEOUT, true));
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_RECOVER_NO_COMMAND,
                  assistant_step_for_multinet(3000, 10000, 3000, ASSISTANT_MN_STATE_TIMEOUT, true));
    return true;
}

static bool test_detected_without_results_recovers(void) {
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_RECOVER_EMPTY_RESULT,
                  assistant_step_for_multinet(4000, 10000, 3000, ASSISTANT_MN_STATE_DETECTED, false));
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_CONTINUE,
                  assistant_step_for_multinet(4000, 10000, 3000, ASSISTANT_MN_STATE_DETECTED, true));
    return true;
}

static bool test_session_timeout_forces_recovery(void) {
    ASSERT_TRUE(!assistant_session_timed_out(false, true, 30000, 30000));
    ASSERT_TRUE(!assistant_session_timed_out(true, false, 30000, 30000));
    ASSERT_TRUE(!assistant_session_timed_out(true, true, 29999, 30000));
    ASSERT_TRUE(assistant_session_timed_out(true, true, 30000, 30000));
    return true;
}

static bool test_task_timeout_requires_heartbeat_and_threshold(void) {
    ASSERT_TRUE(!assistant_task_timed_out(false, 30000, 30000));
    ASSERT_TRUE(!assistant_task_timed_out(true, 29999, 30000));
    ASSERT_TRUE(assistant_task_timed_out(true, 30000, 30000));
    return true;
}

static bool test_elapsed_ms_clamps_future_ticks(void) {
    ASSERT_EQ_INT(0, (int) assistant_elapsed_ms_since_tick(0, 0));
    ASSERT_EQ_INT(0, (int) assistant_elapsed_ms_since_tick(1000, 1001));
    ASSERT_EQ_INT(25, (int) assistant_elapsed_ms_since_tick(1025, 1000));
    return true;
}

static bool test_presence_clock_redraw_rules_match_runtime_behavior(void) {
    ASSERT_TRUE(assistant_presence_clock_should_redraw(false, false, false, "", "", "", ""));
    ASSERT_TRUE(assistant_presence_clock_should_redraw(true, false, true, "8:00 PM", "", "APR 10", ""));
    ASSERT_TRUE(!assistant_presence_clock_should_redraw(true, true, true, "8:00 PM", "8:00 PM", "APR 10", "APR 10"));
    ASSERT_TRUE(assistant_presence_clock_should_redraw(true, true, true, "8:01 PM", "8:00 PM", "APR 10", "APR 10"));
    ASSERT_TRUE(assistant_presence_clock_should_redraw(true, true, true, "8:00 PM", "8:00 PM", "APR 11", "APR 10"));
    ASSERT_TRUE(!assistant_presence_clock_should_redraw(true, false, false, "", "", "", ""));
    return true;
}

const test_case_t g_assistant_state_tests[] = {
    {"missing_fetch_recovers_after_limit", test_missing_fetch_recovers_after_limit},
    {"command_timeout_forces_recovery", test_command_timeout_forces_recovery},
    {"timeout_rules_match_listening_behavior", test_timeout_rules_match_listening_behavior},
    {"detected_without_results_recovers", test_detected_without_results_recovers},
    {"session_timeout_forces_recovery", test_session_timeout_forces_recovery},
    {"task_timeout_requires_heartbeat_and_threshold", test_task_timeout_requires_heartbeat_and_threshold},
    {"elapsed_ms_clamps_future_ticks", test_elapsed_ms_clamps_future_ticks},
    {"presence_clock_redraw_rules_match_runtime_behavior", test_presence_clock_redraw_rules_match_runtime_behavior},
};

const int g_assistant_state_test_count = (int) (sizeof(g_assistant_state_tests) / sizeof(g_assistant_state_tests[0]));
