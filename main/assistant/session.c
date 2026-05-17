#include "assistant/session.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/task.h"

#include "esp_afe_config.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_wn_models.h"

#include "assistant/command_registry.h"
#include "assistant/presence.h"
#include "assistant/watchdog.h"
#include "assistant_diagnostics.h"
#include "assistant_state.h"
#include "commands/assistant_command_text.h"
#include "commands/assistant_commands.h"
#include "hue/hue_command_runtime.h"
#include "system/time_support.h"
#include "timer/timer_runtime.h"

#define COMMAND_WINDOW_MS                  10000
#define COMMAND_MIN_LISTEN_MS              3000
#define MAX_FETCH_FAILURES                 50
#define ASSISTANT_HEARTBEAT_SLEEP_SLICE_MS 250
#define WEATHER_STATUS_HOLD_MS             30000
#define ERROR_STATUS_HOLD_MS               4000
#define PRESENCE_TIMEOUT_MS                30000

static const char *TAG = "assistant-session";
static const char *WAKE_WORD = "Hi ESP";
static const TickType_t STATUS_HOLD_TIME = pdMS_TO_TICKS(1200);
static const TickType_t ERROR_STATUS_HOLD_TIME = pdMS_TO_TICKS(ERROR_STATUS_HOLD_MS);

static void audio_feed_set_paused(assistant_runtime_t *rt, bool paused);
static uint32_t monotonic_ms_now(void);
static void show_timer_status(assistant_runtime_t *rt);
static void audio_feed_task(void *arg);
static void speech_detect_task(void *arg);
static void execute_command(assistant_runtime_t *rt, int command_id, const char *command_text);

/**
 * @brief Convert an assistant stage enum to the matching diagnostic label.
 * @param stage Assistant stage to format.
 * @return Pointer to a static string naming the supplied stage.
 */
const char *assistant_session_stage_name(assistant_stage_t stage) {
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
 * @brief Read the current monotonic uptime in milliseconds.
 * @return Milliseconds since boot truncated to 32 bits.
 */
static uint32_t monotonic_ms_now(void) {
    return (uint32_t) pdTICKS_TO_MS(xTaskGetTickCount());
}

/**
 * @brief Render the active timer countdown or alarm state on screen.
 * @param rt Shared assistant runtime state whose timer should be shown.
 * @return This function does not return a value.
 */
static void show_timer_status(assistant_runtime_t *rt) {
    if (rt == NULL || !rt->timer.active) {
        return;
    }

    char detail[24];
    if (rt->timer.alarming) {
        snprintf(detail, sizeof(detail), "00:00");
        ui_status_set(UI_STATUS_TIMER_ALARM, detail);
        return;
    }

    timer_runtime_format_remaining(&rt->timer, monotonic_ms_now(), detail, sizeof(detail));
    ui_status_set(UI_STATUS_TIMER, detail);
}

/**
 * @brief Clear the active wake/listen/execute session bookkeeping.
 * @param rt Runtime state whose watchdog-visible session fields should be reset.
 * @return This function does not return a value.
 */
void assistant_session_clear_active(assistant_runtime_t *rt) {
    if (rt == NULL) {
        return;
    }

    rt->assistant_awake = false;
    rt->assistant_awake_tick = 0;
    rt->speech_progress_tick = xTaskGetTickCount();
    rt->current_command_id = 0;
    rt->execution_timeout_pending = false;
    rt->execution_timeout_tick = 0;
}

/**
 * @brief Pause or resume microphone feeding into the speech front end.
 * @param rt Shared assistant runtime state whose audio-feed pause flag will be updated.
 * @param paused True to stop feeding audio, false to resume feeding audio.
 * @return This function does not return a value.
 */
static void audio_feed_set_paused(assistant_runtime_t *rt, bool paused) {
    rt->pause_audio_feed = paused;
}

/**
 * @brief Restore the correct idle screen after command execution or recovery completes.
 * @param rt Shared assistant runtime state used to decide which idle UI should be shown.
 * @return This function does not return a value.
 */
void assistant_session_restore_idle_ui(assistant_runtime_t *rt) {
    if (rt->timer.active) {
        show_timer_status(rt);
        return;
    }

    TickType_t now = xTaskGetTickCount();
    bool motion_recent =
        assistant_presence_motion_recent(rt->last_presence_motion_tick, now, pdMS_TO_TICKS(PRESENCE_TIMEOUT_MS));

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
 * @brief Reset the assistant pipeline and return to standby mode.
 * @param rt Shared assistant runtime state to reset back to standby.
 * @return This function does not return a value.
 */
void assistant_session_return_to_standby(assistant_runtime_t *rt) {
    rt->assistant_awake = false;
    rt->assistant_awake_tick = 0;
    rt->speech_progress_tick = xTaskGetTickCount();
    rt->direct_command_prepared = false;
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
 * @brief Show a transient status, restore idle UI, and return the assistant to standby.
 * @param rt Shared assistant runtime state to update.
 * @param state UI status to show during the hold period.
 * @param detail Optional detail text to display.
 * @param hold_time Duration to keep the transient status visible.
 * @return This function does not return a value.
 */
void assistant_session_show_status_then_return_to_standby(assistant_runtime_t *rt,
                                                          ui_status_state_t state,
                                                          const char *detail,
                                                          TickType_t hold_time) {
    audio_feed_set_paused(rt, true);
    if (state == UI_STATUS_ERROR && hold_time < ERROR_STATUS_HOLD_TIME) {
        hold_time = ERROR_STATUS_HOLD_TIME;
    }
    ui_status_set(state, detail);
    assistant_watchdog_sleep_with_heartbeat(rt, hold_time, pdMS_TO_TICKS(ASSISTANT_HEARTBEAT_SLEEP_SLICE_MS));
    assistant_session_restore_idle_ui(rt);
    assistant_session_return_to_standby(rt);
}

/**
 * @brief Execute a resolved command through the feature handler registry.
 * @param rt Shared assistant runtime state for the active command.
 * @param command_id Recognized command id to execute.
 * @param command_text Human-readable command label for logs and fallback UI.
 * @return This function does not return a value.
 */
static void execute_command(assistant_runtime_t *rt, int command_id, const char *command_text) {
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

    assistant_command_dispatch_t dispatch;
    const assistant_command_handler_t *handler = NULL;
    assistant_command_result_t result = {
        .err = ESP_FAIL,
        .detail = {0},
        .hold_time = STATUS_HOLD_TIME,
        .result_visible_start_tick = 0,
        .status_rendered = false,
        .timeout_label = "Command",
    };

    if (!assistant_command_registry_lookup(command_id, rt->group_count, &dispatch, &handler) || handler == NULL ||
        handler->execute == NULL) {
        ESP_LOGW(TAG, "Unhandled command id %d", command_id);
        snprintf(result.detail, sizeof(result.detail), "Command unavailable");
        result.err = ESP_ERR_NOT_SUPPORTED;
    } else {
        assistant_command_context_t context = {
            .runtime = rt,
            .command_id = command_id,
            .command_text = command_text,
        };
        esp_err_t handler_err = handler->execute(&context, &dispatch, &result);
        if (handler_err != ESP_OK && result.err == ESP_FAIL) {
            result.err = handler_err;
        }
    }

    if (rt->execution_timeout_pending) {
        result.err = ESP_ERR_TIMEOUT;
        result.hold_time = STATUS_HOLD_TIME;
        snprintf(result.detail,
                 sizeof(result.detail),
                 "%s timeout",
                 result.timeout_label != NULL ? result.timeout_label : "Command");
    }

    if (result.err == ESP_OK) {
        ESP_LOGI(TAG,
                 "Command result phase=ui_success detail=\"%s\"",
                 result.detail[0] != '\0' ? result.detail : command_text);
        if (!result.status_rendered) {
            ui_status_set(UI_STATUS_SUCCESS, result.detail[0] != '\0' ? result.detail : command_text);
        }
    } else {
        if (result.hold_time < ERROR_STATUS_HOLD_TIME) {
            result.hold_time = ERROR_STATUS_HOLD_TIME;
        }
        ESP_LOGI(TAG,
                 "Command result phase=ui_error detail=\"%s\"",
                 result.detail[0] != '\0' ? result.detail : command_text);
        ui_status_set(UI_STATUS_ERROR, result.detail[0] != '\0' ? result.detail : command_text);
    }

    ESP_LOGI(TAG,
             "Finished command execution for id=%d status=%s elapsed_ms=%lu",
             command_id,
             result.err == ESP_OK ? "ok" : esp_err_to_name(result.err),
             (unsigned long) pdTICKS_TO_MS(xTaskGetTickCount() - execute_start_tick));

    assistant_diag_finish_command(result.err);
    assistant_session_clear_active(rt);
    if (result.err == ESP_OK && result.result_visible_start_tick != 0) {
        TickType_t visible_elapsed = xTaskGetTickCount() - result.result_visible_start_tick;
        result.hold_time = visible_elapsed < result.hold_time ? result.hold_time - visible_elapsed : 0;
    }
    assistant_watchdog_sleep_with_heartbeat(rt, result.hold_time, pdMS_TO_TICKS(ASSISTANT_HEARTBEAT_SLEEP_SLICE_MS));
    assistant_session_restore_idle_ui(rt);
    assistant_session_return_to_standby(rt);
}

/**
 * @brief Initialize the speech model and AFE state for the assistant session tasks.
 * @param rt Shared assistant runtime state that receives initialized model handles.
 * @return ESP_OK on success, or an ESP error code when speech initialization fails.
 */
esp_err_t assistant_session_init_models(assistant_runtime_t *rt) {
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
                                       ASSISTANT_CMD_SET_TIMER,
                                       ASSISTANT_CMD_STOP,
                                       ASSISTANT_CMD_GROUP_BASE);
}

/**
 * @brief Start the assistant audio feed and speech detection tasks.
 * @param rt Shared assistant runtime state passed to both tasks.
 * @return This function does not return a value.
 */
void assistant_session_start_tasks(assistant_runtime_t *rt) {
    xTaskCreatePinnedToCore(audio_feed_task, "audio_feed", 8192, rt, 6, NULL, 0);
    xTaskCreatePinnedToCore(speech_detect_task, "speech_detect", 12288, rt, 5, NULL, 1);
}

/**
 * @brief Continuously read microphone audio and feed it into the AFE pipeline.
 * @param arg Pointer to the shared assistant runtime state.
 * @return This task does not return.
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
                assistant_session_show_status_then_return_to_standby(
                    rt, UI_STATUS_ERROR, "Audio timeout", STATUS_HOLD_TIME);
                fetch_failures = 0;
            }
            continue;
        }
        fetch_failures = 0;
        rt->speech_progress_tick = xTaskGetTickCount();

        if (!rt->assistant_awake) {
            if (rt->direct_command_mode) {
                if (!rt->direct_command_prepared) {
                    rt->multinet->clean(rt->model_data);
                    rt->afe_handle->disable_wakenet(rt->afe_data);
                    rt->direct_command_prepared = true;
                }

                esp_mn_state_t stop_state = rt->multinet->detect(rt->model_data, afe_result->data);
                if (stop_state != ESP_MN_STATE_DETECTED) {
                    continue;
                }

                esp_mn_results_t *stop_result = rt->multinet->get_results(rt->model_data);
                if (stop_result == NULL || stop_result->num <= 0 || stop_result->command_id[0] != ASSISTANT_CMD_STOP) {
                    rt->multinet->clean(rt->model_data);
                    continue;
                }

                rt->assistant_awake = true;
                rt->assistant_awake_tick = xTaskGetTickCount();
                rt->speech_progress_tick = rt->assistant_awake_tick;
                execute_command(rt, ASSISTANT_CMD_STOP, "stop");
                continue;
            }

            if (afe_result->wakeup_state == WAKENET_DETECTED) {
                rt->assistant_awake = true;
                wake_tick = xTaskGetTickCount();
                rt->assistant_awake_tick = wake_tick;
                rt->speech_progress_tick = wake_tick;
                rt->assistant_stage = ASSISTANT_STAGE_LISTENING;
                assistant_diag_capture_wake();
                rt->multinet->clean(rt->model_data);
                rt->afe_handle->disable_wakenet(rt->afe_data);
                rt->direct_command_prepared = false;
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
            assistant_session_show_status_then_return_to_standby(
                rt, UI_STATUS_READY, "No command detected", STATUS_HOLD_TIME);
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
            assistant_session_show_status_then_return_to_standby(
                rt, UI_STATUS_READY, "No command detected", STATUS_HOLD_TIME);
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
            assistant_session_show_status_then_return_to_standby(
                rt, UI_STATUS_ERROR, "Command decode failed", STATUS_HOLD_TIME);
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

        execute_command(rt, command_id, command_text);
    }
}
