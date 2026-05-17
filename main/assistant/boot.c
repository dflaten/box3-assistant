#include "assistant/boot.h"

#include "freertos/task.h"

#include "nvs_flash.h"

#include "esp_check.h"
#include "esp_log.h"

#include "assistant/session.h"
#include "assistant/presence.h"
#include "assistant/watchdog.h"
#include "assistant_diagnostics.h"
#include "board/board_audio.h"
#include "board/ui_status.h"
#include "commands/assistant_commands.h"
#include "hue/hue_client.h"
#include "hue/hue_command_handler.h"
#include "hue/hue_command_runtime.h"
#include "hue/hue_group_store.h"
#include "system/time_support.h"
#include "system/wifi_support.h"
#include "timer/timer_runtime.h"

static const TickType_t BRIDGE_ERROR_HOLD_TIME = pdMS_TO_TICKS(4000);
static const char *TAG = "assistant-boot";

/**
 * @brief Initialize the shared assistant runtime to its boot-time defaults.
 * @param rt Runtime state instance to initialize.
 * @param startup_tick Tick count captured at startup for initial heartbeat timestamps.
 * @return This function does not return a value.
 */
void assistant_boot_prepare_runtime(assistant_runtime_t *rt, TickType_t startup_tick) {
    if (rt == NULL) {
        return;
    }

    rt->assistant_stage = ASSISTANT_STAGE_STANDBY;
    rt->audio_feed_heartbeat_tick = startup_tick;
    rt->speech_detect_heartbeat_tick = startup_tick;
    rt->presence_clock_heartbeat_tick = startup_tick;
    rt->last_presence_motion_tick = 0;
    rt->speech_progress_tick = startup_tick;
    timer_runtime_reset(&rt->timer);
}

/**
 * @brief Initialize firmware subsystems and launch the assistant runtime.
 * @return This function does not return a value.
 */
void assistant_boot_start(void) {
    static assistant_runtime_t runtime;
    assistant_runtime_t *rt = &runtime;
    TickType_t startup_tick = xTaskGetTickCount();
    assistant_boot_prepare_runtime(rt, startup_tick);

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
        char hue_detail[ASSISTANT_COMMAND_RESULT_DETAIL_LEN];
        hue_command_handler_format_probe_error(hue_probe_err, hue_detail, sizeof(hue_detail));
        ESP_LOGW(TAG, "Hue bridge probe failed: %s", esp_err_to_name(hue_probe_err));
        ui_status_set(UI_STATUS_ERROR, hue_detail);
        vTaskDelay(BRIDGE_ERROR_HOLD_TIME);
    }

    ui_status_set(UI_STATUS_BOOTING, "Loading speech models");
    ESP_ERROR_CHECK(assistant_session_init_models(rt));

    ui_status_set(UI_STATUS_BOOTING, "Updating Hue groups");
    esp_err_t sync_err = (hue_probe_err == ESP_OK) ? hue_command_runtime_sync_groups(rt,
                                                                                     ASSISTANT_CMD_SYNC_GROUPS,
                                                                                     ASSISTANT_CMD_WEATHER_TODAY,
                                                                                     ASSISTANT_CMD_WEATHER_TOMORROW,
                                                                                     ASSISTANT_CMD_SET_TIMER,
                                                                                     ASSISTANT_CMD_STOP,
                                                                                     ASSISTANT_CMD_GROUP_BASE)
                                                   : hue_probe_err;
    if (sync_err != ESP_OK) {
        char hue_detail[ASSISTANT_COMMAND_RESULT_DETAIL_LEN];
        if (hue_probe_err != ESP_OK) {
            hue_command_handler_format_probe_error(hue_probe_err, hue_detail, sizeof(hue_detail));
        } else {
            hue_command_handler_format_request_error("Hue sync failed", sync_err, hue_detail, sizeof(hue_detail));
        }
        ESP_LOGW(TAG, "Boot-time Hue sync failed: %s", esp_err_to_name(sync_err));
        ui_status_set(UI_STATUS_ERROR, hue_detail);
        vTaskDelay(BRIDGE_ERROR_HOLD_TIME);
        ESP_ERROR_CHECK(hue_command_runtime_rebuild(rt,
                                                    ASSISTANT_CMD_SYNC_GROUPS,
                                                    ASSISTANT_CMD_WEATHER_TODAY,
                                                    ASSISTANT_CMD_WEATHER_TOMORROW,
                                                    ASSISTANT_CMD_SET_TIMER,
                                                    ASSISTANT_CMD_STOP,
                                                    ASSISTANT_CMD_GROUP_BASE));
    }

    ESP_ERROR_CHECK(board_audio_init_microphone(&rt->mic_codec));
    ESP_ERROR_CHECK(board_audio_init_speaker());
    ESP_ERROR_CHECK(assistant_presence_init());

    ui_status_set(UI_STATUS_READY, NULL);

    assistant_session_start_tasks(rt);
    assistant_watchdog_start_task(rt);
    assistant_presence_start_task(rt);
}
