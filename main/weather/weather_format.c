#include <stdio.h>

#include "weather/weather_format.h"

/**
 * @brief Format a multiline weather summary for the BOX-3 status display.
 * @param report The weather report to format.
 * @param buffer Destination buffer for the formatted text.
 * @param buffer_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
void weather_format_detail(const weather_report_t *report, char *buffer, size_t buffer_size)
{
    if (report == NULL || buffer == NULL || buffer_size == 0) {
        return;
    }

    if (report->has_current_conditions) {
        snprintf(buffer,
                 buffer_size,
                 "Now in %s\n%dF %s\nHI %dF LO %dF\nWIND %d MPH\nRAIN %d%%",
                 report->location,
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
             "Tomorrow in %s\n%s\nHI %dF LO %dF\nRAIN %d%%",
             report->location,
             report->summary,
             report->max_temp_f,
             report->min_temp_f,
             report->max_precip_probability);
}
