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
    ASSERT_TRUE(strstr(detail, "PRECIP 1%") != NULL);
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
    ASSERT_TRUE(strstr(detail, "PRECIP 40%") != NULL);
    ASSERT_TRUE(strstr(detail, "Now in ") == NULL);
    return true;
}

static bool test_weather_spoken_today_mentions_current_conditions(void) {
    weather_report_t report = {
        .location = "New York City, NY",
        .current_temp_f = 53,
        .max_temp_f = 61,
        .min_temp_f = 27,
        .max_precip_probability = 1,
        .precipitation_amount_in = 0.2f,
        .rain_amount_in = 0.2f,
        .precipitation_hours = 2.0f,
        .wind_speed_mph = 7,
        .has_current_conditions = true,
        .summary = "cloudy",
    };
    char spoken[WEATHER_SPOKEN_TEXT_LEN];

    weather_format_spoken(&report, spoken, sizeof(spoken));

    ASSERT_TRUE(strstr(spoken, "Today in New York City, NY, it is 53 degrees and cloudy.") != NULL);
    ASSERT_TRUE(strstr(spoken, "The high is 61 and the low is 27.") != NULL);
    ASSERT_TRUE(strstr(spoken, "Wind is 7 miles per hour. The chance of precipitation is 1 percent.") != NULL);
    ASSERT_TRUE(strstr(spoken, "Expect about 0.2 inches of rain.") != NULL);
    ASSERT_TRUE(strstr(spoken, "It may last about 2 hours.") != NULL);
    return true;
}

static bool test_weather_spoken_tomorrow_uses_forecast_wording(void) {
    weather_report_t report = {
        .location = "New York City, NY",
        .max_temp_f = 49,
        .min_temp_f = 31,
        .max_precip_probability = 40,
        .snowfall_amount_in = 2.3f,
        .has_current_conditions = false,
        .summary = "rain",
    };
    char spoken[WEATHER_SPOKEN_TEXT_LEN];

    weather_format_spoken(&report, spoken, sizeof(spoken));

    ASSERT_TRUE(strstr(spoken, "Tomorrow in New York City, NY, expect rain.") != NULL);
    ASSERT_TRUE(strstr(spoken, "The high is 49 and the low is 31. The chance of precipitation is 40 percent.") != NULL);
    ASSERT_TRUE(strstr(spoken, "Expect about 2.3 inches of snow.") != NULL);
    ASSERT_TRUE(strstr(spoken, "Wind is ") == NULL);
    return true;
}

const test_case_t g_weather_format_tests[] = {
    {"weather_detail_today_includes_date_and_current_conditions",
     test_weather_detail_today_includes_date_and_current_conditions},
    {"weather_detail_tomorrow_shows_date_without_now_line", test_weather_detail_tomorrow_shows_date_without_now_line},
    {"weather_spoken_today_mentions_current_conditions", test_weather_spoken_today_mentions_current_conditions},
    {"weather_spoken_tomorrow_uses_forecast_wording", test_weather_spoken_tomorrow_uses_forecast_wording},
};

const int g_weather_format_test_count = (int) (sizeof(g_weather_format_tests) / sizeof(g_weather_format_tests[0]));
