#pragma once

#include "esp_err.h"
#include "weather/weather_format.h"

const char *weather_client_provider_name(void);
esp_err_t weather_client_fetch_today(weather_report_t *out_report);
esp_err_t weather_client_fetch_tomorrow(weather_report_t *out_report);
esp_err_t weather_client_cancel_active_request(void);
