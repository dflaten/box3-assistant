#pragma once

#include "esp_err.h"

#include "time/time_types.h"

esp_err_t time_client_fetch_location_time(const char *query, time_report_t *out_report);
esp_err_t time_client_cancel_active_request(void);
