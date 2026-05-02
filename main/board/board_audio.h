#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_codec_dev.h"
#include "esp_err.h"

esp_err_t board_audio_init_microphone(esp_codec_dev_handle_t *mic_codec);
esp_err_t board_audio_init_speaker(void);
esp_err_t board_audio_begin_pcm(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample);
esp_err_t board_audio_write_pcm(const void *pcm_data, size_t pcm_size);
esp_err_t board_audio_end_pcm(void);
esp_err_t board_audio_play_pcm(
    const void *pcm_data, size_t pcm_size, uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample);
esp_err_t board_audio_play_timer_chime(void);
