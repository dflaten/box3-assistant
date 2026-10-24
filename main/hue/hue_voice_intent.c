#include "hue/hue_voice_intent.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "hue/hue_group_match.h"

#define HUE_VOICE_INTENT_TEXT_LEN  192
#define HUE_VOICE_INTENT_MAX_WORDS 16

/**
 * @brief One imperative prefix that starts a Hue on/off command.
 */
typedef struct {
    /** Normalized prefix text. */
    const char *prefix;
    /** True when the prefix requests the on state. */
    bool on;
} hue_voice_intent_prefix_t;

static const hue_voice_intent_prefix_t HUE_VOICE_INTENT_PREFIXES[] = {
    {"turn on", true},
    {"turn off", false},
    {"switch on", true},
    {"switch off", false},
    {"lights on", true},
    {"lights off", false},
    {"light on", true},
    {"light off", false},
};

static const char *const HUE_VOICE_INTENT_LEADING_FILLERS[] = {
    "please",
    "the",
    "a",
    "an",
    "my",
    "our",
    "in",
    "at",
};

static const char *const HUE_VOICE_INTENT_TRAILING_FILLERS[] = {
    "please",
    "now",
    "thanks",
};

/**
 * @brief Lowercase a transcript and collapse punctuation and whitespace into single spaces.
 * @param src Source transcript to normalize.
 * @param dst Destination buffer for the normalized text.
 * @param dst_size Size of the destination buffer in bytes.
 * @return True when the normalized text is non-empty, otherwise false.
 */
static bool hue_voice_intent_normalize(const char *src, char *dst, size_t dst_size) {
    if (src == NULL || dst == NULL || dst_size == 0) {
        return false;
    }

    size_t out = 0;
    bool separator = false;
    for (size_t i = 0; src[i] != '\0'; ++i) {
        unsigned char c = (unsigned char) src[i];
        if (isalnum(c) || c == '\'') {
            if (separator && out > 0) {
                if (out + 1 >= dst_size) {
                    return false;
                }
                dst[out++] = ' ';
            }
            separator = false;
            if (out + 1 >= dst_size) {
                return false;
            }
            dst[out++] = (char) tolower(c);
        } else {
            separator = true;
        }
    }

    dst[out] = '\0';
    return out > 0;
}

/**
 * @brief Count whitespace-separated words in normalized text.
 * @param text Normalized text to inspect.
 * @return Number of words in the text.
 */
static size_t hue_voice_intent_word_count(const char *text) {
    size_t count = 0;
    bool in_word = false;
    for (const char *cursor = text; cursor != NULL && *cursor != '\0'; ++cursor) {
        if (*cursor == ' ') {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            count++;
        }
    }
    return count;
}

/**
 * @brief Check whether normalized text starts with a whole-word prefix.
 * @param text Normalized text to inspect.
 * @param prefix Whole-word prefix to look for.
 * @param out_rest Optional output receiving the text after the prefix and following spaces.
 * @return True when the prefix matches at the start of the text, otherwise false.
 */
static bool hue_voice_intent_starts_with(const char *text, const char *prefix, const char **out_rest) {
    size_t prefix_len = strlen(prefix);
    if (strncmp(text, prefix, prefix_len) != 0) {
        return false;
    }
    if (text[prefix_len] != '\0' && text[prefix_len] != ' ') {
        return false;
    }

    const char *rest = text + prefix_len;
    while (*rest == ' ') {
        rest++;
    }
    if (out_rest != NULL) {
        *out_rest = rest;
    }
    return true;
}

/**
 * @brief Check whether normalized text ends with a whole-word suffix.
 * @param text Normalized text to inspect.
 * @param suffix Whole-word suffix to look for.
 * @return True when the suffix matches at the end of the text, otherwise false.
 */
static bool hue_voice_intent_ends_with(const char *text, const char *suffix) {
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > text_len) {
        return false;
    }

    const char *tail = text + text_len - suffix_len;
    if (strcmp(tail, suffix) != 0) {
        return false;
    }
    /* Our callers pass suffixes with a leading space, which already delimits the word. */
    if (suffix[0] == ' ') {
        return true;
    }
    return tail == text || tail[-1] == ' ';
}

/**
 * @brief Skip leading filler words such as "please" and "the".
 * @param text Normalized text to advance.
 * @return Pointer to the first non-filler word.
 */
static const char *hue_voice_intent_skip_leading_fillers(const char *text) {
    const char *cursor = text;
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < sizeof(HUE_VOICE_INTENT_LEADING_FILLERS) / sizeof(HUE_VOICE_INTENT_LEADING_FILLERS[0]);
             ++i) {
            const char *rest = NULL;
            if (hue_voice_intent_starts_with(cursor, HUE_VOICE_INTENT_LEADING_FILLERS[i], &rest)) {
                cursor = rest;
                changed = true;
                break;
            }
        }
    }
    return cursor;
}

/**
 * @brief Remove trailing filler words such as "please" and "now".
 * @param text Normalized text to shorten in place.
 * @return This function does not return a value.
 */
static void hue_voice_intent_trim_trailing_fillers(char *text) {
    bool changed = true;
    while (changed) {
        changed = false;
        size_t text_len = strlen(text);
        for (size_t i = 0; i < sizeof(HUE_VOICE_INTENT_TRAILING_FILLERS) / sizeof(HUE_VOICE_INTENT_TRAILING_FILLERS[0]);
             ++i) {
            size_t filler_len = strlen(HUE_VOICE_INTENT_TRAILING_FILLERS[i]);
            if (text_len > filler_len &&
                strcmp(text + text_len - filler_len, HUE_VOICE_INTENT_TRAILING_FILLERS[i]) == 0 &&
                text[text_len - filler_len - 1] == ' ') {
                text[text_len - filler_len - 1] = '\0';
                changed = true;
                break;
            }
        }
    }
}

/**
 * @brief Extract the group portion of a "turn the kitchen on" style command.
 * @param text Normalized command text.
 * @param out_on Output receiving the requested on/off state.
 * @param out_group Destination buffer for the group portion.
 * @param out_group_size Size of the group buffer in bytes.
 * @return True when the text uses the trailing on/off form, otherwise false.
 */
static bool hue_voice_intent_extract_trailing(const char *text, bool *out_on, char *out_group, size_t out_group_size) {
    const char *verb_rest = NULL;
    if (!hue_voice_intent_starts_with(text, "turn", &verb_rest) &&
        !hue_voice_intent_starts_with(text, "switch", &verb_rest)) {
        return false;
    }

    const char *suffix = NULL;
    bool on = false;
    if (hue_voice_intent_ends_with(text, " on")) {
        suffix = " on";
        on = true;
    } else if (hue_voice_intent_ends_with(text, " off")) {
        suffix = " off";
        on = false;
    } else {
        return false;
    }

    size_t start = (size_t) (verb_rest - text);
    size_t end = strlen(text) - strlen(suffix);
    while (start < end && text[start] == ' ') {
        start++;
    }
    if (start >= end) {
        return false;
    }

    size_t group_len = end - start;
    if (group_len + 1 > out_group_size) {
        return false;
    }

    memcpy(out_group, text + start, group_len);
    out_group[group_len] = '\0';
    if (out_on != NULL) {
        *out_on = on;
    }
    return true;
}

bool hue_voice_intent_parse(const char *transcript,
                            const hue_group_t *groups,
                            size_t group_count,
                            hue_voice_intent_t *out_intent) {
    if (transcript == NULL || groups == NULL || group_count == 0 || out_intent == NULL) {
        return false;
    }

    /* Punctuation is normally stripped below, so reject an explicit question first. */
    if (strchr(transcript, '?') != NULL) {
        return false;
    }

    char text[HUE_VOICE_INTENT_TEXT_LEN];
    if (!hue_voice_intent_normalize(transcript, text, sizeof(text))) {
        return false;
    }
    if (hue_voice_intent_word_count(text) > HUE_VOICE_INTENT_MAX_WORDS) {
        return false;
    }
    if (strstr(text, " and ") != NULL || strstr(text, " or ") != NULL || strstr(text, " then ") != NULL ||
        strstr(text, " if ") != NULL || strstr(text, " when ") != NULL || strstr(text, " unless ") != NULL ||
        strstr(text, " while ") != NULL) {
        return false;
    }

    hue_voice_intent_trim_trailing_fillers(text);
    const char *command = hue_voice_intent_skip_leading_fillers(text);
    if (command[0] == '\0') {
        return false;
    }

    char group_text[HUE_VOICE_INTENT_TEXT_LEN];
    bool on = false;
    bool matched_prefix = false;
    for (size_t i = 0; i < sizeof(HUE_VOICE_INTENT_PREFIXES) / sizeof(HUE_VOICE_INTENT_PREFIXES[0]); ++i) {
        const char *rest = NULL;
        if (hue_voice_intent_starts_with(command, HUE_VOICE_INTENT_PREFIXES[i].prefix, &rest)) {
            on = HUE_VOICE_INTENT_PREFIXES[i].on;
            if (strlen(rest) + 1 > sizeof(group_text)) {
                return false;
            }
            memcpy(group_text, rest, strlen(rest) + 1);
            matched_prefix = true;
            break;
        }
    }

    if (!matched_prefix && !hue_voice_intent_extract_trailing(command, &on, group_text, sizeof(group_text))) {
        return false;
    }

    const char *group_start = hue_voice_intent_skip_leading_fillers(group_text);
    if (group_start[0] == '\0') {
        return false;
    }
    memmove(group_text, group_start, strlen(group_start) + 1);
    hue_voice_intent_trim_trailing_fillers(group_text);
    if (group_text[0] == '\0') {
        return false;
    }

    size_t group_index = 0;
    if (!hue_group_match_find(groups, group_count, group_text, &group_index)) {
        return false;
    }

    out_intent->group_index = group_index;
    out_intent->on = on;
    return true;
}
