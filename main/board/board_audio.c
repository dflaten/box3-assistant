#include "esp_codec_dev.h"
#include "esp_log.h"

#include "bsp/esp-box-3.h"

#include "board/board_audio.h"

static const char *TAG = "hue-voice";

esp_err_t board_audio_init_microphone(esp_codec_dev_handle_t *mic_codec)
{
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
