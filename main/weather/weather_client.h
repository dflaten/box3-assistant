#pragma once

#include <stddef.h>

#include "esp_err.h"

#define WEATHER_SUMMARY_LEN 24
#define WEATHER_BRIEF_TEXT_LEN 64
#define WEATHER_DETAIL_TEXT_LEN 128

typedef struct {
    int current_temp_f;
    int max_temp_f;
    int min_temp_f;
    int max_precip_probability;
    int wind_speed_mph;
    char summary[WEATHER_SUMMARY_LEN];
} weather_report_t;

esp_err_t weather_client_fetch_today(weather_report_t *out_report);
void weather_client_format_brief(const weather_report_t *report, char *buffer, size_t buffer_size);
void weather_client_format_detail(const weather_report_t *report, char *buffer, size_t buffer_size);
