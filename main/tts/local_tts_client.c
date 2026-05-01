#include "tts/local_tts_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cJSON.h"

#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "net/line_socket.h"
#include "system/wifi_support.h"

#define LOCAL_TTS_RESPONSE_INITIAL_BYTES 4096
#define LOCAL_TTS_MAX_RESPONSE_BYTES     (320 * 1024)
#define LOCAL_TTS_LINE_MAX_BYTES         512
#define LOCAL_TTS_STREAM_CHUNK_BYTES     2048

typedef struct {
    uint8_t *body;
    size_t len;
    size_t capacity;
    bool overflowed;
    bool connected;
    bool headers_sent;
    bool finished;
    bool disconnected;
    bool transport_error;
    int data_events;
    char content_type[64];
} local_tts_response_t;

static const char *TAG = "local-tts";
static esp_http_client_handle_t s_active_client;
static int s_active_socket = -1;

/**
 * @brief Read a little-endian 16-bit value from a byte buffer.
 * @param data Pointer to at least two bytes.
 * @return Decoded unsigned 16-bit value.
 */
static uint16_t local_tts_read_le16(const uint8_t *data) {
    return (uint16_t) (data[0] | ((uint16_t) data[1] << 8));
}

/**
 * @brief Read a little-endian 32-bit value from a byte buffer.
 * @param data Pointer to at least four bytes.
 * @return Decoded unsigned 32-bit value.
 */
static uint32_t local_tts_read_le32(const uint8_t *data) {
    return (uint32_t) data[0] | ((uint32_t) data[1] << 8) | ((uint32_t) data[2] << 16) | ((uint32_t) data[3] << 24);
}

/**
 * @brief Ensure the HTTP response buffer can hold at least the requested number of bytes.
 * @param response Response accumulator to grow.
 * @param needed Required total capacity in bytes.
 * @return True when the buffer has enough capacity, false on allocation failure or size limit.
 */
static bool local_tts_response_reserve(local_tts_response_t *response, size_t needed) {
    if (response == NULL) {
        return false;
    }
    if (needed <= response->capacity) {
        return true;
    }

    size_t new_capacity = response->capacity > 0 ? response->capacity : LOCAL_TTS_RESPONSE_INITIAL_BYTES;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    if (new_capacity > LOCAL_TTS_MAX_RESPONSE_BYTES) {
        return false;
    }

    uint8_t *new_body = heap_caps_realloc(response->body, new_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (new_body == NULL) {
        new_body = realloc(response->body, new_capacity);
    }
    if (new_body == NULL) {
        return false;
    }

    response->body = new_body;
    response->capacity = new_capacity;
    return true;
}

/**
 * @brief Check whether a configured TTS URL includes an HTTP or HTTPS scheme.
 * @param url URL string to inspect.
 * @return True when the URL starts with http:// or https://, false otherwise.
 */
static bool local_tts_url_has_scheme(const char *url) {
    return url != NULL && (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
}

/**
 * @brief Extract host and port from a configured TTS base URL.
 * @param base_url Configured URL or host:port value.
 * @param host Destination buffer for the parsed host.
 * @param host_size Size of the host destination buffer in bytes.
 * @param port Destination buffer for the parsed port.
 * @param port_size Size of the port destination buffer in bytes.
 * @return True when parsing succeeds, false when the URL cannot fit or is malformed.
 */
static bool
local_tts_parse_host_port(const char *base_url, char *host, size_t host_size, char *port, size_t port_size) {
    const char *default_port = strncmp(base_url, "https://", 8) == 0 ? "443" : "80";
    return line_socket_parse_host_port(base_url, default_port, host, host_size, port, port_size);
}

/**
 * @brief Log a printable preview of an HTTP response body for TTS diagnostics.
 * @param response Response accumulator whose body should be summarized.
 * @return This function does not return a value.
 */
static void local_tts_log_response_preview(const local_tts_response_t *response) {
    if (response == NULL || response->body == NULL || response->len == 0) {
        ESP_LOGW(TAG, "Local TTS response body preview: <empty>");
        return;
    }

    char preview[97];
    size_t preview_len = response->len < sizeof(preview) - 1 ? response->len : sizeof(preview) - 1;
    for (size_t i = 0; i < preview_len; ++i) {
        uint8_t c = response->body[i];
        preview[i] = (c >= 32 && c <= 126) ? (char) c : '.';
    }
    preview[preview_len] = '\0';
    ESP_LOGW(TAG,
             "Local TTS response body preview (%u/%u bytes): %s",
             (unsigned) preview_len,
             (unsigned) response->len,
             preview);
}

/**
 * @brief Log detailed HTTP request failure metadata for local TTS diagnostics.
 * @param url Request URL.
 * @param err ESP error code returned by the HTTP operation.
 * @param status HTTP status code, if available.
 * @param transport_errno Transport errno reported by esp_http_client.
 * @param elapsed_ms Request duration in milliseconds.
 * @param response Collected response metadata and body preview state.
 * @return This function does not return a value.
 */
static void local_tts_log_request_failure(const char *url,
                                          esp_err_t err,
                                          int status,
                                          int transport_errno,
                                          int64_t elapsed_ms,
                                          const local_tts_response_t *response) {
    ESP_LOGW(TAG,
             "Local TTS HTTP failed: err=%s status=%d errno=%d elapsed_ms=%lld url=%s connected=%s "
             "headers_sent=%s finished=%s disconnected=%s transport_error=%s bytes=%u data_events=%d content_type=%s",
             esp_err_to_name(err),
             status,
             transport_errno,
             (long long) elapsed_ms,
             url != NULL ? url : "<null>",
             response != NULL && response->connected ? "true" : "false",
             response != NULL && response->headers_sent ? "true" : "false",
             response != NULL && response->finished ? "true" : "false",
             response != NULL && response->disconnected ? "true" : "false",
             response != NULL && response->transport_error ? "true" : "false",
             response != NULL ? (unsigned) response->len : 0,
             response != NULL ? response->data_events : 0,
             response != NULL && response->content_type[0] != '\0' ? response->content_type : "<none>");
    if (response != NULL && response->len > 0) {
        local_tts_log_response_preview(response);
    }
}

/**
 * @brief Ensure the buffered PCM audio object can hold at least the requested number of bytes.
 * @param audio Audio object whose backing storage should be grown.
 * @param capacity Current mutable capacity value for the backing storage.
 * @param needed Required total capacity in bytes.
 * @return True when the buffer has enough capacity, false on allocation failure or size limit.
 */
static bool local_tts_pcm_reserve(local_tts_audio_t *audio, size_t *capacity, size_t needed) {
    if (audio == NULL || capacity == NULL) {
        return false;
    }
    if (needed <= *capacity) {
        return true;
    }

    size_t new_capacity = *capacity > 0 ? *capacity : LOCAL_TTS_RESPONSE_INITIAL_BYTES;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    if (new_capacity > LOCAL_TTS_MAX_RESPONSE_BYTES) {
        return false;
    }

    uint8_t *new_data = heap_caps_realloc(audio->wav_data, new_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (new_data == NULL) {
        new_data = realloc(audio->wav_data, new_capacity);
    }
    if (new_data == NULL) {
        return false;
    }

    audio->wav_data = new_data;
    audio->pcm_data = audio->wav_data;
    *capacity = new_capacity;
    return true;
}

/**
 * @brief Send an entire buffer over a socket.
 * @param sock Connected socket descriptor.
 * @param data Pointer to bytes to send.
 * @param len Number of bytes to send.
 * @return ESP_OK on success, or ESP_FAIL when send fails.
 */
static esp_err_t local_tts_socket_send_all(int sock, const void *data, size_t len) {
    return line_socket_send_all(sock, data, len);
}

/**
 * @brief Receive an exact number of bytes from a socket.
 * @param sock Connected socket descriptor.
 * @param data Destination buffer.
 * @param len Number of bytes to read.
 * @return ESP_OK on success, ESP_ERR_INVALID_RESPONSE on EOF, or ESP_ERR_TIMEOUT on socket receive failure.
 */
static esp_err_t local_tts_socket_recv_exact(int sock, void *data, size_t len) {
    return line_socket_recv_exact(sock, data, len);
}

/**
 * @brief Receive a newline-terminated JSON event line from a socket.
 * @param sock Connected socket descriptor.
 * @param line Destination string buffer.
 * @param line_size Size of the destination buffer in bytes.
 * @return ESP_OK on success, ESP_ERR_NO_MEM if the line is too long, or an ESP error code.
 */
static esp_err_t local_tts_socket_recv_line(int sock, char *line, size_t line_size) {
    return line_socket_recv_line(sock, line, line_size);
}

/**
 * @brief Update audio format metadata from a Piper event data object.
 * @param data cJSON object containing optional rate, width, and channel metadata.
 * @param audio Audio metadata object to update.
 * @return This function does not return a value.
 */
static void local_tts_event_update_audio_format(const cJSON *data, local_tts_audio_t *audio) {
    if (!cJSON_IsObject(data) || audio == NULL) {
        return;
    }

    const cJSON *rate = cJSON_GetObjectItemCaseSensitive(data, "rate");
    const cJSON *width = cJSON_GetObjectItemCaseSensitive(data, "width");
    const cJSON *channels = cJSON_GetObjectItemCaseSensitive(data, "channels");
    if (cJSON_IsNumber(rate) && rate->valueint > 0) {
        audio->sample_rate = (uint32_t) rate->valueint;
    }
    if (cJSON_IsNumber(width) && width->valueint > 0) {
        audio->bits_per_sample = (uint16_t) (width->valueint * 8);
    }
    if (cJSON_IsNumber(channels) && channels->valueint > 0) {
        audio->channels = (uint16_t) channels->valueint;
    }
}

/**
 * @brief Open a TCP socket connection to the local TTS service.
 * @param host Hostname or IP address to connect to.
 * @param port Service port string.
 * @param out_sock Output socket descriptor on success.
 * @return ESP_OK on success, or an ESP error code if DNS, socket creation, or connect fails.
 */
static esp_err_t local_tts_socket_connect(const char *host, const char *port, int *out_sock) {
    esp_err_t err = line_socket_connect(host, port, CONFIG_TTS_PIPER_TIMEOUT_MS, out_sock);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Local TTS socket connect failed: host=%s port=%s", host, port);
    }
    return err;
}

/**
 * @brief Synthesize text through the Piper socket-event protocol.
 * @param text Text to synthesize.
 * @param out_audio Audio metadata and optional buffered PCM output.
 * @param writer Optional streaming callback for PCM chunks. When NULL, chunks are buffered in out_audio.
 * @param writer_ctx User context pointer passed to writer.
 * @return ESP_OK on success, or an ESP error code if socket synthesis or PCM handling fails.
 */
static esp_err_t local_tts_client_synthesize_socket(const char *text,
                                                    local_tts_audio_t *out_audio,
                                                    local_tts_pcm_writer_t writer,
                                                    void *writer_ctx) {
    char host[96];
    char port[8];
    if (!local_tts_parse_host_port(CONFIG_TTS_PIPER_BASE_URL, host, sizeof(host), port, sizeof(port))) {
        ESP_LOGW(TAG, "Invalid local TTS socket URL: base_url=%s", CONFIG_TTS_PIPER_BASE_URL);
        return ESP_ERR_INVALID_ARG;
    }

    int64_t start_us = esp_timer_get_time();
    int sock = -1;
    esp_err_t err = local_tts_socket_connect(host, port, &sock);
    if (err != ESP_OK) {
        return err;
    }
    s_active_socket = sock;

    cJSON *request = cJSON_CreateObject();
    cJSON *data = cJSON_CreateObject();
    if (request == NULL || data == NULL) {
        cJSON_Delete(request);
        cJSON_Delete(data);
        close(sock);
        s_active_socket = -1;
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(request, "type", "synthesize");
    cJSON_AddStringToObject(data, "text", text);
    cJSON_AddItemToObject(request, "data", data);
    char *request_body = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);
    if (request_body == NULL) {
        close(sock);
        s_active_socket = -1;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG,
             "Starting local TTS socket request: host=%s port=%s chars=%u timeout_ms=%d",
             host,
             port,
             (unsigned) strlen(text),
             CONFIG_TTS_PIPER_TIMEOUT_MS);
    err = local_tts_socket_send_all(sock, request_body, strlen(request_body));
    if (err == ESP_OK) {
        err = local_tts_socket_send_all(sock, "\n", 1);
    }
    free(request_body);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Local TTS socket send failed: err=%s host=%s port=%s", esp_err_to_name(err), host, port);
        close(sock);
        s_active_socket = -1;
        return err;
    }

    size_t pcm_capacity = 0;
    size_t streamed_bytes = 0;
    char line[LOCAL_TTS_LINE_MAX_BYTES];
    out_audio->sample_rate = 22050;
    out_audio->bits_per_sample = 16;
    out_audio->channels = 1;

    while (true) {
        err = local_tts_socket_recv_line(sock, line, sizeof(line));
        if (err != ESP_OK) {
            ESP_LOGW(TAG,
                     "Local TTS socket read event failed: err=%s elapsed_ms=%lld bytes=%u",
                     esp_err_to_name(err),
                     (long long) ((esp_timer_get_time() - start_us) / 1000),
                     (unsigned) out_audio->pcm_size);
            break;
        }

        cJSON *event = cJSON_Parse(line);
        if (!cJSON_IsObject(event)) {
            ESP_LOGW(TAG, "Local TTS socket returned invalid event JSON: %s", line);
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

        local_tts_event_update_audio_format(data_item, out_audio);
        if (data_length > 0) {
            char *extra_json = malloc((size_t) data_length + 1);
            if (extra_json == NULL) {
                cJSON_Delete(event);
                err = ESP_ERR_NO_MEM;
                break;
            }
            err = local_tts_socket_recv_exact(sock, extra_json, (size_t) data_length);
            extra_json[data_length] = '\0';
            if (err == ESP_OK) {
                cJSON *extra_data = cJSON_Parse(extra_json);
                local_tts_event_update_audio_format(extra_data, out_audio);
                cJSON_Delete(extra_data);
            }
            free(extra_json);
            if (err != ESP_OK) {
                cJSON_Delete(event);
                break;
            }
        }

        if (strcmp(event_type, "audio-chunk") == 0 && payload_length > 0) {
            if (writer != NULL) {
                uint8_t stream_buffer[LOCAL_TTS_STREAM_CHUNK_BYTES];
                int remaining = payload_length;
                while (remaining > 0 && err == ESP_OK) {
                    size_t chunk = remaining > (int) sizeof(stream_buffer) ? sizeof(stream_buffer) : (size_t) remaining;
                    err = local_tts_socket_recv_exact(sock, stream_buffer, chunk);
                    if (err == ESP_OK) {
                        err = writer(stream_buffer, chunk, out_audio, writer_ctx);
                    }
                    if (err == ESP_OK) {
                        out_audio->pcm_size += chunk;
                        streamed_bytes += chunk;
                    }
                    remaining -= (int) chunk;
                }
                if (err != ESP_OK) {
                    cJSON_Delete(event);
                    break;
                }
            } else {
                size_t new_size = out_audio->pcm_size + (size_t) payload_length;
                if (!local_tts_pcm_reserve(out_audio, &pcm_capacity, new_size)) {
                    ESP_LOGW(TAG,
                             "Local TTS PCM buffer allocation failed: needed=%u capacity=%u limit=%u",
                             (unsigned) new_size,
                             (unsigned) pcm_capacity,
                             (unsigned) LOCAL_TTS_MAX_RESPONSE_BYTES);
                    cJSON_Delete(event);
                    err = ESP_ERR_NO_MEM;
                    break;
                }
                err = local_tts_socket_recv_exact(
                    sock, out_audio->wav_data + out_audio->pcm_size, (size_t) payload_length);
                if (err != ESP_OK) {
                    cJSON_Delete(event);
                    break;
                }
                out_audio->pcm_size = new_size;
                out_audio->pcm_data = out_audio->wav_data;
            }
        } else if (payload_length > 0) {
            uint8_t discard[128];
            int remaining = payload_length;
            while (remaining > 0 && err == ESP_OK) {
                size_t chunk = remaining > (int) sizeof(discard) ? sizeof(discard) : (size_t) remaining;
                err = local_tts_socket_recv_exact(sock, discard, chunk);
                remaining -= (int) chunk;
            }
            if (err != ESP_OK) {
                cJSON_Delete(event);
                break;
            }
        }

        bool done = strcmp(event_type, "audio-stop") == 0;
        cJSON_Delete(event);
        if (done) {
            err = out_audio->pcm_size > 0 || streamed_bytes > 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
            break;
        }
    }

    close(sock);
    s_active_socket = -1;

    if (err == ESP_OK) {
        ESP_LOGI(TAG,
                 "Local TTS socket audio received: bytes=%u rate=%lu channels=%u bits=%u elapsed_ms=%lld",
                 (unsigned) out_audio->pcm_size,
                 (unsigned long) out_audio->sample_rate,
                 (unsigned) out_audio->channels,
                 (unsigned) out_audio->bits_per_sample,
                 (long long) ((esp_timer_get_time() - start_us) / 1000));
    } else {
        local_tts_client_free_audio(out_audio);
    }
    return err;
}

/**
 * @brief Collect response bytes for a local TTS request.
 * @param evt The ESP HTTP client event being handled.
 * @return ESP_OK after processing the event.
 */
static esp_err_t local_tts_http_event_handler(esp_http_client_event_t *evt) {
    local_tts_response_t *response = (local_tts_response_t *) evt->user_data;
    if (response == NULL) {
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ERROR) {
        response->transport_error = true;
    } else if (evt->event_id == HTTP_EVENT_ON_CONNECTED) {
        response->connected = true;
    } else if (evt->event_id == HTTP_EVENT_HEADERS_SENT) {
        response->headers_sent = true;
    } else if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->header_key != NULL && evt->header_value != NULL) {
        if (strcasecmp(evt->header_key, "Content-Type") == 0) {
            snprintf(response->content_type, sizeof(response->content_type), "%s", evt->header_value);
        }
    } else if (evt->event_id == HTTP_EVENT_ON_FINISH) {
        response->finished = true;
    } else if (evt->event_id == HTTP_EVENT_DISCONNECTED) {
        response->disconnected = true;
    }

    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != NULL && evt->data_len > 0) {
        response->data_events++;
        size_t new_len = response->len + (size_t) evt->data_len;
        if (new_len > LOCAL_TTS_MAX_RESPONSE_BYTES || !local_tts_response_reserve(response, new_len)) {
            response->overflowed = true;
            return ESP_OK;
        }

        memcpy(response->body + response->len, evt->data, (size_t) evt->data_len);
        response->len = new_len;
    }

    return ESP_OK;
}

/**
 * @brief Validate a WAV response and locate its PCM data payload.
 * @param audio The synthesized audio buffer to validate and annotate.
 * @return ESP_OK on success, or an ESP error code if the WAV is unsupported or malformed.
 */
static esp_err_t local_tts_audio_parse_wav(local_tts_audio_t *audio) {
    if (audio == NULL || audio->wav_data == NULL || audio->wav_size < 44) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t *wav = audio->wav_data;
    if (memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    size_t offset = 12;
    bool found_fmt = false;
    bool found_data = false;

    while (offset + 8 <= audio->wav_size) {
        const uint8_t *chunk = wav + offset;
        uint32_t chunk_size = local_tts_read_le32(chunk + 4);
        size_t chunk_data_offset = offset + 8;
        size_t chunk_end = chunk_data_offset + (size_t) chunk_size;
        if (chunk_end > audio->wav_size) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (memcmp(chunk, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                return ESP_ERR_INVALID_RESPONSE;
            }

            uint16_t audio_format = local_tts_read_le16(wav + chunk_data_offset);
            audio->channels = local_tts_read_le16(wav + chunk_data_offset + 2);
            audio->sample_rate = local_tts_read_le32(wav + chunk_data_offset + 4);
            audio->bits_per_sample = local_tts_read_le16(wav + chunk_data_offset + 14);
            if (audio_format != 1 || audio->channels == 0 || audio->sample_rate == 0 || audio->bits_per_sample != 16) {
                return ESP_ERR_NOT_SUPPORTED;
            }
            found_fmt = true;
        } else if (memcmp(chunk, "data", 4) == 0) {
            audio->pcm_data = wav + chunk_data_offset;
            audio->pcm_size = (size_t) chunk_size;
            found_data = true;
        }

        offset = chunk_end + (chunk_size & 1U);
    }

    return (found_fmt && found_data) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

/**
 * @brief Check whether local TTS is enabled and has a configured endpoint.
 * @return True when the local TTS client can attempt synthesis, false otherwise.
 */
bool local_tts_client_is_configured(void) {
    return CONFIG_TTS_PIPER_ENABLED && CONFIG_TTS_PIPER_BASE_URL[0] != '\0';
}

/**
 * @brief Build the legacy HTTP synthesis URL from configured base URL and path.
 * @param url Destination URL buffer.
 * @param url_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
static void local_tts_format_url(char *url, size_t url_size) {
    if (url == NULL || url_size == 0) {
        return;
    }

    const char *path =
        CONFIG_TTS_PIPER_SYNTH_PATH[0] == '/' ? CONFIG_TTS_PIPER_SYNTH_PATH : "/" CONFIG_TTS_PIPER_SYNTH_PATH;
    snprintf(url, url_size, "%s%s", CONFIG_TTS_PIPER_BASE_URL, path);
}

/**
 * @brief Synthesize text into a buffered audio response.
 * @param text Text to synthesize.
 * @param out_audio Output audio object. Free with local_tts_client_free_audio().
 * @return ESP_OK on success, or an ESP error code on configuration, network, or audio parsing failure.
 */
esp_err_t local_tts_client_synthesize(const char *text, local_tts_audio_t *out_audio) {
    if (text == NULL || out_audio == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!local_tts_client_is_configured()) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!wifi_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }

    *out_audio = (local_tts_audio_t) {0};

#ifdef CONFIG_TTS_PIPER_EVENT_SOCKET
    return local_tts_client_synthesize_socket(text, out_audio, NULL, NULL);
#endif

    char *request_body = NULL;
    const char *content_type = "text/plain";
#ifdef CONFIG_TTS_PIPER_SEND_JSON
    {
        cJSON *request = cJSON_CreateObject();
        if (request == NULL) {
            return ESP_ERR_NO_MEM;
        }

        cJSON_AddStringToObject(request, "text", text);
        if (CONFIG_TTS_PIPER_VOICE[0] != '\0') {
            cJSON_AddStringToObject(request, "voice", CONFIG_TTS_PIPER_VOICE);
        }

        request_body = cJSON_PrintUnformatted(request);
        cJSON_Delete(request);
        content_type = "application/json";
    }
#else
    {
        size_t text_len = strlen(text);
        request_body = malloc(text_len + 1);
        if (request_body != NULL) {
            memcpy(request_body, text, text_len + 1);
        }
    }
#endif
    if (request_body == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = ESP_OK;
    char url[208];
    local_tts_format_url(url, sizeof(url));
    if (!local_tts_url_has_scheme(url)) {
        ESP_LOGW(TAG,
                 "Invalid local TTS URL: url=%s base_url=%s path=%s. Include http:// or https:// in "
                 "CONFIG_TTS_PIPER_BASE_URL.",
                 url,
                 CONFIG_TTS_PIPER_BASE_URL,
                 CONFIG_TTS_PIPER_SYNTH_PATH);
        free(request_body);
        return ESP_ERR_INVALID_ARG;
    }

    local_tts_response_t response = {0};
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = local_tts_http_event_handler,
        .user_data = &response,
        .timeout_ms = CONFIG_TTS_PIPER_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        free(request_body);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "Starting local TTS request: url=%s chars=%u timeout_ms=%d",
             url,
             (unsigned) strlen(text),
             CONFIG_TTS_PIPER_TIMEOUT_MS);
    s_active_client = client;
    esp_http_client_set_header(client, "Content-Type", content_type);
    esp_http_client_set_post_field(client, request_body, (int) strlen(request_body));

    int64_t request_start_us = esp_timer_get_time();
    err = esp_http_client_perform(client);
    int64_t request_elapsed_ms = (esp_timer_get_time() - request_start_us) / 1000;
    int status = esp_http_client_get_status_code(client);
    int transport_errno = esp_http_client_get_errno(client);
    if (err == ESP_OK) {
        if (status < 200 || status >= 300) {
            local_tts_log_request_failure(url, ESP_FAIL, status, transport_errno, request_elapsed_ms, &response);
            err = ESP_FAIL;
        } else if (response.overflowed) {
            ESP_LOGW(TAG,
                     "Local TTS response exceeded buffer: url=%s bytes=%u limit=%u elapsed_ms=%lld",
                     url,
                     (unsigned) response.len,
                     (unsigned) LOCAL_TTS_MAX_RESPONSE_BYTES,
                     (long long) request_elapsed_ms);
            err = ESP_ERR_NO_MEM;
        } else {
            out_audio->wav_data = response.body;
            out_audio->wav_size = response.len;
            response.body = NULL;
            response.len = 0;
            response.capacity = 0;
            err = local_tts_audio_parse_wav(out_audio);
            if (err != ESP_OK) {
                ESP_LOGW(TAG,
                         "Local TTS returned unsupported audio: err=%s status=%d elapsed_ms=%lld bytes=%u "
                         "content_type=%s url=%s",
                         esp_err_to_name(err),
                         status,
                         (long long) request_elapsed_ms,
                         (unsigned) out_audio->wav_size,
                         response.content_type[0] != '\0' ? response.content_type : "<none>",
                         url);
                response.body = out_audio->wav_data;
                response.len = out_audio->wav_size;
                local_tts_log_response_preview(&response);
                response.body = NULL;
                response.len = 0;
            }
        }
    } else {
        local_tts_log_request_failure(url, err, status, transport_errno, request_elapsed_ms, &response);
    }

    esp_http_client_cleanup(client);
    s_active_client = NULL;
    free(request_body);
    free(response.body);

    if (err != ESP_OK) {
        local_tts_client_free_audio(out_audio);
    }
    return err;
}

/**
 * @brief Synthesize text and stream PCM chunks to a caller-provided writer.
 * @param text Text to synthesize.
 * @param writer Callback invoked for each PCM chunk.
 * @param ctx User context pointer passed to the writer callback.
 * @return ESP_OK on success, or an ESP error code if synthesis or writer processing fails.
 */
esp_err_t local_tts_client_synthesize_stream(const char *text, local_tts_pcm_writer_t writer, void *ctx) {
    if (writer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    local_tts_audio_t audio = {0};
#ifdef CONFIG_TTS_PIPER_EVENT_SOCKET
    esp_err_t err = local_tts_client_synthesize_socket(text, &audio, writer, ctx);
    local_tts_client_free_audio(&audio);
    return err;
#else
    esp_err_t err = local_tts_client_synthesize(text, &audio);
    if (err == ESP_OK) {
        err = writer(audio.pcm_data, audio.pcm_size, &audio, ctx);
    }
    local_tts_client_free_audio(&audio);
    return err;
#endif
}

/**
 * @brief Free memory associated with a buffered local TTS audio object.
 * @param audio Audio object to free and reset.
 * @return This function does not return a value.
 */
void local_tts_client_free_audio(local_tts_audio_t *audio) {
    if (audio == NULL) {
        return;
    }

    free(audio->wav_data);
    *audio = (local_tts_audio_t) {0};
}

/**
 * @brief Cancel the currently active local TTS request.
 * @return ESP_OK when a socket or HTTP request was cancelled, ESP_ERR_INVALID_STATE when no request is active, or an
 * ESP error code.
 */
esp_err_t local_tts_client_cancel_active_request(void) {
    if (s_active_socket >= 0) {
        shutdown(s_active_socket, SHUT_RDWR);
        close(s_active_socket);
        s_active_socket = -1;
        return ESP_OK;
    }

    if (s_active_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return esp_http_client_cancel_request(s_active_client);
}
