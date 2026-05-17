#pragma once

#include <stdint.h>

#include "board/ui_status.h"

/**
 * @brief Render a full assistant status screen into the framebuffer.
 * @param frame_buffer Destination RGB565 framebuffer.
 * @param state Status state to render.
 * @param detail Optional detail text shown beneath the title.
 * @return This function does not return a value.
 */
void ui_status_render_status(uint16_t *frame_buffer, ui_status_state_t state, const char *detail);

/**
 * @brief Render the idle clock screen into the framebuffer.
 * @param frame_buffer Destination RGB565 framebuffer.
 * @param time_text Current local time or a short sync-status message.
 * @param date_text Current local date or a short secondary status message.
 * @param location_text Weather or location label shown near the bottom of the screen.
 * @return This function does not return a value.
 */
void ui_status_render_clock(uint16_t *frame_buffer,
                            const char *time_text,
                            const char *date_text,
                            const char *location_text);
