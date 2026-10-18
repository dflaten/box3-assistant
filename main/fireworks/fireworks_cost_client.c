#include "fireworks/fireworks_cost_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "fireworks/fireworks_cost_parse.h"
#include "net/http_trace.h"
#include "system/time_support.h"
#include "system/wifi_support.h"

#define FIREWORKS_COST_HTTP_TRACE_BODY_SIZE 4096
#define FIREWORKS_COST_MONTH_BOUND_LEN      24

typedef http_trace_buffer_t fireworks_cost_trace_t;

static const char *TAG = "fireworks-cost";
static volatile esp_http_client_handle_t s_active_client;

/**
 * @brief Collect HTTP response body bytes for cost requests.
 * @param evt The ESP HTTP client event being handled.
 * @return ESP_OK after processing the event.
 * @note Response data is appended into the caller-provided trace buffer.
 */
static esp_err_t fireworks_cost_http_event_handler(esp_http_client_event_t *evt) {
    fireworks_cost_trace_t *trace = (fireworks_cost_trace_t *) evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != NULL && evt->data_len > 0) {
        return http_trace_append(trace, (const char *) evt->data, evt->data_len);
    }

    return ESP_OK;
}

/**
 * @brief Build the inclusive start and exclusive end bounds for the current UTC month.
 * @param start Destination buffer for the inclusive month start timestamp.
 * @param start_size Size of the start buffer in bytes.
 * @param end Destination buffer for the exclusive next-month timestamp.
 * @param end_size Size of the end buffer in bytes.
 * @return This function does not return a value.
 */
static void fireworks_cost_month_range(char *start, size_t start_size, char *end, size_t end_size) {
    time_t now = time(NULL);
    struct tm utc = {0};
    gmtime_r(&now, &utc);

    int year = utc.tm_year + 1900;
    int month = utc.tm_mon + 1;
    snprintf(start, start_size, "%04d-%02d-01T00:00:00Z", year, month);

    int end_year = year;
    int end_month = month + 1;
    if (end_month > 12) {
        end_month = 1;
        end_year++;
    }
    snprintf(end, end_size, "%04d-%02d-01T00:00:00Z", end_year, end_month);
}

bool fireworks_cost_client_is_configured(void) {
#ifdef CONFIG_FIREWORKS_COST_ENABLED
    return CONFIG_FIREWORKS_API_KEY[0] != '\0' && CONFIG_FIREWORKS_ACCOUNT_ID[0] != '\0' &&
           CONFIG_FIREWORKS_COST_KEY_ID[0] != '\0';
#else
    return false;
#endif
}

esp_err_t fireworks_cost_client_fetch_month_cents(uint32_t *out_cents) {
    if (out_cents == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!fireworks_cost_client_is_configured()) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!wifi_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!time_support_is_synced()) {
        ESP_LOGW(TAG, "Skipping cost query until the clock is synced");
        return ESP_ERR_INVALID_STATE;
    }

    char start[FIREWORKS_COST_MONTH_BOUND_LEN];
    char end[FIREWORKS_COST_MONTH_BOUND_LEN];
    fireworks_cost_month_range(start, sizeof(start), end, sizeof(end));

    char body[384];
    snprintf(body,
             sizeof(body),
             "{\"startTime\":\"%s\",\"endTime\":\"%s\",\"scope\":\"ACCOUNT\",\"groupBy\":[\"API_KEY\"],"
             "\"filter\":{\"apiKeyId\":\"%s\"}}",
             start,
             end,
             CONFIG_FIREWORKS_COST_KEY_ID);

    char url[256];
    snprintf(url,
             sizeof(url),
             "%s/accounts/%s/usageCosts:query",
             CONFIG_FIREWORKS_COST_BASE_URL,
             CONFIG_FIREWORKS_ACCOUNT_ID);

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", CONFIG_FIREWORKS_API_KEY);

    fireworks_cost_trace_t trace = {0};
    trace.body = calloc(1, FIREWORKS_COST_HTTP_TRACE_BODY_SIZE);
    if (trace.body == NULL) {
        return ESP_ERR_NO_MEM;
    }
    trace.capacity = FIREWORKS_COST_HTTP_TRACE_BODY_SIZE;

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = CONFIG_FIREWORKS_TIMEOUT_MS,
        .event_handler = fireworks_cost_http_event_handler,
        .user_data = &trace,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(trace.body);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_post_field(client, body, (int) strlen(body));

    s_active_client = client;
    ESP_LOGI(TAG, "Starting Fireworks cost request: url=%s range=%s..%s", url, start, end);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (s_active_client == client) {
        s_active_client = NULL;
    }
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fireworks cost request failed: %s", esp_err_to_name(err));
        free(trace.body);
        return err;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "Fireworks cost request returned HTTP %d body=\"%.*s\"", status, 200, trace.body);
        free(trace.body);
        return ESP_ERR_INVALID_RESPONSE;
    }

    bool parsed = fireworks_cost_parse_subtotal_cents(trace.body, out_cents);
    free(trace.body);
    if (!parsed) {
        ESP_LOGE(TAG, "Fireworks cost response did not contain a subtotal");
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "Fireworks cost subtotal: %u cents", (unsigned) *out_cents);
    return ESP_OK;
}

esp_err_t fireworks_cost_client_cancel_active_request(void) {
    esp_http_client_handle_t client = s_active_client;
    if (client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "Cancelling active Fireworks cost request");
    return esp_http_client_cancel_request(client);
}
