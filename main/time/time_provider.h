#pragma once

#include "esp_err.h"

#include "time/time_types.h"

typedef struct {
    esp_err_t (*fetch_location)(const char *query, time_location_t *out_location);
    esp_err_t (*fetch_utc_offset)(double latitude, double longitude, int32_t *out_utc_offset_seconds);
    esp_err_t (*cancel_active_request)(void);
} time_provider_t;
