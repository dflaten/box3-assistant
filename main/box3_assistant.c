#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_system.h"
#include "esp_wn_models.h"
#include "esp_codec_dev.h"

#include "bsp/esp-box-3.h"

#include "commands/assistant_command_text.h"
#include "commands/assistant_command_dispatch.h"
#include "commands/assistant_commands.h"
#include "assistant_diagnostics.h"
#include "assistant_state.h"
#include "assistant_runtime.h"
#include "board/board_audio.h"
#include "hue/hue_command_map.h"
#include "hue/hue_command_runtime.h"
#include "hue/hue_client.h"
#include "hue/hue_group_store.h"
#include "board/ui_status.h"
#include "system/time_support.h"
#include "system/wifi_support.h"
#include "weather/weather_client.h"

#define COMMAND_WINDOW_MS                    10000
#define COMMAND_MIN_LISTEN_MS                3000
#define WEATHER_STATUS_HOLD_MS               15000
#define MAX_FETCH_FAILURES                   50
#define ASSISTANT_SESSION_TIMEOUT_MS         30000
#define ASSISTANT_LISTENING_STALL_TIMEOUT_MS 12000
#define ASSISTANT_WATCHDOG_POLL_MS           1000
#define ASSISTANT_EXECUTION_CANCEL_GRACE_MS  5000
#define ASSISTANT_TASK_HEARTBEAT_TIMEOUT_MS  15000
#define ASSISTANT_HEARTBEAT_SLEEP_SLICE_MS   250
#define PRESENCE_TIMEOUT_MS                  30000
#define PRESENCE_POLL_MS                     250
#define PRESENCE_TASK_STACK                  4096
#define PRESENCE_TASK_PRIORITY               3
#define PRESENCE_GPIO                        BSP_PMOD1_IO5

static const char *TAG = "hue-voice";
static const char *WAKE_WORD = "Hi ESP";
static const TickType_t STATUS_HOLD_TIME = pdMS_TO_TICKS(1200);
static const TickType_t ERROR_STATUS_HOLD_TIME = pdMS_TO_TICKS(4000);
static const TickType_t WEATHER_STATUS_HOLD_TIME = pdMS_TO_TICKS(WEATHER_STATUS_HOLD_MS);
static const TickType_t BRIDGE_ERROR_HOLD_TIME = ERROR_STATUS_HOLD_TIME;

static void audio_feed_set_paused(assistant_runtime_t *rt, bool paused);
static void sleep_with_speech_heartbeat(assistant_runtime_t *rt, TickType_t duration);
static void restore_idle_ui(assistant_runtime_t *rt);
static void show_status_then_return_to_standby(assistant_runtime_t *rt,
                                               ui_status_state_t state,
                                               const char *detail,
                                               TickType_t hold_time);
static void clear_active_session(assistant_runtime_t *rt);
static esp_err_t init_presence_sensor(void);
static void presence_clock_task(void *arg);
static void format_hue_probe_error_detail(esp_err_t probe_err, char *detail, size_t detail_size);
static void
format_hue_request_error_detail(const char *fallback, esp_err_t request_err, char *detail, size_t detail_size);

static const char *assistant_stage_name(assistant_stage_t stage) {
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
 * @brief Convert a Hue bridge probe failure into a short UI detail message.
 * @param probe_err The error returned by the Hue bridge probe.
 * @param detail Destination buffer for the user-facing status text.
 * @param detail_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 */
static void format_hue_probe_error_detail(esp_err_t probe_err, char *detail, size_t detail_size) {
    if (detail == NULL || detail_size == 0) {
        return;
    }

    if (!wifi_is_connected() || probe_err == ESP_ERR_INVALID_STATE) {
        snprintf(detail, detail_size, "Hue Wi-Fi disconnected");
    } else if (hue_client_error_is_connectivity(probe_err) || probe_err == ESP_ERR_NOT_FOUND) {
        snprintf(detail, detail_size, "Wrong Hue bridge IP");
    } else {
        snprintf(detail, detail_size, "Hue bridge unavailable");
    }
}

/**
 * @brief Convert a Hue command failure into a short UI detail message.
 * @param fallback Default message to use when the bridge itself is reachable.
 * @param request_err The error returned by the Hue command request.
 * @param detail Destination buffer for the user-facing status text.
 * @param detail_size Size of the destination buffer in bytes.
 * @return This function does not return a value.
 * @note This re-probes the bridge so the UI can distinguish bad bridge addressing from a command-specific failure.
 */
static void
format_hue_request_error_detail(const char *fallback, esp_err_t request_err, char *detail, size_t detail_size) {
    if (detail == NULL || detail_size == 0) {
        return;
    }

    if (!wifi_is_connected()) {
        format_hue_probe_error_detail(ESP_ERR_INVALID_STATE, detail, detail_size);
        return;
    }

    esp_err_t probe_err = hue_client_probe_bridge();
    if (probe_err != ESP_OK) {
        format_hue_probe_error_detail(probe_err, detail, detail_size);
        return;
    }

    snprintf(detail, detail_size, "%s", fallback != NULL ? fallback : "Hue command failed");
    if (hue_client_error_is_connectivity(request_err)) {
        ESP_LOGW(TAG, "Hue request failed even though bridge probe succeeded: %s", esp_err_to_name(request_err));
    }
}

/**
 * @brief Reset the assistant back to standby mode.
 * @param rt Shared assistant runtime state to reset back to standby.
 * @return This function does not return a value.
 * @note This clears the active recognition state, resets the AFE buffer, and re-enables WakeNet.
 */
static void return_to_standby(assistant_runtime_t *rt) {
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
 * @brief Sleep for a bounded duration while continuing to publish speech-task heartbeats.
 * @param rt Shared assistant runtime state whose speech heartbeat will be updated.
 * @param duration Total delay duration to wait.
 * @return This function does not return a value.
 * @note Long UI hold periods should use this helper so the watchdog can distinguish intentional sleeps from stalls.
 */
static void sleep_with_speech_heartbeat(assistant_runtime_t *rt, TickType_t duration) {
    TickType_t remaining = duration;

    while (remaining > 0) {
        TickType_t slice = remaining > pdMS_TO_TICKS(ASSISTANT_HEARTBEAT_SLEEP_SLICE_MS)
                             ? pdMS_TO_TICKS(ASSISTANT_HEARTBEAT_SLEEP_SLICE_MS)
                             : remaining;
        rt->speech_detect_heartbeat_tick = xTaskGetTickCount();
        vTaskDelay(slice);
        remaining -= slice;
    }
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
static void show_status_then_return_to_standby(assistant_runtime_t *rt,
                                               ui_status_state_t state,
                                               const char *detail,
                                               TickType_t hold_time) {
    audio_feed_set_paused(rt, true);
    if (state == UI_STATUS_ERROR && hold_time < ERROR_STATUS_HOLD_TIME) {
        hold_time = ERROR_STATUS_HOLD_TIME;
    }
    ui_status_set(state, detail);
    sleep_with_speech_heartbeat(rt, hold_time);
    restore_idle_ui(rt);
    return_to_standby(rt);
}

/**
 * @brief Restore the correct idle screen after command execution or recovery completes.
 * @param rt Shared assistant runtime state used to determine whether the presence clock should own the display.
 * @return This function does not return a value.
 * @note When presence is still active, this draws the clock immediately to avoid flashing the ready screen first.
 */
static void restore_idle_ui(assistant_runtime_t *rt) {
    TickType_t now = xTaskGetTickCount();
    bool motion_recent = rt->last_presence_motion_tick != 0 &&
                         (now - rt->last_presence_motion_tick) < pdMS_TO_TICKS(PRESENCE_TIMEOUT_MS);

    if (motion_recent) {
        char time_text[24];
        char date_text[32];
        bool clock_synced = time_support_format_now(time_text, sizeof(time_text), date_text, sizeof(date_text));

        if (clock_synced) {
            ui_status_show_clock(time_text, date_text, CONFIG_ASSISTANT_LOCATION_NAME);
        } else {
            ui_status_show_clock("SYNCING TIME", "WAITING FOR NTP", CONFIG_ASSISTANT_LOCATION_NAME);
        }
        return;
    }

    ui_status_set(UI_STATUS_READY, NULL);
}

/**
 * @brief Clear the current wake/listen/execute session without resetting the speech pipeline yet.
 * @param rt Shared assistant runtime state whose watchdog-visible session state will be cleared.
 * @return This function does not return a value.
 * @note This is used once command execution has completed so post-action UI delays do not trip the session watchdog.
 */
static void clear_active_session(assistant_runtime_t *rt) {
    rt->assistant_awake = false;
    rt->assistant_awake_tick = 0;
    rt->speech_progress_tick = xTaskGetTickCount();
    rt->current_command_id = 0;
    rt->execution_timeout_pending = false;
    rt->execution_timeout_tick = 0;
}

/**
 * @brief Configure the dock motion output pin used for presence-triggered clock wakeups.
 * @return ESP_OK on success, or an ESP error code if GPIO setup fails.
 */
static esp_err_t init_presence_sensor(void) {
    const gpio_config_t io_config = {
        .pin_bit_mask = 1ULL << PRESENCE_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&io_config), TAG, "Failed to configure presence GPIO");
    ESP_LOGI(TAG, "Presence monitor using GPIO %d", PRESENCE_GPIO);
    return ESP_OK;
}

/**
 * @brief Show a presence-triggered clock screen while the assistant is idle.
 * @param arg Pointer to the shared assistant runtime state.
 * @return This task does not return.
 * @note The motion sensor output is treated as active-high and only affects the screen during standby.
 */
static void presence_clock_task(void *arg) {
    assistant_runtime_t *rt = (assistant_runtime_t *) arg;
    TickType_t last_motion_tick = 0;
    bool display_owned_by_presence = false;
    bool last_clock_synced = false;

    char time_text[24];
    char date_text[32];
    char last_time_text[24] = {0};
    char last_date_text[32] = {0};

    while (true) {
        rt->presence_clock_heartbeat_tick = xTaskGetTickCount();
        vTaskDelay(pdMS_TO_TICKS(PRESENCE_POLL_MS));

        const TickType_t now = xTaskGetTickCount();
        const bool motion_detected = gpio_get_level(PRESENCE_GPIO) > 0;
        const bool assistant_idle = !rt->assistant_awake && rt->assistant_stage == ASSISTANT_STAGE_STANDBY;

        if (motion_detected) {
            last_motion_tick = now;
            rt->last_presence_motion_tick = now;
        }

        if (!assistant_idle) {
            display_owned_by_presence = false;
            continue;
        }

        const bool motion_recent =
            last_motion_tick != 0 && (now - last_motion_tick) < pdMS_TO_TICKS(PRESENCE_TIMEOUT_MS);

        if (!motion_recent) {
            if (display_owned_by_presence) {
                ESP_LOGI(TAG, "No motion for %d ms; turning clock display off", PRESENCE_TIMEOUT_MS);
                ui_status_display_set(false);
                display_owned_by_presence = false;
                last_clock_synced = false;
                last_time_text[0] = '\0';
                last_date_text[0] = '\0';
            }
            continue;
        }

        bool clock_synced = time_support_format_now(time_text, sizeof(time_text), date_text, sizeof(date_text));
        bool should_redraw = assistant_presence_clock_should_redraw(display_owned_by_presence,
                                                                    last_clock_synced,
                                                                    clock_synced,
                                                                    time_text,
                                                                    last_time_text,
                                                                    date_text,
                                                                    last_date_text);

        if (should_redraw) {
            if (clock_synced) {
                ui_status_show_clock(time_text, date_text, CONFIG_ASSISTANT_LOCATION_NAME);
                strlcpy(last_time_text, time_text, sizeof(last_time_text));
                strlcpy(last_date_text, date_text, sizeof(last_date_text));
            } else {
                ui_status_show_clock("SYNCING TIME", "WAITING FOR NTP", CONFIG_ASSISTANT_LOCATION_NAME);
                last_time_text[0] = '\0';
                last_date_text[0] = '\0';
            }
            display_owned_by_presence = true;
            last_clock_synced = clock_synced;
        }
    }
}

/**
 * @brief Watch for assistant sessions that stay awake too long and force recovery.
 * @param arg Pointer to the shared assistant runtime state.
 * @return This task does not return.
 * @note The watchdog exists to recover from stuck listening, working, or completed states.
 */
static void assistant_session_timeout_task(void *arg) {
    assistant_runtime_t *rt = (assistant_runtime_t *) arg;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(ASSISTANT_WATCHDOG_POLL_MS));

        TickType_t now = xTaskGetTickCount();
        uint32_t audio_stalled_ms = assistant_elapsed_ms_since_tick(now, rt->audio_feed_heartbeat_tick);
        uint32_t speech_detect_stalled_ms = assistant_elapsed_ms_since_tick(now, rt->speech_detect_heartbeat_tick);
        uint32_t presence_clock_stalled_ms = assistant_elapsed_ms_since_tick(now, rt->presence_clock_heartbeat_tick);
        uint32_t ui_render_stalled_ms = ui_status_render_stalled_ms();

        if (assistant_task_timed_out(
                rt->audio_feed_heartbeat_tick != 0, audio_stalled_ms, ASSISTANT_TASK_HEARTBEAT_TIMEOUT_MS)) {
            ESP_LOGE(TAG, "Audio feed task heartbeat stalled for %lu ms; restarting", (unsigned long) audio_stalled_ms);
            esp_restart();
        }
        if (rt->assistant_stage != ASSISTANT_STAGE_EXECUTING &&
            assistant_task_timed_out(
                rt->speech_detect_heartbeat_tick != 0, speech_detect_stalled_ms, ASSISTANT_TASK_HEARTBEAT_TIMEOUT_MS)) {
            ESP_LOGE(TAG,
                     "Speech detect task heartbeat stalled for %lu ms; restarting",
                     (unsigned long) speech_detect_stalled_ms);
            esp_restart();
        }
        if (assistant_task_timed_out(rt->presence_clock_heartbeat_tick != 0,
                                     presence_clock_stalled_ms,
                                     ASSISTANT_TASK_HEARTBEAT_TIMEOUT_MS)) {
            ESP_LOGE(TAG,
                     "Presence clock task heartbeat stalled for %lu ms; restarting",
                     (unsigned long) presence_clock_stalled_ms);
            esp_restart();
        }
        if (ui_render_stalled_ms >= ASSISTANT_TASK_HEARTBEAT_TIMEOUT_MS) {
            ESP_LOGE(TAG, "UI render stalled for %lu ms; restarting", (unsigned long) ui_render_stalled_ms);
            esp_restart();
        }

        if (!rt->assistant_awake || rt->assistant_awake_tick == 0) {
            continue;
        }

        TickType_t elapsed_ms = assistant_elapsed_ms_since_tick(now, rt->assistant_awake_tick);
        TickType_t stalled_ms = assistant_elapsed_ms_since_tick(now, rt->speech_progress_tick);

        if (rt->assistant_stage == ASSISTANT_STAGE_LISTENING && rt->speech_progress_tick != 0 &&
            stalled_ms >= ASSISTANT_LISTENING_STALL_TIMEOUT_MS) {
            ESP_LOGE(TAG,
                     "Speech pipeline stalled in %s for %lu ms; restarting",
                     assistant_stage_name(rt->assistant_stage),
                     (unsigned long) stalled_ms);
            esp_restart();
        }

        if (assistant_session_timed_out(
                rt->assistant_awake, rt->assistant_awake_tick != 0, elapsed_ms, ASSISTANT_SESSION_TIMEOUT_MS)) {
            if (rt->assistant_stage == ASSISTANT_STAGE_EXECUTING) {
                if (!rt->execution_timeout_pending) {
                    rt->execution_timeout_pending = true;
                    rt->execution_timeout_tick = now;
                    rt->speech_progress_tick = now;
                    assistant_diag_mark_timeout(rt->assistant_stage);
                    esp_err_t cancel_err = weather_client_cancel_active_request();
                    if (cancel_err == ESP_ERR_INVALID_STATE) {
                        cancel_err = hue_client_cancel_active_request();
                    }
                    ESP_LOGE(TAG,
                             "Assistant execution exceeded %d ms for command_id=%d; cancel result=%s",
                             ASSISTANT_SESSION_TIMEOUT_MS,
                             rt->current_command_id,
                             esp_err_to_name(cancel_err));
                    continue;
                }

                if (pdTICKS_TO_MS(now - rt->execution_timeout_tick) < ASSISTANT_EXECUTION_CANCEL_GRACE_MS) {
                    continue;
                }

                ESP_LOGE(TAG,
                         "Assistant execution timeout recovery did not finish within %d ms; restarting",
                         ASSISTANT_EXECUTION_CANCEL_GRACE_MS);
            }
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
static esp_err_t init_models(assistant_runtime_t *rt) {
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
static void audio_feed_set_paused(assistant_runtime_t *rt, bool paused) {
    rt->pause_audio_feed = paused;
}

/**
 * @brief Continuously read microphone audio and feed it into the AFE pipeline.
 * @param arg Pointer to the shared assistant runtime state.
 * @return This task does not return.
 * @note Feed failures are logged and retried without terminating the task.
 */
static void audio_feed_task(void *arg) {
    assistant_runtime_t *rt = (assistant_runtime_t *) arg;
    const int feed_chunks = rt->afe_handle->get_feed_chunksize(rt->afe_data);
    const int feed_channels = rt->afe_handle->get_feed_channel_num(rt->afe_data);
    const size_t feed_samples = (size_t) feed_chunks * (size_t) feed_channels;
    const size_t feed_bytes = feed_samples * sizeof(int16_t);

    int16_t *feed_buffer = heap_caps_malloc(feed_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (feed_buffer == NULL) {
        feed_buffer = malloc(feed_bytes);
    }
    assert(feed_buffer != NULL);

    ESP_LOGI(TAG,
             "Starting audio feed: wake word=%s, feed_chunks=%d, feed_channels=%d",
             WAKE_WORD,
             feed_chunks,
             feed_channels);

    while (true) {
        rt->audio_feed_heartbeat_tick = xTaskGetTickCount();
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
static void speech_detect_task(void *arg) {
    assistant_runtime_t *rt = (assistant_runtime_t *) arg;
    TickType_t wake_tick = 0;
    int fetch_failures = 0;

    ESP_LOGI(TAG, "Starting speech detect task");

    while (true) {
        rt->speech_detect_heartbeat_tick = xTaskGetTickCount();
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
                assistant_diag_capture_wake();
                rt->multinet->clean(rt->model_data);
                rt->afe_handle->disable_wakenet(rt->afe_data);
                ESP_LOGI(TAG, "Wake word detected: %s", WAKE_WORD);
                esp_err_t ui_err = ui_status_try_set(UI_STATUS_LISTENING, NULL);
                if (ui_err == ESP_ERR_TIMEOUT) {
                    ESP_LOGW(TAG, "Skipped listening UI update because the display was busy");
                }
            }
            continue;
        }

        TickType_t elapsed_ms = pdTICKS_TO_MS(xTaskGetTickCount() - wake_tick);
        assistant_listen_step_t listen_step = assistant_step_for_multinet(
            elapsed_ms, COMMAND_WINDOW_MS, COMMAND_MIN_LISTEN_MS, ASSISTANT_MN_STATE_DETECTING, true);
        if (listen_step == ASSISTANT_LISTEN_STEP_RECOVER_COMMAND_TIMEOUT) {
            ESP_LOGW(TAG, "Forcing standby after %lu ms without a final command state", (unsigned long) elapsed_ms);
            show_status_then_return_to_standby(rt, UI_STATUS_READY, "No command detected", STATUS_HOLD_TIME);
            continue;
        }

        esp_mn_state_t mn_state = rt->multinet->detect(rt->model_data, afe_result->data);
        listen_step = assistant_step_for_multinet(elapsed_ms,
                                                  COMMAND_WINDOW_MS,
                                                  COMMAND_MIN_LISTEN_MS,
                                                  (mn_state == ESP_MN_STATE_DETECTING)  ? ASSISTANT_MN_STATE_DETECTING
                                                  : (mn_state == ESP_MN_STATE_DETECTED) ? ASSISTANT_MN_STATE_DETECTED
                                                  : (mn_state == ESP_MN_STATE_TIMEOUT)  ? ASSISTANT_MN_STATE_TIMEOUT
                                                                                        : ASSISTANT_MN_STATE_OTHER,
                                                  true);
        if (listen_step == ASSISTANT_LISTEN_STEP_CONTINUE && mn_state == ESP_MN_STATE_DETECTING) {
            continue;
        }

        if (listen_step == ASSISTANT_LISTEN_STEP_IGNORE_EARLY_TIMEOUT) {
            ESP_LOGI(TAG, "Ignoring early timeout at %lu ms after wake word", (unsigned long) elapsed_ms);
            continue;
        }
        if (listen_step == ASSISTANT_LISTEN_STEP_RECOVER_NO_COMMAND) {
            ESP_LOGI(TAG, "Command window timed out after wake word at %lu ms", (unsigned long) elapsed_ms);
            show_status_then_return_to_standby(rt, UI_STATUS_READY, "No command detected", STATUS_HOLD_TIME);
            continue;
        }

        if (mn_state != ESP_MN_STATE_DETECTED) {
            ESP_LOGI(TAG, "Ignoring speech state %d at %lu ms after wake word", mn_state, (unsigned long) elapsed_ms);
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
        const char *command_text = assistant_command_text(
            command_id, rt->groups, rt->group_count, command_text_buffer, sizeof(command_text_buffer));
        ESP_LOGI(TAG,
                 "Detected command_id=%d text=\"%s\" prob=%.3f elapsed_ms=%lu",
                 command_id,
                 mn_result->string,
                 command_prob,
                 (unsigned long) elapsed_ms);

        audio_feed_set_paused(rt, true);
        rt->assistant_stage = ASSISTANT_STAGE_EXECUTING;
        TickType_t execute_start_tick = xTaskGetTickCount();
        rt->assistant_awake_tick = execute_start_tick;
        rt->speech_progress_tick = execute_start_tick;
        rt->current_command_id = command_id;
        rt->execution_timeout_pending = false;
        rt->execution_timeout_tick = 0;
        assistant_diag_start_command(command_id, rt->assistant_stage);
        ESP_LOGI(TAG, "Starting command execution for id=%d label=\"%s\"", command_id, command_text);

        esp_err_t action_err = ESP_FAIL;
        char action_detail[WEATHER_DETAIL_TEXT_LEN] = {0};
        TickType_t hold_time = STATUS_HOLD_TIME;
        assistant_command_dispatch_t dispatch;
        assistant_command_resolve(command_id, rt->group_count, &dispatch);

        if (dispatch.type == ASSISTANT_COMMAND_ACTION_SYNC_GROUPS) {
            action_err = hue_command_runtime_sync_groups(rt,
                                                         ASSISTANT_CMD_SYNC_GROUPS,
                                                         ASSISTANT_CMD_WEATHER_TODAY,
                                                         ASSISTANT_CMD_WEATHER_TOMORROW,
                                                         ASSISTANT_CMD_GROUP_BASE);
            if (action_err != ESP_OK) {
                format_hue_request_error_detail("Hue sync failed", action_err, action_detail, sizeof(action_detail));
            }
        } else if (dispatch.type == ASSISTANT_COMMAND_ACTION_WEATHER_TODAY ||
                   dispatch.type == ASSISTANT_COMMAND_ACTION_WEATHER_TOMORROW) {
            weather_report_t report = {0};
            action_err = (dispatch.type == ASSISTANT_COMMAND_ACTION_WEATHER_TODAY)
                           ? weather_client_fetch_today(&report)
                           : weather_client_fetch_tomorrow(&report);
            if (action_err == ESP_OK) {
                weather_format_detail(&report, action_detail, sizeof(action_detail));
                hold_time = WEATHER_STATUS_HOLD_TIME;
            } else if (action_err == ESP_ERR_HTTP_CONNECT || action_err == ESP_ERR_INVALID_STATE) {
                snprintf(action_detail, sizeof(action_detail), "Weather network error");
            } else {
                snprintf(action_detail, sizeof(action_detail), "Weather unavailable");
            }
        } else if (dispatch.type == ASSISTANT_COMMAND_ACTION_HUE_GROUP) {
            ESP_LOGI(TAG,
                     "Hue action phase=start group_index=%u group_id=%s on=%s",
                     (unsigned) dispatch.group_index,
                     rt->groups[dispatch.group_index].id,
                     dispatch.on ? "true" : "false");
            action_err = hue_client_set_group_by_id(rt->groups[dispatch.group_index].id, dispatch.on);
            ESP_LOGI(TAG, "Hue action phase=after_client err=%s", esp_err_to_name(action_err));
            if (action_err != ESP_OK) {
                ESP_LOGI(TAG, "Hue action phase=format_error_detail");
                format_hue_request_error_detail("Hue command failed", action_err, action_detail, sizeof(action_detail));
                ESP_LOGI(TAG,
                         "Hue action phase=after_format_error_detail detail=\"%s\"",
                         action_detail[0] != '\0' ? action_detail : "<empty>");
            }
        } else {
            if (dispatch.type == ASSISTANT_COMMAND_ACTION_UNKNOWN) {
                ESP_LOGW(TAG, "Unhandled command id %d", command_id);
            } else {
                ESP_LOGW(TAG, "Unhandled command action %d for id %d", dispatch.type, command_id);
            }
        }

        if (rt->execution_timeout_pending) {
            action_err = ESP_ERR_TIMEOUT;
            hold_time = STATUS_HOLD_TIME;
            snprintf(action_detail,
                     sizeof(action_detail),
                     "%s timeout",
                     (dispatch.type == ASSISTANT_COMMAND_ACTION_WEATHER_TODAY ||
                      dispatch.type == ASSISTANT_COMMAND_ACTION_WEATHER_TOMORROW)
                         ? "Weather"
                         : "Command");
        }

        if (action_err == ESP_OK) {
            ESP_LOGI(TAG,
                     "Command result phase=ui_success detail=\"%s\"",
                     action_detail[0] != '\0' ? action_detail : command_text);
            ui_status_set(UI_STATUS_SUCCESS, action_detail[0] != '\0' ? action_detail : command_text);
        } else {
            if (hold_time < ERROR_STATUS_HOLD_TIME) {
                hold_time = ERROR_STATUS_HOLD_TIME;
            }
            ESP_LOGI(TAG,
                     "Command result phase=ui_error detail=\"%s\"",
                     action_detail[0] != '\0' ? action_detail : command_text);
            ui_status_set(UI_STATUS_ERROR, action_detail[0] != '\0' ? action_detail : command_text);
        }

        ESP_LOGI(TAG,
                 "Finished command execution for id=%d status=%s elapsed_ms=%lu",
                 command_id,
                 action_err == ESP_OK ? "ok" : esp_err_to_name(action_err),
                 (unsigned long) pdTICKS_TO_MS(xTaskGetTickCount() - execute_start_tick));

        assistant_diag_finish_command(action_err);
        clear_active_session(rt);
        sleep_with_speech_heartbeat(rt, hold_time);
        restore_idle_ui(rt);
        return_to_standby(rt);
    }
}

/**
 * @brief Initialize the firmware subsystems and start the assistant tasks.
 * @return This function does not return a value.
 * @note Boot-time failures use ESP_ERROR_CHECK and will abort startup rather than continue in a broken state.
 */
void app_main(void) {
    static assistant_runtime_t runtime;
    assistant_runtime_t *rt = &runtime;
    TickType_t startup_tick = xTaskGetTickCount();
    rt->assistant_stage = ASSISTANT_STAGE_STANDBY;
    rt->audio_feed_heartbeat_tick = startup_tick;
    rt->speech_detect_heartbeat_tick = startup_tick;
    rt->presence_clock_heartbeat_tick = startup_tick;
    rt->last_presence_motion_tick = 0;
    rt->speech_progress_tick = startup_tick;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(assistant_diag_init());
    ESP_ERROR_CHECK(ui_status_init());
    assistant_diag_log_previous_issue();
    char boot_diag[48];
    if (assistant_diag_format_previous_issue(boot_diag, sizeof(boot_diag))) {
        ui_status_set(UI_STATUS_ERROR, boot_diag);
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
    ui_status_set(UI_STATUS_BOOTING, NULL);

    ESP_ERROR_CHECK(hue_group_store_init());
    ESP_ERROR_CHECK(hue_command_runtime_load_groups(rt));

    ui_status_set(UI_STATUS_CONNECTING, NULL);
    ESP_ERROR_CHECK(wifi_init_sta());
    esp_err_t time_err = time_support_init();
    if (time_err != ESP_OK) {
        ESP_LOGW(TAG, "Time sync initialization failed: %s", esp_err_to_name(time_err));
    }

    ui_status_set(UI_STATUS_BOOTING, "Checking Hue bridge");
    esp_err_t hue_probe_err = hue_client_probe_bridge();
    if (hue_probe_err != ESP_OK) {
        char hue_detail[WEATHER_DETAIL_TEXT_LEN];
        format_hue_probe_error_detail(hue_probe_err, hue_detail, sizeof(hue_detail));
        ESP_LOGW(TAG, "Hue bridge probe failed: %s", esp_err_to_name(hue_probe_err));
        ui_status_set(UI_STATUS_ERROR, hue_detail);
        vTaskDelay(BRIDGE_ERROR_HOLD_TIME);
    }

    ui_status_set(UI_STATUS_BOOTING, "Loading speech models");
    ESP_ERROR_CHECK(init_models(rt));

    ui_status_set(UI_STATUS_BOOTING, "Updating Hue groups");
    esp_err_t sync_err = (hue_probe_err == ESP_OK) ? hue_command_runtime_sync_groups(rt,
                                                                                     ASSISTANT_CMD_SYNC_GROUPS,
                                                                                     ASSISTANT_CMD_WEATHER_TODAY,
                                                                                     ASSISTANT_CMD_WEATHER_TOMORROW,
                                                                                     ASSISTANT_CMD_GROUP_BASE)
                                                   : hue_probe_err;
    if (sync_err != ESP_OK) {
        char hue_detail[WEATHER_DETAIL_TEXT_LEN];
        if (hue_probe_err != ESP_OK) {
            format_hue_probe_error_detail(hue_probe_err, hue_detail, sizeof(hue_detail));
        } else {
            format_hue_request_error_detail("Hue sync failed", sync_err, hue_detail, sizeof(hue_detail));
        }
        ESP_LOGW(TAG, "Boot-time Hue sync failed: %s", esp_err_to_name(sync_err));
        ui_status_set(UI_STATUS_ERROR, hue_detail);
        vTaskDelay(BRIDGE_ERROR_HOLD_TIME);
        ESP_ERROR_CHECK(hue_command_runtime_rebuild(rt,
                                                    ASSISTANT_CMD_SYNC_GROUPS,
                                                    ASSISTANT_CMD_WEATHER_TODAY,
                                                    ASSISTANT_CMD_WEATHER_TOMORROW,
                                                    ASSISTANT_CMD_GROUP_BASE));
    }

    ESP_ERROR_CHECK(board_audio_init_microphone(&rt->mic_codec));
    ESP_ERROR_CHECK(init_presence_sensor());

    ui_status_set(UI_STATUS_READY, NULL);

    xTaskCreatePinnedToCore(audio_feed_task, "audio_feed", 8192, rt, 6, NULL, 0);
    xTaskCreatePinnedToCore(speech_detect_task, "speech_detect", 12288, rt, 5, NULL, 1);
    xTaskCreate(assistant_session_timeout_task, "assistant_session_timeout", 4096, rt, 4, NULL);
    xTaskCreate(presence_clock_task, "presence_clock", PRESENCE_TASK_STACK, rt, PRESENCE_TASK_PRIORITY, NULL);
}
