#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "time/time_types.h"

bool time_open_meteo_url_encode_query_value(const char *input, char *output, size_t output_size);
esp_err_t time_open_meteo_parse_location_response(const char *json,
                                                  const char *qualifier,
                                                  bool require_qualifier_match,
                                                  time_location_t *out_location);
esp_err_t time_open_meteo_parse_utc_offset_response(const char *json, int32_t *out_utc_offset_seconds);
