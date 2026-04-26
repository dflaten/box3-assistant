#include <stdarg.h>
#include <stdio.h>

#include "weather/weather_format.h"

#define WEATHER_PRECIP_TRACE_THRESHOLD_IN 0.05f
#define WEATHER_SNOW_TRACE_THRESHOLD_IN   0.1f

static void weather_append_text(char *buffer, size_t buffer_size, size_t *offset, const char *format, ...) {
    if (buffer == NULL || buffer_size == 0 || offset == NULL || *offset >= buffer_size) {
        return;
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer + *offset, buffer_size - *offset, format, args);
    va_end(args);
    if (written <= 0) {
        return;
    }

    size_t advance = (size_t) written;
    if (advance >= (buffer_size - *offset)) {
        *offset = buffer_size - 1;
        return;
    }

    *offset += advance;
}

static void
weather_append_spoken_precipitation(const weather_report_t *report, char *buffer, size_t buffer_size, size_t *offset) {
    if (report->snowfall_amount_in >= WEATHER_SNOW_TRACE_THRESHOLD_IN) {
        weather_append_text(
            buffer, buffer_size, offset, " Expect about %.1f inches of snow.", (double) report->snowfall_amount_in);
        return;
    }

    if (report->rain_amount_in >= WEATHER_PRECIP_TRACE_THRESHOLD_IN &&
        report->showers_amount_in >= WEATHER_PRECIP_TRACE_THRESHOLD_IN) {
        weather_append_text(buffer,
                            buffer_size,
                            offset,
                            " Expect about %.1f inches of rain and %.1f inches of showers.",
                            (double) report->rain_amount_in,
                            (double) report->showers_amount_in);
    } else if (report->rain_amount_in >= WEATHER_PRECIP_TRACE_THRESHOLD_IN) {
        weather_append_text(
            buffer, buffer_size, offset, " Expect about %.1f inches of rain.", (double) report->rain_amount_in);
    } else if (report->showers_amount_in >= WEATHER_PRECIP_TRACE_THRESHOLD_IN) {
        weather_append_text(
            buffer, buffer_size, offset, " Expect about %.1f inches of showers.", (double) report->showers_amount_in);
    } else if (report->precipitation_amount_in >= WEATHER_PRECIP_TRACE_THRESHOLD_IN) {
        weather_append_text(buffer,
                            buffer_size,
                            offset,
                            " Expect about %.1f inches of precipitation.",
                            (double) report->precipitation_amount_in);
    } else if (report->max_precip_probability == 0) {
        weather_append_text(buffer, buffer_size, offset, " No precipitation is expected.");
        return;
    }

    if (report->precipitation_hours >= 0.5f) {
        weather_append_text(
            buffer, buffer_size, offset, " It may last about %.0f hours.", (double) report->precipitation_hours);
    }
}

/**
 * @brief Format a multiline weather summary for the BOX-3 status display.
 * @param report The weather report to format.
 * @param buffer Destination buffer for the formatted text.
 * @param buffer_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
void weather_format_detail(const weather_report_t *report, char *buffer, size_t buffer_size) {
    if (report == NULL || buffer == NULL || buffer_size == 0) {
        return;
    }

    if (report->has_current_conditions) {
        snprintf(buffer,
                 buffer_size,
                 "Now in %s\n%dF %s\nHI %dF LO %dF\nWIND %d MPH\nPRECIP %d%%",
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
             "Tomorrow in %s\n%s\nHI %dF LO %dF\nPRECIP %d%%",
             report->location,
             report->summary,
             report->max_temp_f,
             report->min_temp_f,
             report->max_precip_probability);
}

/**
 * @brief Format a natural-language weather summary for spoken playback.
 * @param report The weather report to format.
 * @param buffer Destination buffer for the formatted text.
 * @param buffer_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
void weather_format_spoken(const weather_report_t *report, char *buffer, size_t buffer_size) {
    if (report == NULL || buffer == NULL || buffer_size == 0) {
        return;
    }

    size_t offset = 0;
    if (report->has_current_conditions) {
        weather_append_text(buffer,
                            buffer_size,
                            &offset,
                            "Today in %s, it is %d degrees and %s. The high is %d and the low is %d. "
                            "Wind is %d miles per hour. The chance of precipitation is %d percent.",
                            report->location,
                            report->current_temp_f,
                            report->summary,
                            report->max_temp_f,
                            report->min_temp_f,
                            report->wind_speed_mph,
                            report->max_precip_probability);
        weather_append_spoken_precipitation(report, buffer, buffer_size, &offset);
        return;
    }

    weather_append_text(buffer,
                        buffer_size,
                        &offset,
                        "Tomorrow in %s, expect %s. The high is %d and the low is %d. "
                        "The chance of precipitation is %d percent.",
                        report->location,
                        report->summary,
                        report->max_temp_f,
                        report->min_temp_f,
                        report->max_precip_probability);
    weather_append_spoken_precipitation(report, buffer, buffer_size, &offset);
}
