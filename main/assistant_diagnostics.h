#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_system.h"

typedef enum {
    ASSISTANT_DIAG_DETAIL_NONE = 0,
    ASSISTANT_DIAG_DETAIL_WAKE,
    ASSISTANT_DIAG_DETAIL_EXEC_START,
    ASSISTANT_DIAG_DETAIL_WEATHER_START,
    ASSISTANT_DIAG_DETAIL_WEATHER_ATTEMPT,
    ASSISTANT_DIAG_DETAIL_WEATHER_PARSE,
    ASSISTANT_DIAG_DETAIL_WEATHER_DONE,
    ASSISTANT_DIAG_DETAIL_HUE_REQUEST,
    ASSISTANT_DIAG_DETAIL_TIMEOUT,
    ASSISTANT_DIAG_DETAIL_FINISH,
} assistant_diag_detail_t;

typedef struct {
    bool active;
    bool timed_out;
    int32_t command_id;
    int32_t last_err;
    int32_t weather_day;
    uint32_t attempt;
    uint32_t free_heap_bytes;
    uint32_t largest_free_block_bytes;
    uint32_t uptime_ms;
    uint8_t assistant_stage;
    uint8_t detail_stage;
} assistant_diag_record_t;

esp_err_t assistant_diag_init(void);
void assistant_diag_capture_wake(void);
void assistant_diag_start_command(int command_id, uint8_t assistant_stage);
void assistant_diag_update_detail(uint8_t assistant_stage,
                                  assistant_diag_detail_t detail_stage,
                                  int32_t weather_day,
                                  uint32_t attempt,
                                  esp_err_t last_err);
void assistant_diag_mark_timeout(uint8_t assistant_stage);
void assistant_diag_finish_command(esp_err_t last_err);
bool assistant_diag_format_previous_issue(char *buffer, size_t buffer_size);
void assistant_diag_log_previous_issue(void);
esp_reset_reason_t assistant_diag_previous_reset_reason(void);
