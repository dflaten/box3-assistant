#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Extract a header value from an SSDP response using case-insensitive header-name matching.
 * @param response The raw SSDP response text.
 * @param header_name Header name to locate, without the trailing colon.
 * @param out_value Destination buffer for the trimmed header value.
 * @param out_value_size Size of the destination buffer in bytes.
 * @return True when the header was found and copied, otherwise false.
 */
bool hue_discovery_extract_header_value(const char *response,
                                        const char *header_name,
                                        char *out_value,
                                        size_t out_value_size);

/**
 * @brief Check whether an SSDP response looks like it came from a Hue bridge.
 * @param response The raw SSDP response text.
 * @return True when the Hue-specific bridge identifier header is present, otherwise false.
 */
bool hue_discovery_response_is_hue_bridge(const char *response);
