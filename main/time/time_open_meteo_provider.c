#include "time/time_open_meteo_provider.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "net/http_trace.h"
#include "system/wifi_support.h"
#include "time/time_format.h"
#include "time/time_open_meteo_parse.h"

#define TIME_HTTP_TRACE_BODY_SIZE  12288
#define TIME_URL_ENCODED_QUERY_LEN 192

typedef http_trace_buffer_t time_http_trace_t;

static const char *TAG = "time-open-meteo";
static esp_http_client_handle_t s_active_client;

/**
 * @brief Append HTTP response body bytes for time lookup requests.
 * @param evt ESP HTTP event produced by the active client.
 * @return ESP_OK after processing the event.
 */
static esp_err_t time_http_event_handler(esp_http_client_event_t *evt) {
    time_http_trace_t *trace = (time_http_trace_t *) evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != NULL && evt->data_len > 0) {
        return http_trace_append(trace, (const char *) evt->data, evt->data_len);
    }

    return ESP_OK;
}

/**
 * @brief Allocate a temporary trace buffer for an HTTP response body.
 * @param trace Trace structure to initialize.
 * @return ESP_OK on success, or an ESP error code if allocation fails.
 */
static esp_err_t time_http_trace_init(time_http_trace_t *trace) {
    if (trace == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    trace->body = calloc(1, TIME_HTTP_TRACE_BODY_SIZE);
    if (trace->body == NULL) {
        trace->capacity = 0;
        trace->len = 0;
        return ESP_ERR_NO_MEM;
    }

    trace->capacity = TIME_HTTP_TRACE_BODY_SIZE;
    trace->len = 0;
    return ESP_OK;
}

/**
 * @brief Release a trace buffer allocated for an HTTP response body.
 * @param trace Trace structure to clear.
 * @return This function does not return a value.
 */
static void time_http_trace_deinit(time_http_trace_t *trace) {
    if (trace == NULL) {
        return;
    }

    free(trace->body);
    trace->body = NULL;
    trace->capacity = 0;
    trace->len = 0;
}

/**
 * @brief Perform an HTTP GET and copy the response body.
 * @param url Fully formed URL to request.
 * @param out_body Output receiving a heap-allocated response body owned by the caller.
 * @return ESP_OK on success, or an ESP error code on network or allocation failure.
 */
static esp_err_t time_http_get_body(const char *url, char **out_body) {
    if (url == NULL || out_body == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!wifi_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    *out_body = NULL;
    time_http_trace_t trace = {0};
    ESP_RETURN_ON_ERROR(time_http_trace_init(&trace), TAG, "Failed to allocate time HTTP trace");

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = CONFIG_TIME_LOOKUP_TIMEOUT_MS,
        .event_handler = time_http_event_handler,
        .user_data = &trace,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        time_http_trace_deinit(&trace);
        return ESP_FAIL;
    }

    s_active_client = client;
    int64_t request_start_us = esp_timer_get_time();
    esp_err_t err = esp_http_client_perform(client);
    int64_t request_elapsed_ms = (esp_timer_get_time() - request_start_us) / 1000;
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    s_active_client = NULL;

    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "Time lookup failed after %lld ms: %s (status=%d)",
                 (long long) request_elapsed_ms,
                 esp_err_to_name(err),
                 status);
        time_http_trace_deinit(&trace);
        return err;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "Time lookup returned HTTP %d after %lld ms", status, (long long) request_elapsed_ms);
        time_http_trace_deinit(&trace);
        return ESP_FAIL;
    }

    char *body = malloc((size_t) trace.len + 1U);
    if (body == NULL) {
        time_http_trace_deinit(&trace);
        return ESP_ERR_NO_MEM;
    }
    memcpy(body, trace.body, (size_t) trace.len);
    body[trace.len] = '\0';
    time_http_trace_deinit(&trace);

    *out_body = body;
    return ESP_OK;
}

/**
 * @brief Fetch and parse one Open-Meteo geocoding candidate.
 * @param search_query Location name to send to the geocoding endpoint.
 * @param qualifier Optional admin region or country used to rank geocoding results.
 * @param require_qualifier_match True to reject candidates without a matching qualifier.
 * @param out_location Output receiving the resolved location metadata.
 * @return ESP_OK on success, or an ESP error code if lookup fails.
 */
static esp_err_t time_open_meteo_fetch_location_candidate(const char *search_query,
                                                          const char *qualifier,
                                                          bool require_qualifier_match,
                                                          time_location_t *out_location) {
    if (search_query == NULL || search_query[0] == '\0' || out_location == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_location = (time_location_t) {0};

    char encoded_query[TIME_URL_ENCODED_QUERY_LEN];
    if (!time_open_meteo_url_encode_query_value(search_query, encoded_query, sizeof(encoded_query))) {
        return ESP_ERR_INVALID_SIZE;
    }

    char url[384];
    snprintf(url,
             sizeof(url),
             "%s/v1/search?name=%s&count=10&language=en&format=json",
             CONFIG_TIME_GEOCODING_BASE_URL,
             encoded_query);

    char *body = NULL;
    esp_err_t err = time_http_get_body(url, &body);
    if (err != ESP_OK) {
        return err;
    }

    err = time_open_meteo_parse_location_response(body, qualifier, require_qualifier_match, out_location);
    free(body);
    return err;
}

/**
 * @brief Fetch the best matching location from Open-Meteo geocoding.
 * @param query User-provided location search text.
 * @param out_location Output receiving the resolved location metadata.
 * @return ESP_OK on success, or an ESP error code if lookup fails.
 */
static esp_err_t time_open_meteo_fetch_location(const char *query, time_location_t *out_location) {
    if (query == NULL || query[0] == '\0' || out_location == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = time_open_meteo_fetch_location_candidate(query, NULL, false, out_location);
    if (err != ESP_ERR_NOT_FOUND) {
        return err;
    }

    for (size_t qualifier_words = 1; qualifier_words <= 2; ++qualifier_words) {
        char search_query[TIME_QUERY_TEXT_LEN];
        char qualifier[TIME_QUERY_TEXT_LEN];
        if (!time_format_split_location_qualifier(
                query, qualifier_words, search_query, sizeof(search_query), qualifier, sizeof(qualifier))) {
            continue;
        }

        err = time_open_meteo_fetch_location_candidate(search_query, qualifier, true, out_location);
        if (err == ESP_OK) {
            return ESP_OK;
        }
        if (err != ESP_ERR_NOT_FOUND) {
            return err;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

/**
 * @brief Fetch the current UTC offset for a coordinate from Open-Meteo.
 * @param latitude Location latitude.
 * @param longitude Location longitude.
 * @param out_utc_offset_seconds Output receiving seconds east of UTC.
 * @return ESP_OK on success, or an ESP error code if lookup fails.
 */
static esp_err_t time_open_meteo_fetch_utc_offset(double latitude, double longitude, int32_t *out_utc_offset_seconds) {
    if (out_utc_offset_seconds == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char url[384];
    snprintf(url,
             sizeof(url),
             "%s/v1/forecast?latitude=%.4f&longitude=%.4f&current=is_day&timezone=auto&forecast_days=1",
             CONFIG_TIME_LOOKUP_BASE_URL,
             latitude,
             longitude);

    char *body = NULL;
    esp_err_t err = time_http_get_body(url, &body);
    if (err != ESP_OK) {
        return err;
    }

    err = time_open_meteo_parse_utc_offset_response(body, out_utc_offset_seconds);
    free(body);
    return err;
}

/**
 * @brief Cancel the active Open-Meteo time HTTP request if one is running.
 * @return ESP_OK when cancellation is requested, or ESP_ERR_INVALID_STATE if no request is active.
 */
static esp_err_t time_open_meteo_cancel_active_request(void) {
    esp_http_client_handle_t client = s_active_client;
    if (client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "Cancelling active time lookup request");
    return esp_http_client_cancel_request(client);
}

static const time_provider_t s_time_open_meteo_provider = {
    .fetch_location = time_open_meteo_fetch_location,
    .fetch_utc_offset = time_open_meteo_fetch_utc_offset,
    .cancel_active_request = time_open_meteo_cancel_active_request,
};

/**
 * @brief Get the Open-Meteo-backed time provider descriptor.
 * @return Pointer to the static Open-Meteo provider descriptor.
 */
const time_provider_t *time_open_meteo_provider_get(void) {
    return &s_time_open_meteo_provider;
}
