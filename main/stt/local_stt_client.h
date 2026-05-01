#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief Check whether local STT is enabled and has an endpoint configured.
 * @return True when local STT can be attempted, false otherwise.
 */
bool local_stt_client_is_configured(void);

/**
 * @brief Send raw PCM audio to the local STT service and receive transcript text.
 * @param audio_bytes Pointer to PCM16 audio bytes.
 * @param audio_size Size of the PCM audio buffer in bytes.
 * @param rate Audio sample rate in Hz.
 * @param width Bytes per sample.
 * @param channels Audio channel count.
 * @param transcript Destination buffer for transcript text.
 * @param transcript_size Size of the destination buffer in bytes.
 * @return ESP_OK on success, or an ESP error code on configuration, network, or transcript failure.
 */
esp_err_t local_stt_client_transcribe(const uint8_t *audio_bytes,
                                      size_t audio_size,
                                      uint32_t rate,
                                      uint8_t width,
                                      uint8_t channels,
                                      char *transcript,
                                      size_t transcript_size);

/**
 * @brief Cancel the currently active local STT request, if any.
 * @return ESP_OK when a request was cancelled, ESP_ERR_INVALID_STATE when no request is active, or an ESP error code.
 */
esp_err_t local_stt_client_cancel_active_request(void);
