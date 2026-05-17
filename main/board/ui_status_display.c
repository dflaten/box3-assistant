#include "board/ui_status_display.h"

#include <stdlib.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "bsp/esp-box-3.h"

#define UI_SCREEN_WIDTH     BSP_LCD_H_RES
#define UI_SCREEN_HEIGHT    BSP_LCD_V_RES
#define UI_FLUSH_CHUNK_ROWS 20

static const char *TAG = "ui-status-display";

/**
 * @brief Initialize the BOX-3 display hardware and allocate the framebuffer.
 * @param out_panel Output LCD panel handle.
 * @param out_io Output LCD panel IO handle.
 * @param out_frame_buffer Output framebuffer pointer.
 * @return ESP_OK on success, or an ESP error code when initialization fails.
 */
esp_err_t ui_status_display_init(esp_lcd_panel_handle_t *out_panel,
                                 esp_lcd_panel_io_handle_t *out_io,
                                 uint16_t **out_frame_buffer) {
    if (out_panel == NULL || out_io == NULL || out_frame_buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const bsp_display_config_t display_cfg = {
        .max_transfer_sz = BSP_LCD_H_RES * UI_FLUSH_CHUNK_ROWS * (int) sizeof(uint16_t),
    };

    ESP_RETURN_ON_ERROR(bsp_display_new(&display_cfg, out_panel, out_io), TAG, "Failed to initialize display");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*out_panel, true), TAG, "Failed to enable LCD panel");

    esp_err_t backlight_err = bsp_display_backlight_on();
    if (backlight_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable display backlight: %s", esp_err_to_name(backlight_err));
    }

    size_t frame_bytes = (size_t) UI_SCREEN_WIDTH * UI_SCREEN_HEIGHT * sizeof(uint16_t);
    uint16_t *frame_buffer = heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (frame_buffer == NULL) {
        frame_buffer = malloc(frame_bytes);
    }
    if (frame_buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    *out_frame_buffer = frame_buffer;
    return ESP_OK;
}

/**
 * @brief Turn the LCD panel and backlight on or off as a single operation.
 * @param panel LCD panel handle to control.
 * @param ready True when the display subsystem is initialized.
 * @param display_on Current power state tracked by the caller.
 * @param on Requested power state.
 * @return ESP_OK on success, or an ESP error code if panel power switching fails.
 */
esp_err_t ui_status_display_power_set(esp_lcd_panel_handle_t panel, bool ready, bool *display_on, bool on) {
    if (!ready || display_on == NULL || *display_on == on) {
        return ESP_OK;
    }

    esp_err_t err = esp_lcd_panel_disp_on_off(panel, on);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to switch LCD panel %s: %s", on ? "on" : "off", esp_err_to_name(err));
        return err;
    }

    err = on ? bsp_display_backlight_on() : bsp_display_backlight_off();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to switch display backlight %s: %s", on ? "on" : "off", esp_err_to_name(err));
        return err;
    }

    *display_on = on;
    return ESP_OK;
}

/**
 * @brief Flush a framebuffer to the LCD in large row bands.
 * @param panel LCD panel handle to draw into.
 * @param frame_buffer RGB565 framebuffer to flush.
 * @return ESP_OK on success, or an ESP error code if a panel transfer fails.
 */
esp_err_t ui_status_display_flush(esp_lcd_panel_handle_t panel, const uint16_t *frame_buffer) {
    if (frame_buffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int y = 0; y < UI_SCREEN_HEIGHT; y += UI_FLUSH_CHUNK_ROWS) {
        int chunk_rows = UI_SCREEN_HEIGHT - y;
        if (chunk_rows > UI_FLUSH_CHUNK_ROWS) {
            chunk_rows = UI_FLUSH_CHUNK_ROWS;
        }

        esp_err_t err = esp_lcd_panel_draw_bitmap(
            panel, 0, y, UI_SCREEN_WIDTH, y + chunk_rows, frame_buffer + ((size_t) y * UI_SCREEN_WIDTH));
        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}
