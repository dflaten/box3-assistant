#include "assistant/followup_audio.h"

#include <stdlib.h>

#include "freertos/task.h"

#include "esp_codec_dev.h"
#include "esp_heap_caps.h"

#define FOLLOWUP_AUDIO_CHUNK_FRAMES 512

/**
 * @brief Capture a mono PCM16 clip from the BOX-3 microphone.
 * @param rt Runtime that owns the active microphone codec and progress tick.
 * @param capture_ms Capture duration in milliseconds.
 * @param sample_rate_hz Mono sample rate used to size the output buffer.
 * @param out_pcm Output pointer receiving heap-allocated PCM16 audio bytes.
 * @param out_size Output pointer receiving the PCM byte length.
 * @return ESP_OK on success, or an ESP error code when capture or allocation fails.
 */
esp_err_t assistant_followup_audio_capture_mono(
    assistant_runtime_t *rt, uint32_t capture_ms, uint32_t sample_rate_hz, uint8_t **out_pcm, size_t *out_size) {
    if (rt == NULL || out_pcm == NULL || out_size == NULL || capture_ms == 0 || sample_rate_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t mono_samples = (size_t) ((capture_ms * (uint64_t) sample_rate_hz) / 1000ULL);
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
        captured += frames;
        rt->speech_progress_tick = xTaskGetTickCount();
    }

    free(stereo_buffer);
    *out_pcm = (uint8_t *) mono_buffer;
    *out_size = mono_bytes;
    return ESP_OK;
}
