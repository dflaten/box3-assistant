#pragma once

#include <stdbool.h>
#include <stddef.h>

#define FIREWORKS_ANSWER_MAX_LEN    1024
#define FIREWORKS_TOOL_NAME_MAX_LEN 48
#define FIREWORKS_TOOL_ARGS_MAX_LEN 512

/**
 * @brief High-level classification of a parsed model response.
 */
typedef enum {
    FIREWORKS_RESPONSE_NONE = 0,
    FIREWORKS_RESPONSE_ANSWER,
    FIREWORKS_RESPONSE_TOOL_CALL,
} fireworks_response_kind_t;

/**
 * @brief Parsed model reply containing either plain answer text or a single tool call.
 */
typedef struct {
    /** Kind of reply that was parsed. */
    fireworks_response_kind_t kind;
    /** Plain answer text when kind is FIREWORKS_RESPONSE_ANSWER. */
    char answer[FIREWORKS_ANSWER_MAX_LEN];
    /** Selected tool name when kind is FIREWORKS_RESPONSE_TOOL_CALL. */
    char tool_name[FIREWORKS_TOOL_NAME_MAX_LEN];
    /** Raw JSON argument string for the selected tool. */
    char tool_arguments[FIREWORKS_TOOL_ARGS_MAX_LEN];
} fireworks_response_t;

/**
 * @brief Parse an OpenAI-compatible chat completion response into an answer or tool call.
 * @param json Raw response body returned by the Fireworks inference API.
 * @param out_response Parsed reply result populated on success.
 * @return True when the body parsed into a response object, otherwise false.
 * @note A successfully parsed body with no content and no tool calls yields FIREWORKS_RESPONSE_NONE.
 */
bool fireworks_response_parse(const char *json, fireworks_response_t *out_response);
