#include "hue/hue_group_match.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

#define HUE_GROUP_MATCH_NAME_LEN   96
#define HUE_GROUP_MATCH_MIN_SUBSTR 3

/* Match quality tiers, higher is better. */
#define HUE_GROUP_MATCH_TIER_NONE      0
#define HUE_GROUP_MATCH_TIER_SUBSTRING 1
#define HUE_GROUP_MATCH_TIER_WORD      2
#define HUE_GROUP_MATCH_TIER_EXACT     3

/**
 * @brief Copy text into a fixed buffer while converting it to lowercase.
 * @param dst Destination buffer for the lowercase result.
 * @param dst_size Size of the destination buffer in bytes.
 * @param src Source text to copy and normalize.
 * @return This function does not return a value.
 */
static void hue_group_match_lowercase(char *dst, size_t dst_size, const char *src) {
    if (dst == NULL || dst_size == 0) {
        return;
    }

    size_t i = 0;
    for (; i + 1 < dst_size && src != NULL && src[i] != '\0'; ++i) {
        dst[i] = (char) tolower((unsigned char) src[i]);
    }
    dst[i] = '\0';
}

/**
 * @brief Check whether a needle appears in a haystack as a whole word.
 * @param haystack Normalized text to search.
 * @param needle Normalized text to find.
 * @return True when the needle appears delimited by non-alphanumeric characters, otherwise false.
 */
static bool hue_group_match_word_contains(const char *haystack, const char *needle) {
    size_t needle_len = strlen(needle);
    if (needle_len == 0) {
        return false;
    }

    const char *cursor = haystack;
    while ((cursor = strstr(cursor, needle)) != NULL) {
        char before = cursor == haystack ? ' ' : cursor[-1];
        char after = cursor[needle_len];
        if (!isalnum((unsigned char) before) && !isalnum((unsigned char) after)) {
            return true;
        }
        cursor++;
    }

    return false;
}

/**
 * @brief Score how well a candidate group name matches a query.
 * @param candidate Normalized candidate group name.
 * @param query Normalized query name.
 * @return Match tier for the pair, where a higher value means a stronger match.
 */
static int hue_group_match_tier(const char *candidate, const char *query) {
    size_t candidate_len = strlen(candidate);
    size_t query_len = strlen(query);
    if (candidate_len == 0 || query_len == 0) {
        return HUE_GROUP_MATCH_TIER_NONE;
    }

    if (strcmp(candidate, query) == 0) {
        return HUE_GROUP_MATCH_TIER_EXACT;
    }

    if (hue_group_match_word_contains(candidate, query) || hue_group_match_word_contains(query, candidate)) {
        return HUE_GROUP_MATCH_TIER_WORD;
    }

    if (candidate_len >= HUE_GROUP_MATCH_MIN_SUBSTR && query_len >= HUE_GROUP_MATCH_MIN_SUBSTR &&
        (strstr(candidate, query) != NULL || strstr(query, candidate) != NULL)) {
        return HUE_GROUP_MATCH_TIER_SUBSTRING;
    }

    return HUE_GROUP_MATCH_TIER_NONE;
}

/**
 * @brief Find the runtime Hue group that best matches a spoken or model-provided name.
 * @param groups Runtime Hue groups to search.
 * @param group_count Number of valid entries in groups.
 * @param name Group name to match, typically returned by a model tool call.
 * @param out_index Optional output receiving the matched group index.
 * @return True when a group matched, otherwise false.
 * @note Stronger match tiers win, and ties are broken by the candidate whose length is closest to the query.
 */
bool hue_group_match_find(const hue_group_t *groups, size_t group_count, const char *name, size_t *out_index) {
    if (groups == NULL || name == NULL || name[0] == '\0') {
        return false;
    }

    char query[HUE_GROUP_MATCH_NAME_LEN];
    hue_group_match_lowercase(query, sizeof(query), name);
    size_t query_len = strlen(query);

    bool found = false;
    size_t best_index = 0;
    int best_tier = HUE_GROUP_MATCH_TIER_NONE;
    size_t best_diff = 0;

    for (size_t i = 0; i < group_count; ++i) {
        char candidate[HUE_GROUP_MATCH_NAME_LEN];
        hue_group_match_lowercase(candidate, sizeof(candidate), groups[i].name);

        int tier = hue_group_match_tier(candidate, query);
        if (tier == HUE_GROUP_MATCH_TIER_NONE) {
            continue;
        }

        size_t candidate_len = strlen(candidate);
        size_t diff = candidate_len > query_len ? candidate_len - query_len : query_len - candidate_len;

        if (!found || tier > best_tier || (tier == best_tier && diff < best_diff)) {
            found = true;
            best_index = i;
            best_tier = tier;
            best_diff = diff;
        }
    }

    if (found && out_index != NULL) {
        *out_index = best_index;
    }
    return found;
}
