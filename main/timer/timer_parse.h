#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Parse a timer duration phrase into a total number of seconds.
 * @param text Input text to parse, typically returned by STT.
 * @param max_seconds Maximum accepted duration in seconds.
 * @param out_seconds Output total duration in seconds on success.
 * @return True when a valid duration was parsed, otherwise false.
 */
bool timer_parse_duration_text(const char *text, uint32_t max_seconds, uint32_t *out_seconds);

/**
 * @brief Format a duration as a compact countdown clock string.
 * @param total_seconds Total duration in seconds.
 * @param buffer Destination buffer for the formatted text.
 * @param buffer_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
void timer_format_clock(uint32_t total_seconds, char *buffer, size_t buffer_size);
