#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    LOCAL_STT_PROTOCOL_EVENT_CONTINUE = 0,
    LOCAL_STT_PROTOCOL_EVENT_TRANSCRIPT,
    LOCAL_STT_PROTOCOL_EVENT_ERROR,
} local_stt_protocol_event_result_t;

/**
 * @brief Choose a preferred non-empty string, falling back to a secondary string when needed.
 * @param preferred Preferred candidate string.
 * @param fallback Fallback candidate string.
 * @param out Destination buffer for the selected value.
 * @param out_size Size of the destination buffer in bytes.
 * @return True when a non-empty string was selected, otherwise false.
 */
bool local_stt_protocol_select_string(const char *preferred, const char *fallback, char *out, size_t out_size);

/**
 * @brief Classify a Wyoming STT event into transcript, error, or continue handling.
 * @param event_type Event type string.
 * @param text Resolved transcript text for the event, if any.
 * @param message Resolved error message for the event, if any.
 * @return The high-level result of the event.
 */
local_stt_protocol_event_result_t
local_stt_protocol_classify_event(const char *event_type, const char *text, const char *message);
