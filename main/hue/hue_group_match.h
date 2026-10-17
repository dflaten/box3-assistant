#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "hue/hue_group.h"

/**
 * @brief Find the runtime Hue group that best matches a spoken or model-provided name.
 * @param groups Runtime Hue groups to search.
 * @param group_count Number of valid entries in groups.
 * @param name Group name to match, typically returned by a model tool call.
 * @param out_index Optional output receiving the matched group index.
 * @return True when a group matched, otherwise false.
 * @note Matching prefers an exact case-insensitive match, then a whole-word match in either direction, then a
 * substring match; ties are broken by the candidate whose length is closest to the query.
 */
bool hue_group_match_find(const hue_group_t *groups, size_t group_count, const char *name, size_t *out_index);
