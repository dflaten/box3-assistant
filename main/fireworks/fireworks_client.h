#pragma once

#include "esp_err.h"

#include "fireworks/fireworks_response_parse.h"

/**
 * @brief Check whether Fireworks routing is enabled and fully configured.
 * @return True when the assistant can call the Fireworks inference API, otherwise false.
 */
bool fireworks_client_is_configured(void);

/**
 * @brief Send a chat completion request to the Fireworks inference API.
 * @param system_prompt System instruction text for the assistant persona.
 * @param user_text User transcript text to submit.
 * @param tools_json Optional JSON tool schema array string advertised to the model.
 * @param out_response Parsed model reply populated on success.
 * @return ESP_OK on success, or an ESP error code on configuration, network, or parse failure.
 * @note The call blocks until the response arrives, times out, or is cancelled by the watchdog.
 */
esp_err_t fireworks_client_complete(const char *system_prompt,
                                    const char *user_text,
                                    const char *tools_json,
                                    fireworks_response_t *out_response);

/**
 * @brief Cancel the currently active Fireworks request, if any.
 * @return ESP_OK when a request was cancelled, ESP_ERR_INVALID_STATE when no request is active, or an ESP error code.
 */
esp_err_t fireworks_client_cancel_active_request(void);
