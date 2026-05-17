#pragma once

#include <stdbool.h>

#include "freertos/FreeRTOS.h"

#include "esp_err.h"

#include "assistant_runtime.h"

/**
 * @brief Determine whether the most recent presence motion sample is still considered active.
 * @param last_motion_tick Tick count captured when motion was last detected.
 * @param now Current tick count used for the comparison.
 * @param timeout Maximum age for motion to still count as recent.
 * @return True when motion is recent enough to keep presence-owned UI active; otherwise false.
 */
bool assistant_presence_motion_recent(TickType_t last_motion_tick, TickType_t now, TickType_t timeout);

/**
 * @brief Initialize the presence sensor input used for idle clock wakeups.
 * @return ESP_OK on success, or an ESP error code if GPIO setup fails.
 */
esp_err_t assistant_presence_init(void);

/**
 * @brief Start the presence clock task.
 * @param rt Shared assistant runtime state passed to the task.
 * @return This function does not return a value.
 */
void assistant_presence_start_task(assistant_runtime_t *rt);
