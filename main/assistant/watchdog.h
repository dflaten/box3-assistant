#pragma once

#include "freertos/FreeRTOS.h"

#include "assistant_runtime.h"

/**
 * @brief Sleep while continuing to publish the speech-detect heartbeat.
 * @param rt Runtime state whose speech heartbeat should be refreshed during the delay.
 * @param duration Total delay duration to wait.
 * @param slice_duration Maximum single sleep slice before the heartbeat is refreshed again.
 * @return This function does not return a value.
 */
void assistant_watchdog_sleep_with_heartbeat(assistant_runtime_t *rt, TickType_t duration, TickType_t slice_duration);

/**
 * @brief Start the assistant watchdog task.
 * @param rt Shared assistant runtime state monitored by the watchdog.
 * @return This function does not return a value.
 */
void assistant_watchdog_start_task(assistant_runtime_t *rt);
