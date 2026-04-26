#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    uint8_t *wav_data;
    size_t wav_size;
    const uint8_t *pcm_data;
    size_t pcm_size;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
} local_tts_audio_t;

/**
 * @brief Callback invoked for streamed PCM audio from the local TTS client.
 * @param pcm_data Pointer to PCM bytes for this chunk.
 * @param pcm_size Size of the PCM chunk in bytes.
 * @param format Current audio format metadata for the PCM stream.
 * @param ctx User context pointer supplied to local_tts_client_synthesize_stream().
 * @return ESP_OK to continue streaming, or an ESP error code to abort synthesis.
 */
typedef esp_err_t (*local_tts_pcm_writer_t)(const uint8_t *pcm_data,
                                            size_t pcm_size,
                                            const local_tts_audio_t *format,
                                            void *ctx);

/**
 * @brief Check whether local TTS is enabled and has an endpoint configured.
 * @return True when local TTS can be attempted, false otherwise.
 */
bool local_tts_client_is_configured(void);

/**
 * @brief Synthesize text into a buffered audio object.
 * @param text Text to synthesize.
 * @param out_audio Output audio buffer and format metadata. Free with local_tts_client_free_audio().
 * @return ESP_OK on success, or an ESP error code on configuration, network, or audio parsing failure.
 */
esp_err_t local_tts_client_synthesize(const char *text, local_tts_audio_t *out_audio);

/**
 * @brief Synthesize text and stream PCM chunks to a caller-provided writer.
 * @param text Text to synthesize.
 * @param writer Callback that receives each PCM chunk.
 * @param ctx User context pointer passed to the writer callback.
 * @return ESP_OK on success, or an ESP error code if synthesis, streaming, or writer handling fails.
 */
esp_err_t local_tts_client_synthesize_stream(const char *text, local_tts_pcm_writer_t writer, void *ctx);

/**
 * @brief Release audio storage returned by local_tts_client_synthesize().
 * @param audio Audio object to free and reset.
 * @return This function does not return a value.
 */
void local_tts_client_free_audio(local_tts_audio_t *audio);

/**
 * @brief Cancel the currently active local TTS request, if any.
 * @return ESP_OK when a request was cancelled, ESP_ERR_INVALID_STATE when no request is active, or an ESP error code.
 */
esp_err_t local_tts_client_cancel_active_request(void);
