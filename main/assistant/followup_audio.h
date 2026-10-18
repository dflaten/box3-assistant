#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "assistant/capture_endpoint.h"
#include "assistant_runtime.h"

/**
 * @brief Callback invoked for each captured mono chunk while a streamed capture is running.
 * @param context Caller-provided context pointer.
 * @param samples Mono PCM16 samples for the newest chunk.
 * @param sample_count Number of valid samples in the chunk.
 * @param elapsed_ms Milliseconds of audio captured so far, including this chunk.
 * @return True to stop the capture immediately, otherwise false to keep capturing.
 */
typedef bool (*assistant_followup_audio_chunk_cb_t)(void *context,
                                                    const int16_t *samples,
                                                    size_t sample_count,
                                                    uint32_t elapsed_ms);

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
    assistant_runtime_t *rt, uint32_t capture_ms, uint32_t sample_rate_hz, uint8_t **out_pcm, size_t *out_size);

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
                                                      size_t *out_size);
