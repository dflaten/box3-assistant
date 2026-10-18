#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FIREWORKS_COST_TEXT_LEN 16

/**
 * @brief Parse the subtotal from a Fireworks usageCosts:query response into whole cents.
 * @param json Raw JSON response body.
 * @param out_cents Output subtotal rounded to whole cents.
 * @return True when a subtotal object was found, otherwise false.
 * @note The Fireworks Money subtotal carries whole `units` plus `nanos` (10^-9), so the value is rounded to cents.
 */
bool fireworks_cost_parse_subtotal_cents(const char *json, uint32_t *out_cents);

/**
 * @brief Format a whole-cent amount as a dollar string.
 * @param cents Amount in whole cents.
 * @param buffer Destination buffer for the formatted text.
 * @param buffer_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
void fireworks_cost_format(uint32_t cents, char *buffer, size_t buffer_size);
