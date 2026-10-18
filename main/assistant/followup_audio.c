#include "assistant/followup_audio.h"

#include <stdlib.h>

#include "freertos/task.h"

#include "esp_codec_dev.h"
#include "esp_heap_caps.h"

#define FOLLOWUP_AUDIO_CHUNK_FRAMES 512

/**
 * @brief Read mono PCM16 audio into a heap buffer until a stop condition is reached.
 * @param rt Runtime that owns the active microphone codec and progress tick.
 * @param max_capture_ms Upper bound on capture duration in milliseconds.
 * @param sample_rate_hz Mono sample rate used to size the output buffer.
 * @param endpoint Optional trailing-silence detector, or NULL for a fixed-length capture.
 * @param on_chunk Optional per-chunk callback, or NULL to ignore chunks.
 * @param context Context pointer forwarded to on_chunk.
 * @param out_pcm Output pointer receiving heap-allocated PCM16 audio bytes.
 * @param out_size Output pointer receiving the captured PCM byte length.
 * @return ESP_OK on success, or an ESP error code when capture or allocation fails.
 */
static esp_err_t capture_mono_loop(assistant_runtime_t *rt,
                                   uint32_t max_capture_ms,
                                   uint32_t sample_rate_hz,
                                   capture_endpoint_t *endpoint,
                                   assistant_followup_audio_chunk_cb_t on_chunk,
                                   void *context,
                                   uint8_t **out_pcm,
                                   size_t *out_size) {
    if (rt == NULL || out_pcm == NULL || out_size == NULL || max_capture_ms == 0 || sample_rate_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t mono_samples = (size_t) ((max_capture_ms * (uint64_t) sample_rate_hz) / 1000ULL);
    const size_t mono_bytes = mono_samples * sizeof(int16_t);
    int16_t *mono_buffer = heap_caps_malloc(mono_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mono_buffer == NULL) {
        mono_buffer = malloc(mono_bytes);
    }
    if (mono_buffer == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const size_t stereo_samples = (size_t) FOLLOWUP_AUDIO_CHUNK_FRAMES * 2U;
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

        size_t frames = FOLLOWUP_AUDIO_CHUNK_FRAMES;
        if (frames > (mono_samples - captured)) {
            frames = mono_samples - captured;
        }

        for (size_t i = 0; i < frames; ++i) {
            mono_buffer[captured + i] = stereo_buffer[i * 2];
        }
        size_t chunk_start = captured;
        captured += frames;
        rt->speech_progress_tick = xTaskGetTickCount();

        if (on_chunk != NULL &&
            on_chunk(context, mono_buffer + chunk_start, frames, (uint32_t) ((captured * 1000ULL) / sample_rate_hz))) {
            break;
        }
        if (endpoint != NULL && capture_endpoint_should_stop(endpoint, mono_buffer + chunk_start, frames)) {
            break;
        }
    }

    free(stereo_buffer);
    *out_pcm = (uint8_t *) mono_buffer;
    *out_size = captured * sizeof(int16_t);
    return ESP_OK;
}

/**
 * @brief Capture a fixed-length mono PCM16 clip from the BOX-3 microphone.
 * @param rt Runtime that owns the active microphone codec and progress tick.
 * @param capture_ms Capture duration in milliseconds.
 * @param sample_rate_hz Mono sample rate used to size the output buffer.
 * @param out_pcm Output pointer receiving heap-allocated PCM16 audio bytes.
 * @param out_size Output pointer receiving the PCM byte length.
 * @return ESP_OK on success, or an ESP error code when capture or allocation fails.
 */
esp_err_t assistant_followup_audio_capture_mono(
    assistant_runtime_t *rt, uint32_t capture_ms, uint32_t sample_rate_hz, uint8_t **out_pcm, size_t *out_size) {
    return capture_mono_loop(rt, capture_ms, sample_rate_hz, NULL, NULL, NULL, out_pcm, out_size);
}

/**
 * @brief Capture mono PCM16 audio until a silence endpoint or a chunk callback stops it.
 * @param rt Runtime that owns the active microphone codec and progress tick.
 * @param max_capture_ms Hard upper bound on capture duration in milliseconds.
 * @param endpoint Optional trailing-silence detector, or NULL to capture the full window.
 * @param sample_rate_hz Mono sample rate used to size the output buffer.
 * @param on_chunk Optional callback invoked for each chunk, or NULL to ignore chunks.
 * @param context Context pointer forwarded to on_chunk.
 * @param out_pcm Output pointer receiving heap-allocated PCM16 audio bytes.
 * @param out_size Output pointer receiving the captured PCM byte length, which may be shorter than the max window.
 * @return ESP_OK on success, or an ESP error code when capture or allocation fails.
 * @note The capture always stops at max_capture_ms even when silence is never detected.
 */
esp_err_t assistant_followup_audio_capture_mono_until(assistant_runtime_t *rt,
                                                      uint32_t max_capture_ms,
                                                      capture_endpoint_t *endpoint,
                                                      uint32_t sample_rate_hz,
                                                      assistant_followup_audio_chunk_cb_t on_chunk,
                                                      void *context,
                                                      uint8_t **out_pcm,
                                                      size_t *out_size) {
    return capture_mono_loop(rt, max_capture_ms, sample_rate_hz, endpoint, on_chunk, context, out_pcm, out_size);
}
