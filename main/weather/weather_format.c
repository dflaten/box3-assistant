#include <stdio.h>

#include "weather/weather_format.h"

void weather_format_brief(const weather_report_t *report, char *buffer, size_t buffer_size)
{
    if (report == NULL || buffer == NULL || buffer_size == 0) {
        return;
    }

    snprintf(buffer,
             buffer_size,
             "%s %d/%d %s",
             report->date,
             report->max_temp_f,
             report->min_temp_f,
             report->summary);
}

void weather_format_detail(const weather_report_t *report, char *buffer, size_t buffer_size)
{
    if (report == NULL || buffer == NULL || buffer_size == 0) {
        return;
    }

    if (report->has_current_conditions) {
        snprintf(buffer,
                 buffer_size,
                 "%s\nNOW %dF %s\nHI %dF LO %dF\nWIND %d MPH\nRAIN %d%%",
                 report->date,
                 report->current_temp_f,
                 report->summary,
                 report->max_temp_f,
                 report->min_temp_f,
                 report->wind_speed_mph,
                 report->max_precip_probability);
        return;
    }

    snprintf(buffer,
             buffer_size,
             "%s\n%s\nHI %dF LO %dF\nRAIN %d%%",
             report->date,
             report->summary,
             report->max_temp_f,
             report->min_temp_f,
             report->max_precip_probability);
}
