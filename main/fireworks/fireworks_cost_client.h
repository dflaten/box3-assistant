#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/**
 * @brief Check whether the Fireworks cost display is enabled and fully configured.
 * @return True when the assistant can query Fireworks usage costs, otherwise false.
 */
bool fireworks_cost_client_is_configured(void);

/**
 * @brief Fetch the current month's rated cost for the configured API key.
 * @param out_cents Output subtotal rounded to whole cents.
 * @return ESP_OK on success, or an ESP error code on configuration, network, or parse failure.
 */
esp_err_t fireworks_cost_client_fetch_month_cents(uint32_t *out_cents);

/**
 * @brief Cancel the currently active cost request, if any.
 * @return ESP_OK when a request was cancelled, ESP_ERR_INVALID_STATE when no request is active, or an ESP error code.
 */
esp_err_t fireworks_cost_client_cancel_active_request(void);
