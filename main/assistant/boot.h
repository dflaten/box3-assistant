#pragma once

#include "freertos/FreeRTOS.h"

#include "assistant_runtime.h"

/**
 * @brief Initialize the shared assistant runtime to its boot-time defaults.
 * @param rt Runtime state instance to initialize.
 * @param startup_tick Tick count captured at startup for initial heartbeat timestamps.
 * @return This function does not return a value.
 */
void assistant_boot_prepare_runtime(assistant_runtime_t *rt, TickType_t startup_tick);

/**
 * @brief Initialize firmware subsystems and launch the assistant runtime.
 * @return This function does not return a value.
 */
void assistant_boot_start(void);
