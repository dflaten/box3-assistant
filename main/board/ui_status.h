#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    UI_STATUS_BOOTING,
    UI_STATUS_CONNECTING,
    UI_STATUS_READY,
    UI_STATUS_CLOCK,
    UI_STATUS_LISTENING,
    UI_STATUS_WORKING,
    UI_STATUS_SUCCESS,
    UI_STATUS_ERROR,
} ui_status_state_t;

esp_err_t ui_status_init(void);
void ui_status_set(ui_status_state_t state, const char *detail);
esp_err_t ui_status_try_set(ui_status_state_t state, const char *detail);
void ui_status_show_clock(const char *time_text, const char *date_text, const char *location_text);
esp_err_t ui_status_display_set(bool on);
