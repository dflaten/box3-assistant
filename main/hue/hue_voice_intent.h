#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "hue/hue_group.h"

/**
 * @brief Local Hue on/off intent parsed from a speech-to-text transcript.
 */
typedef struct {
    /** Index into the runtime Hue group array. */
    size_t group_index;
    /** True for a turn-on request, false for a turn-off request. */
    bool on;
} hue_voice_intent_t;

/**
 * @brief Match a short imperative transcript against the known Hue groups.
 * @param transcript Speech-to-text transcript to inspect.
 * @param groups Runtime Hue groups available for matching.
 * @param group_count Number of valid entries in groups.
 * @param out_intent Output receiving the matched group and requested state.
 * @return True when the transcript is a confident Hue on/off command, otherwise false.
 * @note Only conservative imperative forms are accepted, such as "turn on the kitchen", "switch off bedroom", or
 * "lights on in the living room". Questions, multi-clause requests, and unknown group names return false so the
 * caller can fall back to the model.
 */
bool hue_voice_intent_parse(const char *transcript,
                            const hue_group_t *groups,
                            size_t group_count,
                            hue_voice_intent_t *out_intent);
