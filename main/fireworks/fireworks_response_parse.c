#include "fireworks/fireworks_response_parse.h"

#include <stdio.h>

#include "cJSON.h"

/**
 * @brief Copy a JSON string field into a fixed buffer.
 * @param object JSON object to inspect.
 * @param field Field name to read.
 * @param buffer Destination buffer for the copied string.
 * @param buffer_size Size of the destination buffer in bytes.
 * @return True when a non-empty string was copied, otherwise false.
 */
static bool fireworks_response_copy_string(const cJSON *object, const char *field, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return false;
    }

    buffer[0] = '\0';
    if (!cJSON_IsObject(object) || field == NULL) {
        return false;
    }

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, field);
    if (!cJSON_IsString(item) || item->valuestring == NULL || item->valuestring[0] == '\0') {
        return false;
    }

    snprintf(buffer, buffer_size, "%s", item->valuestring);
    return true;
}

/**
 * @brief Parse an OpenAI-compatible chat completion response into an answer or tool call.
 * @param json Raw response body returned by the Fireworks inference API.
 * @param out_response Parsed reply result populated on success.
 * @return True when the body parsed into a response object, otherwise false.
 */
bool fireworks_response_parse(const char *json, fireworks_response_t *out_response) {
    if (json == NULL || out_response == NULL) {
        return false;
    }

    *out_response = (fireworks_response_t) {0};

    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }

    const cJSON *choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    const cJSON *first_choice = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
    const cJSON *message =
        cJSON_IsObject(first_choice) ? cJSON_GetObjectItemCaseSensitive(first_choice, "message") : NULL;
    if (!cJSON_IsObject(message)) {
        cJSON_Delete(root);
        return false;
    }

    const cJSON *tool_calls = cJSON_GetObjectItemCaseSensitive(message, "tool_calls");
    const cJSON *first_tool = cJSON_IsArray(tool_calls) ? cJSON_GetArrayItem(tool_calls, 0) : NULL;
    if (cJSON_IsObject(first_tool)) {
        const cJSON *function = cJSON_GetObjectItemCaseSensitive(first_tool, "function");
        if (fireworks_response_copy_string(
                function, "name", out_response->tool_name, sizeof(out_response->tool_name))) {
            fireworks_response_copy_string(
                function, "arguments", out_response->tool_arguments, sizeof(out_response->tool_arguments));
            out_response->kind = FIREWORKS_RESPONSE_TOOL_CALL;
            cJSON_Delete(root);
            return true;
        }
    }

    if (fireworks_response_copy_string(message, "content", out_response->answer, sizeof(out_response->answer))) {
        out_response->kind = FIREWORKS_RESPONSE_ANSWER;
    }

    cJSON_Delete(root);
    return true;
}
