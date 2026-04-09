#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "hue/hue_group.h"

/**
 * @brief Check whether a Hue client error represents a connectivity problem.
 * @param err The ESP error code returned by a Hue client operation.
 * @return True when the error indicates Wi-Fi or TCP connectivity failure, otherwise false.
 */
bool hue_client_error_is_connectivity(esp_err_t err);

/**
 * @brief Probe the configured Hue bridge and verify the response looks like a Hue bridge.
 * @return ESP_OK when the bridge is reachable and returns a valid Hue config payload, or an ESP error code otherwise.
 */
esp_err_t hue_client_probe_bridge(void);
esp_err_t hue_client_set_group_by_id(const char *group_id, bool on);
esp_err_t hue_client_sync_groups(hue_group_t *groups, size_t max_groups, size_t *out_count);
esp_err_t hue_client_cancel_active_request(void);
