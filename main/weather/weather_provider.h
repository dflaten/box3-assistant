#pragma once

#include "esp_err.h"
#include "weather/weather_types.h"

typedef struct {
    const char *name;
    esp_err_t (*fetch_forecast)(weather_forecast_day_t day, weather_report_t *out_report);
    esp_err_t (*cancel_active_request)(void);
} weather_provider_t;
