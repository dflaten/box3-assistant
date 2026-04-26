#pragma once

#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief Check whether the assistant speech player has a configured local TTS backend.
 * @return True when speech playback can be attempted, false otherwise.
 */
bool tts_player_is_configured(void);

/**
 * @brief Speak text through the configured local TTS backend and BOX-3 speaker path.
 * @param text Text to synthesize and play.
 * @return ESP_OK on success, or an ESP error code if synthesis or playback fails.
 */
esp_err_t tts_player_speak(const char *text);

/**
 * @brief Cancel the currently active TTS playback or synthesis request.
 * @return ESP_OK when a request was cancelled, ESP_ERR_INVALID_STATE when no request is active, or an ESP error code.
 */
esp_err_t tts_player_cancel(void);
