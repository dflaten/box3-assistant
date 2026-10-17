#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "hue/hue_group.h"

#define FIREWORKS_SYSTEM_PROMPT_MAX_LEN 1024

/**
 * @brief Build the JSON tool schema array advertised to the model.
 * @param hue_enabled True to advertise the Hue group tool.
 * @param timer_enabled True to advertise the timer tool.
 * @param weather_enabled True to advertise the weather tool.
 * @param volume_enabled True to advertise the volume tool.
 * @return Heap-allocated JSON array string owned by the caller, or NULL on allocation failure.
 * @note Disabled integrations are omitted so the model cannot select them.
 */
char *
fireworks_request_build_tools_json(bool hue_enabled, bool timer_enabled, bool weather_enabled, bool volume_enabled);

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
                                           size_t buffer_size);

/**
 * @brief Build the chat completions endpoint URL from a configured base URL.
 * @param base_url Configured API base URL, typically the inference API root.
 * @param buffer Destination buffer for the assembled URL.
 * @param buffer_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 * @note A trailing slash is trimmed. A base that already ends with `/chat/completions` is kept as-is, and a base
 * ending with `/completions` has that suffix replaced, so both the API root and a pasted completions endpoint work.
 */
void fireworks_request_build_url(const char *base_url, char *buffer, size_t buffer_size);

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
    const char *model, const char *system_prompt, const char *user_text, const char *tools_json, int max_tokens);
