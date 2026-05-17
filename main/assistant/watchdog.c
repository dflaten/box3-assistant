#include "assistant/watchdog.h"

#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"

#include "assistant/session.h"
#include "assistant_diagnostics.h"
#include "assistant_state.h"
#include "board/ui_status.h"
#include "net/request_cancel.h"
#include "hue/hue_client.h"
#include "stt/local_stt_client.h"
#include "tts/tts_player.h"
#include "weather/weather_client.h"

#define ASSISTANT_SESSION_TIMEOUT_MS         30000
#define ASSISTANT_LISTENING_STALL_TIMEOUT_MS 12000
#define ASSISTANT_WATCHDOG_POLL_MS           1000
#define ASSISTANT_EXECUTION_CANCEL_GRACE_MS  5000
#define ASSISTANT_TASK_HEARTBEAT_TIMEOUT_MS  15000

static const char *TAG = "assistant-watchdog";

static void assistant_session_timeout_task(void *arg);

/**
 * @brief Sleep while continuing to publish the speech-detect heartbeat.
 * @param rt Runtime state whose speech heartbeat should be refreshed during the delay.
 * @param duration Total delay duration to wait.
 * @param slice_duration Maximum single sleep slice before the heartbeat is refreshed again.
 * @return This function does not return a value.
 */
void assistant_watchdog_sleep_with_heartbeat(assistant_runtime_t *rt, TickType_t duration, TickType_t slice_duration) {
    TickType_t remaining = duration;

    while (remaining > 0) {
        TickType_t slice = remaining > slice_duration ? slice_duration : remaining;
        if (rt != NULL) {
            rt->speech_detect_heartbeat_tick = xTaskGetTickCount();
        }
        vTaskDelay(slice);
        remaining -= slice;
    }
}

/**
 * @brief Start the assistant watchdog task.
 * @param rt Shared assistant runtime state monitored by the watchdog.
 * @return This function does not return a value.
 */
void assistant_watchdog_start_task(assistant_runtime_t *rt) {
    xTaskCreate(assistant_session_timeout_task, "assistant_session_timeout", 4096, rt, 4, NULL);
}

/**
 * @brief Watch for assistant sessions that stay awake too long and force recovery.
 * @param arg Pointer to the shared assistant runtime state.
 * @return This task does not return.
 */
static void assistant_session_timeout_task(void *arg) {
    assistant_runtime_t *rt = (assistant_runtime_t *) arg;
    static const request_cancel_fn_t cancel_fns[] = {
        weather_client_cancel_active_request,
        hue_client_cancel_active_request,
        local_stt_client_cancel_active_request,
        tts_player_cancel,
    };

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
                     assistant_session_stage_name(rt->assistant_stage),
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
                    esp_err_t cancel_err = request_cancel_first_active(cancel_fns, 4);
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
                     assistant_session_stage_name(rt->assistant_stage));
            esp_restart();
        }
    }
}
