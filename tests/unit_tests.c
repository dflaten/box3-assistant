#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "assistant_state.h"
#include "weather/weather_format.h"

static int s_failures;
static int s_tests_run;

#define ASSERT_TRUE(expr)                                                                                               \
    do {                                                                                                                \
        if (!(expr)) {                                                                                                  \
            fprintf(stderr, "Assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #expr);                             \
            return false;                                                                                               \
        }                                                                                                               \
    } while (0)

#define ASSERT_EQ_INT(expected, actual)                                                                                 \
    do {                                                                                                                \
        if ((expected) != (actual)) {                                                                                   \
            fprintf(stderr, "Assertion failed at %s:%d: expected %d got %d\n",                                         \
                    __FILE__,                                                                                            \
                    __LINE__,                                                                                            \
                    (expected),                                                                                          \
                    (actual));                                                                                           \
            return false;                                                                                               \
        }                                                                                                               \
    } while (0)

static bool test_missing_fetch_recovers_after_limit(void)
{
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_CONTINUE, assistant_step_for_missing_fetch(false, 99, 50));
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_CONTINUE, assistant_step_for_missing_fetch(true, 49, 50));
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_RECOVER_FETCH_STALL, assistant_step_for_missing_fetch(true, 50, 50));
    return true;
}

static bool test_command_timeout_forces_recovery(void)
{
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_CONTINUE,
                  assistant_step_for_multinet(9999, 10000, 3000, ASSISTANT_MN_STATE_DETECTING, true));
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_RECOVER_COMMAND_TIMEOUT,
                  assistant_step_for_multinet(10000, 10000, 3000, ASSISTANT_MN_STATE_DETECTING, true));
    return true;
}

static bool test_timeout_rules_match_listening_behavior(void)
{
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_IGNORE_EARLY_TIMEOUT,
                  assistant_step_for_multinet(2000, 10000, 3000, ASSISTANT_MN_STATE_TIMEOUT, true));
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_RECOVER_NO_COMMAND,
                  assistant_step_for_multinet(3000, 10000, 3000, ASSISTANT_MN_STATE_TIMEOUT, true));
    return true;
}

static bool test_detected_without_results_recovers(void)
{
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_RECOVER_EMPTY_RESULT,
                  assistant_step_for_multinet(4000, 10000, 3000, ASSISTANT_MN_STATE_DETECTED, false));
    ASSERT_EQ_INT(ASSISTANT_LISTEN_STEP_CONTINUE,
                  assistant_step_for_multinet(4000, 10000, 3000, ASSISTANT_MN_STATE_DETECTED, true));
    return true;
}

static bool test_session_watchdog_covers_completed_hang_regression(void)
{
    ASSERT_TRUE(!assistant_session_watchdog_expired(false, true, 30000, 30000));
    ASSERT_TRUE(!assistant_session_watchdog_expired(true, false, 30000, 30000));
    ASSERT_TRUE(!assistant_session_watchdog_expired(true, true, 29999, 30000));
    ASSERT_TRUE(assistant_session_watchdog_expired(true, true, 30000, 30000));
    return true;
}

static bool test_weather_detail_today_includes_date_and_current_conditions(void)
{
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

static bool test_weather_detail_tomorrow_shows_date_without_now_line(void)
{
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

static void run_test(const char *name, bool (*fn)(void))
{
    s_tests_run++;
    if (!fn()) {
        s_failures++;
        fprintf(stderr, "FAILED: %s\n", name);
        return;
    }
    printf("PASS: %s\n", name);
}

int main(void)
{
    run_test("missing_fetch_recovers_after_limit", test_missing_fetch_recovers_after_limit);
    run_test("command_timeout_forces_recovery", test_command_timeout_forces_recovery);
    run_test("timeout_rules_match_listening_behavior", test_timeout_rules_match_listening_behavior);
    run_test("detected_without_results_recovers", test_detected_without_results_recovers);
    run_test("session_watchdog_covers_completed_hang_regression", test_session_watchdog_covers_completed_hang_regression);
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
