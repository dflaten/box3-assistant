#include "assistant/presence.h"

#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_log.h"

#include "assistant/session.h"
#include "assistant_state.h"
#include "board/board_audio.h"
#include "board/ui_status.h"
#include "bsp/esp-box-3.h"
#include "system/time_support.h"
#include "timer/timer_runtime.h"

#define PRESENCE_TIMEOUT_MS         30000
#define PRESENCE_POLL_MS            250
#define PRESENCE_TASK_STACK         4096
#define PRESENCE_TASK_PRIORITY      3
#define PRESENCE_GPIO               BSP_PMOD1_IO5
#define TIMER_ALARM_CHIME_PERIOD_MS 1400

static const char *TAG = "assistant-presence";

static void presence_clock_task(void *arg);
static uint32_t monotonic_ms_now(void);
static void show_timer_status(assistant_runtime_t *rt);

/**
 * @brief Determine whether the most recent presence motion sample is still considered active.
 * @param last_motion_tick Tick count captured when motion was last detected.
 * @param now Current tick count used for the comparison.
 * @param timeout Maximum age for motion to still count as recent.
 * @return True when motion is recent enough to keep presence-owned UI active; otherwise false.
 */
bool assistant_presence_motion_recent(TickType_t last_motion_tick, TickType_t now, TickType_t timeout) {
    return last_motion_tick != 0 && (now - last_motion_tick) < timeout;
}

/**
 * @brief Read the current monotonic uptime in milliseconds.
 * @return Milliseconds since boot truncated to 32 bits.
 */
static uint32_t monotonic_ms_now(void) {
    return (uint32_t) pdTICKS_TO_MS(xTaskGetTickCount());
}

/**
 * @brief Render the active timer countdown or alarm state on screen.
 * @param rt Shared assistant runtime state whose timer should be shown.
 * @return This function does not return a value.
 */
static void show_timer_status(assistant_runtime_t *rt) {
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

/**
 * @brief Initialize the presence sensor input used for idle clock wakeups.
 * @return ESP_OK on success, or an ESP error code if GPIO setup fails.
 */
esp_err_t assistant_presence_init(void) {
    const gpio_config_t io_config = {
        .pin_bit_mask = 1ULL << PRESENCE_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "Failed to configure presence GPIO");
    ESP_LOGI(TAG, "Presence monitor using GPIO %d", PRESENCE_GPIO);
    return ESP_OK;
}

/**
 * @brief Start the presence clock task.
 * @param rt Shared assistant runtime state passed to the task.
 * @return This function does not return a value.
 */
void assistant_presence_start_task(assistant_runtime_t *rt) {
    xTaskCreate(presence_clock_task, "presence_clock", PRESENCE_TASK_STACK, rt, PRESENCE_TASK_PRIORITY, NULL);
}

/**
 * @brief Show a presence-triggered clock screen while the assistant is idle.
 * @param arg Pointer to the shared assistant runtime state.
 * @return This task does not return.
 */
static void presence_clock_task(void *arg) {
    assistant_runtime_t *rt = (assistant_runtime_t *) arg;
    TickType_t last_motion_tick = 0;
    bool display_owned_by_presence = false;
    bool last_clock_synced = false;
    bool last_timer_alarming = false;
    uint32_t last_timer_remaining_seconds = UINT32_MAX;
    TickType_t last_alarm_chime_tick = 0;

    char time_text[24];
    char date_text[32];
    char last_time_text[24] = {0};
    char last_date_text[32] = {0};

    while (true) {
        rt->presence_clock_heartbeat_tick = xTaskGetTickCount();
        vTaskDelay(pdMS_TO_TICKS(PRESENCE_POLL_MS));

        const TickType_t now = xTaskGetTickCount();
        const bool motion_detected = gpio_get_level(PRESENCE_GPIO) > 0;
        const bool assistant_idle = !rt->assistant_awake && rt->assistant_stage == ASSISTANT_STAGE_STANDBY;
        bool timer_expired_now = timer_runtime_update(&rt->timer, monotonic_ms_now());

        if (motion_detected) {
            last_motion_tick = now;
            rt->last_presence_motion_tick = now;
        }

        if (timer_expired_now) {
            rt->direct_command_mode = true;
            rt->direct_command_prepared = false;
            last_alarm_chime_tick = 0;
        }

        if (rt->timer.active) {
            if (!assistant_idle) {
                continue;
            }

            if (rt->timer.alarming) {
                if (!display_owned_by_presence || !last_timer_alarming) {
                    ui_status_set(UI_STATUS_TIMER_ALARM, "00:00");
                    display_owned_by_presence = true;
                }
                if (last_alarm_chime_tick == 0 ||
                    (now - last_alarm_chime_tick) >= pdMS_TO_TICKS(TIMER_ALARM_CHIME_PERIOD_MS)) {
                    esp_err_t chime_err = board_audio_play_timer_chime();
                    if (chime_err != ESP_OK) {
                        ESP_LOGW(TAG, "Timer chime playback failed: %s", esp_err_to_name(chime_err));
                    }
                    last_alarm_chime_tick = now;
                }
                last_timer_alarming = true;
                last_timer_remaining_seconds = 0;
                continue;
            }

            uint32_t remaining_seconds = timer_runtime_remaining_seconds(&rt->timer, monotonic_ms_now());
            if (!display_owned_by_presence || last_timer_alarming ||
                remaining_seconds != last_timer_remaining_seconds) {
                show_timer_status(rt);
                display_owned_by_presence = true;
            }
            last_timer_alarming = false;
            last_timer_remaining_seconds = remaining_seconds;
            continue;
        }

        last_timer_alarming = false;
        last_timer_remaining_seconds = UINT32_MAX;
        last_alarm_chime_tick = 0;

        if (!assistant_idle) {
            display_owned_by_presence = false;
            continue;
        }

        if (!assistant_presence_motion_recent(last_motion_tick, now, pdMS_TO_TICKS(PRESENCE_TIMEOUT_MS))) {
            if (display_owned_by_presence) {
                ESP_LOGI(TAG, "No motion for %d ms; turning clock display off", PRESENCE_TIMEOUT_MS);
                ui_status_display_set(false);
                display_owned_by_presence = false;
                last_clock_synced = false;
                last_time_text[0] = '\0';
                last_date_text[0] = '\0';
            }
            continue;
        }

        bool clock_synced = time_support_format_now(time_text, sizeof(time_text), date_text, sizeof(date_text));
        bool should_redraw = assistant_presence_clock_should_redraw(display_owned_by_presence,
                                                                    last_clock_synced,
                                                                    clock_synced,
                                                                    time_text,
                                                                    last_time_text,
                                                                    date_text,
                                                                    last_date_text);

        if (should_redraw) {
            if (clock_synced) {
                ui_status_show_clock(time_text, date_text, CONFIG_ASSISTANT_LOCATION_NAME);
                strlcpy(last_time_text, time_text, sizeof(last_time_text));
                strlcpy(last_date_text, date_text, sizeof(last_date_text));
            } else {
                ui_status_show_clock("SYNCING TIME", "WAITING FOR NTP", CONFIG_ASSISTANT_LOCATION_NAME);
                last_time_text[0] = '\0';
                last_date_text[0] = '\0';
            }
            display_owned_by_presence = true;
            last_clock_synced = clock_synced;
        }
    }
}
