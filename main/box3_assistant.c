#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "esp_process_sdkconfig.h"
#include "esp_wn_models.h"
#include "esp_codec_dev.h"
#include "model_path.h"

#include "board_audio.h"
#include "hue_client.h"
#include "wifi_support.h"
#include "ui_status.h"


#define HUE_CMD_ON  1
#define HUE_CMD_OFF 2

#define COMMAND_WINDOW_MS 10000
#define COMMAND_MIN_LISTEN_MS 3000

static const char *TAG = "hue-voice";
static const char *WAKE_WORD = "Hi ESP";
static const TickType_t STATUS_HOLD_TIME = pdMS_TO_TICKS(1200);

static bool s_assistant_awake;

static esp_mn_iface_t *s_multinet = NULL;
static model_iface_data_t *s_model_data = NULL;
static const esp_afe_sr_iface_t *s_afe_handle = NULL;
static esp_afe_sr_data_t *s_afe_data = NULL;
static esp_codec_dev_handle_t s_mic_codec = NULL;

// Reset the post-wake command state and re-arm wake-word detection.
static const char *friendly_command_text(int command_id)
{
    switch (command_id) {
    case HUE_CMD_ON:
        return "Turn on living room";
    case HUE_CMD_OFF:
        return "Turn off living room";
    default:
        return "Unknown command";
    }
}

static void return_to_standby(void)
{
    s_assistant_awake = false;
    if (s_multinet != NULL && s_model_data != NULL) {
        s_multinet->clean(s_model_data);
    }
    if (s_afe_handle != NULL && s_afe_data != NULL) {
        s_afe_handle->enable_wakenet(s_afe_data);
    }
    ESP_LOGI(TAG, "Assistant returned to standby");
}

static esp_err_t init_speech_commands(void)
{
    esp_mn_error_t *err = esp_mn_commands_update_from_sdkconfig(s_multinet, s_model_data);
    if (err != NULL) {
        ESP_LOGE(TAG, "Failed to load speech commands from sdkconfig phoneme table");
        return ESP_FAIL;
    }

    esp_mn_active_commands_print();
    return ESP_OK;
}

static esp_err_t init_models(void)
{
    // Speech models are loaded from the dedicated "model" flash partition.
    srmodel_list_t *models = esp_srmodel_init("model");
    if (models == NULL || models->num == 0) {
        ESP_LOGE(TAG, "No speech models found in the 'model' partition");
        return ESP_ERR_NOT_FOUND;
    }

    char *mn_name = esp_srmodel_filter(models, ESP_MN_PREFIX, NULL);
    if (mn_name == NULL) {
        ESP_LOGE(TAG, "No MultiNet model enabled in sdkconfig");
        return ESP_ERR_NOT_FOUND;
    }

    char *wn_name = esp_srmodel_filter(models, ESP_WN_PREFIX, "hiesp");
    if (wn_name == NULL) {
        ESP_LOGE(TAG, "No Hi, ESP WakeNet model enabled in sdkconfig");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Using WakeNet model: %s", wn_name);
    ESP_LOGI(TAG, "Using MultiNet model: %s", mn_name);

    s_multinet = esp_mn_handle_from_name(mn_name);
    if (s_multinet == NULL) {
        ESP_LOGE(TAG, "Failed to get MultiNet handle for %s", mn_name);
        return ESP_ERR_NOT_FOUND;
    }

    s_model_data = s_multinet->create(mn_name, COMMAND_WINDOW_MS);
    if (s_model_data == NULL) {
        ESP_LOGE(TAG, "Failed to create MultiNet model instance");
        return ESP_FAIL;
    }

    afe_config_t *afe_cfg = afe_config_init("MM", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (afe_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to allocate AFE config");
        return ESP_ERR_NO_MEM;
    }

    afe_cfg->wakenet_init = true;
    afe_cfg->wakenet_model_name = wn_name;
    afe_cfg->wakenet_mode = DET_MODE_2CH_90;
    afe_cfg->vad_init = true;
    afe_cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe_cfg->afe_ringbuf_size = 50;
    afe_cfg->fixed_first_channel = false;

    s_afe_handle = esp_afe_handle_from_config(afe_cfg);
    if (s_afe_handle == NULL) {
        afe_config_free(afe_cfg);
        ESP_LOGE(TAG, "Failed to get AFE handle");
        return ESP_FAIL;
    }

    s_afe_data = s_afe_handle->create_from_config(afe_cfg);
    afe_config_free(afe_cfg);
    if (s_afe_data == NULL) {
        ESP_LOGE(TAG, "Failed to create AFE instance");
        return ESP_FAIL;
    }

    s_afe_handle->print_pipeline(s_afe_data);
    return init_speech_commands();
}

static void audio_feed_task(void *arg)
{
    // Feed raw PCM from the BOX-3 microphone into the AFE continuously on its own task.
    const int feed_chunks = s_afe_handle->get_feed_chunksize(s_afe_data);
    const int feed_channels = s_afe_handle->get_feed_channel_num(s_afe_data);
    const size_t feed_samples = (size_t)feed_chunks * (size_t)feed_channels;
    const size_t feed_bytes = feed_samples * sizeof(int16_t);

    int16_t *feed_buffer = heap_caps_malloc(feed_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (feed_buffer == NULL) {
        feed_buffer = malloc(feed_bytes);
    }
    assert(feed_buffer != NULL);

    ESP_LOGI(TAG, "Starting audio feed: wake word=%s, feed_chunks=%d, feed_channels=%d",
             WAKE_WORD, feed_chunks, feed_channels);

    while (true) {
        int ret = esp_codec_dev_read(s_mic_codec, feed_buffer, feed_bytes);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "Microphone read failed: %d", ret);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        int fed = s_afe_handle->feed(s_afe_data, feed_buffer);
        if (fed <= 0) {
            ESP_LOGW(TAG, "AFE feed failed: %d", fed);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static void speech_detect_task(void *arg)
{
    TickType_t wake_tick = 0;

    ESP_LOGI(TAG, "Starting speech detect task");

    while (true) {
        afe_fetch_result_t *afe_result = s_afe_handle->fetch(s_afe_data);
        if (afe_result == NULL || afe_result->data == NULL) {
            continue;
        }

        if (!s_assistant_awake) {
            if (afe_result->wakeup_state == WAKENET_DETECTED) {
                // Once awake, disable WakeNet and let MultiNet consume the AFE output.
                s_assistant_awake = true;
                wake_tick = xTaskGetTickCount();
                s_multinet->clean(s_model_data);
                s_afe_handle->disable_wakenet(s_afe_data);
                ESP_LOGI(TAG, "Wake word detected: %s", WAKE_WORD);
                ui_status_set(UI_STATUS_LISTENING, NULL);
            }
            continue;
        }

        TickType_t elapsed_ms = pdTICKS_TO_MS(xTaskGetTickCount() - wake_tick);
        esp_mn_state_t mn_state = s_multinet->detect(s_model_data, afe_result->data);
        if (mn_state == ESP_MN_STATE_DETECTING) {
            continue;
        }

        if (mn_state == ESP_MN_STATE_TIMEOUT) {
            // MultiNet can report timeout early; keep listening briefly so the user can finish speaking.
            if (elapsed_ms < COMMAND_MIN_LISTEN_MS) {
                ESP_LOGI(TAG, "Ignoring early timeout at %lu ms after wake word", (unsigned long)elapsed_ms);
                continue;
            }

            ESP_LOGI(TAG, "Command window timed out after wake word at %lu ms", (unsigned long)elapsed_ms);
            ui_status_set(UI_STATUS_ERROR, "No command heard");
            vTaskDelay(STATUS_HOLD_TIME);
            return_to_standby();
            ui_status_set(UI_STATUS_READY, NULL);
            continue;
        }

        if (mn_state != ESP_MN_STATE_DETECTED) {
            ESP_LOGI(TAG, "Ignoring speech state %d at %lu ms after wake word", mn_state, (unsigned long)elapsed_ms);
            continue;
        }

        esp_mn_results_t *mn_result = s_multinet->get_results(s_model_data);
        if (mn_result == NULL || mn_result->num <= 0) {
            continue;
        }

        const int command_id = mn_result->command_id[0];
        const float command_prob = mn_result->prob[0];
        const char *command_text = friendly_command_text(command_id);
        ESP_LOGI(TAG, "Detected command_id=%d text=\"%s\" prob=%.3f elapsed_ms=%lu",
                 command_id,
                 mn_result->string,
                 command_prob,
                 (unsigned long)elapsed_ms);

        ui_status_set(UI_STATUS_WORKING, command_text);

        esp_err_t action_err = ESP_FAIL;
        if (command_id == HUE_CMD_ON) {
            action_err = hue_client_set_group(true);
        } else if (command_id == HUE_CMD_OFF) {
            action_err = hue_client_set_group(false);
        } else {
            ESP_LOGW(TAG, "Unhandled command id %d", command_id);
        }

        if (action_err == ESP_OK) {
            ui_status_set(UI_STATUS_SUCCESS, command_text);
        } else {
            ui_status_set(UI_STATUS_ERROR, command_text);
        }

        vTaskDelay(STATUS_HOLD_TIME);
        return_to_standby();
        ui_status_set(UI_STATUS_READY, NULL);
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(ui_status_init());
    ui_status_set(UI_STATUS_BOOTING, NULL);

    ui_status_set(UI_STATUS_CONNECTING, NULL);
    ESP_ERROR_CHECK(wifi_init_sta());

    ui_status_set(UI_STATUS_BOOTING, "Loading speech models");
    ESP_ERROR_CHECK(init_models());
    ESP_ERROR_CHECK(board_audio_init_microphone(&s_mic_codec));

    ui_status_set(UI_STATUS_READY, NULL);

    xTaskCreatePinnedToCore(audio_feed_task, "audio_feed", 8192, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(speech_detect_task, "speech_detect", 12288, NULL, 5, NULL, 1);
}
