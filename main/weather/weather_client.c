#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "weather/weather_client.h"

#define WEATHER_HTTP_TRACE_BODY_SIZE 4096
#define FARGO_LATITUDE 46.8772
#define FARGO_LONGITUDE -96.7898

typedef struct {
    char *body;
    int capacity;
    int len;
} weather_http_trace_t;

static const char *TAG = "weather";

static esp_err_t weather_http_event_handler(esp_http_client_event_t *evt)
{
    weather_http_trace_t *trace = (weather_http_trace_t *)evt->user_data;
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

static esp_err_t weather_http_trace_init(weather_http_trace_t *trace)
{
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

static void weather_http_trace_deinit(weather_http_trace_t *trace)
{
    if (trace == NULL) {
        return;
    }

    free(trace->body);
    trace->body = NULL;
    trace->capacity = 0;
    trace->len = 0;
}

static const char *weather_code_summary(int code)
{
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

static esp_err_t json_get_number(const cJSON *object, const char *key, double *out_value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsNumber(item)) {
        return ESP_FAIL;
    }

    *out_value = item->valuedouble;
    return ESP_OK;
}

static esp_err_t json_get_first_array_number(const cJSON *object, const char *key, double *out_value)
{
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!cJSON_IsArray(array)) {
        return ESP_FAIL;
    }

    const cJSON *item = cJSON_GetArrayItem(array, 0);
    if (!cJSON_IsNumber(item)) {
        return ESP_FAIL;
    }

    *out_value = item->valuedouble;
    return ESP_OK;
}

esp_err_t weather_client_fetch_today(weather_report_t *out_report)
{
    if (out_report == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_report = (weather_report_t){ 0 };

    char url[512];
    snprintf(url,
             sizeof(url),
             "%s/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code,wind_speed_10m&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code&temperature_unit=fahrenheit&wind_speed_unit=mph&timezone=America%%2FChicago&forecast_days=1",
             CONFIG_WEATHER_BASE_URL,
             FARGO_LATITUDE,
             FARGO_LONGITUDE);

    weather_http_trace_t trace = { 0 };
    ESP_RETURN_ON_ERROR(weather_http_trace_init(&trace), TAG, "Failed to allocate weather trace buffer");

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = CONFIG_WEATHER_TIMEOUT_MS,
        .event_handler = weather_http_event_handler,
        .user_data = &trace,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    ESP_LOGI(TAG, "Fetching Fargo weather from %s", url);

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        weather_http_trace_deinit(&trace);
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Weather request failed: %s (status=%d)", esp_err_to_name(err), status);
        esp_http_client_cleanup(client);
        weather_http_trace_deinit(&trace);
        return err;
    }

    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "Weather request returned HTTP %d", status);
        esp_http_client_cleanup(client);
        weather_http_trace_deinit(&trace);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(trace.body);
    esp_http_client_cleanup(client);
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

    err = json_get_number(current, "temperature_2m", &current_temp);
    if (err == ESP_OK) {
        err = json_get_number(current, "weather_code", &current_weather_code);
    }
    if (err == ESP_OK) {
        err = json_get_number(current, "wind_speed_10m", &current_wind_speed);
    }
    if (err == ESP_OK) {
        err = json_get_first_array_number(daily, "temperature_2m_max", &max_temp);
    }
    if (err == ESP_OK) {
        err = json_get_first_array_number(daily, "temperature_2m_min", &min_temp);
    }
    if (err == ESP_OK) {
        err = json_get_first_array_number(daily, "precipitation_probability_max", &precip_probability);
    }

    if (err != ESP_OK) {
        cJSON_Delete(root);
        weather_http_trace_deinit(&trace);
        ESP_LOGE(TAG, "Weather response did not contain expected numeric fields");
        return err;
    }

    out_report->current_temp_f = (int)(current_temp >= 0 ? current_temp + 0.5 : current_temp - 0.5);
    out_report->max_temp_f = (int)(max_temp >= 0 ? max_temp + 0.5 : max_temp - 0.5);
    out_report->min_temp_f = (int)(min_temp >= 0 ? min_temp + 0.5 : min_temp - 0.5);
    out_report->max_precip_probability = (int)(precip_probability + 0.5);
    out_report->wind_speed_mph = (int)(current_wind_speed + 0.5);
    snprintf(out_report->summary,
             sizeof(out_report->summary),
             "%s",
             weather_code_summary((int)(current_weather_code + 0.5)));

    ESP_LOGI(TAG,
             "Fargo weather: now=%dF high=%dF low=%dF precip=%d%% wind=%dmph summary=%s",
             out_report->current_temp_f,
             out_report->max_temp_f,
             out_report->min_temp_f,
             out_report->max_precip_probability,
             out_report->wind_speed_mph,
             out_report->summary);

    cJSON_Delete(root);
    weather_http_trace_deinit(&trace);
    return ESP_OK;
}

void weather_client_format_brief(const weather_report_t *report, char *buffer, size_t buffer_size)
{
    if (report == NULL || buffer == NULL || buffer_size == 0) {
        return;
    }

    snprintf(buffer,
             buffer_size,
             "%dF %d/%d %s",
             report->current_temp_f,
             report->max_temp_f,
             report->min_temp_f,
             report->summary);
}

void weather_client_format_detail(const weather_report_t *report, char *buffer, size_t buffer_size)
{
    if (report == NULL || buffer == NULL || buffer_size == 0) {
        return;
    }

    snprintf(buffer,
             buffer_size,
             "NOW %dF %s\nHI %dF LO %dF\nWIND %d MPH\nRAIN %d%%",
             report->current_temp_f,
             report->summary,
             report->max_temp_f,
             report->min_temp_f,
             report->wind_speed_mph,
             report->max_precip_probability);
}
