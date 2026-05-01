#include "timer/timer_runtime.h"

#include <stdio.h>

#include "timer/timer_parse.h"

void timer_runtime_reset(timer_runtime_t *timer) {
    if (timer == NULL) {
        return;
    }

    *timer = (timer_runtime_t) {0};
}

bool timer_runtime_start(timer_runtime_t *timer, uint32_t duration_seconds, uint32_t now_ms) {
    if (timer == NULL || duration_seconds == 0) {
        return false;
    }

    timer->active = true;
    timer->alarming = false;
    timer->duration_seconds = duration_seconds;
    timer->start_ms = now_ms;
    timer->duration_ms = duration_seconds * 1000U;
    return true;
}

bool timer_runtime_stop(timer_runtime_t *timer) {
    if (timer == NULL) {
        return false;
    }

    bool was_active = timer->active;
    timer_runtime_reset(timer);
    return was_active;
}

bool timer_runtime_update(timer_runtime_t *timer, uint32_t now_ms) {
    if (timer == NULL || !timer->active || timer->alarming) {
        return false;
    }

    if ((now_ms - timer->start_ms) >= timer->duration_ms) {
        timer->alarming = true;
        return true;
    }

    return false;
}

uint32_t timer_runtime_remaining_seconds(const timer_runtime_t *timer, uint32_t now_ms) {
    if (timer == NULL || !timer->active || timer->alarming) {
        return 0;
    }

    uint32_t elapsed_ms = now_ms - timer->start_ms;
    if (elapsed_ms >= timer->duration_ms) {
        return 0;
    }

    uint32_t remaining_ms = timer->duration_ms - elapsed_ms;
    return (remaining_ms + 999U) / 1000U;
}

void timer_runtime_format_remaining(const timer_runtime_t *timer, uint32_t now_ms, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    uint32_t seconds = timer_runtime_remaining_seconds(timer, now_ms);
    timer_format_clock(seconds, buffer, buffer_size);
}
