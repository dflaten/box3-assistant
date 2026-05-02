#include <string.h>

#include "test_support.h"
#include "timer/timer_parse.h"

static bool test_timer_parse_accepts_digit_and_word_durations(void) {
    uint32_t seconds = 0;

    ASSERT_TRUE(timer_parse_duration_text("20 seconds", 86400, &seconds));
    ASSERT_EQ_INT(20, (int) seconds);

    ASSERT_TRUE(timer_parse_duration_text("1 minute 30 seconds", 86400, &seconds));
    ASSERT_EQ_INT(90, (int) seconds);

    ASSERT_TRUE(timer_parse_duration_text("two minutes and five seconds", 86400, &seconds));
    ASSERT_EQ_INT(125, (int) seconds);

    ASSERT_TRUE(timer_parse_duration_text("1 hour 2 minutes", 86400, &seconds));
    ASSERT_EQ_INT(3720, (int) seconds);

    ASSERT_TRUE(timer_parse_duration_text("set a timer for two minutes", 86400, &seconds));
    ASSERT_EQ_INT(120, (int) seconds);
    return true;
}

static bool test_timer_parse_rejects_invalid_or_out_of_range_text(void) {
    uint32_t seconds = 0;

    ASSERT_TRUE(!timer_parse_duration_text("for a while", 86400, &seconds));
    ASSERT_TRUE(!timer_parse_duration_text("90", 86400, &seconds));
    ASSERT_TRUE(!timer_parse_duration_text("0 seconds", 86400, &seconds));
    ASSERT_TRUE(!timer_parse_duration_text("25 hours", 86400, &seconds));
    ASSERT_TRUE(!timer_parse_duration_text("2 minutes 2 minutes", 86400, &seconds));
    return true;
}

static bool test_timer_format_clock_compacts_under_one_hour(void) {
    char text[16];

    timer_format_clock(5, text, sizeof(text));
    ASSERT_TRUE(strcmp(text, "00:05") == 0);

    timer_format_clock(90, text, sizeof(text));
    ASSERT_TRUE(strcmp(text, "01:30") == 0);

    timer_format_clock(3720, text, sizeof(text));
    ASSERT_TRUE(strcmp(text, "1:02:00") == 0);
    return true;
}

const test_case_t g_timer_parse_tests[] = {
    {"timer_parse_accepts_digit_and_word_durations", test_timer_parse_accepts_digit_and_word_durations},
    {"timer_parse_rejects_invalid_or_out_of_range_text", test_timer_parse_rejects_invalid_or_out_of_range_text},
    {"timer_format_clock_compacts_under_one_hour", test_timer_format_clock_compacts_under_one_hour},
};

const int g_timer_parse_test_count = (int) (sizeof(g_timer_parse_tests) / sizeof(g_timer_parse_tests[0]));
