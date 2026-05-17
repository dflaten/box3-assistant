#pragma once

#include <stddef.h>

#include "esp_err.h"

typedef esp_err_t (*request_cancel_fn_t)(void);

/**
 * @brief Attempt request cancellation functions until one reports an active request.
 * @param cancel_fns Array of cancellation callbacks to try in order.
 * @param cancel_fn_count Number of callbacks in cancel_fns.
 * @return The first non-ESP_ERR_INVALID_STATE result, or ESP_ERR_INVALID_STATE when none were active.
 */
static inline esp_err_t request_cancel_first_active(const request_cancel_fn_t *cancel_fns, size_t cancel_fn_count) {
    if (cancel_fns == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_ERR_INVALID_STATE;
    for (size_t i = 0; i < cancel_fn_count; ++i) {
        if (cancel_fns[i] == NULL) {
            continue;
        }

        err = cancel_fns[i]();
        if (err != ESP_ERR_INVALID_STATE) {
            return err;
        }
    }

    return err;
}
