#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "commands/assistant_command_dispatch.h"
#include "commands/assistant_command_text.h"
#include "commands/assistant_commands.h"
#include "assistant_state.h"
#include "hue/hue_command_map.h"
#include "weather/weather_format.h"

static int s_failures;
static int s_tests_run;

#define ASSERT_TRUE(expr)                                                                                              \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            fprintf(stderr, "Assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            return false;                                                                                              \
        }                                                                                                              \
    } while (0)

#define ASSERT_EQ_INT(expected, actual)                                                                                \
    do {                                                                                                               \
        if ((expected) != (actual)) {                                                                                  \
            fprintf(                                                                                                   \
                stderr, "Assertion failed at %s:%d: expected %d got %d\n", __FILE__, __LINE__, (expected), (actual));  \
            return false;                                                                                              \
        }                                                                                                              \
    } while (0)

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

static bool test_command_text_labels_builtin_and_hue_commands(void) {
    hue_group_t groups[] = {
        {.id = "1", .name = "kitchen"},
        {.id = "2", .name = "desk"},
    };
    char text[96];

    ASSERT_TRUE(strcmp(assistant_command_text(ASSISTANT_CMD_SYNC_GROUPS, groups, 2, text, sizeof(text)),
                       "Update groups from Hue") == 0);
    ASSERT_TRUE(strcmp(assistant_command_text(ASSISTANT_CMD_WEATHER_TODAY, groups, 2, text, sizeof(text)),
                       "Weather today") == 0);
    ASSERT_TRUE(strcmp(assistant_command_text(ASSISTANT_CMD_WEATHER_TOMORROW, groups, 2, text, sizeof(text)),
                       "Weather tomorrow") == 0);

    ASSERT_TRUE(strcmp(assistant_command_text(
                           hue_group_command_id(ASSISTANT_CMD_GROUP_BASE, 0, true), groups, 2, text, sizeof(text)),
                       "Turn on kitchen") == 0);
    ASSERT_TRUE(strcmp(assistant_command_text(
                           hue_group_command_id(ASSISTANT_CMD_GROUP_BASE, 1, false), groups, 2, text, sizeof(text)),
                       "Turn off desk") == 0);
    return true;
}

static bool test_command_text_unknown_for_invalid_ids(void) {
    hue_group_t groups[] = {
        {.id = "1", .name = "kitchen"},
    };
    char text[96];

    ASSERT_TRUE(strcmp(assistant_command_text(9999, groups, 1, text, sizeof(text)), "Unknown command") == 0);
    ASSERT_TRUE(strcmp(assistant_command_text(
                           hue_group_command_id(ASSISTANT_CMD_GROUP_BASE, 0, true), groups, 0, text, sizeof(text)),
                       "Unknown command") == 0);
    ASSERT_TRUE(
        strcmp(assistant_command_text(hue_group_command_id(ASSISTANT_CMD_GROUP_BASE, 0, true), groups, 1, NULL, 0),
               "Unknown command") == 0);
    return true;
}

static bool test_command_dispatch_resolves_builtin_and_hue_actions(void) {
    assistant_command_dispatch_t dispatch;

    assistant_command_resolve(ASSISTANT_CMD_SYNC_GROUPS, 3, &dispatch);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_SYNC_GROUPS, dispatch.type);

    assistant_command_resolve(ASSISTANT_CMD_WEATHER_TODAY, 3, &dispatch);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_WEATHER_TODAY, dispatch.type);

    assistant_command_resolve(ASSISTANT_CMD_WEATHER_TOMORROW, 3, &dispatch);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_WEATHER_TOMORROW, dispatch.type);

    assistant_command_resolve(hue_group_command_id(ASSISTANT_CMD_GROUP_BASE, 2, false), 3, &dispatch);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_HUE_GROUP, dispatch.type);
    ASSERT_EQ_INT(2, (int) dispatch.group_index);
    ASSERT_TRUE(!dispatch.on);
    return true;
}

static bool test_command_dispatch_unknown_for_out_of_range_ids(void) {
    assistant_command_dispatch_t dispatch = {
        .type = ASSISTANT_COMMAND_ACTION_HUE_GROUP,
        .group_index = 99,
        .on = true,
    };

    assistant_command_resolve(9999, 1, &dispatch);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_UNKNOWN, dispatch.type);
    ASSERT_EQ_INT(0, (int) dispatch.group_index);
    ASSERT_TRUE(!dispatch.on);

    assistant_command_resolve(hue_group_command_id(ASSISTANT_CMD_GROUP_BASE, 0, true), 0, &dispatch);
    ASSERT_EQ_INT(ASSISTANT_COMMAND_ACTION_UNKNOWN, dispatch.type);
    return true;
}

static bool test_weather_detail_today_includes_date_and_current_conditions(void) {
    weather_report_t report = {
        .date = "2026-03-20",
        .location = "New York City, NY",
        .current_temp_f = 53,
        .max_temp_f = 61,
        .min_temp_f = 27,
        .max_precip_probability = 1,
        .wind_speed_mph = 7,
        .has_current_conditions = true,
        .summary = "Cloudy",
    };
    char detail[WEATHER_DETAIL_TEXT_LEN];

    weather_format_detail(&report, detail, sizeof(detail));

    ASSERT_TRUE(strstr(detail, "Now in New York City, NY") != NULL);
    ASSERT_TRUE(strstr(detail, "53F Cloudy") != NULL);
    ASSERT_TRUE(strstr(detail, "HI 61F LO 27F") != NULL);
    ASSERT_TRUE(strstr(detail, "COMMAND COMPLETED") == NULL);
    return true;
}

static bool test_weather_detail_tomorrow_shows_date_without_now_line(void) {
    weather_report_t report = {
        .date = "2026-03-21",
        .location = "New York City, NY",
        .max_temp_f = 49,
        .min_temp_f = 31,
        .max_precip_probability = 40,
        .has_current_conditions = false,
        .summary = "Rain",
    };
    char detail[WEATHER_DETAIL_TEXT_LEN];

    weather_format_detail(&report, detail, sizeof(detail));

    ASSERT_TRUE(strstr(detail, "Tomorrow in New York City, NY") != NULL);
    ASSERT_TRUE(strstr(detail, "Rain") != NULL);
    ASSERT_TRUE(strstr(detail, "Now in ") == NULL);
    return true;
}

static void run_test(const char *name, bool (*fn)(void)) {
    s_tests_run++;
    if (!fn()) {
        s_failures++;
        fprintf(stderr, "FAILED: %s\n", name);
        return;
    }
    printf("PASS: %s\n", name);
}

int main(void) {
    run_test("missing_fetch_recovers_after_limit", test_missing_fetch_recovers_after_limit);
    run_test("command_timeout_forces_recovery", test_command_timeout_forces_recovery);
    run_test("timeout_rules_match_listening_behavior", test_timeout_rules_match_listening_behavior);
    run_test("detected_without_results_recovers", test_detected_without_results_recovers);
    run_test("session_timeout_forces_recovery", test_session_timeout_forces_recovery);
    run_test("task_timeout_requires_heartbeat_and_threshold", test_task_timeout_requires_heartbeat_and_threshold);
    run_test("command_text_labels_builtin_and_hue_commands", test_command_text_labels_builtin_and_hue_commands);
    run_test("command_text_unknown_for_invalid_ids", test_command_text_unknown_for_invalid_ids);
    run_test("command_dispatch_resolves_builtin_and_hue_actions",
             test_command_dispatch_resolves_builtin_and_hue_actions);
    run_test("command_dispatch_unknown_for_out_of_range_ids", test_command_dispatch_unknown_for_out_of_range_ids);
    run_test("weather_detail_today_includes_date_and_current_conditions",
             test_weather_detail_today_includes_date_and_current_conditions);
    run_test("weather_detail_tomorrow_shows_date_without_now_line",
             test_weather_detail_tomorrow_shows_date_without_now_line);

    if (s_failures != 0) {
        fprintf(stderr, "%d/%d tests failed\n", s_failures, s_tests_run);
        return 1;
    }

    printf("%d tests passed\n", s_tests_run);
    return 0;
}
