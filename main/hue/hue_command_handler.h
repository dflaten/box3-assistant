#pragma once

#include "assistant/command_handler.h"

/**
 * @brief Get the Hue feature command handler registration.
 * @return Pointer to the static Hue command handler descriptor.
 */
const assistant_command_handler_t *hue_command_handler_get(void);

/**
 * @brief Convert a Hue bridge probe failure into a short UI detail message.
 * @param probe_err Error returned by Hue bridge probing.
 * @param detail Destination buffer for the user-facing status text.
 * @param detail_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
void hue_command_handler_format_probe_error(esp_err_t probe_err, char *detail, size_t detail_size);

/**
 * @brief Convert a Hue command failure into a short UI detail message.
 * @param fallback Default message to use when the bridge itself is reachable.
 * @param request_err Error returned by the Hue request.
 * @param detail Destination buffer for the user-facing status text.
 * @param detail_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
void hue_command_handler_format_request_error(const char *fallback,
                                              esp_err_t request_err,
                                              char *detail,
                                              size_t detail_size);
