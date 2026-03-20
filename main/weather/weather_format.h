#pragma once

#include <stddef.h>

#include "weather/weather_types.h"

void weather_format_brief(const weather_report_t *report, char *buffer, size_t buffer_size);
void weather_format_detail(const weather_report_t *report, char *buffer, size_t buffer_size);
