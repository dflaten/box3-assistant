#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"

#include "board/ui_status.h"
#include "board/ui_status_display.h"
#include "board/ui_status_render.h"

#define UI_IDLE_TIMEOUT_MS    30000
#define UI_IDLE_POLL_MS       1000
#define UI_MUTEX_TIMEOUT_MS   2000
#define UI_IDLE_TASK_STACK    4096
#define UI_IDLE_TASK_PRIORITY 2

static const char *TAG = "ui-status";

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static bool s_ready;
static bool s_display_on;
static ui_status_state_t s_current_state = UI_STATUS_BOOTING;
static TickType_t s_last_activity_tick;
static uint16_t *s_frame_buffer;
static SemaphoreHandle_t s_ui_mutex;
static volatile TickType_t s_ui_render_start_tick;
static volatile bool s_ui_render_in_progress;

static void ui_idle_task(void *arg);
static void ui_status_note_activity(void);
static void ui_render_begin(void);
static void ui_render_end(void);

/**
 * @brief Initialize the BOX-3 display and start the UI idle-management task.
 * @return ESP_OK on success, or an ESP error code if display startup fails.
 */
esp_err_t ui_status_init(void) {
    if (s_ready) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(ui_status_display_init(&s_panel, &s_io, &s_frame_buffer), TAG, "Failed to initialize display");
    s_display_on = true;

    s_ui_mutex = xSemaphoreCreateMutex();
    if (s_ui_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_last_activity_tick = xTaskGetTickCount();
    s_ready = true;
    xTaskCreate(ui_idle_task, "ui_idle", UI_IDLE_TASK_STACK, NULL, UI_IDLE_TASK_PRIORITY, NULL);
    ui_status_set(UI_STATUS_BOOTING, NULL);
    return ESP_OK;
}

/**
 * @brief Record user-visible activity so the ready-screen idle timer resets.
 * @return This function does not return a value.
 */
static void ui_status_note_activity(void) {
    s_last_activity_tick = xTaskGetTickCount();
}

/**
 * @brief Mark the beginning of a synchronous LCD render operation for watchdog visibility.
 * @return This function does not return a value.
 */
static void ui_render_begin(void) {
    s_ui_render_start_tick = xTaskGetTickCount();
    s_ui_render_in_progress = true;
}

/**
 * @brief Mark the end of a synchronous LCD render operation.
 * @return This function does not return a value.
 */
static void ui_render_end(void) {
    s_ui_render_in_progress = false;
    s_ui_render_start_tick = 0;
}

/**
 * @brief Power down the display after extended idle time in the ready state.
 * @param arg Unused FreeRTOS task parameter.
 * @return This task does not return.
 */
static void ui_idle_task(void *arg) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(UI_IDLE_POLL_MS));
        if (!s_ready || s_ui_mutex == NULL) {
            continue;
        }

        if (xSemaphoreTake(s_ui_mutex, 0) == pdTRUE) {
            if (s_display_on && s_current_state == UI_STATUS_READY) {
                TickType_t idle_ticks = xTaskGetTickCount() - s_last_activity_tick;
                if (idle_ticks >= pdMS_TO_TICKS(UI_IDLE_TIMEOUT_MS)) {
                    ESP_LOGI(TAG, "Display idle timeout reached; turning screen off");
                    ui_status_display_power_set(s_panel, s_ready, &s_display_on, false);
                }
            }
            xSemaphoreGive(s_ui_mutex);
        }
    }
}

/**
 * @brief Update the screen to a new UI status state and optional detail text.
 * @param state The new status state to display.
 * @param detail Optional detail text shown on the screen.
 * @return This function does not return a value.
 */
void ui_status_set(ui_status_state_t state, const char *detail) {
    if (!s_ready || s_ui_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(UI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Timed out waiting for UI mutex in ui_status_set");
        return;
    }

    ui_status_note_activity();
    s_current_state = state;
    ui_render_begin();
    esp_err_t power_err = ui_status_display_power_set(s_panel, s_ready, &s_display_on, true);
    esp_err_t flush_err = ESP_OK;
    if (power_err == ESP_OK) {
        ui_status_render_status(s_frame_buffer, state, detail);
        flush_err = ui_status_display_flush(s_panel, s_frame_buffer);
    }
    ui_render_end();
    xSemaphoreGive(s_ui_mutex);

    if (power_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to power status screen: %s", esp_err_to_name(power_err));
    } else if (flush_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to flush status screen: %s", esp_err_to_name(flush_err));
    }
}

/**
 * @brief Try to update the screen without blocking if another UI render is already in progress.
 * @param state The new status state to display.
 * @param detail Optional detail text shown on the screen.
 * @return ESP_OK when the update was rendered, ESP_ERR_TIMEOUT when the UI is busy, or ESP_ERR_INVALID_STATE when the
 * UI is not ready.
 */
esp_err_t ui_status_try_set(ui_status_state_t state, const char *detail) {
    if (!s_ready || s_ui_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ui_mutex, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    ui_status_note_activity();
    s_current_state = state;
    ui_render_begin();
    esp_err_t power_err = ui_status_display_power_set(s_panel, s_ready, &s_display_on, true);
    esp_err_t flush_err = ESP_OK;
    if (power_err == ESP_OK) {
        ui_status_render_status(s_frame_buffer, state, detail);
        flush_err = ui_status_display_flush(s_panel, s_frame_buffer);
    }
    ui_render_end();
    xSemaphoreGive(s_ui_mutex);

    if (power_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to power status screen: %s", esp_err_to_name(power_err));
        return power_err;
    }
    if (flush_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to flush status screen: %s", esp_err_to_name(flush_err));
        return flush_err;
    }

    return ESP_OK;
}

/**
 * @brief Render the idle clock screen and wake the display if needed.
 * @param time_text Current local time or a short sync-status message.
 * @param date_text Current local date or a short secondary status message.
 * @param location_text Weather/location label shown under the time.
 * @return This function does not return a value.
 */
void ui_status_show_clock(const char *time_text, const char *date_text, const char *location_text) {
    if (!s_ready || s_ui_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(UI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Timed out waiting for UI mutex in ui_status_show_clock");
        return;
    }

    s_current_state = UI_STATUS_CLOCK;
    ui_render_begin();
    esp_err_t power_err = ui_status_display_power_set(s_panel, s_ready, &s_display_on, true);
    esp_err_t flush_err = ESP_OK;
    if (power_err == ESP_OK) {
        ui_status_render_clock(s_frame_buffer, time_text, date_text, location_text);
        flush_err = ui_status_display_flush(s_panel, s_frame_buffer);
    }
    ui_render_end();
    xSemaphoreGive(s_ui_mutex);

    if (power_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to power clock screen: %s", esp_err_to_name(power_err));
    } else if (flush_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to flush clock screen: %s", esp_err_to_name(flush_err));
    }
}

/**
 * @brief Report how long the current UI render has been stuck in progress.
 * @return Milliseconds since the current render began, or zero when no render is active.
 */
uint32_t ui_status_render_stalled_ms(void) {
    if (!s_ready || !s_ui_render_in_progress || s_ui_render_start_tick == 0) {
        return 0;
    }

    TickType_t now = xTaskGetTickCount();
    if (s_ui_render_start_tick > now) {
        return 0;
    }

    return (uint32_t) pdTICKS_TO_MS(now - s_ui_render_start_tick);
}

/**
 * @brief Explicitly turn the LCD display and backlight on or off.
 * @param on True to enable the display, false to disable it.
 * @return ESP_OK on success, or an ESP error code if the display transition fails.
 */
esp_err_t ui_status_display_set(bool on) {
    if (!s_ready || s_ui_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(UI_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Timed out waiting for UI mutex in ui_status_display_set");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = ui_status_display_power_set(s_panel, s_ready, &s_display_on, on);
    xSemaphoreGive(s_ui_mutex);
    return err;
}
