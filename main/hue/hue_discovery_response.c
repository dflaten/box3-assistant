#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#include "hue/hue_discovery_response.h"

static bool header_name_matches(const char *line, size_t line_len, const char *header_name) {
    size_t header_len = strlen(header_name);
    if (line_len <= header_len || line_len < header_len + 1 || line[header_len] != ':') {
        return false;
    }

    for (size_t i = 0; i < header_len; ++i) {
        if (tolower((unsigned char) line[i]) != tolower((unsigned char) header_name[i])) {
            return false;
        }
    }

    return true;
}

bool hue_discovery_extract_header_value(const char *response,
                                        const char *header_name,
                                        char *out_value,
                                        size_t out_value_size) {
    if (response == NULL || header_name == NULL || out_value == NULL || out_value_size == 0) {
        return false;
    }

    out_value[0] = '\0';

    const char *line = response;
    while (*line != '\0') {
        const char *line_end = strstr(line, "\r\n");
        if (line_end == NULL) {
            line_end = line + strlen(line);
        }

        size_t line_len = (size_t) (line_end - line);
        if (header_name_matches(line, line_len, header_name)) {
            const char *value = line + strlen(header_name) + 1;
            while (*value == ' ' || *value == '\t') {
                value++;
            }

            const char *value_end = line_end;
            while (value_end > value && (value_end[-1] == ' ' || value_end[-1] == '\t')) {
                value_end--;
            }

            size_t copy_len = (size_t) (value_end - value);
            if (copy_len >= out_value_size) {
                copy_len = out_value_size - 1;
            }

            memcpy(out_value, value, copy_len);
            out_value[copy_len] = '\0';
            return true;
        }

        if (*line_end == '\0') {
            break;
        }
        line = line_end + 2;
    }

    return false;
}

bool hue_discovery_response_is_hue_bridge(const char *response) {
    char bridge_id[32];
    return hue_discovery_extract_header_value(response, "hue-bridgeid", bridge_id, sizeof(bridge_id));
}
