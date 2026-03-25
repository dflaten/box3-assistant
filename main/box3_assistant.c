#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
#include "esp_system.h"
#include "esp_wn_models.h"
#include "esp_codec_dev.h"
#include "flite_g2p.h"

#include "commands/assistant_command_text.h"
#include "commands/assistant_command_dispatch.h"
#include "commands/assistant_commands.h"
#include "assistant_state.h"
#include "assistant_runtime.h"
#include "board/board_audio.h"
#include "hue/hue_command_map.h"
#include "hue/hue_command_runtime.h"
#include "hue/hue_client.h"
#include "hue/hue_group_store.h"
#include "board/ui_status.h"
#include "system/wifi_support.h"
#include "weather/weather_client.h"

#define COMMAND_WINDOW_MS 10000
#define COMMAND_MIN_LISTEN_MS 3000
#define WEATHER_STATUS_HOLD_MS 15000
#define MAX_FETCH_FAILURES 50
#define ASSISTANT_SESSION_TIMEOUT_MS 30000
#define ASSISTANT_LISTENING_STALL_TIMEOUT_MS 12000
#define ASSISTANT_WATCHDOG_POLL_MS 1000

static const char *TAG = "hue-voice";
static const char *WAKE_WORD = "Hi ESP";
static const TickType_t STATUS_HOLD_TIME = pdMS_TO_TICKS(1200);
static const TickType_t WEATHER_STATUS_HOLD_TIME = pdMS_TO_TICKS(WEATHER_STATUS_HOLD_MS);

static void audio_feed_set_paused(assistant_runtime_t *rt, bool paused);
static void show_status_then_return_to_standby(assistant_runtime_t *rt, ui_status_state_t state, const char *detail, TickType_t hold_time);

static const char *assistant_stage_name(assistant_stage_t stage)
{
    switch (stage) {
    case ASSISTANT_STAGE_STANDBY:
        return "standby";
    case ASSISTANT_STAGE_LISTENING:
        return "listening";
    case ASSISTANT_STAGE_EXECUTING:
        return "executing";
    default:
        return "unknown";
    }
}

/**
 * @brief Reset the assistant back to standby mode.
 * @param rt Shared assistant runtime state to reset back to standby.
 * @return This function does not return a value.
 * @note This clears the active recognition state, resets the AFE buffer, and re-enables WakeNet.
 */
static void return_to_standby(assistant_runtime_t *rt)
{
    rt->assistant_awake = false;
    rt->assistant_awake_tick = 0;
    rt->speech_progress_tick = xTaskGetTickCount();
    audio_feed_set_paused(rt, true);
    TickType_t wait_start = xTaskGetTickCount();
    while (rt->audio_feed_busy || !rt->audio_feed_paused) {
        if ((xTaskGetTickCount() - wait_start) >= pdMS_TO_TICKS(500)) {
            ESP_LOGW(TAG, "Timed out waiting for audio feed task to pause cleanly");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (rt->multinet != NULL && rt->model_data != NULL) {
        rt->multinet->clean(rt->model_data);
    }
    if (rt->afe_handle != NULL && rt->afe_data != NULL) {
        rt->afe_handle->reset_buffer(rt->afe_data);
        rt->afe_handle->enable_wakenet(rt->afe_data);
    }
    audio_feed_set_paused(rt, false);
    rt->assistant_stage = ASSISTANT_STAGE_STANDBY;
    rt->speech_progress_tick = xTaskGetTickCount();
    ESP_LOGI(TAG, "Assistant returned to standby");
}

/**
 * @brief Show a transient UI status while audio is paused, then restore standby mode.
 * @param rt Shared assistant runtime state to pause and reset.
 * @param state The status state to show during the hold period.
 * @param detail Optional detail text shown while the status is held.
 * @param hold_time Duration to keep the transient status visible before returning to standby.
 * @return This function does not return a value.
 * @note Pausing first prevents the AFE feed ringbuffer from filling while the detect task is sleeping.
 */
static void show_status_then_return_to_standby(assistant_runtime_t *rt, ui_status_state_t state, const char *detail, TickType_t hold_time)
{
    audio_feed_set_paused(rt, true);
    ui_status_set(state, detail);
    vTaskDelay(hold_time);
    return_to_standby(rt);
    ui_status_set(UI_STATUS_READY, NULL);
}

/**
 * @brief Watch for assistant sessions that stay awake too long and force recovery.
 * @param arg Pointer to the shared assistant runtime state.
 * @return This task does not return.
 * @note The watchdog exists to recover from stuck listening, working, or completed states.
 */
static void assistant_session_timeout_task(void *arg)
{
    assistant_runtime_t *rt = (assistant_runtime_t *)arg;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(ASSISTANT_WATCHDOG_POLL_MS));

        if (!rt->assistant_awake || rt->assistant_awake_tick == 0) {
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        TickType_t elapsed_ms = pdTICKS_TO_MS(now - rt->assistant_awake_tick);
        TickType_t stalled_ms = rt->speech_progress_tick == 0 ? 0 : pdTICKS_TO_MS(now - rt->speech_progress_tick);

        if (rt->assistant_stage == ASSISTANT_STAGE_LISTENING &&
            rt->speech_progress_tick != 0 &&
            stalled_ms >= ASSISTANT_LISTENING_STALL_TIMEOUT_MS) {
            ESP_LOGE(TAG,
                     "Speech pipeline stalled in %s for %lu ms; restarting",
                     assistant_stage_name(rt->assistant_stage),
                     (unsigned long)stalled_ms);
            esp_restart();
        }

        if (assistant_session_timed_out(rt->assistant_awake,
                                        rt->assistant_awake_tick != 0,
                                        elapsed_ms,
                                        ASSISTANT_SESSION_TIMEOUT_MS)) {
            ESP_LOGE(TAG,
                     "Assistant session exceeded %d ms in %s; restarting",
                     ASSISTANT_SESSION_TIMEOUT_MS,
                     assistant_stage_name(rt->assistant_stage));
            esp_restart();
        }
    }
}


/**
 * @brief Initialize the speech models and audio front end used by the assistant.
 * @param rt Shared assistant runtime state that receives initialized model and AFE handles.
 * @return ESP_OK on success, or an ESP error code if models or AFE setup fail.
 * @note This selects the enabled WakeNet and MultiNet models from the ESP-SR model partition.
 */
static esp_err_t init_models(assistant_runtime_t *rt)
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

    rt->multinet = esp_mn_handle_from_name(mn_name);
    if (rt->multinet == NULL) {
        ESP_LOGE(TAG, "Failed to get MultiNet handle for %s", mn_name);
        return ESP_ERR_NOT_FOUND;
    }

    rt->model_data = rt->multinet->create(mn_name, COMMAND_WINDOW_MS);
    if (rt->model_data == NULL) {
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

    rt->afe_handle = esp_afe_handle_from_config(afe_cfg);
    if (rt->afe_handle == NULL) {
        afe_config_free(afe_cfg);
        ESP_LOGE(TAG, "Failed to get AFE handle");
        return ESP_FAIL;
    }

    rt->afe_data = rt->afe_handle->create_from_config(afe_cfg);
    afe_config_free(afe_cfg);
    if (rt->afe_data == NULL) {
        ESP_LOGE(TAG, "Failed to create AFE instance");
        return ESP_FAIL;
    }

    rt->afe_handle->print_pipeline(rt->afe_data);
    return hue_command_runtime_rebuild(rt,
                                       ASSISTANT_CMD_SYNC_GROUPS,
                                       ASSISTANT_CMD_WEATHER_TODAY,
                                       ASSISTANT_CMD_WEATHER_TOMORROW,
                                       ASSISTANT_CMD_GROUP_BASE);
}

/**
 * @brief Pause or resume microphone feeding into the speech front end.
 * @param rt Shared assistant runtime state whose audio-feed pause flag will be updated.
 * @param paused True to stop feeding audio, false to resume feeding audio.
 * @return This function does not return a value.
 * @note Audio is paused during command execution so the AFE ring buffer does not overflow.
 */
static void audio_feed_set_paused(assistant_runtime_t *rt, bool paused)
{
    rt->pause_audio_feed = paused;
}

/**
 * @brief Continuously read microphone audio and feed it into the AFE pipeline.
 * @param arg Pointer to the shared assistant runtime state.
 * @return This task does not return.
 * @note Feed failures are logged and retried without terminating the task.
 */
static void audio_feed_task(void *arg)
{
    assistant_runtime_t *rt = (assistant_runtime_t *)arg;
    const int feed_chunks = rt->afe_handle->get_feed_chunksize(rt->afe_data);
    const int feed_channels = rt->afe_handle->get_feed_channel_num(rt->afe_data);
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
        if (rt->pause_audio_feed) {
            rt->audio_feed_busy = false;
            rt->audio_feed_paused = true;
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        rt->audio_feed_paused = false;
        rt->audio_feed_busy = true;

        if (rt->pause_audio_feed) {
            rt->audio_feed_busy = false;
            continue;
        }

        int ret = esp_codec_dev_read(rt->mic_codec, feed_buffer, feed_bytes);
        if (ret != ESP_CODEC_DEV_OK) {
            rt->audio_feed_busy = false;
            ESP_LOGW(TAG, "Microphone read failed: %d", ret);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (rt->pause_audio_feed) {
            rt->audio_feed_busy = false;
            continue;
        }

        int fed = rt->afe_handle->feed(rt->afe_data, feed_buffer);
        rt->audio_feed_busy = false;
        if (fed <= 0) {
            ESP_LOGW(TAG, "AFE feed failed: %d", fed);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

/**
 * @brief Run the wake/listen state machine and dispatch recognized commands.
 * @param arg Pointer to the shared assistant runtime state.
 * @return This task does not return.
 * @note This task handles wake word detection, command timeout recovery, and command execution.
 */
static void speech_detect_task(void *arg)
{
    assistant_runtime_t *rt = (assistant_runtime_t *)arg;
    TickType_t wake_tick = 0;
    int fetch_failures = 0;

    ESP_LOGI(TAG, "Starting speech detect task");

    while (true) {
        rt->speech_progress_tick = xTaskGetTickCount();
        afe_fetch_result_t *afe_result = rt->afe_handle->fetch(rt->afe_data);
        if (afe_result == NULL || afe_result->data == NULL) {
            if (rt->assistant_awake && fetch_failures < MAX_FETCH_FAILURES) {
                fetch_failures++;
            }
            if (assistant_step_for_missing_fetch(rt->assistant_awake, fetch_failures, MAX_FETCH_FAILURES) ==
                ASSISTANT_LISTEN_STEP_RECOVER_FETCH_STALL) {
                ESP_LOGW(TAG, "AFE fetch stalled while listening; forcing standby recovery");
                show_status_then_return_to_standby(rt, UI_STATUS_ERROR, "Audio timeout", STATUS_HOLD_TIME);
                fetch_failures = 0;
            }
            continue;
        }
        fetch_failures = 0;
        rt->speech_progress_tick = xTaskGetTickCount();

        if (!rt->assistant_awake) {
            if (afe_result->wakeup_state == WAKENET_DETECTED) {
                rt->assistant_awake = true;
                wake_tick = xTaskGetTickCount();
                rt->assistant_awake_tick = wake_tick;
                rt->speech_progress_tick = wake_tick;
                rt->assistant_stage = ASSISTANT_STAGE_LISTENING;
                rt->multinet->clean(rt->model_data);
                rt->afe_handle->disable_wakenet(rt->afe_data);
                ESP_LOGI(TAG, "Wake word detected: %s", WAKE_WORD);
                ui_status_set(UI_STATUS_LISTENING, NULL);
            }
            continue;
        }

        TickType_t elapsed_ms = pdTICKS_TO_MS(xTaskGetTickCount() - wake_tick);
        assistant_listen_step_t listen_step = assistant_step_for_multinet(elapsed_ms,
                                                                          COMMAND_WINDOW_MS,
                                                                          COMMAND_MIN_LISTEN_MS,
                                                                          ASSISTANT_MN_STATE_DETECTING,
                                                                          true);
        if (listen_step == ASSISTANT_LISTEN_STEP_RECOVER_COMMAND_TIMEOUT) {
            ESP_LOGW(TAG, "Forcing standby after %lu ms without a final command state", (unsigned long)elapsed_ms);
            show_status_then_return_to_standby(rt, UI_STATUS_ERROR, "Command timeout", STATUS_HOLD_TIME);
            continue;
        }

        esp_mn_state_t mn_state = rt->multinet->detect(rt->model_data, afe_result->data);
        listen_step = assistant_step_for_multinet(elapsed_ms,
                                                  COMMAND_WINDOW_MS,
                                                  COMMAND_MIN_LISTEN_MS,
                                                  (mn_state == ESP_MN_STATE_DETECTING) ? ASSISTANT_MN_STATE_DETECTING
                                                                                       : (mn_state == ESP_MN_STATE_DETECTED)
                                                                                             ? ASSISTANT_MN_STATE_DETECTED
                                                                                             : (mn_state == ESP_MN_STATE_TIMEOUT)
                                                                                                   ? ASSISTANT_MN_STATE_TIMEOUT
                                                                                                   : ASSISTANT_MN_STATE_OTHER,
                                                  true);
        if (listen_step == ASSISTANT_LISTEN_STEP_CONTINUE && mn_state == ESP_MN_STATE_DETECTING) {
            continue;
        }

        if (listen_step == ASSISTANT_LISTEN_STEP_IGNORE_EARLY_TIMEOUT) {
            ESP_LOGI(TAG, "Ignoring early timeout at %lu ms after wake word", (unsigned long)elapsed_ms);
            continue;
        }
        if (listen_step == ASSISTANT_LISTEN_STEP_RECOVER_NO_COMMAND) {
            ESP_LOGI(TAG, "Command window timed out after wake word at %lu ms", (unsigned long)elapsed_ms);
            show_status_then_return_to_standby(rt, UI_STATUS_ERROR, "No command heard", STATUS_HOLD_TIME);
            continue;
        }

        if (mn_state != ESP_MN_STATE_DETECTED) {
            ESP_LOGI(TAG, "Ignoring speech state %d at %lu ms after wake word", mn_state, (unsigned long)elapsed_ms);
            continue;
        }

        esp_mn_results_t *mn_result = rt->multinet->get_results(rt->model_data);
        if (assistant_step_for_multinet(elapsed_ms,
                                        COMMAND_WINDOW_MS,
                                        COMMAND_MIN_LISTEN_MS,
                                        ASSISTANT_MN_STATE_DETECTED,
                                        mn_result != NULL && mn_result->num > 0) ==
            ASSISTANT_LISTEN_STEP_RECOVER_EMPTY_RESULT) {
            ESP_LOGW(TAG, "MultiNet reported detection without results; forcing standby recovery");
            show_status_then_return_to_standby(rt, UI_STATUS_ERROR, "Command decode failed", STATUS_HOLD_TIME);
            continue;
        }

        const int command_id = mn_result->command_id[0];
        const float command_prob = mn_result->prob[0];
        char command_text_buffer[96];
        const char *command_text = assistant_command_text(command_id,
                                                          rt->groups,
                                                          rt->group_count,
                                                          command_text_buffer,
                                                          sizeof(command_text_buffer));
        ESP_LOGI(TAG, "Detected command_id=%d text=\"%s\" prob=%.3f elapsed_ms=%lu",
                 command_id,
                 mn_result->string,
                 command_prob,
                 (unsigned long)elapsed_ms);

        audio_feed_set_paused(rt, true);
        rt->assistant_stage = ASSISTANT_STAGE_EXECUTING;
        rt->speech_progress_tick = xTaskGetTickCount();
        ui_status_set(UI_STATUS_WORKING, command_text);

        esp_err_t action_err = ESP_FAIL;
        char action_detail[WEATHER_DETAIL_TEXT_LEN] = { 0 };
        TickType_t hold_time = STATUS_HOLD_TIME;
        assistant_command_dispatch_t dispatch;
        assistant_command_resolve(command_id, rt->group_count, &dispatch);

        if (dispatch.type == ASSISTANT_COMMAND_ACTION_SYNC_GROUPS) {
            action_err = hue_command_runtime_sync_groups(rt,
                                                         ASSISTANT_CMD_SYNC_GROUPS,
                                                         ASSISTANT_CMD_WEATHER_TODAY,
                                                         ASSISTANT_CMD_WEATHER_TOMORROW,
                                                         ASSISTANT_CMD_GROUP_BASE);
        } else if (dispatch.type == ASSISTANT_COMMAND_ACTION_WEATHER_TODAY ||
                   dispatch.type == ASSISTANT_COMMAND_ACTION_WEATHER_TOMORROW) {
            weather_report_t report = { 0 };
            action_err = (dispatch.type == ASSISTANT_COMMAND_ACTION_WEATHER_TODAY)
                             ? weather_client_fetch_today(&report)
                             : weather_client_fetch_tomorrow(&report);
            if (action_err == ESP_OK) {
                weather_format_detail(&report, action_detail, sizeof(action_detail));
                hold_time = WEATHER_STATUS_HOLD_TIME;
            }
        } else if (dispatch.type == ASSISTANT_COMMAND_ACTION_HUE_GROUP) {
            action_err = hue_client_set_group_by_id(rt->groups[dispatch.group_index].id, dispatch.on);
        } else {
            if (dispatch.type == ASSISTANT_COMMAND_ACTION_UNKNOWN) {
                ESP_LOGW(TAG, "Unhandled command id %d", command_id);
            } else {
                ESP_LOGW(TAG, "Unhandled command action %d for id %d", dispatch.type, command_id);
            }
        }

        if (action_err == ESP_OK) {
            ui_status_set(UI_STATUS_SUCCESS, action_detail[0] != '\0' ? action_detail : command_text);
        } else {
            ui_status_set(UI_STATUS_ERROR, command_text);
        }

        vTaskDelay(hold_time);
        return_to_standby(rt);
        ui_status_set(UI_STATUS_READY, NULL);
    }
}

/**
 * @brief Initialize the firmware subsystems and start the assistant tasks.
 * @return This function does not return a value.
 * @note Boot-time failures use ESP_ERROR_CHECK and will abort startup rather than continue in a broken state.
 */
void app_main(void)
{
    static assistant_runtime_t runtime;
    assistant_runtime_t *rt = &runtime;
    rt->assistant_stage = ASSISTANT_STAGE_STANDBY;
    rt->speech_progress_tick = xTaskGetTickCount();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(ui_status_init());
    ui_status_set(UI_STATUS_BOOTING, NULL);

    ESP_ERROR_CHECK(hue_group_store_init());
    ESP_ERROR_CHECK(hue_command_runtime_load_groups(rt));

    ui_status_set(UI_STATUS_CONNECTING, NULL);
    ESP_ERROR_CHECK(wifi_init_sta());

    ui_status_set(UI_STATUS_BOOTING, "Loading speech models");
    ESP_ERROR_CHECK(init_models(rt));

    ui_status_set(UI_STATUS_BOOTING, "Updating Hue groups");
    esp_err_t sync_err = hue_command_runtime_sync_groups(rt,
                                                         ASSISTANT_CMD_SYNC_GROUPS,
                                                         ASSISTANT_CMD_WEATHER_TODAY,
                                                         ASSISTANT_CMD_WEATHER_TOMORROW,
                                                         ASSISTANT_CMD_GROUP_BASE);
    if (sync_err != ESP_OK) {
        ESP_LOGW(TAG, "Boot-time Hue sync failed: %s", esp_err_to_name(sync_err));
        ESP_ERROR_CHECK(hue_command_runtime_rebuild(rt,
                                                    ASSISTANT_CMD_SYNC_GROUPS,
                                                    ASSISTANT_CMD_WEATHER_TODAY,
                                                    ASSISTANT_CMD_WEATHER_TOMORROW,
                                                    ASSISTANT_CMD_GROUP_BASE));
    }

    ESP_ERROR_CHECK(board_audio_init_microphone(&rt->mic_codec));

    ui_status_set(UI_STATUS_READY, NULL);

    xTaskCreatePinnedToCore(audio_feed_task, "audio_feed", 8192, rt, 6, NULL, 0);
    xTaskCreatePinnedToCore(speech_detect_task, "speech_detect", 12288, rt, 5, NULL, 1);
    xTaskCreate(assistant_session_timeout_task, "assistant_session_timeout", 4096, rt, 4, NULL);
}
