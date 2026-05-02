#include "timer/timer_parse.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *word;
    int value;
} number_word_t;

static const number_word_t s_small_numbers[] = {
    {"zero", 0},     {"one", 1},      {"two", 2},        {"three", 3},     {"four", 4},
    {"five", 5},     {"six", 6},      {"seven", 7},      {"eight", 8},     {"nine", 9},
    {"ten", 10},     {"eleven", 11},  {"twelve", 12},    {"thirteen", 13}, {"fourteen", 14},
    {"fifteen", 15}, {"sixteen", 16}, {"seventeen", 17}, {"eighteen", 18}, {"nineteen", 19},
};

static const number_word_t s_tens_numbers[] = {
    {"twenty", 20},
    {"thirty", 30},
    {"forty", 40},
    {"fifty", 50},
    {"sixty", 60},
    {"seventy", 70},
    {"eighty", 80},
    {"ninety", 90},
};

/**
 * @brief Normalize freeform duration text into lowercase space-delimited tokens.
 * @param src Input text to normalize.
 * @param dst Destination buffer.
 * @param dst_size Size of the destination buffer in bytes.
 * @return True when normalization succeeds, otherwise false.
 */
static bool normalize_text(const char *src, char *dst, size_t dst_size) {
    if (src == NULL || dst == NULL || dst_size == 0) {
        return false;
    }

    size_t out = 0;
    bool last_was_space = true;
    for (size_t i = 0; src[i] != '\0'; ++i) {
        unsigned char c = (unsigned char) src[i];
        char normalized = '\0';
        if (isalnum(c)) {
            normalized = (char) tolower(c);
            last_was_space = false;
        } else {
            if (last_was_space) {
                continue;
            }
            normalized = ' ';
            last_was_space = true;
        }

        if (out + 1 >= dst_size) {
            return false;
        }
        dst[out++] = normalized;
    }

    while (out > 0 && dst[out - 1] == ' ') {
        out--;
    }
    dst[out] = '\0';
    return out > 0;
}

/**
 * @brief Look up a fixed small-number word.
 * @param token Lowercase token to match.
 * @return Word value on success, or -1 when not recognized.
 */
static int parse_small_number_word(const char *token) {
    for (size_t i = 0; i < sizeof(s_small_numbers) / sizeof(s_small_numbers[0]); ++i) {
        if (strcmp(token, s_small_numbers[i].word) == 0) {
            return s_small_numbers[i].value;
        }
    }
    return -1;
}

/**
 * @brief Look up a tens-number word.
 * @param token Lowercase token to match.
 * @return Word value on success, or -1 when not recognized.
 */
static int parse_tens_number_word(const char *token) {
    for (size_t i = 0; i < sizeof(s_tens_numbers) / sizeof(s_tens_numbers[0]); ++i) {
        if (strcmp(token, s_tens_numbers[i].word) == 0) {
            return s_tens_numbers[i].value;
        }
    }
    return -1;
}

/**
 * @brief Parse a single token as either an integer or an English number word.
 * @param token Lowercase token to parse.
 * @param current_value Current in-progress number value for scale words.
 * @param out_value Updated number value on success.
 * @return True when the token contributed to a number, otherwise false.
 */
static bool parse_number_token(const char *token, uint32_t current_value, uint32_t *out_value) {
    if (token == NULL || out_value == NULL || token[0] == '\0') {
        return false;
    }

    char *end = NULL;
    unsigned long number = strtoul(token, &end, 10);
    if (end != NULL && *end == '\0') {
        *out_value = (uint32_t) number;
        return true;
    }

    int small = parse_small_number_word(token);
    if (small >= 0) {
        *out_value = current_value + (uint32_t) small;
        return true;
    }

    int tens = parse_tens_number_word(token);
    if (tens >= 0) {
        *out_value = current_value + (uint32_t) tens;
        return true;
    }

    if (strcmp(token, "hundred") == 0 && current_value > 0) {
        *out_value = current_value * 100U;
        return true;
    }

    return false;
}

/**
 * @brief Parse a duration unit token into its equivalent scale in seconds.
 * @param token Lowercase token to classify.
 * @return Unit scale in seconds, or zero when not recognized as a duration unit.
 */
static uint32_t parse_unit_seconds(const char *token) {
    if (token == NULL) {
        return 0;
    }
    if (strcmp(token, "second") == 0 || strcmp(token, "seconds") == 0) {
        return 1;
    }
    if (strcmp(token, "minute") == 0 || strcmp(token, "minutes") == 0) {
        return 60;
    }
    if (strcmp(token, "hour") == 0 || strcmp(token, "hours") == 0) {
        return 3600;
    }
    return 0;
}

bool timer_parse_duration_text(const char *text, uint32_t max_seconds, uint32_t *out_seconds) {
    if (text == NULL || out_seconds == NULL || max_seconds == 0) {
        return false;
    }

    char normalized[160];
    if (!normalize_text(text, normalized, sizeof(normalized))) {
        return false;
    }

    uint32_t total_seconds = 0;
    uint32_t current_number = 0;
    bool have_number = false;
    bool used_hours = false;
    bool used_minutes = false;
    bool used_seconds = false;

    for (char *token = strtok(normalized, " "); token != NULL; token = strtok(NULL, " ")) {
        if (strcmp(token, "and") == 0 || strcmp(token, "for") == 0 || strcmp(token, "a") == 0 ||
            strcmp(token, "an") == 0 || strcmp(token, "timer") == 0 || strcmp(token, "set") == 0) {
            continue;
        }

        uint32_t parsed_number = 0;
        if (parse_number_token(token, current_number, &parsed_number)) {
            current_number = parsed_number;
            have_number = true;
            continue;
        }

        uint32_t unit_seconds = parse_unit_seconds(token);
        if (unit_seconds == 0 || !have_number) {
            return false;
        }

        if ((unit_seconds == 3600 && used_hours) || (unit_seconds == 60 && used_minutes) ||
            (unit_seconds == 1 && used_seconds)) {
            return false;
        }

        if (current_number > (UINT32_MAX / unit_seconds)) {
            return false;
        }
        total_seconds += current_number * unit_seconds;
        if (total_seconds == 0 || total_seconds > max_seconds) {
            return false;
        }

        used_hours = used_hours || unit_seconds == 3600;
        used_minutes = used_minutes || unit_seconds == 60;
        used_seconds = used_seconds || unit_seconds == 1;
        current_number = 0;
        have_number = false;
    }

    if (have_number || total_seconds == 0 || total_seconds > max_seconds) {
        return false;
    }

    *out_seconds = total_seconds;
    return true;
}

void timer_format_clock(uint32_t total_seconds, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    uint32_t hours = total_seconds / 3600U;
    uint32_t minutes = (total_seconds % 3600U) / 60U;
    uint32_t seconds = total_seconds % 60U;

    if (hours > 0) {
        snprintf(buffer,
                 buffer_size,
                 "%lu:%02lu:%02lu",
                 (unsigned long) hours,
                 (unsigned long) minutes,
                 (unsigned long) seconds);
    } else {
        snprintf(buffer, buffer_size, "%02lu:%02lu", (unsigned long) minutes, (unsigned long) seconds);
    }
}
