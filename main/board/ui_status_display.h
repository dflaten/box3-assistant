#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

/**
 * @brief Initialize the BOX-3 display hardware and allocate the framebuffer.
 * @param out_panel Output LCD panel handle.
 * @param out_io Output LCD panel IO handle.
 * @param out_frame_buffer Output framebuffer pointer.
 * @return ESP_OK on success, or an ESP error code when initialization fails.
 */
esp_err_t ui_status_display_init(esp_lcd_panel_handle_t *out_panel,
                                 esp_lcd_panel_io_handle_t *out_io,
                                 uint16_t **out_frame_buffer);

/**
 * @brief Turn the LCD panel and backlight on or off as a single operation.
 * @param panel LCD panel handle to control.
 * @param ready True when the display subsystem is initialized.
 * @param display_on Current power state tracked by the caller.
 * @param on Requested power state.
 * @return ESP_OK on success, or an ESP error code if panel power switching fails.
 */
esp_err_t ui_status_display_power_set(esp_lcd_panel_handle_t panel, bool ready, bool *display_on, bool on);

/**
 * @brief Flush a framebuffer to the LCD in large row bands.
 * @param panel LCD panel handle to draw into.
 * @param frame_buffer RGB565 framebuffer to flush.
 * @return ESP_OK on success, or an ESP error code if a panel transfer fails.
 */
esp_err_t ui_status_display_flush(esp_lcd_panel_handle_t panel, const uint16_t *frame_buffer);
