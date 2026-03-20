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
#include "esp_wn_models.h"
#include "esp_codec_dev.h"
#include "flite_g2p.h"
#include "model_path.h"

#include "board/board_audio.h"
#include "hue/hue_client.h"
#include "hue/hue_group_store.h"
#include "board/ui_status.h"
#include "system/wifi_support.h"
#include "weather/weather_client.h"

#define CMD_SYNC_GROUPS 1
#define CMD_WEATHER_TODAY 2
#define CMD_GROUP_BASE  100

#define COMMAND_WINDOW_MS 10000
#define COMMAND_MIN_LISTEN_MS 3000
#define MAX_SYNCED_GROUPS HUE_GROUP_MAX_COUNT
#define WEATHER_STATUS_HOLD_MS 15000
#define MAX_FETCH_FAILURES 50
#define ASSISTANT_SESSION_TIMEOUT_MS 30000
#define ASSISTANT_WATCHDOG_POLL_MS 1000

static const char *TAG = "hue-voice";
static const char *WAKE_WORD = "Hi ESP";
static const TickType_t STATUS_HOLD_TIME = pdMS_TO_TICKS(1200);
static const TickType_t WEATHER_STATUS_HOLD_TIME = pdMS_TO_TICKS(WEATHER_STATUS_HOLD_MS);

static bool s_assistant_awake;
static bool s_commands_allocated;
static volatile bool s_pause_audio_feed;
static TickType_t s_assistant_awake_tick;

static esp_mn_iface_t *s_multinet = NULL;
static model_iface_data_t *s_model_data = NULL;
static const esp_afe_sr_iface_t *s_afe_handle = NULL;
static esp_afe_sr_data_t *s_afe_data = NULL;
static esp_codec_dev_handle_t s_mic_codec = NULL;

static hue_group_t s_groups[MAX_SYNCED_GROUPS];
static size_t s_group_count;

static void audio_feed_set_paused(bool paused);

static int group_command_id(size_t index, bool on)
{
    return CMD_GROUP_BASE + (int)(index * 2) + (on ? 0 : 1);
}

static bool decode_group_command_id(int command_id, size_t *group_index, bool *on)
{
    if (command_id < CMD_GROUP_BASE) {
        return false;
    }

    int offset = command_id - CMD_GROUP_BASE;
    size_t index = (size_t)(offset / 2);
    if (index >= s_group_count) {
        return false;
    }

    if (group_index != NULL) {
        *group_index = index;
    }
    if (on != NULL) {
        *on = (offset % 2) == 0;
    }
    return true;
}

static const char *friendly_command_text(int command_id)
{
    static char text[96];
    if (command_id == CMD_SYNC_GROUPS) {
        return "Update groups from Hue";
    }
    if (command_id == CMD_WEATHER_TODAY) {
        return "Weather today";
    }

    size_t index = 0;
    bool on = false;
    if (decode_group_command_id(command_id, &index, &on)) {
        snprintf(text, sizeof(text), "Turn %s %s", on ? "on" : "off", s_groups[index].name);
        return text;
    }

    return "Unknown command";
}

static void return_to_standby(void)
{
    s_assistant_awake = false;
    s_assistant_awake_tick = 0;
    audio_feed_set_paused(true);
    if (s_multinet != NULL && s_model_data != NULL) {
        s_multinet->clean(s_model_data);
    }
    if (s_afe_handle != NULL && s_afe_data != NULL) {
        s_afe_handle->reset_buffer(s_afe_data);
        s_afe_handle->enable_wakenet(s_afe_data);
    }
    audio_feed_set_paused(false);
    ESP_LOGI(TAG, "Assistant returned to standby");
}

static void assistant_watchdog_task(void *arg)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(ASSISTANT_WATCHDOG_POLL_MS));

        if (!s_assistant_awake || s_assistant_awake_tick == 0) {
            continue;
        }

        TickType_t elapsed_ms = pdTICKS_TO_MS(xTaskGetTickCount() - s_assistant_awake_tick);
        if (elapsed_ms < ASSISTANT_SESSION_TIMEOUT_MS) {
            continue;
        }

        ESP_LOGW(TAG, "Assistant session exceeded %d ms; forcing standby recovery", ASSISTANT_SESSION_TIMEOUT_MS);
        ui_status_set(UI_STATUS_ERROR, "Assistant reset");
        vTaskDelay(STATUS_HOLD_TIME);
        return_to_standby();
        ui_status_set(UI_STATUS_READY, NULL);
    }
}

static esp_err_t add_runtime_phrase(int command_id, const char *text)
{
    char *phonemes = flite_g2p(text, 1);
    if (phonemes == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_mn_commands_phoneme_add(command_id, text, phonemes);
    free(phonemes);
    return err;
}

static esp_err_t rebuild_command_table(void)
{
    if (!s_commands_allocated) {
        ESP_RETURN_ON_ERROR(esp_mn_commands_alloc(s_multinet, s_model_data), TAG, "Failed to allocate command table");
        s_commands_allocated = true;
    } else {
        ESP_RETURN_ON_ERROR(esp_mn_commands_clear(), TAG, "Failed to clear command table");
    }

    ESP_RETURN_ON_ERROR(add_runtime_phrase(CMD_SYNC_GROUPS, "update groups from hue"), TAG, "Failed to add sync command");
    ESP_RETURN_ON_ERROR(add_runtime_phrase(CMD_WEATHER_TODAY, "weather today"), TAG, "Failed to add weather command");

    for (size_t i = 0; i < s_group_count; ++i) {
        char on_phrase[96];
        char off_phrase[96];
        snprintf(on_phrase, sizeof(on_phrase), "turn on %s", s_groups[i].name);
        snprintf(off_phrase, sizeof(off_phrase), "turn off %s", s_groups[i].name);

        ESP_RETURN_ON_ERROR(add_runtime_phrase(group_command_id(i, true), on_phrase), TAG, "Failed to add on command");
        ESP_RETURN_ON_ERROR(add_runtime_phrase(group_command_id(i, false), off_phrase), TAG, "Failed to add off command");
    }

    esp_mn_error_t *err = esp_mn_commands_update();
    if (err != NULL) {
        ESP_LOGE(TAG, "Failed to update MultiNet command table");
        for (int i = 0; i < err->num; ++i) {
            if (err->phrases[i] != NULL) {
                ESP_LOGE(TAG, "Rejected phrase: %s", err->phrases[i]->string);
            }
        }
        return ESP_FAIL;
    }

    esp_mn_commands_print();
    esp_mn_active_commands_print();
    return ESP_OK;
}

static esp_err_t load_groups_from_storage(void)
{
    size_t count = 0;
    ESP_RETURN_ON_ERROR(hue_group_store_load(s_groups, MAX_SYNCED_GROUPS, &count), TAG, "Failed to load stored Hue groups");
    s_group_count = count;
    return ESP_OK;
}

static esp_err_t sync_groups_from_hue(void)
{
    size_t synced_count = 0;
    ESP_RETURN_ON_ERROR(hue_client_sync_groups(s_groups, MAX_SYNCED_GROUPS, &synced_count),
                        TAG,
                        "Failed to sync Hue groups");
    s_group_count = synced_count;

    ESP_RETURN_ON_ERROR(hue_group_store_save(s_groups, s_group_count), TAG, "Failed to save Hue groups");
    ESP_RETURN_ON_ERROR(rebuild_command_table(), TAG, "Failed to rebuild command table after Hue sync");

    ESP_LOGI(TAG, "Synced %u usable Hue group(s)", (unsigned)s_group_count);
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
    return rebuild_command_table();
}

static void audio_feed_set_paused(bool paused)
{
    s_pause_audio_feed = paused;
}

static void audio_feed_task(void *arg)
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

    ESP_LOGI(TAG, "Starting audio feed: wake word=%s, feed_chunks=%d, feed_channels=%d",
             WAKE_WORD, feed_chunks, feed_channels);

    while (true) {
        if (s_pause_audio_feed) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

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
    int fetch_failures = 0;

    ESP_LOGI(TAG, "Starting speech detect task");

    while (true) {
        afe_fetch_result_t *afe_result = s_afe_handle->fetch(s_afe_data);
        if (afe_result == NULL || afe_result->data == NULL) {
            if (s_assistant_awake) {
                fetch_failures++;
                if (fetch_failures >= MAX_FETCH_FAILURES) {
                    ESP_LOGW(TAG, "AFE fetch stalled while listening; forcing standby recovery");
                    ui_status_set(UI_STATUS_ERROR, "Audio timeout");
                    vTaskDelay(STATUS_HOLD_TIME);
                    return_to_standby();
                    ui_status_set(UI_STATUS_READY, NULL);
                    fetch_failures = 0;
                }
            }
            continue;
        }
        fetch_failures = 0;

        if (!s_assistant_awake) {
            if (afe_result->wakeup_state == WAKENET_DETECTED) {
                s_assistant_awake = true;
                wake_tick = xTaskGetTickCount();
                s_assistant_awake_tick = wake_tick;
                s_multinet->clean(s_model_data);
                s_afe_handle->disable_wakenet(s_afe_data);
                ESP_LOGI(TAG, "Wake word detected: %s", WAKE_WORD);
                ui_status_set(UI_STATUS_LISTENING, NULL);
            }
            continue;
        }

        TickType_t elapsed_ms = pdTICKS_TO_MS(xTaskGetTickCount() - wake_tick);
        if (elapsed_ms >= COMMAND_WINDOW_MS) {
            ESP_LOGW(TAG, "Forcing standby after %lu ms without a final command state", (unsigned long)elapsed_ms);
            ui_status_set(UI_STATUS_ERROR, "Command timeout");
            vTaskDelay(STATUS_HOLD_TIME);
            return_to_standby();
            ui_status_set(UI_STATUS_READY, NULL);
            continue;
        }

        esp_mn_state_t mn_state = s_multinet->detect(s_model_data, afe_result->data);
        if (mn_state == ESP_MN_STATE_DETECTING) {
            continue;
        }

        if (mn_state == ESP_MN_STATE_TIMEOUT) {
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
            ESP_LOGW(TAG, "MultiNet reported detection without results; forcing standby recovery");
            ui_status_set(UI_STATUS_ERROR, "Command decode failed");
            vTaskDelay(STATUS_HOLD_TIME);
            return_to_standby();
            ui_status_set(UI_STATUS_READY, NULL);
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

        audio_feed_set_paused(true);
        ui_status_set(UI_STATUS_WORKING, command_text);

        esp_err_t action_err = ESP_FAIL;
        char action_detail[WEATHER_DETAIL_TEXT_LEN] = { 0 };
        TickType_t hold_time = STATUS_HOLD_TIME;
        if (command_id == CMD_SYNC_GROUPS) {
            action_err = sync_groups_from_hue();
        } else if (command_id == CMD_WEATHER_TODAY) {
            weather_report_t report = { 0 };
            action_err = weather_client_fetch_today(&report);
            if (action_err == ESP_OK) {
                weather_client_format_detail(&report, action_detail, sizeof(action_detail));
                hold_time = WEATHER_STATUS_HOLD_TIME;
            }
        } else {
            size_t group_index = 0;
            bool on = false;
            if (decode_group_command_id(command_id, &group_index, &on)) {
                action_err = hue_client_set_group_by_id(s_groups[group_index].id, on);
            } else {
                ESP_LOGW(TAG, "Unhandled command id %d", command_id);
            }
        }

        if (action_err == ESP_OK) {
            ui_status_set(UI_STATUS_SUCCESS, action_detail[0] != '\0' ? action_detail : command_text);
        } else {
            ui_status_set(UI_STATUS_ERROR, command_text);
        }

        vTaskDelay(hold_time);
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

    ESP_ERROR_CHECK(hue_group_store_init());
    ESP_ERROR_CHECK(load_groups_from_storage());

    ui_status_set(UI_STATUS_CONNECTING, NULL);
    ESP_ERROR_CHECK(wifi_init_sta());

    ui_status_set(UI_STATUS_BOOTING, "Loading speech models");
    ESP_ERROR_CHECK(init_models());

    ui_status_set(UI_STATUS_BOOTING, "Updating Hue groups");
    esp_err_t sync_err = sync_groups_from_hue();
    if (sync_err != ESP_OK) {
        ESP_LOGW(TAG, "Boot-time Hue sync failed: %s", esp_err_to_name(sync_err));
        ESP_ERROR_CHECK(rebuild_command_table());
    }

    ESP_ERROR_CHECK(board_audio_init_microphone(&s_mic_codec));

    ui_status_set(UI_STATUS_READY, NULL);

    xTaskCreatePinnedToCore(audio_feed_task, "audio_feed", 8192, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(speech_detect_task, "speech_detect", 12288, NULL, 5, NULL, 1);
    xTaskCreate(assistant_watchdog_task, "assistant_watchdog", 4096, NULL, 4, NULL);
}
