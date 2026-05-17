#include "board/ui_status_render.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "bsp/esp-box-3.h"

#include "board/ui_status_font.h"
#include "system/wifi_support.h"

#define UI_SCREEN_WIDTH  BSP_LCD_H_RES
#define UI_SCREEN_HEIGHT BSP_LCD_V_RES
#define UI_TITLE_SCALE   4
#define UI_BODY_SCALE    2
#define UI_CHAR_SPACING  2

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);
static uint16_t state_bg(ui_status_state_t state);
static const char *state_title(ui_status_state_t state);
static const char *state_subtitle(ui_status_state_t state);
static void fill_rect(uint16_t *frame_buffer, int x, int y, int w, int h, uint16_t color);
static void draw_rect_outline(uint16_t *frame_buffer, int x, int y, int w, int h, int thickness, uint16_t color);
static void uppercase_copy(char *dst, size_t dst_size, const char *src);
static int text_width(const char *text, int scale);
static void draw_text(uint16_t *frame_buffer, int x, int y, int scale, uint16_t color, const char *text);
static int max_chars_per_line(int scale);
static void draw_text_centered(uint16_t *frame_buffer, int y, int scale, uint16_t color, const char *text);
static void draw_wifi_indicator(uint16_t *frame_buffer);
static void draw_text_block_centered(uint16_t *frame_buffer, int start_y, int scale, uint16_t color, const char *text);

/**
 * @brief Convert 8-bit RGB components into RGB565 panel color format.
 * @param r Red component in 8-bit space.
 * @param g Green component in 8-bit space.
 * @param b Blue component in 8-bit space.
 * @return Packed RGB565 color value.
 */
static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/**
 * @brief Pick the background color associated with a UI status state.
 * @param state Status state to render.
 * @return RGB565 background color for the state.
 */
static uint16_t state_bg(ui_status_state_t state) {
    switch (state) {
    case UI_STATUS_BOOTING:
        return rgb565(82, 82, 91);
    case UI_STATUS_CONNECTING:
        return rgb565(34, 94, 168);
    case UI_STATUS_READY:
        return rgb565(45, 55, 72);
    case UI_STATUS_CLOCK:
        return rgb565(15, 23, 42);
    case UI_STATUS_LISTENING:
        return rgb565(194, 120, 3);
    case UI_STATUS_WORKING:
        return rgb565(180, 83, 9);
    case UI_STATUS_WEATHER_LOADING:
        return rgb565(3, 105, 161);
    case UI_STATUS_TIMER:
        return rgb565(8, 47, 73);
    case UI_STATUS_TIMER_ALARM:
        return rgb565(146, 64, 14);
    case UI_STATUS_SUCCESS:
        return rgb565(22, 163, 74);
    case UI_STATUS_ERROR:
    default:
        return rgb565(185, 28, 28);
    }
}

/**
 * @brief Get the primary title text shown for a UI status state.
 * @param state Status state to describe.
 * @return Static title text for the state.
 */
static const char *state_title(ui_status_state_t state) {
    switch (state) {
    case UI_STATUS_BOOTING:
        return "STARTING";
    case UI_STATUS_CONNECTING:
        return "CONNECTING";
    case UI_STATUS_READY:
        return "READY";
    case UI_STATUS_CLOCK:
        return "";
    case UI_STATUS_LISTENING:
        return "LISTENING";
    case UI_STATUS_WORKING:
        return "WORKING";
    case UI_STATUS_WEATHER_LOADING:
        return "";
    case UI_STATUS_TIMER:
        return "TIMER";
    case UI_STATUS_TIMER_ALARM:
        return "TIME IS UP";
    case UI_STATUS_SUCCESS:
        return "";
    case UI_STATUS_ERROR:
    default:
        return "ATTENTION";
    }
}

/**
 * @brief Get the secondary subtitle text shown for a UI status state.
 * @param state Status state to describe.
 * @return Static subtitle text for the state.
 */
static const char *state_subtitle(ui_status_state_t state) {
    switch (state) {
    case UI_STATUS_BOOTING:
        return "INITIALIZING ASSISTANT";
    case UI_STATUS_CONNECTING:
        return "JOINING WI-FI";
    case UI_STATUS_READY:
        return "SAY HI ESP";
    case UI_STATUS_CLOCK:
        return "";
    case UI_STATUS_LISTENING:
        return "SAY A COMMAND";
    case UI_STATUS_WORKING:
        return "PROCESSING REQUEST";
    case UI_STATUS_WEATHER_LOADING:
        return "";
    case UI_STATUS_TIMER:
        return "COUNTDOWN ACTIVE";
    case UI_STATUS_TIMER_ALARM:
        return "SAY STOP";
    case UI_STATUS_SUCCESS:
        return "";
    case UI_STATUS_ERROR:
    default:
        return "COMMAND NEEDS ATTENTION";
    }
}

/**
 * @brief Fill a clipped rectangle in the framebuffer with a solid color.
 * @param frame_buffer Destination framebuffer.
 * @param x Left edge in pixels.
 * @param y Top edge in pixels.
 * @param w Rectangle width in pixels.
 * @param h Rectangle height in pixels.
 * @param color RGB565 fill color.
 * @return This function does not return a value.
 */
static void fill_rect(uint16_t *frame_buffer, int x, int y, int w, int h, uint16_t color) {
    if (frame_buffer == NULL || x >= UI_SCREEN_WIDTH || y >= UI_SCREEN_HEIGHT || w <= 0 || h <= 0) {
        return;
    }

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > UI_SCREEN_WIDTH) {
        w = UI_SCREEN_WIDTH - x;
    }
    if (y + h > UI_SCREEN_HEIGHT) {
        h = UI_SCREEN_HEIGHT - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    for (int row = 0; row < h; ++row) {
        uint16_t *dst = frame_buffer + ((size_t) (y + row) * UI_SCREEN_WIDTH) + x;
        for (int col = 0; col < w; ++col) {
            dst[col] = color;
        }
    }
}

/**
 * @brief Draw a clipped rectangle outline in the framebuffer.
 * @param frame_buffer Destination framebuffer.
 * @param x Left edge in pixels.
 * @param y Top edge in pixels.
 * @param w Rectangle width in pixels.
 * @param h Rectangle height in pixels.
 * @param thickness Outline thickness in pixels.
 * @param color RGB565 outline color.
 * @return This function does not return a value.
 */
static void draw_rect_outline(uint16_t *frame_buffer, int x, int y, int w, int h, int thickness, uint16_t color) {
    if (w <= 0 || h <= 0 || thickness <= 0) {
        return;
    }

    if (thickness * 2 > w) {
        thickness = w / 2;
    }
    if (thickness * 2 > h) {
        thickness = h / 2;
    }
    if (thickness <= 0) {
        thickness = 1;
    }

    fill_rect(frame_buffer, x, y, w, thickness, color);
    fill_rect(frame_buffer, x, y + h - thickness, w, thickness, color);
    fill_rect(frame_buffer, x, y, thickness, h, color);
    fill_rect(frame_buffer, x + w - thickness, y, thickness, h, color);
}

/**
 * @brief Copy text into a buffer while converting it to uppercase.
 * @param dst Destination buffer for the uppercase result.
 * @param dst_size Size of the destination buffer in bytes.
 * @param src Source text to copy and normalize.
 * @return This function does not return a value.
 */
static void uppercase_copy(char *dst, size_t dst_size, const char *src) {
    if (dst_size == 0) {
        return;
    }

    size_t i = 0;
    for (; i + 1 < dst_size && src != NULL && src[i] != '\0'; ++i) {
        dst[i] = (char) toupper((unsigned char) src[i]);
    }
    dst[i] = '\0';
}

/**
 * @brief Measure the rendered pixel width of a string at a given scale.
 * @param text Text to measure.
 * @param scale Pixel scale factor for the built-in bitmap font.
 * @return Total width of the rendered string in pixels.
 */
static int text_width(const char *text, int scale) {
    const int char_width = (5 * scale) + UI_CHAR_SPACING;
    return (int) strlen(text) * char_width - UI_CHAR_SPACING;
}

/**
 * @brief Draw a single left-aligned line of text on the status screen.
 * @param frame_buffer Destination framebuffer.
 * @param x Left pixel position for the rendered text.
 * @param y Top pixel position for the text baseline block.
 * @param scale Pixel scale factor for the built-in font.
 * @param color RGB565 text color.
 * @param text Text to render.
 * @return This function does not return a value.
 */
static void draw_text(uint16_t *frame_buffer, int x, int y, int scale, uint16_t color, const char *text) {
    char upper[64];
    uppercase_copy(upper, sizeof(upper), text);

    const int char_width = 5 * scale;

    for (size_t i = 0; upper[i] != '\0'; ++i) {
        const ui_status_glyph_t *glyph = ui_status_font_find_glyph(upper[i]);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (glyph->rows[row] & (1U << (4 - col))) {
                    fill_rect(frame_buffer, x + (col * scale), y + (row * scale), scale, scale, color);
                }
            }
        }
        x += char_width + UI_CHAR_SPACING;
    }
}

/**
 * @brief Calculate the maximum number of monospaced characters that fit on one UI text line.
 * @param scale Pixel scale factor for the built-in font.
 * @return Maximum character count that fits within the screen width.
 */
static int max_chars_per_line(int scale) {
    const int char_width = (5 * scale) + UI_CHAR_SPACING;
    int max_chars = (UI_SCREEN_WIDTH + UI_CHAR_SPACING) / char_width;
    return max_chars > 0 ? max_chars : 1;
}

/**
 * @brief Draw a single centered line of text on the status screen.
 * @param frame_buffer Destination framebuffer.
 * @param y Top pixel position for the text baseline block.
 * @param scale Pixel scale factor for the built-in font.
 * @param color RGB565 text color.
 * @param text Text to render.
 * @return This function does not return a value.
 */
static void draw_text_centered(uint16_t *frame_buffer, int y, int scale, uint16_t color, const char *text) {
    char upper[64];
    uppercase_copy(upper, sizeof(upper), text);

    int width = text_width(upper, scale);
    int x = (UI_SCREEN_WIDTH - width) / 2;
    draw_text(frame_buffer, x, y, scale, color, upper);
}

/**
 * @brief Draw a compact Wi-Fi signal-strength icon in the top-right corner.
 * @param frame_buffer Destination framebuffer.
 * @return This function does not return a value.
 */
static void draw_wifi_indicator(uint16_t *frame_buffer) {
    const uint8_t level = wifi_signal_level();
    const uint16_t active = rgb565(255, 255, 255);
    const int base_x = UI_SCREEN_WIDTH - 36;
    const int base_y = 22;
    const int bar_width = 4;
    const int bar_gap = 3;
    const int bar_heights[4] = {4, 8, 12, 16};

    for (int i = 0; i < 4; ++i) {
        const int x = base_x + (i * (bar_width + bar_gap));
        const int h = bar_heights[i];
        const int y = base_y - h;
        if (i < level) {
            fill_rect(frame_buffer, x, y, bar_width, h, active);
        } else {
            draw_rect_outline(frame_buffer, x, y, bar_width, h, 1, active);
        }
    }
}

/**
 * @brief Draw a centered multi-line text block split on newline characters.
 * @param frame_buffer Destination framebuffer.
 * @param start_y Top pixel position for the first rendered line.
 * @param scale Pixel scale factor for the built-in font.
 * @param color RGB565 text color.
 * @param text Multi-line text block to render.
 * @return This function does not return a value.
 */
static void draw_text_block_centered(uint16_t *frame_buffer, int start_y, int scale, uint16_t color, const char *text) {
    if (text == NULL || text[0] == '\0') {
        return;
    }

    const int max_chars = max_chars_per_line(scale);
    const int line_height = (7 * scale) + (scale * 2);
    const char *cursor = text;
    int line_index = 0;

    while (*cursor != '\0') {
        char line[64];
        size_t line_len = 0;
        const char *segment_end = cursor;
        const char *last_space = NULL;

        while (*segment_end != '\0' && *segment_end != '\n' && line_len < (size_t) max_chars) {
            if (*segment_end == ' ') {
                last_space = segment_end;
            }
            segment_end++;
            line_len++;
        }

        if (*segment_end != '\0' && *segment_end != '\n' && line_len == (size_t) max_chars && last_space != NULL) {
            segment_end = last_space;
            line_len = (size_t) (segment_end - cursor);
        }

        if (line_len >= sizeof(line)) {
            line_len = sizeof(line) - 1;
        }

        memcpy(line, cursor, line_len);
        line[line_len] = '\0';

        draw_text_centered(frame_buffer, start_y + (line_index * line_height), scale, color, line);
        line_index++;

        cursor = segment_end;
        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor == '\n') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
    }
}

/**
 * @brief Render a full assistant status screen into the framebuffer.
 * @param frame_buffer Destination RGB565 framebuffer.
 * @param state Status state to render.
 * @param detail Optional detail text shown beneath the title.
 * @return This function does not return a value.
 */
void ui_status_render_status(uint16_t *frame_buffer, ui_status_state_t state, const char *detail) {
    const uint16_t bg = state_bg(state);
    const uint16_t fg = rgb565(255, 255, 255);
    const char *title = state_title(state);
    const char *subtitle = state_subtitle(state);

    fill_rect(frame_buffer, 0, 0, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT, bg);
    draw_wifi_indicator(frame_buffer);
    if (title != NULL && title[0] != '\0') {
        draw_text_centered(frame_buffer, 28, UI_TITLE_SCALE, fg, title);
    }
    if (subtitle != NULL && subtitle[0] != '\0') {
        draw_text_centered(frame_buffer, 110, UI_BODY_SCALE, fg, subtitle);
    }
    if (detail != NULL && detail[0] != '\0') {
        const int detail_y = (title != NULL && title[0] == '\0' && subtitle != NULL && subtitle[0] == '\0') ? 72 : 160;
        draw_text_block_centered(frame_buffer, detail_y, UI_BODY_SCALE, fg, detail);
    }
}

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
                            const char *location_text) {
    const uint16_t bg = state_bg(UI_STATUS_CLOCK);
    const uint16_t fg = rgb565(255, 255, 255);
    const uint16_t muted = rgb565(191, 219, 254);

    fill_rect(frame_buffer, 0, 0, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT, bg);
    draw_wifi_indicator(frame_buffer);
    draw_text_centered(frame_buffer, 24, UI_BODY_SCALE, muted, "LOCAL TIME");
    draw_text_centered(frame_buffer, 72, UI_TITLE_SCALE, fg, time_text != NULL ? time_text : "");
    draw_text_centered(frame_buffer, 136, UI_BODY_SCALE, fg, date_text != NULL ? date_text : "");
    if (location_text != NULL && location_text[0] != '\0') {
        draw_text_block_centered(frame_buffer, 182, UI_BODY_SCALE, muted, location_text);
    }
}
