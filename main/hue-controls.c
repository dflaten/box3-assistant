#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_codec_dev.h"
#include "model_path.h"

#include "bsp/esp-box-3.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

#define HUE_CMD_ON  1
#define HUE_CMD_OFF 2

static const char *TAG = "hue-voice";

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num;

static esp_mn_iface_t *s_multinet = NULL;
static model_iface_data_t *s_model_data = NULL;
static const esp_afe_sr_iface_t *s_afe_handle = NULL;
static esp_afe_sr_data_t *s_afe_data = NULL;
static esp_codec_dev_handle_t s_mic_codec = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

static esp_err_t wifi_init_sta(void)
{
    if (strlen(CONFIG_HUE_WIFI_SSID) == 0) {
        ESP_LOGE(TAG, "Wi-Fi SSID is empty. Set CONFIG_HUE_WIFI_SSID in menuconfig or sdkconfig.");
        return ESP_ERR_INVALID_STATE;
    }

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop creation failed");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id),
        TAG,
        "register wifi event handler failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip),
        TAG,
        "register IP event handler failed");

    wifi_config_t wifi_config = { 0 };
    strlcpy((char *)wifi_config.sta.ssid, CONFIG_HUE_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, CONFIG_HUE_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), TAG, "esp_wifi_set_config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(30000));

    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip);
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to Wi-Fi SSID \"%s\"", CONFIG_HUE_WIFI_SSID);
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to connect to Wi-Fi SSID \"%s\"", CONFIG_HUE_WIFI_SSID);
    return ESP_ERR_TIMEOUT;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < CONFIG_HUE_WIFI_MAXIMUM_RETRY) {
            s_retry_num++;
            ESP_LOGW(TAG, "Wi-Fi disconnected, retry %d/%d", s_retry_num, CONFIG_HUE_WIFI_MAXIMUM_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t set_hue_group(bool on)
{
    char url[160];
    snprintf(url, sizeof(url),
             "http://%s/api/%s/groups/%s/action",
             CONFIG_HUE_BRIDGE_IP,
             CONFIG_HUE_BRIDGE_API_KEY,
             CONFIG_HUE_BRIDGE_GROUP_ID);

    const char *body = on ? "{\"on\":true}" : "{\"on\":false}";
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PUT,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Hue request status=%d, content_length=%lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "Hue HTTP PUT failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t init_speech_commands(void)
{
    ESP_RETURN_ON_ERROR(esp_mn_commands_alloc(s_multinet, s_model_data), TAG, "command alloc failed");
    ESP_RETURN_ON_ERROR(esp_mn_commands_add(HUE_CMD_ON, "turn on living room"), TAG, "add on command failed");
    ESP_RETURN_ON_ERROR(esp_mn_commands_add(HUE_CMD_OFF, "turn off living room"), TAG, "add off command failed");

    esp_mn_error_t *err = esp_mn_commands_update();
    if (err != NULL) {
        ESP_LOGE(TAG, "Failed to update speech commands");
        return ESP_FAIL;
    }

    esp_mn_active_commands_print();
    return ESP_OK;
}

static esp_err_t init_models(void)
{
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

    ESP_LOGI(TAG, "Using MultiNet model: %s", mn_name);

    s_multinet = esp_mn_handle_from_name(mn_name);
    if (s_multinet == NULL) {
        ESP_LOGE(TAG, "Failed to get MultiNet handle for %s", mn_name);
        return ESP_ERR_NOT_FOUND;
    }

    s_model_data = s_multinet->create(mn_name, 6000);
    if (s_model_data == NULL) {
        ESP_LOGE(TAG, "Failed to create MultiNet model instance");
        return ESP_FAIL;
    }

    afe_config_t *afe_cfg = afe_config_init("MM", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    if (afe_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to allocate AFE config");
        return ESP_ERR_NO_MEM;
    }

    afe_cfg->wakenet_init = false;
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

static esp_err_t init_board_audio(void)
{
    s_mic_codec = bsp_audio_codec_microphone_init();
    if (s_mic_codec == NULL) {
        ESP_LOGE(TAG, "Failed to initialize BOX-3 microphone codec");
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = 16000,
        .channel = 2,
        .bits_per_sample = 16,
    };

    int ret = esp_codec_dev_open(s_mic_codec, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open microphone stream: %d", ret);
        return ESP_FAIL;
    }

    ret = esp_codec_dev_set_in_gain(s_mic_codec, CONFIG_HUE_MIC_GAIN_DB);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Unable to set microphone gain: %d", ret);
    }

    return ESP_OK;
}

static void voice_command_task(void *arg)
{
    const int feed_chunks = s_afe_handle->get_feed_chunksize(s_afe_data);
    const int feed_channels = s_afe_handle->get_feed_channel_num(s_afe_data);
    const size_t feed_samples = (size_t)feed_chunks * (size_t)feed_channels;
    const size_t feed_bytes = feed_samples * sizeof(int16_t);

    int16_t *feed_buffer = heap_caps_malloc(feed_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (feed_buffer == NULL) {
        feed_buffer = malloc(feed_bytes);
    }
    assert(feed_buffer != NULL);

    ESP_LOGI(TAG, "Starting voice loop: feed_chunks=%d, feed_channels=%d", feed_chunks, feed_channels);

    while (true) {
        int ret = esp_codec_dev_read(s_mic_codec, feed_buffer, feed_bytes);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "Microphone read failed: %d", ret);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        s_afe_handle->feed(s_afe_data, feed_buffer);
        afe_fetch_result_t *afe_result = s_afe_handle->fetch(s_afe_data);
        if (afe_result == NULL || afe_result->data == NULL) {
            continue;
        }

        esp_mn_state_t mn_state = s_multinet->detect(s_model_data, afe_result->data);
        if (mn_state != ESP_MN_STATE_DETECTED) {
            continue;
        }

        esp_mn_results_t *mn_result = s_multinet->get_results(s_model_data);
        if (mn_result == NULL || mn_result->num <= 0) {
            continue;
        }

        const int command_id = mn_result->command_id[0];
        ESP_LOGI(TAG, "Detected command_id=%d text=\"%s\" prob=%.3f",
                 command_id,
                 mn_result->string,
                 mn_result->prob[0]);

        if (command_id == HUE_CMD_ON) {
            set_hue_group(true);
        } else if (command_id == HUE_CMD_OFF) {
            set_hue_group(false);
        } else {
            ESP_LOGW(TAG, "Unhandled command id %d", command_id);
        }

        s_multinet->clean(s_model_data);
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

    ESP_ERROR_CHECK(wifi_init_sta());
    ESP_ERROR_CHECK(init_models());
    ESP_ERROR_CHECK(init_board_audio());

    xTaskCreatePinnedToCore(voice_command_task, "voice_cmd", 12288, NULL, 5, NULL, 1);
}
