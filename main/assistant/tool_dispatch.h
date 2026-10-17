#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "commands/assistant_command_dispatch.h"
#include "hue/hue_group.h"

#define ASSISTANT_TOOL_DETAIL_LEN 96

/**
 * @brief Outcome of resolving a model tool call into a command dispatch.
 */
typedef enum {
    TOOL_DISPATCH_OK = 0,
    TOOL_DISPATCH_UNKNOWN_TOOL,
    TOOL_DISPATCH_INVALID_ARGS,
    TOOL_DISPATCH_UNKNOWN_GROUP,
} tool_dispatch_status_t;

/**
 * @brief Resolved tool call ready for dispatch to a feature command handler.
 */
typedef struct {
    /** Resolution outcome. */
    tool_dispatch_status_t status;
    /** Command dispatch produced when status is TOOL_DISPATCH_OK. */
    assistant_command_dispatch_t dispatch;
    /** User-facing label or unmatched value used for logs and spoken feedback. */
    char detail[ASSISTANT_TOOL_DETAIL_LEN];
} tool_dispatch_result_t;

/**
 * @brief Resolve a model tool call into a command dispatch for the existing handler registry.
 * @param tool_name Function name returned by the model.
 * @param arguments_json Raw JSON argument string returned by the model.
 * @param groups Runtime Hue groups used to resolve group names.
 * @param group_count Number of valid entries in groups.
 * @param max_timer_seconds Upper bound applied to timer durations.
 * @param max_volume_steps Upper bound applied to the absolute volume step count.
 * @return A resolution result; inspect status before using the dispatch.
 * @note Hue group names are matched against groups, and out-of-range values are clamped rather than rejected.
 */
tool_dispatch_result_t tool_dispatch_resolve(const char *tool_name,
                                             const char *arguments_json,
                                             const hue_group_t *groups,
                                             size_t group_count,
                                             uint32_t max_timer_seconds,
                                             uint8_t max_volume_steps);
