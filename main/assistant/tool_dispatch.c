#include "assistant/tool_dispatch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "hue/hue_group_match.h"

/**
 * @brief Read a string field from a tool argument object.
 * @param arguments Parsed tool argument object.
 * @param key Field name to read.
 * @param out Destination buffer for the copied string.
 * @param out_size Size of the destination buffer in bytes.
 * @return True when a non-empty string was copied, otherwise false.
 */
static bool tool_dispatch_get_string(const cJSON *arguments, const char *key, char *out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return false;
    }

    out[0] = '\0';
    if (!cJSON_IsObject(arguments)) {
        return false;
    }

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(arguments, key);
    if (!cJSON_IsString(item) || item->valuestring == NULL || item->valuestring[0] == '\0') {
        return false;
    }

    snprintf(out, out_size, "%s", item->valuestring);
    return true;
}

/**
 * @brief Read an integer field from a tool argument object.
 * @param arguments Parsed tool argument object.
 * @param key Field name to read.
 * @param out_value Output receiving the parsed integer.
 * @return True when a numeric field within the int range was found, otherwise false.
 * @note Values outside the 32-bit int range are rejected rather than silently clamped or truncated.
 */
static bool tool_dispatch_get_int(const cJSON *arguments, const char *key, int *out_value) {
    if (out_value == NULL || !cJSON_IsObject(arguments)) {
        return false;
    }

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(arguments, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }

    double value = item->valuedouble;
    if (value < -2147483648.0 || value > 2147483647.0) {
        return false;
    }

    *out_value = (int) value;
    return true;
}

/**
 * @brief Read a boolean field from a tool argument object.
 * @param arguments Parsed tool argument object.
 * @param key Field name to read.
 * @param out_value Output receiving the parsed boolean.
 * @return True when a boolean field was found, otherwise false.
 */
static bool tool_dispatch_get_bool(const cJSON *arguments, const char *key, bool *out_value) {
    if (out_value == NULL || !cJSON_IsObject(arguments)) {
        return false;
    }

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(arguments, key);
    if (!cJSON_IsBool(item)) {
        return false;
    }

    *out_value = cJSON_IsTrue(item);
    return true;
}

/**
 * @brief Resolve a hue_set_group tool call into a Hue group dispatch.
 * @param arguments Parsed tool argument object.
 * @param groups Runtime Hue groups used to resolve the requested name.
 * @param group_count Number of valid entries in groups.
 * @param result Result structure to populate.
 * @return This function does not return a value.
 */
static void tool_dispatch_resolve_hue(const cJSON *arguments,
                                      const hue_group_t *groups,
                                      size_t group_count,
                                      tool_dispatch_result_t *result) {
    char group[HUE_GROUP_NAME_LEN];
    bool on = false;
    if (!tool_dispatch_get_string(arguments, "group", group, sizeof(group)) ||
        !tool_dispatch_get_bool(arguments, "on", &on)) {
        result->status = TOOL_DISPATCH_INVALID_ARGS;
        snprintf(result->detail, sizeof(result->detail), "Hue command needs a group and state");
        return;
    }

    size_t index = 0;
    if (!hue_group_match_find(groups, group_count, group, &index)) {
        result->status = TOOL_DISPATCH_UNKNOWN_GROUP;
        snprintf(result->detail, sizeof(result->detail), "%s", group);
        return;
    }

    result->status = TOOL_DISPATCH_OK;
    result->dispatch.type = ASSISTANT_COMMAND_ACTION_HUE_GROUP;
    result->dispatch.group_index = index;
    result->dispatch.on = on;
    snprintf(result->detail, sizeof(result->detail), "Turn %s %s", on ? "on" : "off", group);
}

/**
 * @brief Resolve a timer_start tool call into a direct timer dispatch.
 * @param arguments Parsed tool argument object.
 * @param max_timer_seconds Upper bound applied to the requested duration.
 * @param result Result structure to populate.
 * @return This function does not return a value.
 */
static void
tool_dispatch_resolve_timer(const cJSON *arguments, uint32_t max_timer_seconds, tool_dispatch_result_t *result) {
    int duration = 0;
    if (!tool_dispatch_get_int(arguments, "duration_seconds", &duration) || duration <= 0) {
        result->status = TOOL_DISPATCH_INVALID_ARGS;
        snprintf(result->detail, sizeof(result->detail), "Timer needs a positive duration");
        return;
    }

    uint32_t seconds = (uint32_t) duration;
    if (max_timer_seconds > 0 && seconds > max_timer_seconds) {
        seconds = max_timer_seconds;
    }

    result->status = TOOL_DISPATCH_OK;
    result->dispatch.type = ASSISTANT_COMMAND_ACTION_SET_TIMER_DURATION;
    result->dispatch.duration_seconds = seconds;
    snprintf(result->detail, sizeof(result->detail), "Timer for %lu seconds", (unsigned long) seconds);
}

/**
 * @brief Resolve a weather_get tool call into a weather dispatch.
 * @param arguments Parsed tool argument object.
 * @param result Result structure to populate.
 * @return This function does not return a value.
 */
static void tool_dispatch_resolve_weather(const cJSON *arguments, tool_dispatch_result_t *result) {
    char period[16];
    if (!tool_dispatch_get_string(arguments, "period", period, sizeof(period))) {
        result->status = TOOL_DISPATCH_INVALID_ARGS;
        snprintf(result->detail, sizeof(result->detail), "Weather needs a period");
        return;
    }

    if (strcmp(period, "tomorrow") == 0) {
        result->dispatch.type = ASSISTANT_COMMAND_ACTION_WEATHER_TOMORROW;
        snprintf(result->detail, sizeof(result->detail), "Weather tomorrow");
    } else if (strcmp(period, "today") == 0) {
        result->dispatch.type = ASSISTANT_COMMAND_ACTION_WEATHER_TODAY;
        snprintf(result->detail, sizeof(result->detail), "Weather today");
    } else {
        result->status = TOOL_DISPATCH_INVALID_ARGS;
        snprintf(result->detail, sizeof(result->detail), "Unknown weather period");
        return;
    }

    result->status = TOOL_DISPATCH_OK;
}

/**
 * @brief Resolve a time_now tool call into a local time dispatch.
 * @param result Result structure to populate.
 * @return This function does not return a value.
 * @note The model has no clock of its own, so a clock question must resolve to the local time handler.
 */
static void tool_dispatch_resolve_time(tool_dispatch_result_t *result) {
    result->status = TOOL_DISPATCH_OK;
    result->dispatch.type = ASSISTANT_COMMAND_ACTION_TIME_NOW;
    snprintf(result->detail, sizeof(result->detail), "Current time");
}

/**
 * @brief Resolve a volume_adjust tool call into a volume dispatch.
 * @param arguments Parsed tool argument object.
 * @param max_volume_steps Upper bound applied to the absolute step count.
 * @param result Result structure to populate.
 * @return This function does not return a value.
 */
static void
tool_dispatch_resolve_volume(const cJSON *arguments, uint8_t max_volume_steps, tool_dispatch_result_t *result) {
    int steps = 0;
    if (!tool_dispatch_get_int(arguments, "steps", &steps) || steps == 0) {
        result->status = TOOL_DISPATCH_INVALID_ARGS;
        snprintf(result->detail, sizeof(result->detail), "Volume needs a non-zero step count");
        return;
    }

    bool increase = steps > 0;
    int magnitude = increase ? steps : -steps;
    if (max_volume_steps > 0 && magnitude > (int) max_volume_steps) {
        magnitude = (int) max_volume_steps;
    }

    result->status = TOOL_DISPATCH_OK;
    result->dispatch.type = increase ? ASSISTANT_COMMAND_ACTION_VOLUME_UP : ASSISTANT_COMMAND_ACTION_VOLUME_DOWN;
    result->dispatch.volume_steps = (uint8_t) magnitude;
    snprintf(result->detail, sizeof(result->detail), "Volume %s by %d", increase ? "up" : "down", magnitude);
}

/**
 * @brief Resolve a model tool call into a command dispatch for the existing handler registry.
 * @param tool_name Function name returned by the model.
 * @param arguments_json Raw JSON argument string returned by the model.
 * @param groups Runtime Hue groups used to resolve group names.
 * @param group_count Number of valid entries in groups.
 * @param max_timer_seconds Upper bound applied to timer durations.
 * @param max_volume_steps Upper bound applied to the absolute volume step count.
 * @return A resolution result; inspect status before using the dispatch.
 */
tool_dispatch_result_t tool_dispatch_resolve(const char *tool_name,
                                             const char *arguments_json,
                                             const hue_group_t *groups,
                                             size_t group_count,
                                             uint32_t max_timer_seconds,
                                             uint8_t max_volume_steps) {
    tool_dispatch_result_t result = {
        .status = TOOL_DISPATCH_UNKNOWN_TOOL,
        .dispatch = {0},
        .detail = {0},
    };

    if (tool_name == NULL || tool_name[0] == '\0') {
        snprintf(result.detail, sizeof(result.detail), "No tool selected");
        return result;
    }

    cJSON *arguments = NULL;
    if (arguments_json != NULL && arguments_json[0] != '\0') {
        arguments = cJSON_Parse(arguments_json);
    }
    if (arguments != NULL && !cJSON_IsObject(arguments)) {
        cJSON_Delete(arguments);
        arguments = NULL;
    }

    if (strcmp(tool_name, "hue_set_group") == 0) {
        tool_dispatch_resolve_hue(arguments, groups, group_count, &result);
    } else if (strcmp(tool_name, "timer_start") == 0) {
        tool_dispatch_resolve_timer(arguments, max_timer_seconds, &result);
    } else if (strcmp(tool_name, "weather_get") == 0) {
        tool_dispatch_resolve_weather(arguments, &result);
    } else if (strcmp(tool_name, "time_now") == 0) {
        tool_dispatch_resolve_time(&result);
    } else if (strcmp(tool_name, "volume_adjust") == 0) {
        tool_dispatch_resolve_volume(arguments, max_volume_steps, &result);
    } else {
        snprintf(result.detail, sizeof(result.detail), "Unknown tool %s", tool_name);
    }

    cJSON_Delete(arguments);
    return result;
}
