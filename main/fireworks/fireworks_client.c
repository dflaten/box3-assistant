#include "fireworks/fireworks_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "fireworks/fireworks_request.h"
#include "net/http_trace.h"
#include "system/wifi_support.h"

#define FIREWORKS_HTTP_TRACE_BODY_SIZE 8192

typedef http_trace_buffer_t fireworks_http_trace_t;

static const char *TAG = "fireworks";
static esp_http_client_handle_t s_active_client;

/**
 * @brief Collect HTTP response body bytes for Fireworks requests.
 * @param evt The ESP HTTP client event being handled.
 * @return ESP_OK after processing the event.
 * @note Response data is appended into the caller-provided trace buffer.
 */
static esp_err_t fireworks_http_event_handler(esp_http_client_event_t *evt) {
    fireworks_http_trace_t *trace = (fireworks_http_trace_t *) evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != NULL && evt->data_len > 0) {
        return http_trace_append(trace, (const char *) evt->data, evt->data_len);
    }

    return ESP_OK;
}

bool fireworks_client_is_configured(void) {
#ifdef CONFIG_INTEGRATION_FIREWORKS_ENABLED
    return CONFIG_FIREWORKS_BASE_URL[0] != '\0' && CONFIG_FIREWORKS_API_KEY[0] != '\0' &&
           CONFIG_FIREWORKS_MODEL[0] != '\0';
#else
    return false;
#endif
}

esp_err_t fireworks_client_complete(const char *system_prompt,
                                    const char *user_text,
                                    const char *tools_json,
                                    fireworks_response_t *out_response) {
    if (system_prompt == NULL || user_text == NULL || out_response == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!fireworks_client_is_configured()) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!wifi_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    char *body = fireworks_request_build_body(
        CONFIG_FIREWORKS_MODEL, system_prompt, user_text, tools_json, CONFIG_FIREWORKS_MAX_TOKENS);
    if (body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    fireworks_http_trace_t trace = {0};
    trace.body = calloc(1, FIREWORKS_HTTP_TRACE_BODY_SIZE);
    if (trace.body == NULL) {
        free(body);
        return ESP_ERR_NO_MEM;
    }
    trace.capacity = FIREWORKS_HTTP_TRACE_BODY_SIZE;

    char url[256];
    fireworks_request_build_url(CONFIG_FIREWORKS_BASE_URL, url, sizeof(url));

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", CONFIG_FIREWORKS_API_KEY);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = CONFIG_FIREWORKS_TIMEOUT_MS,
        .event_handler = fireworks_http_event_handler,
        .user_data = &trace,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(trace.body);
        free(body);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_post_field(client, body, (int) strlen(body));

    s_active_client = client;
    ESP_LOGI(TAG,
             "Starting Fireworks request: url=%s model=%s bytes=%u",
             url,
             CONFIG_FIREWORKS_MODEL,
             (unsigned) strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (s_active_client == client) {
        s_active_client = NULL;
    }
    esp_http_client_cleanup(client);
    free(body);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fireworks request failed: %s body=\"%.*s\"", esp_err_to_name(err), 200, trace.body);
        free(trace.body);
        return err;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "Fireworks request returned HTTP %d body=\"%.*s\"", status, 200, trace.body);
        free(trace.body);
        return ESP_ERR_INVALID_RESPONSE;
    }

    bool parsed = fireworks_response_parse(trace.body, out_response);
    ESP_LOGI(TAG,
             "Fireworks response parsed=%s kind=%d bytes=%d",
             parsed ? "true" : "false",
             (int) out_response->kind,
             trace.len);
    free(trace.body);

    return parsed ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t fireworks_client_cancel_active_request(void) {
    esp_http_client_handle_t client = s_active_client;
    if (client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "Cancelling active Fireworks request");
    return esp_http_client_cancel_request(client);
}
