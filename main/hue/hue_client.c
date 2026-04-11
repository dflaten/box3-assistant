#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs.h"

#include "assistant_diagnostics.h"
#include "assistant_runtime.h"
#include "hue/hue_client.h"
#include "hue/hue_discovery_response.h"
#include "system/wifi_support.h"

#define HUE_HTTP_TRACE_BODY_SIZE 2048
#define HUE_BRIDGE_IP_LEN        16
#define HUE_DISCOVERY_NAMESPACE  "hue_client"
#define HUE_DISCOVERY_KEY        "bridge_ip"
#define HUE_SSDP_MULTICAST_IP    "239.255.255.250"
#define HUE_SSDP_PORT            1900
#define HUE_SSDP_TIMEOUT_MS      1500
#define HUE_SSDP_RESPONSE_SIZE   1024

typedef struct {
    char *body;
    int capacity;
    int len;
} hue_http_trace_t;

static const char *TAG = "hue-voice";
static esp_http_client_handle_t s_active_client;
static char s_bridge_ip[HUE_BRIDGE_IP_LEN];
static bool s_bridge_ip_initialized;

/**
 * @brief Check whether a Hue client error represents a connectivity problem.
 * @param err The ESP error code returned by a Hue client operation.
 * @return True when the error indicates Wi-Fi or TCP connectivity failure, otherwise false.
 */
bool hue_client_error_is_connectivity(esp_err_t err) {
    return err == ESP_ERR_HTTP_CONNECT || err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_STATE;
}

/**
 * @brief Persist the currently selected Hue bridge IP to NVS.
 * @param bridge_ip The IPv4 address string to store.
 * @return ESP_OK on success, or an ESP error code if the value could not be saved.
 */
static esp_err_t hue_bridge_ip_store(const char *bridge_ip) {
    if (bridge_ip == NULL || bridge_ip[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(HUE_DISCOVERY_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, HUE_DISCOVERY_KEY, bridge_ip);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

/**
 * @brief Load a previously discovered Hue bridge IP from NVS.
 * @param out_ip Destination buffer for the IPv4 address string.
 * @param out_ip_size Size of the destination buffer in bytes.
 * @return ESP_OK on success, or an ESP error code if no cached address is available.
 */
static esp_err_t hue_bridge_ip_load(char *out_ip, size_t out_ip_size) {
    if (out_ip == NULL || out_ip_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    out_ip[0] = '\0';

    nvs_handle_t handle;
    esp_err_t err = nvs_open(HUE_DISCOVERY_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_str(handle, HUE_DISCOVERY_KEY, out_ip, &out_ip_size);
    nvs_close(handle);
    return err;
}

/**
 * @brief Remember a Hue bridge IP in memory and best-effort persist it for future boots.
 * @param bridge_ip The IPv4 address string to activate.
 * @return This function does not return a value.
 */
static void hue_bridge_ip_set_active(const char *bridge_ip) {
    if (bridge_ip == NULL || bridge_ip[0] == '\0') {
        return;
    }

    strlcpy(s_bridge_ip, bridge_ip, sizeof(s_bridge_ip));
    s_bridge_ip_initialized = true;

    esp_err_t err = hue_bridge_ip_store(bridge_ip);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to cache discovered Hue bridge IP %s: %s", bridge_ip, esp_err_to_name(err));
    }
}

/**
 * @brief Resolve the currently preferred Hue bridge IP from memory, cache, or config defaults.
 * @param out_ip Destination buffer for the IPv4 address string.
 * @param out_ip_size Size of the destination buffer in bytes.
 * @return ESP_OK on success, or an ESP error code if no usable bridge IP is available.
 */
static esp_err_t hue_bridge_ip_get_preferred(char *out_ip, size_t out_ip_size) {
    if (out_ip == NULL || out_ip_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    out_ip[0] = '\0';

    if (s_bridge_ip_initialized && s_bridge_ip[0] != '\0') {
        strlcpy(out_ip, s_bridge_ip, out_ip_size);
        return ESP_OK;
    }

    if (hue_bridge_ip_load(out_ip, out_ip_size) == ESP_OK && out_ip[0] != '\0') {
        strlcpy(s_bridge_ip, out_ip, sizeof(s_bridge_ip));
        s_bridge_ip_initialized = true;
        return ESP_OK;
    }

    if (strlen(CONFIG_HUE_BRIDGE_IP) == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    strlcpy(out_ip, CONFIG_HUE_BRIDGE_IP, out_ip_size);
    return ESP_OK;
}

/**
 * @brief Discover a Hue bridge on the local network using SSDP and cache its IP address.
 * @param out_ip Destination buffer for the discovered IPv4 address string.
 * @param out_ip_size Size of the destination buffer in bytes.
 * @return ESP_OK on success, or an ESP error code if discovery fails.
 */
static esp_err_t hue_bridge_ip_discover(char *out_ip, size_t out_ip_size) {
    if (out_ip == NULL || out_ip_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!wifi_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    out_ip[0] = '\0';

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGW(TAG, "Failed to create Hue SSDP socket: errno=%d", errno);
        return ESP_FAIL;
    }

    struct timeval timeout = {
        .tv_sec = HUE_SSDP_TIMEOUT_MS / 1000,
        .tv_usec = (HUE_SSDP_TIMEOUT_MS % 1000) * 1000,
    };
    (void) setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(HUE_SSDP_PORT),
        .sin_addr.s_addr = inet_addr(HUE_SSDP_MULTICAST_IP),
    };

    static const char request[] = "M-SEARCH * HTTP/1.1\r\n"
                                  "HOST: 239.255.255.250:1900\r\n"
                                  "MAN: \"ssdp:discover\"\r\n"
                                  "MX: 1\r\n"
                                  "ST: upnp:rootdevice\r\n\r\n";

    if (sendto(sock, request, sizeof(request) - 1, 0, (struct sockaddr *) &dest_addr, sizeof(dest_addr)) < 0) {
        ESP_LOGW(TAG, "Failed to send Hue SSDP discovery: errno=%d", errno);
        close(sock);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Searching for Hue bridge via SSDP");

    while (true) {
        char response[HUE_SSDP_RESPONSE_SIZE];
        struct sockaddr_in source_addr = {0};
        socklen_t source_len = sizeof(source_addr);
        int len = recvfrom(sock, response, sizeof(response) - 1, 0, (struct sockaddr *) &source_addr, &source_len);
        if (len < 0) {
            close(sock);
            return ESP_ERR_NOT_FOUND;
        }

        response[len] = '\0';
        if (!hue_discovery_response_is_hue_bridge(response)) {
            continue;
        }

        const char *addr = inet_ntoa(source_addr.sin_addr);
        if (addr == NULL || addr[0] == '\0') {
            continue;
        }

        strlcpy(out_ip, addr, out_ip_size);
        close(sock);
        hue_bridge_ip_set_active(out_ip);
        ESP_LOGI(TAG, "Discovered Hue bridge at %s", out_ip);
        return ESP_OK;
    }
}

/**
 * @brief Collect HTTP response body bytes for Hue requests.
 * @param evt The ESP HTTP client event being handled.
 * @return ESP_OK after processing the event.
 * @note Response data is appended into the caller-provided trace buffer.
 */
static esp_err_t hue_http_event_handler(esp_http_client_event_t *evt) {
    hue_http_trace_t *trace = (hue_http_trace_t *) evt->user_data;
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
static void normalize_group_name(const char *src, char *dst, size_t dst_size) {
    size_t out = 0;
    bool pending_space = false;

    if (dst_size == 0) {
        return;
    }

    for (size_t i = 0; src != NULL && src[i] != '\0' && out + 1 < dst_size; ++i) {
        unsigned char ch = (unsigned char) src[i];
        if (isalnum(ch)) {
            if (pending_space && out > 0 && out + 1 < dst_size) {
                dst[out++] = ' ';
            }
            dst[out++] = (char) tolower(ch);
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
static bool is_group_name_valid(const char *name) {
    return name != NULL && strlen(name) >= 3;
}

/**
 * @brief Check whether a Hue config response body looks like it came from a Hue bridge.
 * @param body The captured HTTP response body, which may be truncated for large chunked responses.
 * @return True when the body contains early Hue-specific config fields, otherwise false.
 */
static bool hue_probe_body_looks_like_bridge(const char *body) {
    return body != NULL && strstr(body, "\"bridgeid\"") != NULL && strstr(body, "\"ipaddress\"") != NULL;
}

/**
 * @brief Allocate and initialize a temporary response trace buffer for Hue HTTP calls.
 * @param trace The trace structure to initialize.
 * @return ESP_OK on success, or an ESP error code if allocation fails.
 */
static esp_err_t hue_http_trace_init(hue_http_trace_t *trace) {
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
static void hue_http_trace_deinit(hue_http_trace_t *trace) {
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
static esp_err_t hue_http_perform(
    const char *url, esp_http_client_method_t method, const char *body, hue_http_trace_t *trace, int *out_status) {
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "Skipping Hue HTTP request because Wi-Fi is not connected");
        if (out_status != NULL) {
            *out_status = 0;
        }
        return ESP_ERR_INVALID_STATE;
    }

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
    s_active_client = client;
    assistant_diag_update_detail(ASSISTANT_STAGE_EXECUTING, ASSISTANT_DIAG_DETAIL_HUE_REQUEST, -1, 0, ESP_OK);

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
        ESP_LOGI(TAG,
                 "Hue request status=%d, content_length=%lld, chunked=%s, response_bytes=%d",
                 status,
                 content_length,
                 is_chunked ? "true" : "false",
                 trace != NULL ? trace->len : 0);
        if (trace != NULL && trace->len > 0) {
            ESP_LOGI(TAG, "Hue response body: %s", trace->body);
        }
        esp_http_client_cleanup(client);
        s_active_client = NULL;
        return ESP_OK;
    }

    // Some Hue bridge firmware returns a valid 2xx chunked JSON body but closes in a way
    // esp_http_client reports as incomplete. We already have the success payload, so treat
    // this specific case as success instead of surfacing it as a failed light action.
    if (err == ESP_ERR_HTTP_INCOMPLETE_DATA && status >= 200 && status < 300) {
        ESP_LOGW(TAG, "Hue response ended early but bridge returned HTTP %d; treating as success", status);
        ESP_LOGW(TAG,
                 "Hue response details: content_length=%lld, chunked=%s, response_bytes=%d",
                 content_length,
                 is_chunked ? "true" : "false",
                 trace != NULL ? trace->len : 0);
        if (trace != NULL && trace->len > 0) {
            ESP_LOGW(TAG, "Hue response body: %s", trace->body);
        }
        esp_http_client_cleanup(client);
        s_active_client = NULL;
        return ESP_OK;
    }

    ESP_LOGE(TAG,
             "Hue HTTP request failed: %s (status=%d, content_length=%lld, chunked=%s, response_bytes=%d)",
             esp_err_to_name(err),
             status,
             content_length,
             is_chunked ? "true" : "false",
             trace != NULL ? trace->len : 0);
    if (trace != NULL && trace->len > 0) {
        ESP_LOGE(TAG, "Hue response body: %s", trace->body);
    }

    esp_http_client_cleanup(client);
    s_active_client = NULL;
    return err;
}

/**
 * @brief Probe a specific Hue bridge IP and verify the response looks like a Hue bridge.
 * @param bridge_ip The candidate Hue bridge IPv4 address string.
 * @return ESP_OK when the bridge is reachable and returns a valid Hue config payload, or an ESP error code otherwise.
 */
static esp_err_t hue_probe_bridge_at_ip(const char *bridge_ip) {
    if (bridge_ip == NULL || bridge_ip[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char url[160];
    snprintf(url, sizeof(url), "http://%s/api/%s/config", bridge_ip, CONFIG_HUE_BRIDGE_API_KEY);

    hue_http_trace_t trace = {0};
    int status = 0;
    ESP_RETURN_ON_ERROR(hue_http_trace_init(&trace), TAG, "Failed to allocate HTTP trace buffer");

    ESP_LOGI(TAG, "Probing Hue bridge at %s", bridge_ip);
    esp_err_t err = hue_http_perform(url, HTTP_METHOD_GET, NULL, &trace, &status);
    if (err != ESP_OK) {
        hue_http_trace_deinit(&trace);
        return err;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "Hue bridge probe returned HTTP %d", status);
        hue_http_trace_deinit(&trace);
        return ESP_ERR_NOT_FOUND;
    }

    bool valid_bridge = hue_probe_body_looks_like_bridge(trace.body);
    hue_http_trace_deinit(&trace);

    if (!valid_bridge) {
        ESP_LOGW(TAG, "Hue bridge probe response did not look like a Hue bridge");
        return ESP_ERR_NOT_FOUND;
    }

    return ESP_OK;
}

/**
 * @brief Probe the configured or discovered Hue bridge and recover from stale IPs via SSDP discovery.
 * @return ESP_OK when the bridge is reachable and returns a valid Hue config payload, or an ESP error code otherwise.
 */
esp_err_t hue_client_probe_bridge(void) {
    char bridge_ip[HUE_BRIDGE_IP_LEN];
    esp_err_t err = hue_bridge_ip_get_preferred(bridge_ip, sizeof(bridge_ip));
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) {
        return err;
    }

    if (bridge_ip[0] != '\0') {
        err = hue_probe_bridge_at_ip(bridge_ip);
        if (err == ESP_OK) {
            hue_bridge_ip_set_active(bridge_ip);
            return ESP_OK;
        }

        if (err != ESP_ERR_NOT_FOUND && !hue_client_error_is_connectivity(err)) {
            return err;
        }
    }

    err = hue_bridge_ip_discover(bridge_ip, sizeof(bridge_ip));
    if (err != ESP_OK) {
        return err;
    }

    return hue_probe_bridge_at_ip(bridge_ip);
}

/**
 * @brief Fetch the raw Hue groups list from the configured bridge.
 * @param groups Output array for fetched group entries.
 * @param max_groups Maximum number of entries that fit in the output array.
 * @param out_count Output for the number of groups written.
 * @return ESP_OK on success, or an ESP error code if fetch or parsing fails.
 */
static esp_err_t hue_client_fetch_groups(hue_group_t *groups, size_t max_groups, size_t *out_count) {
    if (groups == NULL || out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;

    char bridge_ip[HUE_BRIDGE_IP_LEN];
    ESP_RETURN_ON_ERROR(hue_bridge_ip_get_preferred(bridge_ip, sizeof(bridge_ip)), TAG, "Hue bridge IP is unavailable");

    char url[160];
    snprintf(url, sizeof(url), "http://%s/api/%s/groups", bridge_ip, CONFIG_HUE_BRIDGE_API_KEY);

    hue_http_trace_t trace = {0};
    int status = 0;
    ESP_RETURN_ON_ERROR(hue_http_trace_init(&trace), TAG, "Failed to allocate HTTP trace buffer");

    ESP_LOGI(TAG, "Fetching Hue groups from bridge=%s", bridge_ip);
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
    ESP_LOGI(TAG, "Fetched %u Hue group(s) from bridge", (unsigned) count);
    return ESP_OK;
}

/**
 * @brief Send an on or off action to a specific Hue group ID.
 * @param group_id The Hue bridge group identifier to control.
 * @param on True to turn the group on, false to turn it off.
 * @return ESP_OK on success, or an ESP error code if the request fails.
 */
esp_err_t hue_client_set_group_by_id(const char *group_id, bool on) {
    if (group_id == NULL || group_id[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Hue client set_group_by_id start group=%s on=%s", group_id, on ? "true" : "false");

    char bridge_ip[HUE_BRIDGE_IP_LEN];
    ESP_RETURN_ON_ERROR(hue_bridge_ip_get_preferred(bridge_ip, sizeof(bridge_ip)), TAG, "Hue bridge IP is unavailable");

    char url[192];
    snprintf(url, sizeof(url), "http://%s/api/%s/groups/%s/action", bridge_ip, CONFIG_HUE_BRIDGE_API_KEY, group_id);

    const char *body = on ? "{\"on\":true}" : "{\"on\":false}";
    hue_http_trace_t trace = {0};
    ESP_RETURN_ON_ERROR(hue_http_trace_init(&trace), TAG, "Failed to allocate HTTP trace buffer");

    ESP_LOGI(TAG, "Sending Hue group action: bridge=%s group=%s payload=%s", bridge_ip, group_id, body);

    int status = 0;
    esp_err_t err = hue_http_perform(url, HTTP_METHOD_PUT, body, &trace, &status);
    if (err == ESP_OK && (status < 200 || status >= 300)) {
        ESP_LOGE(TAG, "Hue group action returned HTTP %d", status);
        err = ESP_FAIL;
    }
    ESP_LOGI(TAG, "Hue client set_group_by_id done group=%s status=%d err=%s", group_id, status, esp_err_to_name(err));
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
esp_err_t hue_client_sync_groups(hue_group_t *groups, size_t max_groups, size_t *out_count) {
    if (groups == NULL || out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    hue_group_t fetched[HUE_GROUP_MAX_COUNT];
    size_t fetched_count = 0;
    ESP_RETURN_ON_ERROR(
        hue_client_fetch_groups(fetched, HUE_GROUP_MAX_COUNT, &fetched_count), TAG, "Failed to fetch Hue groups");

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
            ESP_LOGW(
                TAG, "Skipping Hue group '%s' because its normalized name duplicates another group", fetched[i].name);
            continue;
        }

        snprintf(groups[kept].id, sizeof(groups[kept].id), "%s", fetched[i].id);
        snprintf(groups[kept].name, sizeof(groups[kept].name), "%s", normalized_name);
        kept++;
    }

    *out_count = kept;
    ESP_LOGI(TAG, "Accepted %u usable Hue group(s) for runtime commands", (unsigned) kept);
    return ESP_OK;
}

esp_err_t hue_client_cancel_active_request(void) {
    esp_http_client_handle_t client = s_active_client;
    if (client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "Cancelling active Hue request");
    return esp_http_client_cancel_request(client);
}
