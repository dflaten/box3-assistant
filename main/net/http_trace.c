#include "net/http_trace.h"

#include <string.h>

/**
 * @brief Append HTTP response bytes into a bounded trace buffer.
 * @param trace Trace buffer to extend.
 * @param data Response bytes to append.
 * @param data_len Byte count available at data.
 * @return ESP_OK after processing the append request.
 */
esp_err_t http_trace_append(http_trace_buffer_t *trace, const char *data, int data_len) {
    if (trace == NULL || trace->body == NULL || trace->capacity <= 0 || data == NULL || data_len <= 0) {
        return ESP_OK;
    }

    int remaining = trace->capacity - 1 - trace->len;
    if (remaining <= 0) {
        return ESP_OK;
    }

    int copy_len = data_len < remaining ? data_len : remaining;
    memcpy(trace->body + trace->len, data, (size_t) copy_len);
    trace->len += copy_len;
    trace->body[trace->len] = '\0';
    return ESP_OK;
}
