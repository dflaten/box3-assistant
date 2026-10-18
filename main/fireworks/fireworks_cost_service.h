#pragma once

/**
 * @brief Start the background task that refreshes the Fireworks monthly cost display.
 * @return This function does not return a value.
 * @note The task is not started when the cost display is disabled or unconfigured.
 */
void fireworks_cost_service_start(void);
