#include "tts/tts_player.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

#include "board/board_audio.h"
#include "tts/local_tts_client.h"

#define TTS_PLAYER_OUTPUT_SAMPLE_RATE    16000
#define TTS_PLAYER_RESAMPLE_BUFFER_BYTES 2048

typedef struct {
    bool started;
    uint32_t source_rate;
    uint32_t output_rate;
    uint32_t resample_accumulator;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint8_t pending[8];
    size_t pending_size;
} tts_player_playback_t;

static const char *TAG = "tts-player";

/**
 * @brief Convert streamed TTS PCM into the BOX-3 speaker format and write it to audio output.
 * @param pcm_data Pointer to PCM bytes received from the local TTS backend.
 * @param pcm_size Size of the PCM chunk in bytes.
 * @param format Source audio format metadata for this stream.
 * @param ctx Pointer to the current playback conversion state.
 * @return ESP_OK on success, or an ESP error code if the format is unsupported or playback fails.
 * @note The current BOX-3 microphone path keeps I2S at 16 kHz, so higher-rate TTS audio is downsampled here.
 */
static esp_err_t
tts_player_write_pcm(const uint8_t *pcm_data, size_t pcm_size, const local_tts_audio_t *format, void *ctx) {
    tts_player_playback_t *playback = (tts_player_playback_t *) ctx;
    if (playback == NULL || format == NULL || pcm_data == NULL || pcm_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (format->sample_rate == 0 || format->channels == 0 || format->bits_per_sample != 16) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!playback->started) {
        playback->source_rate = format->sample_rate;
        playback->output_rate = TTS_PLAYER_OUTPUT_SAMPLE_RATE;
        playback->channels = (uint8_t) format->channels;
        playback->bits_per_sample = (uint8_t) format->bits_per_sample;
        ESP_LOGI(TAG,
                 "Starting TTS playback stream: source_rate=%lu output_rate=%lu channels=%u bits=%u",
                 (unsigned long) playback->source_rate,
                 (unsigned long) playback->output_rate,
                 (unsigned) playback->channels,
                 (unsigned) playback->bits_per_sample);
        ESP_RETURN_ON_ERROR(board_audio_begin_pcm(playback->output_rate, playback->channels, playback->bits_per_sample),
                            TAG,
                            "TTS speaker stream open failed");
        playback->started = true;
    } else if (format->sample_rate != playback->source_rate || format->channels != playback->channels ||
               format->bits_per_sample != playback->bits_per_sample) {
        ESP_LOGW(TAG,
                 "TTS audio format changed mid-stream: rate=%lu/%lu channels=%u/%u bits=%u/%u",
                 (unsigned long) playback->source_rate,
                 (unsigned long) format->sample_rate,
                 (unsigned) playback->channels,
                 (unsigned) format->channels,
                 (unsigned) playback->bits_per_sample,
                 (unsigned) format->bits_per_sample);
        return ESP_ERR_NOT_SUPPORTED;
    }

    const size_t frame_size = (size_t) playback->channels * sizeof(int16_t);
    if (frame_size == 0 || frame_size > sizeof(playback->pending) || playback->source_rate < playback->output_rate) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t out[TTS_PLAYER_RESAMPLE_BUFFER_BYTES];
    size_t out_size = 0;
    size_t cursor = 0;

    if (playback->pending_size > 0) {
        size_t needed = frame_size - playback->pending_size;
        if (needed > pcm_size) {
            memcpy(playback->pending + playback->pending_size, pcm_data, pcm_size);
            playback->pending_size += pcm_size;
            return ESP_OK;
        }

        memcpy(playback->pending + playback->pending_size, pcm_data, needed);
        cursor = needed;
        playback->pending_size = 0;
        playback->resample_accumulator += playback->output_rate;
        if (playback->resample_accumulator >= playback->source_rate) {
            memcpy(out, playback->pending, frame_size);
            out_size = frame_size;
            playback->resample_accumulator -= playback->source_rate;
        }
    }

    while (cursor + frame_size <= pcm_size) {
        playback->resample_accumulator += playback->output_rate;
        if (playback->resample_accumulator >= playback->source_rate) {
            if (out_size + frame_size > sizeof(out)) {
                ESP_RETURN_ON_ERROR(board_audio_write_pcm(out, out_size), TAG, "TTS speaker stream write failed");
                out_size = 0;
            }
            memcpy(out + out_size, pcm_data + cursor, frame_size);
            out_size += frame_size;
            playback->resample_accumulator -= playback->source_rate;
        }
        cursor += frame_size;
    }

    playback->pending_size = pcm_size - cursor;
    if (playback->pending_size > 0) {
        memcpy(playback->pending, pcm_data + cursor, playback->pending_size);
    }

    if (out_size > 0) {
        ESP_RETURN_ON_ERROR(board_audio_write_pcm(out, out_size), TAG, "TTS speaker stream write failed");
    }
    return ESP_OK;
}

/**
 * @brief Check whether assistant speech playback has a configured local TTS backend.
 * @return True when speech playback can be attempted, false otherwise.
 */
bool tts_player_is_configured(void) {
    return local_tts_client_is_configured();
}

/**
 * @brief Synthesize and play text through the BOX-3 speaker.
 * @param text Text to synthesize and play.
 * @return ESP_OK on success, or an ESP error code if synthesis or playback fails.
 */
esp_err_t tts_player_speak(const char *text) {
    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    tts_player_playback_t playback = {0};
    esp_err_t err = local_tts_client_synthesize_stream(text, tts_player_write_pcm, &playback);
    esp_err_t close_err = board_audio_end_pcm();
    return err != ESP_OK ? err : close_err;
}

/**
 * @brief Cancel the currently active local TTS synthesis or playback request.
 * @return ESP_OK when a request was cancelled, ESP_ERR_INVALID_STATE when no request is active, or an ESP error code.
 */
esp_err_t tts_player_cancel(void) {
    return local_tts_client_cancel_active_request();
}
