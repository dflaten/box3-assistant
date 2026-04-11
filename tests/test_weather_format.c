#include <stdbool.h>
#include <string.h>

#include "test_support.h"
#include "weather/weather_format.h"

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

const test_case_t g_weather_format_tests[] = {
    {"weather_detail_today_includes_date_and_current_conditions",
     test_weather_detail_today_includes_date_and_current_conditions},
    {"weather_detail_tomorrow_shows_date_without_now_line", test_weather_detail_tomorrow_shows_date_without_now_line},
};

const int g_weather_format_test_count = (int) (sizeof(g_weather_format_tests) / sizeof(g_weather_format_tests[0]));
