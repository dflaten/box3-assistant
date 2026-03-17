#pragma once

#include "esp_codec_dev.h"
#include "esp_err.h"

esp_err_t board_audio_init_microphone(esp_codec_dev_handle_t *mic_codec);
