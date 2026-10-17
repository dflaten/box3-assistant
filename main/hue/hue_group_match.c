#include "hue/hue_group_match.h"

#include <ctype.h>
#include <string.h>

#define HUE_GROUP_MATCH_NAME_LEN   96
#define HUE_GROUP_MATCH_MIN_SUBSTR 3

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
 * @brief Check whether one normalized name is a substring of the other.
 * @param candidate Normalized candidate group name.
 * @param query Normalized query name.
 * @return True when either name contains the other, otherwise false.
 */
static bool hue_group_match_substring(const char *candidate, const char *query) {
    size_t candidate_len = strlen(candidate);
    size_t query_len = strlen(query);

    if (candidate_len < HUE_GROUP_MATCH_MIN_SUBSTR || query_len < HUE_GROUP_MATCH_MIN_SUBSTR) {
        return false;
    }

    return strstr(candidate, query) != NULL || strstr(query, candidate) != NULL;
}

/**
 * @brief Find the runtime Hue group that best matches a spoken or model-provided name.
 * @param groups Runtime Hue groups to search.
 * @param group_count Number of valid entries in groups.
 * @param name Group name to match, typically returned by a model tool call.
 * @param out_index Optional output receiving the matched group index.
 * @return True when a group matched, otherwise false.
 */
bool hue_group_match_find(const hue_group_t *groups, size_t group_count, const char *name, size_t *out_index) {
    if (groups == NULL || name == NULL || name[0] == '\0') {
        return false;
    }

    char query[HUE_GROUP_MATCH_NAME_LEN];
    hue_group_match_lowercase(query, sizeof(query), name);

    for (size_t i = 0; i < group_count; ++i) {
        char candidate[HUE_GROUP_MATCH_NAME_LEN];
        hue_group_match_lowercase(candidate, sizeof(candidate), groups[i].name);
        if (candidate[0] != '\0' && strcmp(candidate, query) == 0) {
            if (out_index != NULL) {
                *out_index = i;
            }
            return true;
        }
    }

    for (size_t i = 0; i < group_count; ++i) {
        char candidate[HUE_GROUP_MATCH_NAME_LEN];
        hue_group_match_lowercase(candidate, sizeof(candidate), groups[i].name);
        if (candidate[0] != '\0' && hue_group_match_substring(candidate, query)) {
            if (out_index != NULL) {
                *out_index = i;
            }
            return true;
        }
    }

    return false;
}
