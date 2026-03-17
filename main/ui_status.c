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

#include "ui_status.h"

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
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04}},
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

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

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
        return "DONE";
    case UI_STATUS_ERROR:
    default:
        return "ATTENTION";
    }
}

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
        return "UPDATING LIGHTS";
    case UI_STATUS_SUCCESS:
        return "COMMAND COMPLETED";
    case UI_STATUS_ERROR:
    default:
        return "COMMAND NEEDS ATTENTION";
    }
}

static const glyph_t *find_glyph(char c)
{
    for (size_t i = 0; i < sizeof(s_font) / sizeof(s_font[0]); ++i) {
        if (s_font[i].code == c) {
            return &s_font[i];
        }
    }
    return &s_font[0];
}

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

static int text_width(const char *text, int scale)
{
    const int char_width = (5 * scale) + UI_CHAR_SPACING;
    return (int)strlen(text) * char_width - UI_CHAR_SPACING;
}

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

static void render_status(ui_status_state_t state, const char *detail)
{
    const uint16_t bg = state_bg(state);
    const uint16_t fg = rgb565(255, 255, 255);

    fill_rect(0, 0, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT, bg);
    draw_text_centered(28, UI_TITLE_SCALE, fg, state_title(state));
    draw_text_centered(110, UI_BODY_SCALE, fg, state_subtitle(state));
    if (detail != NULL && detail[0] != '\0') {
        draw_text_centered(170, UI_BODY_SCALE, fg, detail);
    }
}

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

void ui_status_note_activity(void)
{
    if (!s_ready) {
        return;
    }
    s_last_activity_tick = xTaskGetTickCount();
}

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
