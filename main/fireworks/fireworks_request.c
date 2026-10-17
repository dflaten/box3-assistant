#include "fireworks/fireworks_request.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#define FIREWORKS_TOOL_MAX_ENUM_VALUES 3

/**
 * @brief Description of one required property in a tool parameter schema.
 */
typedef struct {
    /** Property name. */
    const char *name;
    /** JSON schema type for the property. */
    const char *type;
    /** Human-readable description of the property. */
    const char *description;
    /** Optional NULL-terminated list of allowed values for enumerated properties. */
    const char *enum_values[FIREWORKS_TOOL_MAX_ENUM_VALUES];
} fireworks_tool_property_t;

#define FIREWORKS_CHAT_COMPLETIONS_SUFFIX "/chat/completions"
#define FIREWORKS_COMPLETIONS_SUFFIX      "/completions"

/**
 * @brief Check whether a string ends with a given suffix.
 * @param text String to inspect.
 * @param suffix Suffix to look for.
 * @return True when text ends with suffix, otherwise false.
 */
static bool fireworks_request_ends_with(const char *text, const char *suffix) {
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > text_len) {
        return false;
    }

    return strcmp(text + text_len - suffix_len, suffix) == 0;
}

/**
 * @brief Build the chat completions endpoint URL from a configured base URL.
 * @param base_url Configured API base URL, typically the inference API root.
 * @param buffer Destination buffer for the assembled URL.
 * @param buffer_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
void fireworks_request_build_url(const char *base_url, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    snprintf(buffer, buffer_size, "%s", base_url != NULL ? base_url : "");
    size_t len = strlen(buffer);
    while (len > 0 && buffer[len - 1] == '/') {
        buffer[--len] = '\0';
    }

    if (fireworks_request_ends_with(buffer, FIREWORKS_CHAT_COMPLETIONS_SUFFIX)) {
        return;
    }

    if (fireworks_request_ends_with(buffer, FIREWORKS_COMPLETIONS_SUFFIX)) {
        len -= strlen(FIREWORKS_COMPLETIONS_SUFFIX);
        buffer[len] = '\0';
    }

    snprintf(buffer + len, buffer_size - len, "%s", FIREWORKS_CHAT_COMPLETIONS_SUFFIX);
}

/**
 * @brief Add one required property to a tool parameter properties object.
 * @param properties Parameter properties object to extend.
 * @param property Property description to add.
 * @return This function does not return a value.
 */
static void fireworks_request_add_property(cJSON *properties, const fireworks_tool_property_t *property) {
    if (!cJSON_IsObject(properties) || property == NULL || property->name == NULL) {
        return;
    }

    cJSON *item = cJSON_CreateObject();
    if (item == NULL) {
        return;
    }

    cJSON_AddStringToObject(item, "type", property->type);
    cJSON_AddStringToObject(item, "description", property->description);
    if (property->enum_values[0] != NULL) {
        cJSON *values = cJSON_CreateArray();
        if (values != NULL) {
            for (size_t i = 0; i < FIREWORKS_TOOL_MAX_ENUM_VALUES && property->enum_values[i] != NULL; ++i) {
                cJSON_AddItemToArray(values, cJSON_CreateString(property->enum_values[i]));
            }
            cJSON_AddItemToObject(item, "enum", values);
        }
    }
    cJSON_AddItemToObject(properties, property->name, item);
}

/**
 * @brief Add one function tool definition to the tool schema array.
 * @param tools Tool schema array to extend.
 * @param name Function name.
 * @param description Human-readable description of the function.
 * @param properties Required properties for the function.
 * @param property_count Number of entries in properties.
 * @return This function does not return a value.
 * @note Every advertised property is treated as required, which matches the fixed tool set.
 */
static void fireworks_request_add_tool(cJSON *tools,
                                       const char *name,
                                       const char *description,
                                       const fireworks_tool_property_t *properties,
                                       size_t property_count) {
    if (!cJSON_IsArray(tools) || name == NULL || properties == NULL) {
        return;
    }

    cJSON *tool = cJSON_CreateObject();
    cJSON *function = cJSON_CreateObject();
    cJSON *parameters = cJSON_CreateObject();
    cJSON *property_object = cJSON_CreateObject();
    cJSON *required = cJSON_CreateArray();
    if (tool == NULL || function == NULL || parameters == NULL || property_object == NULL || required == NULL) {
        cJSON_Delete(tool);
        cJSON_Delete(function);
        cJSON_Delete(parameters);
        cJSON_Delete(property_object);
        cJSON_Delete(required);
        return;
    }

    for (size_t i = 0; i < property_count; ++i) {
        fireworks_request_add_property(property_object, &properties[i]);
        if (properties[i].name != NULL) {
            cJSON_AddItemToArray(required, cJSON_CreateString(properties[i].name));
        }
    }

    cJSON_AddStringToObject(tool, "type", "function");
    cJSON_AddStringToObject(function, "name", name);
    cJSON_AddStringToObject(function, "description", description);
    cJSON_AddStringToObject(parameters, "type", "object");
    cJSON_AddItemToObject(parameters, "properties", property_object);
    cJSON_AddItemToObject(parameters, "required", required);
    cJSON_AddItemToObject(function, "parameters", parameters);
    cJSON_AddItemToObject(tool, "function", function);
    cJSON_AddItemToArray(tools, tool);
}

/**
 * @brief Build the JSON tool schema array advertised to the model.
 * @param hue_enabled True to advertise the Hue group tool.
 * @param timer_enabled True to advertise the timer tool.
 * @param weather_enabled True to advertise the weather tool.
 * @param volume_enabled True to advertise the volume tool.
 * @return Heap-allocated JSON array string owned by the caller, or NULL on allocation failure.
 */
char *
fireworks_request_build_tools_json(bool hue_enabled, bool timer_enabled, bool weather_enabled, bool volume_enabled) {
    cJSON *tools = cJSON_CreateArray();
    if (tools == NULL) {
        return NULL;
    }

    if (hue_enabled) {
        static const fireworks_tool_property_t properties[] = {
            {"group", "string", "Name of the Hue group", {NULL}},
            {"on", "boolean", "true to turn on, false to turn off", {NULL}},
        };
        fireworks_request_add_tool(tools, "hue_set_group", "Turn a Hue light group on or off.", properties, 2);
    }

    if (timer_enabled) {
        static const fireworks_tool_property_t properties[] = {
            {"duration_seconds", "integer", "Timer length in seconds", {NULL}},
        };
        fireworks_request_add_tool(tools, "timer_start", "Start a countdown timer.", properties, 1);
    }

    if (weather_enabled) {
        static const fireworks_tool_property_t properties[] = {
            {"period", "string", "Forecast period to retrieve", {"today", "tomorrow", NULL}},
        };
        fireworks_request_add_tool(tools, "weather_get", "Get the local weather forecast.", properties, 1);
    }

    if (volume_enabled) {
        static const fireworks_tool_property_t properties[] = {
            {"steps", "integer", "Number of ten-percent steps, negative to lower volume", {NULL}},
        };
        fireworks_request_add_tool(
            tools, "volume_adjust", "Adjust the speaker volume in ten-percent steps.", properties, 1);
    }

    char *json = cJSON_PrintUnformatted(tools);
    cJSON_Delete(tools);
    return json;
}

/**
 * @brief Build the system prompt describing the assistant persona and known Hue groups.
 * @param groups Runtime Hue groups available for control.
 * @param group_count Number of valid entries in groups.
 * @param buffer Destination buffer for the system prompt.
 * @param buffer_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
void fireworks_request_build_system_prompt(const hue_group_t *groups,
                                           size_t group_count,
                                           char *buffer,
                                           size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    int written = snprintf(buffer,
                           buffer_size,
                           "You are the box3 voice assistant for a smart home. "
                           "Use the provided tools to control the home. "
                           "When no tool is needed, answer the user's question directly and concisely in at most three "
                           "short sentences. Never invent device names. ");

    if (written < 0 || (size_t) written >= buffer_size) {
        buffer[buffer_size - 1] = '\0';
        return;
    }

    if (group_count == 0 || groups == NULL) {
        snprintf(buffer + written, buffer_size - (size_t) written, "There are no configured Hue groups.");
        return;
    }

    written += snprintf(buffer + written, buffer_size - (size_t) written, "Known Hue groups: ");
    for (size_t i = 0; i < group_count && (size_t) written < buffer_size; ++i) {
        written +=
            snprintf(buffer + written, buffer_size - (size_t) written, "%s%s", i == 0 ? "" : ", ", groups[i].name);
    }
    if ((size_t) written < buffer_size) {
        snprintf(buffer + written, buffer_size - (size_t) written, ".");
    }
}

/**
 * @brief Build the chat completions request body sent to the Fireworks inference API.
 * @param model Model name to request.
 * @param system_prompt System instruction text.
 * @param user_text User transcript text.
 * @param tools_json Optional JSON tool schema array string.
 * @param max_tokens Maximum response tokens to request.
 * @return Heap-allocated JSON request body owned by the caller, or NULL on allocation failure.
 */
char *fireworks_request_build_body(
    const char *model, const char *system_prompt, const char *user_text, const char *tools_json, int max_tokens) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(root, "model", model != NULL ? model : "");
    cJSON_AddNumberToObject(root, "max_tokens", max_tokens > 0 ? max_tokens : 256);
    cJSON_AddNumberToObject(root, "temperature", 0.2);

    cJSON *messages = cJSON_CreateArray();
    if (messages != NULL) {
        cJSON *system_message = cJSON_CreateObject();
        if (system_message != NULL) {
            cJSON_AddStringToObject(system_message, "role", "system");
            cJSON_AddStringToObject(system_message, "content", system_prompt != NULL ? system_prompt : "");
            cJSON_AddItemToArray(messages, system_message);
        }
        cJSON *user_message = cJSON_CreateObject();
        if (user_message != NULL) {
            cJSON_AddStringToObject(user_message, "role", "user");
            cJSON_AddStringToObject(user_message, "content", user_text != NULL ? user_text : "");
            cJSON_AddItemToArray(messages, user_message);
        }
        cJSON_AddItemToObject(root, "messages", messages);
    }

    if (tools_json != NULL && tools_json[0] != '\0') {
        cJSON *tools = cJSON_Parse(tools_json);
        if (cJSON_IsArray(tools) && cJSON_GetArraySize(tools) > 0) {
            cJSON_AddItemToObject(root, "tools", tools);
            cJSON_AddStringToObject(root, "tool_choice", "auto");
        } else {
            cJSON_Delete(tools);
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}
