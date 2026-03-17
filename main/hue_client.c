#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "hue_client.h"

typedef struct {
    char body[512];
    int len;
} hue_http_trace_t;

static const char *TAG = "hue-voice";

// Collect enough of the Hue bridge response body for useful success and error logs.
static esp_err_t hue_http_event_handler(esp_http_client_event_t *evt)
{
    hue_http_trace_t *trace = (hue_http_trace_t *)evt->user_data;
    if (trace == NULL) {
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != NULL && evt->data_len > 0) {
        int remaining = (int)sizeof(trace->body) - 1 - trace->len;
        if (remaining > 0) {
            int copy_len = evt->data_len < remaining ? evt->data_len : remaining;
            memcpy(trace->body + trace->len, evt->data, copy_len);
            trace->len += copy_len;
            trace->body[trace->len] = '\0';
        }
    }

    return ESP_OK;
}

esp_err_t hue_client_set_group(bool on)
{
    // Hue often replies with chunked bodies; treat an HTTP 2xx as success even if the body ends early.
    char url[160];
    snprintf(url, sizeof(url),
             "http://%s/api/%s/groups/%s/action",
             CONFIG_HUE_BRIDGE_IP,
             CONFIG_HUE_BRIDGE_API_KEY,
             CONFIG_HUE_BRIDGE_GROUP_ID);

    const char *body = on ? "{\"on\":true}" : "{\"on\":false}";
    hue_http_trace_t trace = { 0 };
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PUT,
        .timeout_ms = 5000,
        .event_handler = hue_http_event_handler,
        .user_data = &trace,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    ESP_LOGI(TAG, "Sending Hue group action: bridge=%s group=%s payload=%s",
             CONFIG_HUE_BRIDGE_IP,
             CONFIG_HUE_BRIDGE_GROUP_ID,
             body);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int64_t content_length = esp_http_client_get_content_length(client);
    bool is_chunked = esp_http_client_is_chunked_response(client);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Hue request status=%d, content_length=%lld, chunked=%s, response_bytes=%d",
                 status,
                 content_length,
                 is_chunked ? "true" : "false",
                 trace.len);
        if (trace.len > 0) {
            ESP_LOGI(TAG, "Hue response body: %s", trace.body);
        }
        esp_http_client_cleanup(client);
        return ESP_OK;
    }

    if (err == ESP_ERR_HTTP_INCOMPLETE_DATA && status >= 200 && status < 300) {
        ESP_LOGW(TAG, "Hue response ended early but bridge returned HTTP %d; treating as success", status);
        ESP_LOGW(TAG, "Hue response details: content_length=%lld, chunked=%s, response_bytes=%d",
                 content_length,
                 is_chunked ? "true" : "false",
                 trace.len);
        if (trace.len > 0) {
            ESP_LOGW(TAG, "Hue response body: %s", trace.body);
        }
        esp_http_client_cleanup(client);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Hue HTTP PUT failed: %s (status=%d, content_length=%lld, chunked=%s, response_bytes=%d)",
             esp_err_to_name(err),
             status,
             content_length,
             is_chunked ? "true" : "false",
             trace.len);
    if (trace.len > 0) {
        ESP_LOGE(TAG, "Hue response body: %s", trace.body);
    }

    esp_http_client_cleanup(client);
    return err;
}
