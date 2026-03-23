#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"
#include "esp_log.h"

#include "bsp/esp-box-3.h"

#include "board/ui_status.h"

static const char *TAG = "hue-voice";

#define UI_SCREEN_WIDTH        BSP_LCD_H_RES
#define UI_SCREEN_HEIGHT       BSP_LCD_V_RES
#define UI_TITLE_SCALE         4
#define UI_BODY_SCALE          2
#define UI_CHAR_SPACING        2
#define UI_IDLE_TIMEOUT_MS     30000
#define UI_IDLE_POLL_MS        1000
#define UI_IDLE_TASK_STACK     4096
#define UI_IDLE_TASK_PRIORITY  2

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static bool s_ready;
static bool s_display_on;
static ui_status_state_t s_current_state = UI_STATUS_BOOTING;
static TickType_t s_last_activity_tick;
static uint16_t s_line_buffer[UI_SCREEN_WIDTH];

typedef struct {
    char code;
    uint8_t rows[7];
} glyph_t;

static const glyph_t s_font[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'%', {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x06, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}},
    {'6', {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
};

/**
 * @brief Convert 8-bit RGB components into RGB565 panel color format.
 * @param r Red component in 8-bit space.
 * @param g Green component in 8-bit space.
 * @param b Blue component in 8-bit space.
 * @return The packed RGB565 color value.
 */
static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/**
 * @brief Pick the background color associated with a UI status state.
 * @param state The status state to render.
 * @return The RGB565 background color for that state.
 */
static uint16_t state_bg(ui_status_state_t state)
{
    switch (state) {
    case UI_STATUS_BOOTING:
        return rgb565(82, 82, 91);
    case UI_STATUS_CONNECTING:
        return rgb565(34, 94, 168);
    case UI_STATUS_READY:
        return rgb565(45, 55, 72);
    case UI_STATUS_LISTENING:
        return rgb565(194, 120, 3);
    case UI_STATUS_WORKING:
        return rgb565(180, 83, 9);
    case UI_STATUS_SUCCESS:
        return rgb565(22, 163, 74);
    case UI_STATUS_ERROR:
    default:
        return rgb565(185, 28, 28);
    }
}

/**
 * @brief Get the primary title text shown for a UI status state.
 * @param state The status state to describe.
 * @return A pointer to static title text for the state.
 */
static const char *state_title(ui_status_state_t state)
{
    switch (state) {
    case UI_STATUS_BOOTING:
        return "STARTING";
    case UI_STATUS_CONNECTING:
        return "CONNECTING";
    case UI_STATUS_READY:
        return "READY";
    case UI_STATUS_LISTENING:
        return "LISTENING";
    case UI_STATUS_WORKING:
        return "WORKING";
    case UI_STATUS_SUCCESS:
        return "";
    case UI_STATUS_ERROR:
    default:
        return "ATTENTION";
    }
}

/**
 * @brief Get the secondary subtitle text shown for a UI status state.
 * @param state The status state to describe.
 * @return A pointer to static subtitle text for the state.
 */
static const char *state_subtitle(ui_status_state_t state)
{
    switch (state) {
    case UI_STATUS_BOOTING:
        return "INITIALIZING ASSISTANT";
    case UI_STATUS_CONNECTING:
        return "JOINING WI-FI";
    case UI_STATUS_READY:
        return "SAY HI ESP";
    case UI_STATUS_LISTENING:
        return "SAY A COMMAND";
    case UI_STATUS_WORKING:
        return "PROCESSING REQUEST";
    case UI_STATUS_SUCCESS:
        return "";
    case UI_STATUS_ERROR:
    default:
        return "COMMAND NEEDS ATTENTION";
    }
}

/**
 * @brief Look up a bitmap glyph definition for a character.
 * @param c The character to render.
 * @return A pointer to the matching glyph, or the space glyph if none exists.
 */
static const glyph_t *find_glyph(char c)
{
    for (size_t i = 0; i < sizeof(s_font) / sizeof(s_font[0]); ++i) {
        if (s_font[i].code == c) {
            return &s_font[i];
        }
    }
    return &s_font[0];
}

/**
 * @brief Turn the LCD panel and backlight on or off as a single operation.
 * @param on True to enable the display, false to disable it.
 * @return ESP_OK on success, or an ESP error code if panel power switching fails.
 */
static esp_err_t display_power_set(bool on)
{
    if (!s_ready || s_display_on == on) {
        return ESP_OK;
    }

    esp_err_t err = esp_lcd_panel_disp_on_off(s_panel, on);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to switch LCD panel %s: %s", on ? "on" : "off", esp_err_to_name(err));
        return err;
    }

    err = on ? bsp_display_backlight_on() : bsp_display_backlight_off();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to switch display backlight %s: %s", on ? "on" : "off", esp_err_to_name(err));
        return err;
    }

    s_display_on = on;
    return ESP_OK;
}

/**
 * @brief Fill a clipped rectangle on the display with a solid color.
 * @param x Left edge in pixels.
 * @param y Top edge in pixels.
 * @param w Rectangle width in pixels.
 * @param h Rectangle height in pixels.
 * @param color RGB565 fill color.
 * @return This function does not return a value.
 */
static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x >= UI_SCREEN_WIDTH || y >= UI_SCREEN_HEIGHT || w <= 0 || h <= 0) {
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

    for (int i = 0; i < w; ++i) {
        s_line_buffer[i] = color;
    }
    for (int row = 0; row < h; ++row) {
        esp_lcd_panel_draw_bitmap(s_panel, x, y + row, x + w, y + row + 1, s_line_buffer);
    }
}

/**
 * @brief Copy text into a buffer while converting it to uppercase.
 * @param dst Destination buffer for the uppercase result.
 * @param dst_size Size of the destination buffer in bytes.
 * @param src Source text to copy and normalize.
 * @return This function does not return a value.
 */
static void uppercase_copy(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) {
        return;
    }
    size_t i = 0;
    for (; i + 1 < dst_size && src != NULL && src[i] != '\0'; ++i) {
        dst[i] = (char)toupper((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

/**
 * @brief Measure the rendered pixel width of a string at a given scale.
 * @param text The text to measure.
 * @param scale Pixel scale factor for the built-in bitmap font.
 * @return The total width of the rendered string in pixels.
 */
static int text_width(const char *text, int scale)
{
    const int char_width = (5 * scale) + UI_CHAR_SPACING;
    return (int)strlen(text) * char_width - UI_CHAR_SPACING;
}

/**
 * @brief Calculate the maximum number of monospaced characters that fit on one UI text line.
 * @param scale Pixel scale factor for the built-in font.
 * @return The maximum character count that fits within the screen width.
 */
static int max_chars_per_line(int scale)
{
    const int char_width = (5 * scale) + UI_CHAR_SPACING;
    int max_chars = (UI_SCREEN_WIDTH + UI_CHAR_SPACING) / char_width;
    return max_chars > 0 ? max_chars : 1;
}

/**
 * @brief Draw a single centered line of text on the status screen.
 * @param y Top pixel position for the text baseline block.
 * @param scale Pixel scale factor for the built-in font.
 * @param color RGB565 text color.
 * @param text The text to render.
 * @return This function does not return a value.
 */
static void draw_text_centered(int y, int scale, uint16_t color, const char *text)
{
    char upper[64];
    uppercase_copy(upper, sizeof(upper), text);

    int width = text_width(upper, scale);
    int x = (UI_SCREEN_WIDTH - width) / 2;
    const int char_width = 5 * scale;

    for (size_t i = 0; upper[i] != '\0'; ++i) {
        const glyph_t *glyph = find_glyph(upper[i]);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (glyph->rows[row] & (1U << (4 - col))) {
                    fill_rect(x + (col * scale), y + (row * scale), scale, scale, color);
                }
            }
        }
        x += char_width + UI_CHAR_SPACING;
    }
}

/**
 * @brief Draw a centered multi-line text block split on newline characters.
 * @param start_y Top pixel position for the first rendered line.
 * @param scale Pixel scale factor for the built-in font.
 * @param color RGB565 text color.
 * @param text The multi-line text block to render.
 * @return This function does not return a value.
 */
static void draw_text_block_centered(int start_y, int scale, uint16_t color, const char *text)
{
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

        while (*segment_end != '\0' && *segment_end != '\n' && line_len < (size_t)max_chars) {
            if (*segment_end == ' ') {
                last_space = segment_end;
            }
            segment_end++;
            line_len++;
        }

        if (*segment_end != '\0' && *segment_end != '\n' && line_len == (size_t)max_chars && last_space != NULL) {
            segment_end = last_space;
            line_len = (size_t)(segment_end - cursor);
        }

        if (line_len >= sizeof(line)) {
            line_len = sizeof(line) - 1;
        }

        memcpy(line, cursor, line_len);
        line[line_len] = '\0';

        draw_text_centered(start_y + (line_index * line_height), scale, color, line);
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
 * @brief Render the full status screen for the requested state and detail text.
 * @param state The status state to display.
 * @param detail Optional detail text to show beneath the title and subtitle.
 * @return This function does not return a value.
 */
static void render_status(ui_status_state_t state, const char *detail)
{
    const uint16_t bg = state_bg(state);
    const uint16_t fg = rgb565(255, 255, 255);
    const char *title = state_title(state);
    const char *subtitle = state_subtitle(state);

    fill_rect(0, 0, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT, bg);
    if (title != NULL && title[0] != '\0') {
        draw_text_centered(28, UI_TITLE_SCALE, fg, title);
    }
    if (subtitle != NULL && subtitle[0] != '\0') {
        draw_text_centered(110, UI_BODY_SCALE, fg, subtitle);
    }
    if (detail != NULL && detail[0] != '\0') {
        const int detail_y = (title != NULL && title[0] == '\0' && subtitle != NULL && subtitle[0] == '\0') ? 72 : 160;
        draw_text_block_centered(detail_y, UI_BODY_SCALE, fg, detail);
    }
}

/**
 * @brief Power down the display after extended idle time in the ready state.
 * @param arg Unused FreeRTOS task parameter.
 * @return This task does not return.
 * @note The display stays awake for non-ready states and any recent activity.
 */
static void ui_idle_task(void *arg)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(UI_IDLE_POLL_MS));
        if (!s_ready || !s_display_on) {
            continue;
        }
        if (s_current_state != UI_STATUS_READY) {
            continue;
        }

        TickType_t idle_ticks = xTaskGetTickCount() - s_last_activity_tick;
        if (idle_ticks >= pdMS_TO_TICKS(UI_IDLE_TIMEOUT_MS)) {
            ESP_LOGI(TAG, "Display idle timeout reached; turning screen off");
            display_power_set(false);
        }
    }
}

/**
 * @brief Initialize the BOX-3 display and start the UI idle-management task.
 * @return ESP_OK on success, or an ESP error code if display startup fails.
 * @note This also shows the initial booting screen immediately after setup.
 */
esp_err_t ui_status_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    const bsp_display_config_t display_cfg = {
        .max_transfer_sz = BSP_LCD_H_RES * 20 * (int)sizeof(uint16_t),
    };

    esp_err_t err = bsp_display_new(&display_cfg, &s_panel, &s_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display: %s", esp_err_to_name(err));
        return err;
    }

    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "Failed to enable LCD panel");
    s_display_on = true;

    err = bsp_display_backlight_on();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable display backlight: %s", esp_err_to_name(err));
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
 * @note Calls made before UI initialization are ignored.
 */
static void ui_status_note_activity(void)
{
    if (!s_ready) {
        return;
    }
    s_last_activity_tick = xTaskGetTickCount();
}

/**
 * @brief Update the screen to a new UI status state and optional detail text.
 * @param state The new status state to display.
 * @param detail Optional detail text shown on the screen.
 * @return This function does not return a value.
 * @note This wakes the display if it has been idled off.
 */
void ui_status_set(ui_status_state_t state, const char *detail)
{
    if (!s_ready) {
        return;
    }

    ui_status_note_activity();
    s_current_state = state;
    display_power_set(true);
    render_status(state, detail);
}
