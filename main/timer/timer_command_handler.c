#include "timer/timer_command_handler.h"

#include <stdio.h>
#include <stdlib.h>

#include "freertos/task.h"

#include "esp_codec_dev.h"
#include "esp_heap_caps.h"

#include "assistant/watchdog.h"
#include "board/ui_status.h"
#include "stt/local_stt_client.h"
#include "timer/timer_parse.h"
#include "timer/timer_runtime.h"

#define TIMER_AUDIO_CHUNK_FRAMES          512
#define TIMER_HEARTBEAT_SLEEP_SLICE_MS    250
#define TIMER_FOLLOWUP_SETTLE_DELAY_MS    250
#define TIMER_AUDIO_PAUSE_WAIT_TIMEOUT_MS 500

static esp_err_t timer_command_execute(const assistant_command_context_t *context,
                                       const assistant_command_dispatch_t *dispatch,
                                       assistant_command_result_t *out_result);
static esp_err_t capture_timer_followup_audio(assistant_runtime_t *rt, uint8_t **out_pcm, size_t *out_size);
static uint32_t monotonic_ms_now(void);

/**
 * @brief Get the timer feature command handler registration.
 * @return Pointer to the static timer command handler descriptor.
 */
const assistant_command_handler_t *timer_command_handler_get(void) {
    static const assistant_command_handler_t handler = {
        .action = ASSISTANT_COMMAND_ACTION_SET_TIMER,
        .execute = timer_command_execute,
    };

    return &handler;
}

/**
 * @brief Read monotonic uptime in milliseconds.
 * @return Milliseconds since boot truncated to 32 bits.
 */
static uint32_t monotonic_ms_now(void) {
    return (uint32_t) pdTICKS_TO_MS(xTaskGetTickCount());
}

/**
 * @brief Capture a short mono PCM clip for timer duration transcription.
 * @param rt Runtime that owns the active microphone codec.
 * @param out_pcm Output pointer receiving heap-allocated PCM16 audio bytes.
 * @param out_size Output pointer receiving the PCM byte length.
 * @return ESP_OK on success, or an ESP error code when capture or allocation fails.
 */
static esp_err_t capture_timer_followup_audio(assistant_runtime_t *rt, uint8_t **out_pcm, size_t *out_size) {
    if (rt == NULL || out_pcm == NULL || out_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t mono_samples = (size_t) ((CONFIG_LOCAL_STT_CAPTURE_MS * 16000ULL) / 1000ULL);
    const size_t mono_bytes = mono_samples * sizeof(int16_t);
    int16_t *mono_buffer = heap_caps_malloc(mono_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mono_buffer == NULL) {
        mono_buffer = malloc(mono_bytes);
    }
    if (mono_buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const size_t stereo_samples = (size_t) TIMER_AUDIO_CHUNK_FRAMES * 2U;
    const size_t stereo_bytes = stereo_samples * sizeof(int16_t);
    int16_t *stereo_buffer = heap_caps_malloc(stereo_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (stereo_buffer == NULL) {
        stereo_buffer = malloc(stereo_bytes);
    }
    if (stereo_buffer == NULL) {
        free(mono_buffer);
        return ESP_ERR_NO_MEM;
    }

    size_t captured = 0;
    while (captured < mono_samples) {
        int ret = esp_codec_dev_read(rt->mic_codec, stereo_buffer, stereo_bytes);
        if (ret != ESP_CODEC_DEV_OK) {
            free(stereo_buffer);
            free(mono_buffer);
            return ESP_FAIL;
        }

        size_t frames = TIMER_AUDIO_CHUNK_FRAMES;
        if (frames > (mono_samples - captured)) {
            frames = mono_samples - captured;
        }

        for (size_t i = 0; i < frames; ++i) {
            mono_buffer[captured + i] = stereo_buffer[i * 2];
        }
        captured += frames;
        rt->speech_progress_tick = xTaskGetTickCount();
    }

    free(stereo_buffer);
    *out_pcm = (uint8_t *) mono_buffer;
    *out_size = mono_bytes;
    return ESP_OK;
}

/**
 * @brief Execute timer-specific command actions.
 * @param context Assistant command context for the active command.
 * @param dispatch Resolved dispatch metadata describing the timer action.
 * @param out_result Result structure to populate for assistant core.
 * @return ESP_OK after handling the command result structure, or an ESP error code on invalid input.
 */
static esp_err_t timer_command_execute(const assistant_command_context_t *context,
                                       const assistant_command_dispatch_t *dispatch,
                                       assistant_command_result_t *out_result) {
    if (context == NULL || context->runtime == NULL || dispatch == NULL || out_result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    assistant_runtime_t *rt = context->runtime;
    out_result->timeout_label = "Command";

    if (dispatch->type == ASSISTANT_COMMAND_ACTION_STOP) {
        if (!timer_runtime_stop(&rt->timer)) {
            snprintf(out_result->detail, sizeof(out_result->detail), "No timer is active");
            out_result->err = ESP_ERR_INVALID_STATE;
            return ESP_OK;
        }

        rt->direct_command_mode = false;
        rt->direct_command_prepared = false;
        snprintf(out_result->detail, sizeof(out_result->detail), "Timer stopped");
        out_result->err = ESP_OK;
        return ESP_OK;
    }

    if (dispatch->type != ASSISTANT_COMMAND_ACTION_SET_TIMER) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Timer command unavailable");
        out_result->err = ESP_ERR_NOT_SUPPORTED;
        return ESP_OK;
    }

    if (!local_stt_client_is_configured()) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Timer voice service unavailable");
        out_result->err = ESP_ERR_NOT_SUPPORTED;
        return ESP_OK;
    }

    ui_status_set(UI_STATUS_LISTENING, "Say duration now");
    assistant_watchdog_sleep_with_heartbeat(
        rt, pdMS_TO_TICKS(TIMER_FOLLOWUP_SETTLE_DELAY_MS), pdMS_TO_TICKS(TIMER_HEARTBEAT_SLEEP_SLICE_MS));

    TickType_t pause_wait_start = xTaskGetTickCount();
    while (!rt->audio_feed_paused) {
        if ((xTaskGetTickCount() - pause_wait_start) >= pdMS_TO_TICKS(TIMER_AUDIO_PAUSE_WAIT_TIMEOUT_MS)) {
            snprintf(out_result->detail, sizeof(out_result->detail), "Timer audio capture failed");
            out_result->err = ESP_ERR_TIMEOUT;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    uint8_t *pcm = NULL;
    size_t pcm_size = 0;
    esp_err_t err = capture_timer_followup_audio(rt, &pcm, &pcm_size);
    if (err != ESP_OK) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Timer audio capture failed");
        out_result->err = err;
        return ESP_OK;
    }

    ui_status_set(UI_STATUS_WORKING, "Understanding timer");
    char transcript[96];
    err = local_stt_client_transcribe(pcm, pcm_size, 16000, 2, 1, transcript, sizeof(transcript));
    free(pcm);
    if (err != ESP_OK) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Timer voice service unavailable");
        out_result->err = err;
        return ESP_OK;
    }

    uint32_t duration_seconds = 0;
    if (!timer_parse_duration_text(transcript, CONFIG_TIMER_MAX_DURATION_SECONDS, &duration_seconds)) {
        snprintf(out_result->detail, sizeof(out_result->detail), "Could not understand timer");
        out_result->err = ESP_ERR_INVALID_ARG;
        return ESP_OK;
    }

    timer_runtime_start(&rt->timer, duration_seconds, monotonic_ms_now());
    rt->direct_command_mode = false;
    rt->direct_command_prepared = false;
    timer_runtime_format_remaining(&rt->timer, monotonic_ms_now(), out_result->detail, sizeof(out_result->detail));
    out_result->err = ESP_OK;
    return ESP_OK;
}
