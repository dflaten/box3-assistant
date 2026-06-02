#include "time/time_open_meteo_parse.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

/**
 * @brief Encode a string for use as a URL query parameter value.
 * @param input Unencoded string.
 * @param output Destination buffer for the encoded value.
 * @param output_size Size of the destination buffer in bytes.
 * @return True when the full string was encoded into the destination buffer.
 */
bool time_open_meteo_url_encode_query_value(const char *input, char *output, size_t output_size) {
    static const char hex[] = "0123456789ABCDEF";
    if (input == NULL || output == NULL || output_size == 0) {
        return false;
    }

    size_t out = 0;
    for (size_t i = 0; input[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char) input[i];
        bool plain = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' ||
                     ch == '_' || ch == '.' || ch == '~';
        if (plain) {
            if (out + 1 >= output_size) {
                return false;
            }
            output[out++] = (char) ch;
        } else if (ch == ' ') {
            if (out + 1 >= output_size) {
                return false;
            }
            output[out++] = '+';
        } else {
            if (out + 3 >= output_size) {
                return false;
            }
            output[out++] = '%';
            output[out++] = hex[(ch >> 4) & 0x0F];
            output[out++] = hex[ch & 0x0F];
        }
    }

    output[out] = '\0';
    return true;
}

/**
 * @brief Copy a JSON string field into a destination buffer.
 * @param object JSON object containing the field.
 * @param key Field name to read.
 * @param buffer Destination buffer for the copied string.
 * @param buffer_size Size of the destination buffer in bytes.
 * @return True when a non-empty string field was copied.
 */
static bool copy_json_string(const cJSON *object, const char *key, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return false;
    }

    buffer[0] = '\0';
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL || item->valuestring[0] == '\0') {
        return false;
    }

    snprintf(buffer, buffer_size, "%s", item->valuestring);
    return true;
}

/**
 * @brief Compare two location labels with case-insensitive separator normalization.
 * @param left First label to compare.
 * @param right Second label to compare.
 * @return True when both labels contain the same words.
 */
static bool normalized_location_equals(const char *left, const char *right) {
    if (left == NULL || right == NULL || left[0] == '\0' || right[0] == '\0') {
        return false;
    }

    size_t left_index = 0;
    size_t right_index = 0;
    while (left[left_index] != '\0' || right[right_index] != '\0') {
        while (left[left_index] != '\0' && !isalnum((unsigned char) left[left_index])) {
            ++left_index;
        }
        while (right[right_index] != '\0' && !isalnum((unsigned char) right[right_index])) {
            ++right_index;
        }

        if (left[left_index] == '\0' || right[right_index] == '\0') {
            break;
        }
        if (tolower((unsigned char) left[left_index]) != tolower((unsigned char) right[right_index])) {
            return false;
        }
        ++left_index;
        ++right_index;
    }

    while (left[left_index] != '\0' && !isalnum((unsigned char) left[left_index])) {
        ++left_index;
    }
    while (right[right_index] != '\0' && !isalnum((unsigned char) right[right_index])) {
        ++right_index;
    }
    return left[left_index] == '\0' && right[right_index] == '\0';
}

/**
 * @brief Check whether a US state abbreviation names an admin region.
 * @param abbreviation Two-letter state abbreviation from user speech.
 * @param admin1 Provider admin region label.
 * @return True when the abbreviation expands to the admin region.
 */
static bool us_state_abbreviation_matches_admin1(const char *abbreviation, const char *admin1) {
    if (abbreviation == NULL || admin1 == NULL || strlen(abbreviation) != 2) {
        return false;
    }

    const struct {
        const char *abbr;
        const char *name;
    } states[] = {
        {"AL", "Alabama"},        {"AK", "Alaska"},        {"AZ", "Arizona"},
        {"AR", "Arkansas"},       {"CA", "California"},    {"CO", "Colorado"},
        {"CT", "Connecticut"},    {"DE", "Delaware"},      {"FL", "Florida"},
        {"GA", "Georgia"},        {"HI", "Hawaii"},        {"ID", "Idaho"},
        {"IL", "Illinois"},       {"IN", "Indiana"},       {"IA", "Iowa"},
        {"KS", "Kansas"},         {"KY", "Kentucky"},      {"LA", "Louisiana"},
        {"ME", "Maine"},          {"MD", "Maryland"},      {"MA", "Massachusetts"},
        {"MI", "Michigan"},       {"MN", "Minnesota"},     {"MS", "Mississippi"},
        {"MO", "Missouri"},       {"MT", "Montana"},       {"NE", "Nebraska"},
        {"NV", "Nevada"},         {"NH", "New Hampshire"}, {"NJ", "New Jersey"},
        {"NM", "New Mexico"},     {"NY", "New York"},      {"NC", "North Carolina"},
        {"ND", "North Dakota"},   {"OH", "Ohio"},          {"OK", "Oklahoma"},
        {"OR", "Oregon"},         {"PA", "Pennsylvania"},  {"RI", "Rhode Island"},
        {"SC", "South Carolina"}, {"SD", "South Dakota"},  {"TN", "Tennessee"},
        {"TX", "Texas"},          {"UT", "Utah"},          {"VT", "Vermont"},
        {"VA", "Virginia"},       {"WA", "Washington"},    {"WV", "West Virginia"},
        {"WI", "Wisconsin"},      {"WY", "Wyoming"},       {"DC", "District of Columbia"},
    };

    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); ++i) {
        if (normalized_location_equals(abbreviation, states[i].abbr) &&
            normalized_location_equals(admin1, states[i].name)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Check whether a country alias names a provider country or country code.
 * @param qualifier User-provided qualifier.
 * @param country Provider country label.
 * @param country_code Provider ISO country code.
 * @return True when the qualifier matches the country through a common alias.
 */
static bool country_alias_matches(const char *qualifier, const char *country, const char *country_code) {
    if (normalized_location_equals(qualifier, "USA") || normalized_location_equals(qualifier, "US")) {
        return normalized_location_equals(country, "United States") || normalized_location_equals(country_code, "US");
    }
    if (normalized_location_equals(qualifier, "UK")) {
        return normalized_location_equals(country, "United Kingdom") || normalized_location_equals(country_code, "GB");
    }
    return false;
}

/**
 * @brief Check whether provider metadata matches a user location qualifier.
 * @param qualifier User-provided trailing qualifier.
 * @param admin1 Provider admin region label.
 * @param country Provider country label.
 * @param country_code Provider ISO country code.
 * @return True when the qualifier identifies the admin region or country.
 */
static bool
metadata_matches_qualifier(const char *qualifier, const char *admin1, const char *country, const char *country_code) {
    return normalized_location_equals(qualifier, admin1) || normalized_location_equals(qualifier, country) ||
           normalized_location_equals(qualifier, country_code) ||
           us_state_abbreviation_matches_admin1(qualifier, admin1) ||
           country_alias_matches(qualifier, country, country_code);
}

/**
 * @brief Format a location label from provider name, admin region, and country fields.
 * @param name Provider location name.
 * @param admin1 Provider admin region label.
 * @param country Provider country label.
 * @param output Destination for the formatted location label.
 * @param output_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
static void
format_location_label(const char *name, const char *admin1, const char *country, char *output, size_t output_size) {
    if (output == NULL || output_size == 0) {
        return;
    }

    if (name == NULL || name[0] == '\0') {
        output[0] = '\0';
        return;
    }

    if (admin1 != NULL && admin1[0] != '\0' && !normalized_location_equals(name, admin1)) {
        snprintf(output, output_size, "%s, %s", name, admin1);
    } else if (country != NULL && country[0] != '\0') {
        snprintf(output, output_size, "%s, %s", name, country);
    } else {
        snprintf(output, output_size, "%s", name);
    }
}

/**
 * @brief Copy an Open-Meteo location result into the public location structure.
 * @param result JSON result object from the geocoding response.
 * @param out_location Output receiving the parsed location.
 * @param out_admin1 Output receiving the admin region label used for ranking.
 * @param admin1_size Size of the admin region destination buffer in bytes.
 * @param out_country Output receiving the country label used for ranking.
 * @param country_size Size of the country destination buffer in bytes.
 * @param out_country_code Output receiving the country code used for ranking.
 * @param country_code_size Size of the country code destination buffer in bytes.
 * @return ESP_OK on success, or an ESP error code when the result is incomplete.
 */
static esp_err_t parse_location_result(const cJSON *result,
                                       time_location_t *out_location,
                                       char *out_admin1,
                                       size_t admin1_size,
                                       char *out_country,
                                       size_t country_size,
                                       char *out_country_code,
                                       size_t country_code_size) {
    if (!cJSON_IsObject(result) || out_location == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char name[30];
    copy_json_string(result, "name", name, sizeof(name));
    copy_json_string(result, "admin1", out_admin1, admin1_size);
    copy_json_string(result, "country", out_country, country_size);
    copy_json_string(result, "country_code", out_country_code, country_code_size);
    copy_json_string(result, "timezone", out_location->timezone, sizeof(out_location->timezone));

    const cJSON *latitude = cJSON_GetObjectItemCaseSensitive(result, "latitude");
    const cJSON *longitude = cJSON_GetObjectItemCaseSensitive(result, "longitude");
    if (!cJSON_IsNumber(latitude) || !cJSON_IsNumber(longitude) || name[0] == '\0') {
        return ESP_ERR_INVALID_RESPONSE;
    }
    out_location->latitude = latitude->valuedouble;
    out_location->longitude = longitude->valuedouble;

    format_location_label(name, out_admin1, out_country, out_location->location, sizeof(out_location->location));
    return ESP_OK;
}

/**
 * @brief Parse an Open-Meteo geocoding response into a resolved location.
 * @param json JSON response body from the geocoding endpoint.
 * @param qualifier Optional admin region or country used to rank location results.
 * @param require_qualifier_match True to reject results that do not match the qualifier.
 * @param out_location Output receiving the first resolved location.
 * @return ESP_OK on success, or an ESP error code when the response is missing required data.
 */
esp_err_t time_open_meteo_parse_location_response(const char *json,
                                                  const char *qualifier,
                                                  bool require_qualifier_match,
                                                  time_location_t *out_location) {
    if (json == NULL || out_location == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_location = (time_location_t) {0};
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *results = cJSON_GetObjectItemCaseSensitive(root, "results");
    const cJSON *first = cJSON_IsArray(results) ? cJSON_GetArrayItem(results, 0) : NULL;
    if (!cJSON_IsObject(first)) {
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }

    bool has_qualifier = qualifier != NULL && qualifier[0] != '\0';
    esp_err_t fallback_err = ESP_ERR_NOT_FOUND;
    cJSON *result = NULL;
    cJSON_ArrayForEach(result, results) {
        time_location_t candidate = {0};
        char admin1[30];
        char country[30];
        char country_code[8];
        esp_err_t err = parse_location_result(
            result, &candidate, admin1, sizeof(admin1), country, sizeof(country), country_code, sizeof(country_code));
        if (err != ESP_OK) {
            fallback_err = err;
            continue;
        }

        if (!has_qualifier || metadata_matches_qualifier(qualifier, admin1, country, country_code)) {
            *out_location = candidate;
            cJSON_Delete(root);
            return ESP_OK;
        }
    }

    cJSON_Delete(root);
    if (require_qualifier_match) {
        return ESP_ERR_NOT_FOUND;
    }
    return fallback_err;
}

/**
 * @brief Parse an Open-Meteo forecast response UTC offset.
 * @param json JSON response body from the forecast endpoint.
 * @param out_utc_offset_seconds Output receiving seconds east of UTC.
 * @return ESP_OK on success, or an ESP error code when the response is missing the offset.
 */
esp_err_t time_open_meteo_parse_utc_offset_response(const char *json, int32_t *out_utc_offset_seconds) {
    if (json == NULL || out_utc_offset_seconds == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *offset = cJSON_GetObjectItemCaseSensitive(root, "utc_offset_seconds");
    if (!cJSON_IsNumber(offset)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *out_utc_offset_seconds = (int32_t) offset->valueint;
    cJSON_Delete(root);
    return ESP_OK;
}
