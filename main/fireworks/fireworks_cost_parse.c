#include "fireworks/fireworks_cost_parse.h"

#include <stdio.h>
#include <stdlib.h>

#include "cJSON.h"

#define FIREWORKS_COST_NANOS_PER_CENT 10000000

/**
 * @brief Read a Money amount's whole units, accepting either a JSON string or number.
 * @param subtotal Parsed Money object.
 * @param out_units Output receiving the whole currency units.
 * @return True when units were read or absent, false when a present units value is malformed.
 */
static bool fireworks_cost_read_units(const cJSON *subtotal, long long *out_units) {
    *out_units = 0;

    const cJSON *units = cJSON_GetObjectItemCaseSensitive(subtotal, "units");
    if (cJSON_IsString(units) && units->valuestring != NULL) {
        char *end = NULL;
        long long value = strtoll(units->valuestring, &end, 10);
        if (end == units->valuestring) {
            return false;
        }
        *out_units = value;
        return true;
    }
    if (cJSON_IsNumber(units)) {
        *out_units = (long long) units->valuedouble;
        return true;
    }

    return true;
}

/**
 * @brief Parse the subtotal from a Fireworks usageCosts:query response into whole cents.
 * @param json Raw JSON response body.
 * @param out_cents Output subtotal rounded to whole cents.
 * @return True when a subtotal object was found, otherwise false.
 */
bool fireworks_cost_parse_subtotal_cents(const char *json, uint32_t *out_cents) {
    if (json == NULL || out_cents == NULL) {
        return false;
    }

    *out_cents = 0;

    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const cJSON *subtotal = cJSON_GetObjectItemCaseSensitive(root, "subtotal");
    if (!cJSON_IsObject(subtotal)) {
        cJSON_Delete(root);
        return false;
    }

    long long units = 0;
    if (!fireworks_cost_read_units(subtotal, &units)) {
        cJSON_Delete(root);
        return false;
    }

    const cJSON *nanos_item = cJSON_GetObjectItemCaseSensitive(subtotal, "nanos");
    long long nanos = cJSON_IsNumber(nanos_item) ? (long long) nanos_item->valuedouble : 0;

    if (units < 0) {
        units = 0;
    }
    if (nanos < 0) {
        nanos = 0;
    }

    long long cents = (units * 100) + ((nanos + (FIREWORKS_COST_NANOS_PER_CENT / 2)) / FIREWORKS_COST_NANOS_PER_CENT);
    if (cents < 0) {
        cents = 0;
    }

    *out_cents = (uint32_t) cents;
    cJSON_Delete(root);
    return true;
}

/**
 * @brief Format a whole-cent amount as a dollar string.
 * @param cents Amount in whole cents.
 * @param buffer Destination buffer for the formatted text.
 * @param buffer_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
void fireworks_cost_format(uint32_t cents, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    snprintf(buffer, buffer_size, "$%u.%02u", (unsigned) (cents / 100U), (unsigned) (cents % 100U));
}
