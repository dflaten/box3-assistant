#pragma once

#include <stdbool.h>

#define WEATHER_SUMMARY_LEN 24
#define WEATHER_BRIEF_TEXT_LEN 64
#define WEATHER_DATE_LEN 16
#define WEATHER_DETAIL_TEXT_LEN 160

typedef enum {
    WEATHER_FORECAST_TODAY = 0,
    WEATHER_FORECAST_TOMORROW = 1,
} weather_forecast_day_t;

typedef struct {
    char date[WEATHER_DATE_LEN];
    int current_temp_f;
    int max_temp_f;
    int min_temp_f;
    int max_precip_probability;
    int wind_speed_mph;
    bool has_current_conditions;
    char summary[WEATHER_SUMMARY_LEN];
} weather_report_t;
