#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cJSON.h"

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "assistant_diagnostics.h"
#include "assistant_runtime.h"
#include "system/wifi_support.h"
#include "weather/weather_client.h"

#define WEATHER_HTTP_TRACE_BODY_SIZE 4096
#define WEATHER_MAX_CONNECT_ATTEMPTS 3
#define WEATHER_RETRY_DELAY_MS       250

typedef struct {
    char *body;
    int capacity;
    int len;
} weather_http_trace_t;

static const char *TAG = "weather";
static esp_http_client_handle_t s_active_client;

static bool weather_should_retry(esp_err_t err) {
    return err == ESP_ERR_HTTP_CONNECT;
}

/**
 * @brief Collect HTTP response body bytes for weather requests.
 * @param evt The ESP HTTP client event being handled.
 * @return ESP_OK after processing the event.
 * @note Response data is appended into the caller-provided trace buffer.
 */
static esp_err_t weather_http_event_handler(esp_http_client_event_t *evt) {
    weather_http_trace_t *trace = (weather_http_trace_t *) evt->user_data;
    if (trace == NULL || trace->body == NULL || trace->capacity <= 0) {
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != NULL && evt->data_len > 0) {
        int remaining = trace->capacity - 1 - trace->len;
        if (remaining > 0) {
            int copy_len = evt->data_len < remaining ? evt->data_len : remaining;
            memcpy(trace->body + trace->len, evt->data, copy_len);
            trace->len += copy_len;
            trace->body[trace->len] = '\0';
        }
    }

    return ESP_OK;
}

/**
 * @brief Allocate and initialize a temporary response trace buffer for weather HTTP calls.
 * @param trace The trace structure to initialize.
 * @return ESP_OK on success, or an ESP error code if allocation fails.
 */
static esp_err_t weather_http_trace_init(weather_http_trace_t *trace) {
    if (trace == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    trace->body = calloc(1, WEATHER_HTTP_TRACE_BODY_SIZE);
    if (trace->body == NULL) {
        trace->capacity = 0;
        trace->len = 0;
        return ESP_ERR_NO_MEM;
    }

    trace->capacity = WEATHER_HTTP_TRACE_BODY_SIZE;
    trace->len = 0;
    return ESP_OK;
}

/**
 * @brief Release resources associated with a weather HTTP trace buffer.
 * @param trace The trace structure to clear and free.
 * @return This function does not return a value.
 */
static void weather_http_trace_deinit(weather_http_trace_t *trace) {
    if (trace == NULL) {
        return;
    }

    free(trace->body);
    trace->body = NULL;
    trace->capacity = 0;
    trace->len = 0;
}

/**
 * @brief Convert an Open-Meteo weather code into a short user-facing summary.
 * @param code The numeric weather code from the API response.
 * @return A pointer to static summary text for that code.
 */
static const char *weather_code_summary(int code) {
    switch (code) {
    case 0:
        return "Clear";
    case 1:
    case 2:
        return "Partly cloudy";
    case 3:
        return "Cloudy";
    case 45:
    case 48:
        return "Fog";
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
        return "Drizzle";
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
    case 80:
    case 81:
    case 82:
        return "Rain";
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86:
        return "Snow";
    case 95:
    case 96:
    case 99:
        return "Storms";
    default:
        return "Weather";
    }
}

/**
 * @brief Read a numeric field from a JSON object.
 * @param object The JSON object containing the field.
 * @param key The object key to look up.
 * @param out_value Output for the parsed numeric value.
 * @return ESP_OK on success, or ESP_FAIL if the field is missing or not numeric.
 */
static esp_err_t json_get_number(const cJSON *object, const char *key, double *out_value) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item)) {
        return ESP_FAIL;
    }

    *out_value = item->valuedouble;
    return ESP_OK;
}

/**
 * @brief Read a string value at a specific index from a JSON array field.
 * @param object The JSON object containing the array field.
 * @param key The object key for the array.
 * @param index The array index to read.
 * @param buffer Destination buffer for the copied string.
 * @param buffer_size Size of the destination buffer in bytes.
 * @return ESP_OK on success, or ESP_FAIL if the field is missing or invalid.
 */
static esp_err_t
json_get_array_string_at(const cJSON *object, const char *key, int index, char *buffer, size_t buffer_size) {
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsArray(array) || buffer == NULL || buffer_size == 0) {
        return ESP_FAIL;
    }

    const cJSON *item = cJSON_GetArrayItem(array, index);
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return ESP_FAIL;
    }

    snprintf(buffer, buffer_size, "%s", item->valuestring);
    return ESP_OK;
}

/**
 * @brief Read a numeric value at a specific index from a JSON array field.
 * @param object The JSON object containing the array field.
 * @param key The object key for the array.
 * @param index The array index to read.
 * @param out_value Output for the parsed numeric value.
 * @return ESP_OK on success, or ESP_FAIL if the field is missing or invalid.
 */
static esp_err_t json_get_array_number_at(const cJSON *object, const char *key, int index, double *out_value) {
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsArray(array)) {
        return ESP_FAIL;
    }

    const cJSON *item = cJSON_GetArrayItem(array, index);
    if (!cJSON_IsNumber(item)) {
        return ESP_FAIL;
    }

    *out_value = item->valuedouble;
    return ESP_OK;
}

/**
 * @brief Parse a configured latitude or longitude string into a numeric value.
 * @param value The configuration string to parse.
 * @param label A log-friendly label describing the coordinate type.
 * @param out_value Output for the parsed coordinate.
 * @return ESP_OK on success, or an ESP error code if parsing fails.
 */
static esp_err_t weather_parse_coordinate(const char *value, const char *label, double *out_value) {
    if (value == NULL || out_value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char *end = NULL;
    double parsed = strtod(value, &end);
    if (end == value || (end != NULL && *end != '\0')) {
        ESP_LOGE(TAG, "Invalid weather %s: %s", label, value);
        return ESP_ERR_INVALID_ARG;
    }

    *out_value = parsed;
    return ESP_OK;
}

/**
 * @brief Fetch and parse the configured forecast day from Open-Meteo.
 * @param day The forecast day selector to retrieve.
 * @param out_report Output for the parsed weather report.
 * @return ESP_OK on success, or an ESP error code if fetch or parsing fails.
 */
static esp_err_t weather_client_fetch_forecast(weather_forecast_day_t day, weather_report_t *out_report) {
    if (out_report == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_report = (weather_report_t) {0};

    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "Skipping weather fetch for day=%d because Wi-Fi is disconnected", (int) day);
        return ESP_ERR_INVALID_STATE;
    }

    double latitude = 0;
    double longitude = 0;
    ESP_RETURN_ON_ERROR(
        weather_parse_coordinate(CONFIG_WEATHER_LATITUDE, "latitude", &latitude), TAG, "Invalid weather latitude");
    ESP_RETURN_ON_ERROR(
        weather_parse_coordinate(CONFIG_WEATHER_LONGITUDE, "longitude", &longitude), TAG, "Invalid weather longitude");

    char url[512];
    snprintf(url,
             sizeof(url),
             "%s/v1/"
             "forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code,wind_speed_10m&daily="
             "temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code&temperature_unit="
             "fahrenheit&wind_speed_unit=mph&timezone=%s&forecast_days=2",
             CONFIG_WEATHER_BASE_URL,
             latitude,
             longitude,
             CONFIG_WEATHER_TIMEZONE);

    ESP_LOGI(TAG, "Starting weather fetch day=%d for %s from %s", (int) day, CONFIG_ASSISTANT_LOCATION_NAME, url);
    assistant_diag_update_detail(ASSISTANT_STAGE_EXECUTING, ASSISTANT_DIAG_DETAIL_WEATHER_START, day, 0, ESP_OK);

    esp_err_t err = ESP_FAIL;
    weather_http_trace_t trace = {0};
    cJSON *root = NULL;
    for (int attempt = 1; attempt <= WEATHER_MAX_CONNECT_ATTEMPTS; ++attempt) {
        ESP_RETURN_ON_ERROR(weather_http_trace_init(&trace), TAG, "Failed to allocate weather trace buffer");

        esp_http_client_config_t config = {
            .url = url,
            .method = HTTP_METHOD_GET,
            .timeout_ms = CONFIG_WEATHER_TIMEOUT_MS,
            .event_handler = weather_http_event_handler,
            .user_data = &trace,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) {
            weather_http_trace_deinit(&trace);
            return ESP_FAIL;
        }

        s_active_client = client;
        assistant_diag_update_detail(
            ASSISTANT_STAGE_EXECUTING, ASSISTANT_DIAG_DETAIL_WEATHER_ATTEMPT, day, (uint32_t) attempt, ESP_OK);

        int64_t request_start_us = esp_timer_get_time();
        err = esp_http_client_perform(client);
        int64_t request_elapsed_ms = (esp_timer_get_time() - request_start_us) / 1000;
        int status = esp_http_client_get_status_code(client);
        if (err != ESP_OK) {
            assistant_diag_update_detail(
                ASSISTANT_STAGE_EXECUTING, ASSISTANT_DIAG_DETAIL_WEATHER_ATTEMPT, day, (uint32_t) attempt, err);
            ESP_LOGE(TAG,
                     "Weather request attempt %d/%d failed after %lld ms: %s (status=%d)",
                     attempt,
                     WEATHER_MAX_CONNECT_ATTEMPTS,
                     (long long) request_elapsed_ms,
                     esp_err_to_name(err),
                     status);
            esp_http_client_cleanup(client);
            s_active_client = NULL;
            weather_http_trace_deinit(&trace);
            if (!weather_should_retry(err) || attempt == WEATHER_MAX_CONNECT_ATTEMPTS) {
                return err;
            }
            vTaskDelay(pdMS_TO_TICKS(WEATHER_RETRY_DELAY_MS * attempt));
            continue;
        }

        if (status < 200 || status >= 300) {
            assistant_diag_update_detail(
                ASSISTANT_STAGE_EXECUTING, ASSISTANT_DIAG_DETAIL_WEATHER_ATTEMPT, day, (uint32_t) attempt, ESP_FAIL);
            ESP_LOGE(TAG,
                     "Weather request attempt %d/%d returned HTTP %d after %lld ms",
                     attempt,
                     WEATHER_MAX_CONNECT_ATTEMPTS,
                     status,
                     (long long) request_elapsed_ms);
            esp_http_client_cleanup(client);
            s_active_client = NULL;
            weather_http_trace_deinit(&trace);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG,
                 "Weather request attempt %d/%d completed in %lld ms (status=%d, bytes=%d)",
                 attempt,
                 WEATHER_MAX_CONNECT_ATTEMPTS,
                 (long long) request_elapsed_ms,
                 status,
                 trace.len);

        assistant_diag_update_detail(
            ASSISTANT_STAGE_EXECUTING, ASSISTANT_DIAG_DETAIL_WEATHER_PARSE, day, (uint32_t) attempt, ESP_OK);
        root = cJSON_Parse(trace.body);
        esp_http_client_cleanup(client);
        s_active_client = NULL;
        break;
    }

    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        weather_http_trace_deinit(&trace);
        ESP_LOGE(TAG, "Weather response was not a JSON object");
        return ESP_FAIL;
    }

    const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    if (!cJSON_IsObject(current) || !cJSON_IsObject(daily)) {
        cJSON_Delete(root);
        weather_http_trace_deinit(&trace);
        ESP_LOGE(TAG, "Weather response missing current or daily object");
        return ESP_FAIL;
    }

    double current_temp = 0;
    double current_weather_code = 0;
    double current_wind_speed = 0;
    double max_temp = 0;
    double min_temp = 0;
    double precip_probability = 0;
    double daily_weather_code = 0;
    const int day_index = (day == WEATHER_FORECAST_TOMORROW) ? 1 : 0;

    err = json_get_array_string_at(daily, "time", day_index, out_report->date, sizeof(out_report->date));
    if (err == ESP_OK && day == WEATHER_FORECAST_TODAY) {
        err = json_get_number(current, "temperature_2m", &current_temp);
    }
    if (err == ESP_OK && day == WEATHER_FORECAST_TODAY) {
        err = json_get_number(current, "weather_code", &current_weather_code);
    }
    if (err == ESP_OK && day == WEATHER_FORECAST_TODAY) {
        err = json_get_number(current, "wind_speed_10m", &current_wind_speed);
    }
    if (err == ESP_OK) {
        err = json_get_array_number_at(daily, "temperature_2m_max", day_index, &max_temp);
    }
    if (err == ESP_OK) {
        err = json_get_array_number_at(daily, "temperature_2m_min", day_index, &min_temp);
    }
    if (err == ESP_OK) {
        err = json_get_array_number_at(daily, "precipitation_probability_max", day_index, &precip_probability);
    }
    if (err == ESP_OK) {
        err = json_get_array_number_at(daily, "weather_code", day_index, &daily_weather_code);
    }

    if (err != ESP_OK) {
        cJSON_Delete(root);
        weather_http_trace_deinit(&trace);
        ESP_LOGE(TAG, "Weather response did not contain expected numeric fields");
        return err;
    }

    snprintf(out_report->location, sizeof(out_report->location), "%s", CONFIG_ASSISTANT_LOCATION_NAME);
    out_report->has_current_conditions = day == WEATHER_FORECAST_TODAY;
    out_report->current_temp_f = (int) (current_temp >= 0 ? current_temp + 0.5 : current_temp - 0.5);
    out_report->max_temp_f = (int) (max_temp >= 0 ? max_temp + 0.5 : max_temp - 0.5);
    out_report->min_temp_f = (int) (min_temp >= 0 ? min_temp + 0.5 : min_temp - 0.5);
    out_report->max_precip_probability = (int) (precip_probability + 0.5);
    out_report->wind_speed_mph = (int) (current_wind_speed + 0.5);
    snprintf(out_report->summary,
             sizeof(out_report->summary),
             "%s",
             weather_code_summary(
                 (int) ((day == WEATHER_FORECAST_TODAY ? current_weather_code : daily_weather_code) + 0.5)));

    ESP_LOGI(TAG,
             "%s weather day=%d date=%s now=%dF high=%dF low=%dF precip=%d%% wind=%dmph summary=%s",
             CONFIG_ASSISTANT_LOCATION_NAME,
             day_index,
             out_report->date,
             out_report->current_temp_f,
             out_report->max_temp_f,
             out_report->min_temp_f,
             out_report->max_precip_probability,
             out_report->wind_speed_mph,
             out_report->summary);

    assistant_diag_update_detail(ASSISTANT_STAGE_EXECUTING, ASSISTANT_DIAG_DETAIL_WEATHER_DONE, day, 0, ESP_OK);
    cJSON_Delete(root);
    weather_http_trace_deinit(&trace);
    return ESP_OK;
}

/**
 * @brief Fetch the current-day weather report for the configured location.
 * @param out_report Output for the parsed weather report.
 * @return ESP_OK on success, or an ESP error code if fetch or parsing fails.
 */
esp_err_t weather_client_fetch_today(weather_report_t *out_report) {
    return weather_client_fetch_forecast(WEATHER_FORECAST_TODAY, out_report);
}

/**
 * @brief Fetch the next-day weather report for the configured location.
 * @param out_report Output for the parsed weather report.
 * @return ESP_OK on success, or an ESP error code if fetch or parsing fails.
 */
esp_err_t weather_client_fetch_tomorrow(weather_report_t *out_report) {
    return weather_client_fetch_forecast(WEATHER_FORECAST_TOMORROW, out_report);
}

esp_err_t weather_client_cancel_active_request(void) {
    esp_http_client_handle_t client = s_active_client;
    if (client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "Cancelling active weather request");
    return esp_http_client_cancel_request(client);
}
