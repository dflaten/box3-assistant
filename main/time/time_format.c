#include "time/time_format.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Determine whether a character separates spoken location words.
 * @param ch Character to inspect.
 * @return True when the character should be treated as a word separator.
 */
static bool is_separator(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == ',' || ch == '.';
}

/**
 * @brief Find the first non-separator character in a string.
 * @param text Input string to scan.
 * @return Pointer to the first non-separator character, or the string terminator.
 */
static const char *skip_separators(const char *text) {
    while (text != NULL && is_separator(*text)) {
        ++text;
    }
    return text;
}

/**
 * @brief Check whether text begins with a case-insensitive phrase followed by a word boundary.
 * @param text Text to inspect.
 * @param prefix Phrase to match.
 * @return True when the phrase is present at the start of the text.
 */
static bool starts_with_phrase(const char *text, const char *prefix) {
    size_t prefix_len = strlen(prefix);
    for (size_t i = 0; i < prefix_len; ++i) {
        if (tolower((unsigned char) text[i]) != tolower((unsigned char) prefix[i])) {
            return false;
        }
    }

    return text[prefix_len] == '\0' || is_separator(text[prefix_len]);
}

/**
 * @brief Trim trailing separators from a fixed-size mutable string.
 * @param text String to trim in place.
 * @return This function does not return a value.
 */
static void trim_trailing_separators(char *text) {
    if (text == NULL) {
        return;
    }

    size_t len = strlen(text);
    while (len > 0 && is_separator(text[len - 1])) {
        text[len - 1] = '\0';
        --len;
    }
}

/**
 * @brief Normalize spoken location text into a geocoding search query.
 * @param text Raw transcribed text from the location follow-up prompt.
 * @param query Destination buffer for the normalized query.
 * @param query_size Size of the destination buffer in bytes.
 * @return True when a non-empty query was produced.
 */
bool time_format_normalize_location_query(const char *text, char *query, size_t query_size) {
    if (text == NULL || query == NULL || query_size == 0) {
        return false;
    }

    query[0] = '\0';
    const char *start = skip_separators(text);
    const char *prefixes[] = {
        "what time is it in",
        "what is the time in",
        "time in",
        "in",
        "for",
        "at",
    };

    bool stripped = true;
    while (stripped) {
        stripped = false;
        start = skip_separators(start);
        for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
            if (starts_with_phrase(start, prefixes[i])) {
                start = skip_separators(start + strlen(prefixes[i]));
                stripped = true;
                break;
            }
        }
    }

    if (start == NULL || start[0] == '\0') {
        return false;
    }

    snprintf(query, query_size, "%s", start);
    trim_trailing_separators(query);
    return query[0] != '\0';
}

/**
 * @brief Split a location query into a search phrase and trailing qualifier.
 * @param query Normalized location query to split.
 * @param qualifier_words Number of trailing words to move into the qualifier.
 * @param search_query Destination for the geocoding search phrase.
 * @param search_query_size Size of the search phrase destination buffer in bytes.
 * @param qualifier Destination for the trailing qualifier.
 * @param qualifier_size Size of the qualifier destination buffer in bytes.
 * @return True when both a search phrase and qualifier were produced.
 */
bool time_format_split_location_qualifier(const char *query,
                                          size_t qualifier_words,
                                          char *search_query,
                                          size_t search_query_size,
                                          char *qualifier,
                                          size_t qualifier_size) {
    if (query == NULL || qualifier_words == 0 || search_query == NULL || search_query_size == 0 || qualifier == NULL ||
        qualifier_size == 0) {
        return false;
    }

    search_query[0] = '\0';
    qualifier[0] = '\0';

    size_t end = strlen(query);
    while (end > 0 && is_separator(query[end - 1])) {
        --end;
    }

    size_t split = end;
    for (size_t words = 0; words < qualifier_words; ++words) {
        while (split > 0 && !is_separator(query[split - 1])) {
            --split;
        }
        while (split > 0 && is_separator(query[split - 1])) {
            --split;
        }
        if (split == 0 && words + 1 < qualifier_words) {
            return false;
        }
    }

    size_t qualifier_start = split;
    while (qualifier_start < end && is_separator(query[qualifier_start])) {
        ++qualifier_start;
    }
    if (split == 0 || qualifier_start >= end) {
        return false;
    }

    snprintf(search_query, search_query_size, "%.*s", (int) split, query);
    trim_trailing_separators(search_query);
    snprintf(qualifier, qualifier_size, "%.*s", (int) (end - qualifier_start), query + qualifier_start);
    trim_trailing_separators(qualifier);

    return search_query[0] != '\0' && qualifier[0] != '\0';
}

/**
 * @brief Format local clock text from a UTC epoch and UTC offset.
 * @param utc_now Current UTC epoch seconds.
 * @param utc_offset_seconds Offset from UTC in seconds for the target location.
 * @param time_buffer Destination buffer for the formatted clock text.
 * @param time_buffer_size Size of the time destination buffer in bytes.
 * @param date_buffer Destination buffer for the formatted date text.
 * @param date_buffer_size Size of the date destination buffer in bytes.
 * @return True when both strings were formatted successfully.
 */
bool time_format_from_utc_offset(time_t utc_now,
                                 int32_t utc_offset_seconds,
                                 char *time_buffer,
                                 size_t time_buffer_size,
                                 char *date_buffer,
                                 size_t date_buffer_size) {
    if (time_buffer == NULL || date_buffer == NULL || time_buffer_size == 0 || date_buffer_size == 0) {
        return false;
    }

    time_t local_epoch = utc_now + (time_t) utc_offset_seconds;
    struct tm local_time = {0};
    struct tm *converted = gmtime(&local_epoch);
    if (converted == NULL) {
        return false;
    }
    local_time = *converted;

    if (strftime(time_buffer, time_buffer_size, "%I:%M %p", &local_time) == 0 ||
        strftime(date_buffer, date_buffer_size, "%a %b %d", &local_time) == 0) {
        return false;
    }

    if (time_buffer[0] == '0' && time_buffer[1] != '\0') {
        memmove(time_buffer, time_buffer + 1, strlen(time_buffer));
    }

    return true;
}

/**
 * @brief Format concise UI detail text for a time report.
 * @param report Time report to summarize.
 * @param detail Destination buffer for the UI detail text.
 * @param detail_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
void time_format_detail(const time_report_t *report, char *detail, size_t detail_size) {
    if (detail == NULL || detail_size == 0) {
        return;
    }
    if (report == NULL) {
        detail[0] = '\0';
        return;
    }

    snprintf(detail, detail_size, "%s in %s", report->time_text, report->location);
}

/**
 * @brief Format spoken text for a time report.
 * @param report Time report to speak.
 * @param spoken Destination buffer for the spoken response.
 * @param spoken_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
void time_format_spoken(const time_report_t *report, char *spoken, size_t spoken_size) {
    if (spoken == NULL || spoken_size == 0) {
        return;
    }
    if (report == NULL) {
        spoken[0] = '\0';
        return;
    }

    snprintf(spoken, spoken_size, "It is %s in %s.", report->time_text, report->location);
}
