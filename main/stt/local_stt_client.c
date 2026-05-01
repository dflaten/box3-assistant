#include "stt/local_stt_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cJSON.h"

#include "esp_check.h"
#include "esp_log.h"

#include "net/line_socket.h"
#include "stt/local_stt_protocol.h"
#include "system/wifi_support.h"

#define LOCAL_STT_LINE_MAX_BYTES 512

static const char *TAG = "local-stt";
static int s_active_socket = -1;

/**
 * @brief Copy a Wyoming event error message into a fixed log buffer.
 * @param data_item Parsed event data object that may contain a message field.
 * @param buffer Destination buffer for the message text.
 * @param buffer_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
static void local_stt_extract_error_message(const cJSON *data_item, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    buffer[0] = '\0';
    if (!cJSON_IsObject(data_item)) {
        return;
    }

    const cJSON *message_item = cJSON_GetObjectItemCaseSensitive(data_item, "message");
    if (cJSON_IsString(message_item) && message_item->valuestring != NULL && message_item->valuestring[0] != '\0') {
        snprintf(buffer, buffer_size, "%s", message_item->valuestring);
    }
}

/**
 * @brief Copy a string field from a JSON object into a fixed buffer.
 * @param data_item JSON object to inspect.
 * @param field Field name to copy.
 * @param buffer Destination buffer for the copied string.
 * @param buffer_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
static void
local_stt_copy_json_string_field(const cJSON *data_item, const char *field, char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    buffer[0] = '\0';
    if (!cJSON_IsObject(data_item) || field == NULL || field[0] == '\0') {
        return;
    }

    const cJSON *value_item = cJSON_GetObjectItemCaseSensitive(data_item, field);
    if (cJSON_IsString(value_item) && value_item->valuestring != NULL && value_item->valuestring[0] != '\0') {
        snprintf(buffer, buffer_size, "%s", value_item->valuestring);
    }
}

/**
 * @brief Send a Wyoming event line with an optional payload.
 * @param sock Connected socket descriptor.
 * @param type Event type string.
 * @param data Optional JSON data object for the event.
 * @param payload Optional payload bytes to send after the event line.
 * @param payload_size Payload size in bytes.
 * @return ESP_OK on success, or an ESP error code on JSON or socket failure.
 */
static esp_err_t
local_stt_send_event(int sock, const char *type, cJSON *data, const uint8_t *payload, size_t payload_size) {
    cJSON *event = cJSON_CreateObject();
    if (event == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(event, "type", type);
    if (data != NULL) {
        cJSON_AddItemToObject(event, "data", data);
    }
    if (payload != NULL && payload_size > 0) {
        cJSON_AddNumberToObject(event, "payload_length", (double) payload_size);
    }

    char *line = cJSON_PrintUnformatted(event);
    cJSON_Delete(event);
    if (line == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = line_socket_send_all(sock, line, strlen(line));
    if (err == ESP_OK) {
        err = line_socket_send_all(sock, "\n", 1);
    }
    if (err == ESP_OK && payload != NULL && payload_size > 0) {
        err = line_socket_send_all(sock, payload, payload_size);
    }

    free(line);
    return err;
}

bool local_stt_client_is_configured(void) {
    return CONFIG_LOCAL_STT_ENABLED && CONFIG_LOCAL_STT_BASE_URL[0] != '\0';
}

esp_err_t local_stt_client_transcribe(const uint8_t *audio_bytes,
                                      size_t audio_size,
                                      uint32_t rate,
                                      uint8_t width,
                                      uint8_t channels,
                                      char *transcript,
                                      size_t transcript_size) {
    if (audio_bytes == NULL || audio_size == 0 || transcript == NULL || transcript_size == 0 || rate == 0 ||
        width == 0 || channels == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!local_stt_client_is_configured()) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!wifi_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    transcript[0] = '\0';

    char host[96];
    char port[8];
    if (!line_socket_parse_host_port(CONFIG_LOCAL_STT_BASE_URL, "10300", host, sizeof(host), port, sizeof(port))) {
        ESP_LOGW(TAG, "Invalid local STT address: %s", CONFIG_LOCAL_STT_BASE_URL);
        return ESP_ERR_INVALID_ARG;
    }

    int sock = -1;
    ESP_RETURN_ON_ERROR(
        line_socket_connect(host, port, CONFIG_LOCAL_STT_TIMEOUT_MS, &sock), TAG, "Local STT socket connect failed");
    s_active_socket = sock;
    ESP_LOGI(TAG,
             "Starting local STT request: host=%s port=%s bytes=%u rate=%lu width=%u channels=%u timeout_ms=%d",
             host,
             port,
             (unsigned) audio_size,
             (unsigned long) rate,
             (unsigned) width,
             (unsigned) channels,
             CONFIG_LOCAL_STT_TIMEOUT_MS);

    esp_err_t err = local_stt_send_event(sock, "transcribe", NULL, NULL, 0);
    if (err == ESP_OK) {
        cJSON *start_data = cJSON_CreateObject();
        if (start_data == NULL) {
            err = ESP_ERR_NO_MEM;
        } else {
            cJSON_AddNumberToObject(start_data, "rate", (double) rate);
            cJSON_AddNumberToObject(start_data, "width", (double) width);
            cJSON_AddNumberToObject(start_data, "channels", (double) channels);
            err = local_stt_send_event(sock, "audio-start", start_data, NULL, 0);
        }
    }

    const size_t chunk_size = 1024;
    for (size_t offset = 0; err == ESP_OK && offset < audio_size; offset += chunk_size) {
        size_t chunk_bytes = audio_size - offset;
        if (chunk_bytes > chunk_size) {
            chunk_bytes = chunk_size;
        }

        cJSON *chunk_data = cJSON_CreateObject();
        if (chunk_data == NULL) {
            err = ESP_ERR_NO_MEM;
            break;
        }
        cJSON_AddNumberToObject(chunk_data, "rate", (double) rate);
        cJSON_AddNumberToObject(chunk_data, "width", (double) width);
        cJSON_AddNumberToObject(chunk_data, "channels", (double) channels);
        err = local_stt_send_event(sock, "audio-chunk", chunk_data, audio_bytes + offset, chunk_bytes);
    }

    if (err == ESP_OK) {
        err = local_stt_send_event(sock, "audio-stop", NULL, NULL, 0);
    }

    char line[LOCAL_STT_LINE_MAX_BYTES];
    while (err == ESP_OK) {
        err = line_socket_recv_line(sock, line, sizeof(line));
        if (err != ESP_OK) {
            break;
        }

        cJSON *event = cJSON_Parse(line);
        if (!cJSON_IsObject(event)) {
            cJSON_Delete(event);
            err = ESP_ERR_INVALID_RESPONSE;
            break;
        }

        const cJSON *type_item = cJSON_GetObjectItemCaseSensitive(event, "type");
        const cJSON *data_item = cJSON_GetObjectItemCaseSensitive(event, "data");
        const cJSON *data_length_item = cJSON_GetObjectItemCaseSensitive(event, "data_length");
        const cJSON *payload_length_item = cJSON_GetObjectItemCaseSensitive(event, "payload_length");
        const char *event_type = cJSON_IsString(type_item) ? type_item->valuestring : "";
        int data_length = cJSON_IsNumber(data_length_item) ? data_length_item->valueint : 0;
        int payload_length = cJSON_IsNumber(payload_length_item) ? payload_length_item->valueint : 0;
        char *extra_json = NULL;
        cJSON *extra_data = NULL;
        char inline_text[96];
        char trailing_text[96];
        char resolved_text[96];
        char inline_message[160];
        char trailing_message[160];
        char resolved_message[160];

        ESP_LOGI(TAG,
                 "Local STT received event type=%s data_length=%d payload_length=%d",
                 event_type,
                 data_length,
                 payload_length);

        if (data_length > 0) {
            extra_json = malloc((size_t) data_length + 1U);
            if (extra_json == NULL) {
                cJSON_Delete(event);
                err = ESP_ERR_NO_MEM;
                break;
            }

            err = line_socket_recv_exact(sock, extra_json, (size_t) data_length);
            if (err != ESP_OK) {
                free(extra_json);
                cJSON_Delete(event);
                break;
            }

            extra_json[data_length] = '\0';
            extra_data = cJSON_Parse(extra_json);
            if (!cJSON_IsObject(extra_data)) {
                free(extra_json);
                cJSON_Delete(extra_data);
                cJSON_Delete(event);
                err = ESP_ERR_INVALID_RESPONSE;
                break;
            }
        }

        free(extra_json);
        local_stt_copy_json_string_field(data_item, "text", inline_text, sizeof(inline_text));
        local_stt_copy_json_string_field(extra_data, "text", trailing_text, sizeof(trailing_text));
        local_stt_copy_json_string_field(data_item, "message", inline_message, sizeof(inline_message));
        local_stt_copy_json_string_field(extra_data, "message", trailing_message, sizeof(trailing_message));
        local_stt_protocol_select_string(trailing_text, inline_text, resolved_text, sizeof(resolved_text));
        local_stt_protocol_select_string(trailing_message, inline_message, resolved_message, sizeof(resolved_message));

        if (payload_length > 0) {
            uint8_t discard[128];
            int remaining = payload_length;
            while (remaining > 0 && err == ESP_OK) {
                size_t chunk = remaining > (int) sizeof(discard) ? sizeof(discard) : (size_t) remaining;
                err = line_socket_recv_exact(sock, discard, chunk);
                remaining -= (int) chunk;
            }
        }

        local_stt_protocol_event_result_t result =
            local_stt_protocol_classify_event(event_type, resolved_text, resolved_message);
        if (err == ESP_OK && result == LOCAL_STT_PROTOCOL_EVENT_ERROR) {
            char message[160];
            local_stt_extract_error_message(extra_data != NULL ? extra_data : data_item, message, sizeof(message));
            if (message[0] == '\0') {
                snprintf(message, sizeof(message), "%s", resolved_message);
            }
            ESP_LOGW(TAG, "Local STT server returned error: %s", message[0] != '\0' ? message : "<no message>");
            cJSON_Delete(extra_data);
            cJSON_Delete(event);
            err = ESP_FAIL;
            break;
        }

        if (err == ESP_OK && result == LOCAL_STT_PROTOCOL_EVENT_TRANSCRIPT) {
            snprintf(transcript, transcript_size, "%s", resolved_text);
            ESP_LOGI(TAG, "Local STT transcript received: %s", transcript);
            cJSON_Delete(extra_data);
            cJSON_Delete(event);
            break;
        }

        cJSON_Delete(extra_data);
        cJSON_Delete(event);
    }

    if (s_active_socket == sock) {
        s_active_socket = -1;
    }
    close(sock);

    if (err == ESP_OK && transcript[0] == '\0') {
        ESP_LOGW(TAG, "Local STT request ended without a transcript event");
        return ESP_ERR_NOT_FOUND;
    }
    return err;
}

esp_err_t local_stt_client_cancel_active_request(void) {
    if (s_active_socket < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    int sock = s_active_socket;
    s_active_socket = -1;
    shutdown(sock, SHUT_RDWR);
    return ESP_OK;
}
