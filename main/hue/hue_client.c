#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "hue/hue_client.h"

#define HUE_HTTP_TRACE_BODY_SIZE 2048

typedef struct {
    char *body;
    int capacity;
    int len;
} hue_http_trace_t;

static const char *TAG = "hue-voice";

/**
 * @brief Collect HTTP response body bytes for Hue requests.
 * @param evt The ESP HTTP client event being handled.
 * @return ESP_OK after processing the event.
 * @note Response data is appended into the caller-provided trace buffer.
 */
static esp_err_t hue_http_event_handler(esp_http_client_event_t *evt)
{
    hue_http_trace_t *trace = (hue_http_trace_t *)evt->user_data;
    if (trace == NULL || trace->body == NULL || trace->capacity <= 0) {
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != NULL && evt->data_len > 0) {
        int remaining = trace->capacity - 1 - trace->len;
        if (remaining > 0) {
            int copy_len = evt->data_len < remaining ? evt->data_len : remaining;
            memcpy(trace->body + trace->len, evt->data, copy_len);
            trace->len += copy_len;
            if (trace->body != NULL) {
                trace->body[trace->len] = '\0';
            }
        }
    }

    return ESP_OK;
}

/**
 * @brief Normalize a Hue group name into a simple spoken command form.
 * @param src The source Hue group name from the bridge.
 * @param dst Destination buffer for the normalized name.
 * @param dst_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
static void normalize_group_name(const char *src, char *dst, size_t dst_size)
{
    size_t out = 0;
    bool pending_space = false;

    if (dst_size == 0) {
        return;
    }

    for (size_t i = 0; src != NULL && src[i] != '\0' && out + 1 < dst_size; ++i) {
        unsigned char ch = (unsigned char)src[i];
        if (isalnum(ch)) {
            if (pending_space && out > 0 && out + 1 < dst_size) {
                dst[out++] = ' ';
            }
            dst[out++] = (char)tolower(ch);
            pending_space = false;
        } else if (out > 0) {
            pending_space = true;
        }
    }

    if (out > 0 && dst[out - 1] == ' ') {
        out--;
    }
    dst[out] = '\0';
}

/**
 * @brief Check whether a normalized Hue group name is suitable for speech commands.
 * @param name The normalized group name to validate.
 * @return True if the name is usable for commands, otherwise false.
 */
static bool is_group_name_valid(const char *name)
{
    return name != NULL && strlen(name) >= 3;
}

/**
 * @brief Allocate and initialize a temporary response trace buffer for Hue HTTP calls.
 * @param trace The trace structure to initialize.
 * @return ESP_OK on success, or an ESP error code if allocation fails.
 */
static esp_err_t hue_http_trace_init(hue_http_trace_t *trace)
{
    if (trace == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    trace->body = calloc(1, HUE_HTTP_TRACE_BODY_SIZE);
    if (trace->body == NULL) {
        trace->capacity = 0;
        trace->len = 0;
        return ESP_ERR_NO_MEM;
    }

    trace->capacity = HUE_HTTP_TRACE_BODY_SIZE;
    trace->len = 0;
    return ESP_OK;
}

/**
 * @brief Release resources associated with a Hue HTTP trace buffer.
 * @param trace The trace structure to clear and free.
 * @return This function does not return a value.
 */
static void hue_http_trace_deinit(hue_http_trace_t *trace)
{
    if (trace == NULL) {
        return;
    }

    free(trace->body);
    trace->body = NULL;
    trace->capacity = 0;
    trace->len = 0;
}

/**
 * @brief Execute a Hue bridge HTTP request and capture its response details.
 * @param url The full request URL.
 * @param method The HTTP method to use.
 * @param body Optional request body for PUT-style updates.
 * @param trace Optional trace buffer for the response body.
 * @param out_status Optional output for the HTTP status code.
 * @return ESP_OK on success, or an ESP error code if the request fails.
 */
static esp_err_t hue_http_perform(const char *url,
                                  esp_http_client_method_t method,
                                  const char *body,
                                  hue_http_trace_t *trace,
                                  int *out_status)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = method,
        .timeout_ms = 5000,
        .event_handler = hue_http_event_handler,
        .user_data = trace,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    if (body != NULL) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int64_t content_length = esp_http_client_get_content_length(client);
    bool is_chunked = esp_http_client_is_chunked_response(client);

    if (out_status != NULL) {
        *out_status = status;
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Hue request status=%d, content_length=%lld, chunked=%s, response_bytes=%d",
                 status,
                 content_length,
                 is_chunked ? "true" : "false",
                 trace != NULL ? trace->len : 0);
        if (trace != NULL && trace->len > 0) {
            ESP_LOGI(TAG, "Hue response body: %s", trace->body);
        }
        esp_http_client_cleanup(client);
        return ESP_OK;
    }

    if (err == ESP_ERR_HTTP_INCOMPLETE_DATA && status >= 200 && status < 300) {
        ESP_LOGW(TAG, "Hue response ended early but bridge returned HTTP %d; treating as success", status);
        ESP_LOGW(TAG, "Hue response details: content_length=%lld, chunked=%s, response_bytes=%d",
                 content_length,
                 is_chunked ? "true" : "false",
                 trace != NULL ? trace->len : 0);
        if (trace != NULL && trace->len > 0) {
            ESP_LOGW(TAG, "Hue response body: %s", trace->body);
        }
        esp_http_client_cleanup(client);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Hue HTTP request failed: %s (status=%d, content_length=%lld, chunked=%s, response_bytes=%d)",
             esp_err_to_name(err),
             status,
             content_length,
             is_chunked ? "true" : "false",
             trace != NULL ? trace->len : 0);
    if (trace != NULL && trace->len > 0) {
        ESP_LOGE(TAG, "Hue response body: %s", trace->body);
    }

    esp_http_client_cleanup(client);
    return err;
}

/**
 * @brief Fetch the raw Hue groups list from the configured bridge.
 * @param groups Output array for fetched group entries.
 * @param max_groups Maximum number of entries that fit in the output array.
 * @param out_count Output for the number of groups written.
 * @return ESP_OK on success, or an ESP error code if fetch or parsing fails.
 */
static esp_err_t hue_client_fetch_groups(hue_group_t *groups, size_t max_groups, size_t *out_count)
{
    if (groups == NULL || out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;

    char url[160];
    snprintf(url, sizeof(url),
             "http://%s/api/%s/groups",
             CONFIG_HUE_BRIDGE_IP,
             CONFIG_HUE_BRIDGE_API_KEY);

    hue_http_trace_t trace = { 0 };
    int status = 0;
    ESP_RETURN_ON_ERROR(hue_http_trace_init(&trace), TAG, "Failed to allocate HTTP trace buffer");

    ESP_LOGI(TAG, "Fetching Hue groups from bridge=%s", CONFIG_HUE_BRIDGE_IP);
    esp_err_t err = hue_http_perform(url, HTTP_METHOD_GET, NULL, &trace, &status);
    if (err != ESP_OK) {
        hue_http_trace_deinit(&trace);
        return err;
    }
    if (status < 200 || status >= 300) {
        hue_http_trace_deinit(&trace);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(trace.body);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        hue_http_trace_deinit(&trace);
        ESP_LOGE(TAG, "Hue groups response was not a JSON object");
        return ESP_FAIL;
    }

    size_t count = 0;
    cJSON *group = NULL;
    cJSON_ArrayForEach(group, root) {
        if (count >= max_groups) {
            break;
        }
        if (!cJSON_IsObject(group)) {
            continue;
        }

        cJSON *name = cJSON_GetObjectItemCaseSensitive(group, "name");
        cJSON *action = cJSON_GetObjectItemCaseSensitive(group, "action");
        if (!cJSON_IsString(name) || !cJSON_IsObject(action)) {
            continue;
        }
        if (group->string == NULL || group->string[0] == '\0') {
            continue;
        }

        snprintf(groups[count].id, sizeof(groups[count].id), "%s", group->string);
        snprintf(groups[count].name, sizeof(groups[count].name), "%s", name->valuestring);
        count++;
    }
    cJSON_Delete(root);
    hue_http_trace_deinit(&trace);

    *out_count = count;
    ESP_LOGI(TAG, "Fetched %u Hue group(s) from bridge", (unsigned)count);
    return ESP_OK;
}

/**
 * @brief Send an on or off action to a specific Hue group ID.
 * @param group_id The Hue bridge group identifier to control.
 * @param on True to turn the group on, false to turn it off.
 * @return ESP_OK on success, or an ESP error code if the request fails.
 */
esp_err_t hue_client_set_group_by_id(const char *group_id, bool on)
{
    if (group_id == NULL || group_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char url[192];
    snprintf(url, sizeof(url),
             "http://%s/api/%s/groups/%s/action",
             CONFIG_HUE_BRIDGE_IP,
             CONFIG_HUE_BRIDGE_API_KEY,
             group_id);

    const char *body = on ? "{\"on\":true}" : "{\"on\":false}";
    hue_http_trace_t trace = { 0 };
    ESP_RETURN_ON_ERROR(hue_http_trace_init(&trace), TAG, "Failed to allocate HTTP trace buffer");

    ESP_LOGI(TAG, "Sending Hue group action: bridge=%s group=%s payload=%s",
             CONFIG_HUE_BRIDGE_IP,
             group_id,
             body);

    esp_err_t err = hue_http_perform(url, HTTP_METHOD_PUT, body, &trace, NULL);
    hue_http_trace_deinit(&trace);
    return err;
}

/**
 * @brief Fetch Hue groups and reduce them to unique spoken-command targets.
 * @param groups Output array for accepted normalized group entries.
 * @param max_groups Maximum number of entries that fit in the output array.
 * @param out_count Output for the number of accepted groups written.
 * @return ESP_OK on success, or an ESP error code if syncing fails.
 * @note Names are normalized and duplicates are removed before acceptance.
 */
esp_err_t hue_client_sync_groups(hue_group_t *groups, size_t max_groups, size_t *out_count)
{
    if (groups == NULL || out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    hue_group_t fetched[HUE_GROUP_MAX_COUNT];
    size_t fetched_count = 0;
    ESP_RETURN_ON_ERROR(hue_client_fetch_groups(fetched, HUE_GROUP_MAX_COUNT, &fetched_count),
                        TAG,
                        "Failed to fetch Hue groups");

    size_t kept = 0;
    for (size_t i = 0; i < fetched_count && kept < max_groups; ++i) {
        char normalized_name[HUE_GROUP_NAME_LEN];
        normalize_group_name(fetched[i].name, normalized_name, sizeof(normalized_name));
        if (!is_group_name_valid(normalized_name)) {
            ESP_LOGW(TAG, "Skipping Hue group '%s' because its spoken name is not usable", fetched[i].name);
            continue;
        }

        bool duplicate = false;
        for (size_t j = 0; j < kept; ++j) {
            if (strcmp(groups[j].name, normalized_name) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            ESP_LOGW(TAG, "Skipping Hue group '%s' because its normalized name duplicates another group", fetched[i].name);
            continue;
        }

        snprintf(groups[kept].id, sizeof(groups[kept].id), "%s", fetched[i].id);
        snprintf(groups[kept].name, sizeof(groups[kept].name), "%s", normalized_name);
        kept++;
    }

    *out_count = kept;
    ESP_LOGI(TAG, "Accepted %u usable Hue group(s) for runtime commands", (unsigned)kept);
    return ESP_OK;
}
