#include <stdbool.h>
#include <stdint.h>

#include "esp_codec_dev.h"
#include "esp_check.h"
#include "esp_log.h"

#include "bsp/esp-box-3.h"

#include "board/board_audio.h"

static const char *TAG = "hue-voice";
static esp_codec_dev_handle_t s_speaker_codec;
static bool s_speaker_stream_open;
static const int16_t s_sine64[] = {
    0,      3212,   6393,   9512,   12539,  15446,  18204,  20787,  23170,  25329,  27245,  28898,  30273,
    31356,  32137,  32609,  32767,  32609,  32137,  31356,  30273,  28898,  27245,  25329,  23170,  20787,
    18204,  15446,  12539,  9512,   6393,   3212,   0,      -3212,  -6393,  -9512,  -12539, -15446, -18204,
    -20787, -23170, -25329, -27245, -28898, -30273, -31356, -32137, -32609, -32767, -32609, -32137, -31356,
    -30273, -28898, -27245, -25329, -23170, -20787, -18204, -15446, -12539, -9512,  -6393,  -3212,
};

/**
 * @brief Read a signed sine sample from the fixed 64-step lookup table.
 * @param phase 32-bit phase accumulator value.
 * @return Signed PCM sample in the range of the sine table.
 */
static int16_t board_audio_sine_sample(uint32_t phase) {
    return s_sine64[(phase >> 26) & 0x3FU];
}

/**
 * @brief Initialize the BOX-3 microphone codec for assistant speech capture.
 * @param mic_codec Output handle for the initialized microphone codec.
 * @return ESP_OK on success, or an ESP error code if initialization or opening fails.
 * @note The microphone is configured for 16 kHz, 16-bit, 2-channel capture.
 */
esp_err_t board_audio_init_microphone(esp_codec_dev_handle_t *mic_codec) {
    *mic_codec = bsp_audio_codec_microphone_init();
    if (*mic_codec == NULL) {
        ESP_LOGE(TAG, "Failed to initialize BOX-3 microphone codec");
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
    };

    int ret = esp_codec_dev_open(*mic_codec, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open microphone stream: %d", ret);
        return ESP_FAIL;
    }

    ret = esp_codec_dev_set_in_gain(*mic_codec, CONFIG_HUE_MIC_GAIN_DB);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Unable to set microphone gain: %d", ret);
    }

    return ESP_OK;
}

/**
 * @brief Initialize the BOX-3 speaker codec for assistant playback.
 * @return ESP_OK on success, or an ESP error code if the speaker codec cannot be created.
 * @note The playback stream itself is opened lazily for each utterance based on the synthesized WAV format.
 */
esp_err_t board_audio_init_speaker(void) {
    if (s_speaker_codec != NULL) {
        return ESP_OK;
    }

    s_speaker_codec = bsp_audio_codec_speaker_init();
    if (s_speaker_codec == NULL) {
        ESP_LOGE(TAG, "Failed to initialize BOX-3 speaker codec");
        return ESP_FAIL;
    }

    int ret = esp_codec_dev_set_out_vol(s_speaker_codec, CONFIG_TTS_PIPER_VOLUME_PERCENT);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Unable to set speaker volume: %d", ret);
    }

    return ESP_OK;
}

/**
 * @brief Open the BOX-3 speaker stream for raw PCM playback.
 * @param sample_rate Sample rate in Hz for the PCM stream.
 * @param channels Number of PCM channels.
 * @param bits_per_sample Bits per PCM sample.
 * @return ESP_OK on success, or an ESP error code if the speaker stream cannot be opened.
 */
esp_err_t board_audio_begin_pcm(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample) {
    if (sample_rate == 0 || channels == 0 || bits_per_sample == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(board_audio_init_speaker(), TAG, "Speaker init failed");

    if (s_speaker_stream_open) {
        ESP_RETURN_ON_ERROR(board_audio_end_pcm(), TAG, "Failed to close existing speaker stream");
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = (int) sample_rate,
        .channel = channels,
        .bits_per_sample = bits_per_sample,
    };

    int ret = esp_codec_dev_open(s_speaker_codec, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open speaker stream: %d", ret);
        return ESP_FAIL;
    }

    s_speaker_stream_open = true;
    return ESP_OK;
}

/**
 * @brief Write raw PCM bytes to the currently open speaker stream.
 * @param pcm_data Pointer to PCM audio bytes.
 * @param pcm_size Size of the PCM buffer in bytes.
 * @return ESP_OK on success, or an ESP error code if playback fails.
 */
esp_err_t board_audio_write_pcm(const void *pcm_data, size_t pcm_size) {
    if (pcm_data == NULL || pcm_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_speaker_stream_open || s_speaker_codec == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int ret = esp_codec_dev_write(s_speaker_codec, (void *) pcm_data, (int) pcm_size);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Speaker playback failed: %d", ret);
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief Close the currently open speaker stream.
 * @return ESP_OK on success, or an ESP error code if the stream cannot be closed cleanly.
 */
esp_err_t board_audio_end_pcm(void) {
    if (!s_speaker_stream_open || s_speaker_codec == NULL) {
        return ESP_OK;
    }

    int close_ret = esp_codec_dev_close(s_speaker_codec);
    s_speaker_stream_open = false;
    if (close_ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to close speaker stream cleanly: %d", close_ret);
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief Play a block of raw PCM audio through the BOX-3 speaker.
 * @param pcm_data Pointer to the PCM audio bytes to play.
 * @param pcm_size Size of the PCM buffer in bytes.
 * @param sample_rate Sample rate in Hz for the PCM stream.
 * @param channels Number of PCM channels.
 * @param bits_per_sample Bits per PCM sample.
 * @return ESP_OK on success, or an ESP error code if playback fails.
 */
esp_err_t board_audio_play_pcm(
    const void *pcm_data, size_t pcm_size, uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample) {
    if (pcm_data == NULL || pcm_size == 0 || sample_rate == 0 || channels == 0 || bits_per_sample == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(
        board_audio_begin_pcm(sample_rate, channels, bits_per_sample), TAG, "Speaker stream open failed");
    esp_err_t err = board_audio_write_pcm(pcm_data, pcm_size);
    esp_err_t close_err = board_audio_end_pcm();
    return err != ESP_OK ? err : close_err;
}

/**
 * @brief Play a short two-tone timer chime through the BOX-3 speaker.
 * @return ESP_OK on success, or an ESP error code if playback fails.
 */
esp_err_t board_audio_play_timer_chime(void) {
    static const uint32_t sample_rate = 16000;
    static const uint8_t channels = 1;
    static const uint8_t bits_per_sample = 16;
    static const size_t chunk_samples = 160;
    static const uint32_t first_note_samples = 4000;
    static const uint32_t second_note_samples = 4400;
    static const uint32_t total_samples = first_note_samples + second_note_samples;
    static const uint32_t attack_samples = 320;
    static const uint32_t release_samples = 960;
    static const int32_t peak_gain = 9000;
    int16_t chunk[chunk_samples];
    uint32_t phase_a = 0;
    uint32_t phase_b = 0;
    const uint32_t step_a = (uint32_t) (((uint64_t) 523 * 4294967296ULL) / sample_rate);
    const uint32_t step_b = (uint32_t) (((uint64_t) 784 * 4294967296ULL) / sample_rate);

    ESP_RETURN_ON_ERROR(board_audio_begin_pcm(sample_rate, channels, bits_per_sample), TAG, "Timer chime open failed");

    for (uint32_t sample_index = 0; sample_index < total_samples; sample_index += chunk_samples) {
        size_t samples_this_chunk = total_samples - sample_index;
        if (samples_this_chunk > chunk_samples) {
            samples_this_chunk = chunk_samples;
        }

        for (size_t i = 0; i < samples_this_chunk; ++i) {
            uint32_t absolute_index = sample_index + (uint32_t) i;
            bool first_note = absolute_index < first_note_samples;
            uint32_t note_index = first_note ? absolute_index : (absolute_index - first_note_samples);
            uint32_t note_samples = first_note ? first_note_samples : second_note_samples;
            uint32_t phase = first_note ? phase_a : phase_b;
            int32_t gain = peak_gain;

            if (note_index < attack_samples) {
                gain = (peak_gain * (int32_t) note_index) / (int32_t) attack_samples;
            } else if (note_index + release_samples > note_samples) {
                uint32_t remaining = note_samples - note_index;
                gain = (peak_gain * (int32_t) remaining) / (int32_t) release_samples;
            }

            chunk[i] = (int16_t) ((board_audio_sine_sample(phase) * gain) / 32767);

            if (first_note) {
                phase_a += step_a;
            } else {
                phase_b += step_b;
            }
        }

        esp_err_t err = board_audio_write_pcm(chunk, samples_this_chunk * sizeof(chunk[0]));
        if (err != ESP_OK) {
            board_audio_end_pcm();
            return err;
        }
    }

    return board_audio_end_pcm();
}
