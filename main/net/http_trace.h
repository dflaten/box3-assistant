#pragma once

#include "esp_err.h"

typedef struct {
    char *body;
    int capacity;
    int len;
} http_trace_buffer_t;

/**
 * @brief Append HTTP response bytes into a bounded trace buffer.
 * @param trace Trace buffer to extend.
 * @param data Response bytes to append.
 * @param data_len Byte count available at data.
 * @return ESP_OK after processing the append request.
 */
esp_err_t http_trace_append(http_trace_buffer_t *trace, const char *data, int data_len);
