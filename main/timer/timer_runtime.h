#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool active;
    bool alarming;
    uint32_t duration_seconds;
    uint32_t start_ms;
    uint32_t duration_ms;
} timer_runtime_t;

/**
 * @brief Reset timer state to idle.
 * @param timer Timer state to clear.
 * @return This function does not return a value.
 */
void timer_runtime_reset(timer_runtime_t *timer);

/**
 * @brief Start or replace a timer countdown.
 * @param timer Timer state to update.
 * @param duration_seconds Countdown duration in seconds.
 * @param now_ms Current monotonic time in milliseconds.
 * @return True when the timer was started, otherwise false.
 */
bool timer_runtime_start(timer_runtime_t *timer, uint32_t duration_seconds, uint32_t now_ms);

/**
 * @brief Stop an active or alarming timer.
 * @param timer Timer state to clear.
 * @return True when a timer was active before the stop, otherwise false.
 */
bool timer_runtime_stop(timer_runtime_t *timer);

/**
 * @brief Advance timer state based on the current time.
 * @param timer Timer state to update.
 * @param now_ms Current monotonic time in milliseconds.
 * @return True only when the timer entered alarming state during this call.
 */
bool timer_runtime_update(timer_runtime_t *timer, uint32_t now_ms);

/**
 * @brief Compute the remaining whole seconds for an active timer.
 * @param timer Timer state to inspect.
 * @param now_ms Current monotonic time in milliseconds.
 * @return Remaining whole seconds, or zero if expired or inactive.
 */
uint32_t timer_runtime_remaining_seconds(const timer_runtime_t *timer, uint32_t now_ms);

/**
 * @brief Format the current remaining time for display.
 * @param timer Timer state to inspect.
 * @param now_ms Current monotonic time in milliseconds.
 * @param buffer Destination buffer for the formatted time text.
 * @param buffer_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
void timer_runtime_format_remaining(const timer_runtime_t *timer, uint32_t now_ms, char *buffer, size_t buffer_size);
