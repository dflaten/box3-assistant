#include "assistant/timer_status.h"

#include <stdint.h>
#include <stdio.h>

#include "freertos/task.h"

#include "board/ui_status.h"
#include "timer/timer_runtime.h"

/**
 * @brief Read the current monotonic uptime in milliseconds.
 * @return Milliseconds since boot truncated to 32 bits.
 */
static uint32_t monotonic_ms_now(void) {
    return (uint32_t) pdTICKS_TO_MS(xTaskGetTickCount());
}

void assistant_timer_status_show(assistant_runtime_t *rt) {
    if (rt == NULL || !rt->timer.active) {
        return;
    }

    char detail[24];
    if (rt->timer.alarming) {
        snprintf(detail, sizeof(detail), "00:00");
        ui_status_set(UI_STATUS_TIMER_ALARM, detail);
        return;
    }

    timer_runtime_format_remaining(&rt->timer, monotonic_ms_now(), detail, sizeof(detail));
    ui_status_set(UI_STATUS_TIMER, detail);
}
