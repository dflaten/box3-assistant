#include <string.h>

#include "test_support.h"
#include "timer/timer_runtime.h"

static bool test_timer_runtime_counts_down_and_alarms(void) {
    timer_runtime_t timer = {0};

    ASSERT_TRUE(timer_runtime_start(&timer, 90, 1000));
    ASSERT_TRUE(timer.active);
    ASSERT_TRUE(!timer.alarming);
    ASSERT_EQ_INT(90, (int) timer_runtime_remaining_seconds(&timer, 1000));
    ASSERT_EQ_INT(90, (int) timer_runtime_remaining_seconds(&timer, 1500));
    ASSERT_TRUE(!timer_runtime_update(&timer, 90999));
    ASSERT_TRUE(timer_runtime_update(&timer, 91000));
    ASSERT_TRUE(timer.alarming);
    ASSERT_EQ_INT(0, (int) timer_runtime_remaining_seconds(&timer, 91000));
    return true;
}

static bool test_timer_runtime_formats_remaining_and_stops(void) {
    timer_runtime_t timer = {0};
    char text[16];

    ASSERT_TRUE(timer_runtime_start(&timer, 65, 0));
    timer_runtime_format_remaining(&timer, 0, text, sizeof(text));
    ASSERT_TRUE(strcmp(text, "01:05") == 0);

    ASSERT_TRUE(timer_runtime_stop(&timer));
    ASSERT_TRUE(!timer.active);
    ASSERT_TRUE(!timer.alarming);
    ASSERT_TRUE(!timer_runtime_stop(&timer));
    return true;
}

static bool test_timer_runtime_handles_wraparound_elapsed(void) {
    timer_runtime_t timer = {0};

    ASSERT_TRUE(timer_runtime_start(&timer, 10, 0xFFFFFF00U));
    ASSERT_EQ_INT(10, (int) timer_runtime_remaining_seconds(&timer, 0xFFFFFF00U));
    ASSERT_EQ_INT(10, (int) timer_runtime_remaining_seconds(&timer, 0x00000100U));
    ASSERT_TRUE(!timer_runtime_update(&timer, 0x00002600U));
    ASSERT_TRUE(timer_runtime_update(&timer, 0x00002720U));
    ASSERT_TRUE(timer.alarming);
    return true;
}

const test_case_t g_timer_runtime_tests[] = {
    {"timer_runtime_counts_down_and_alarms", test_timer_runtime_counts_down_and_alarms},
    {"timer_runtime_formats_remaining_and_stops", test_timer_runtime_formats_remaining_and_stops},
    {"timer_runtime_handles_wraparound_elapsed", test_timer_runtime_handles_wraparound_elapsed},
};

const int g_timer_runtime_test_count = (int) (sizeof(g_timer_runtime_tests) / sizeof(g_timer_runtime_tests[0]));
