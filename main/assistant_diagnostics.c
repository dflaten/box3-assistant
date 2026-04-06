#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"

#include "assistant_diagnostics.h"

#define ASSISTANT_DIAG_NAMESPACE "assistant_diag"
#define ASSISTANT_DIAG_KEY       "record"
#define ASSISTANT_DIAG_MAGIC     0x41444947u
#define ASSISTANT_DIAG_VERSION   1u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    assistant_diag_record_t record;
} assistant_diag_blob_t;

static const char *TAG = "hue-voice";

static SemaphoreHandle_t s_diag_mutex;
static assistant_diag_record_t s_record;
static assistant_diag_record_t s_previous_record;
static bool s_have_previous_record;
static esp_reset_reason_t s_previous_reset_reason;

static const char *assistant_diag_detail_name(uint8_t detail_stage)
{
    switch ((assistant_diag_detail_t)detail_stage) {
    case ASSISTANT_DIAG_DETAIL_WAKE:
        return "wake";
    case ASSISTANT_DIAG_DETAIL_EXEC_START:
        return "exec_start";
    case ASSISTANT_DIAG_DETAIL_WEATHER_START:
        return "weather_start";
    case ASSISTANT_DIAG_DETAIL_WEATHER_ATTEMPT:
        return "weather_attempt";
    case ASSISTANT_DIAG_DETAIL_WEATHER_PARSE:
        return "weather_parse";
    case ASSISTANT_DIAG_DETAIL_WEATHER_DONE:
        return "weather_done";
    case ASSISTANT_DIAG_DETAIL_HUE_REQUEST:
        return "hue_request";
    case ASSISTANT_DIAG_DETAIL_TIMEOUT:
        return "timeout";
    case ASSISTANT_DIAG_DETAIL_FINISH:
        return "finish";
    case ASSISTANT_DIAG_DETAIL_NONE:
    default:
        return "none";
    }
}

static const char *assistant_diag_reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_UNKNOWN:
        return "unknown";
    case ESP_RST_POWERON:
        return "poweron";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "int_wdt";
    case ESP_RST_TASK_WDT:
        return "task_wdt";
    case ESP_RST_WDT:
        return "wdt";
    case ESP_RST_DEEPSLEEP:
        return "deepsleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "sdio";
    case ESP_RST_USB:
        return "usb";
    case ESP_RST_JTAG:
        return "jtag";
    case ESP_RST_EFUSE:
        return "efuse";
    case ESP_RST_PWR_GLITCH:
        return "pwr_glitch";
    case ESP_RST_CPU_LOCKUP:
        return "cpu_lockup";
    default:
        return "other";
    }
}

static esp_err_t assistant_diag_store_locked(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ASSISTANT_DIAG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    assistant_diag_blob_t blob = {
        .magic = ASSISTANT_DIAG_MAGIC,
        .version = ASSISTANT_DIAG_VERSION,
        .size = sizeof(blob),
        .record = s_record,
    };

    err = nvs_set_blob(handle, ASSISTANT_DIAG_KEY, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static void assistant_diag_capture_memory_locked(void)
{
    s_record.uptime_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    s_record.free_heap_bytes = (uint32_t)esp_get_free_heap_size();
    s_record.largest_free_block_bytes = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
}

static void assistant_diag_write_locked(uint8_t assistant_stage,
                                        assistant_diag_detail_t detail_stage,
                                        int32_t weather_day,
                                        uint32_t attempt,
                                        esp_err_t last_err,
                                        bool active,
                                        bool timed_out)
{
    s_record.assistant_stage = assistant_stage;
    s_record.detail_stage = (uint8_t)detail_stage;
    s_record.weather_day = weather_day;
    s_record.attempt = attempt;
    s_record.last_err = last_err;
    s_record.active = active;
    s_record.timed_out = timed_out;
    assistant_diag_capture_memory_locked();
    esp_err_t err = assistant_diag_store_locked();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to persist assistant diagnostics: %s", esp_err_to_name(err));
    }
}

esp_err_t assistant_diag_init(void)
{
    if (s_diag_mutex == NULL) {
        s_diag_mutex = xSemaphoreCreateMutex();
        if (s_diag_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (xSemaphoreTake(s_diag_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    s_previous_reset_reason = esp_reset_reason();
    s_have_previous_record = false;
    memset(&s_previous_record, 0, sizeof(s_previous_record));
    memset(&s_record, 0, sizeof(s_record));

    nvs_handle_t handle;
    esp_err_t err = nvs_open(ASSISTANT_DIAG_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        assistant_diag_blob_t blob = { 0 };
        size_t blob_size = sizeof(blob);
        err = nvs_get_blob(handle, ASSISTANT_DIAG_KEY, &blob, &blob_size);
        if (err == ESP_OK &&
            blob_size == sizeof(blob) &&
            blob.magic == ASSISTANT_DIAG_MAGIC &&
            blob.version == ASSISTANT_DIAG_VERSION &&
            blob.size == sizeof(blob)) {
            s_previous_record = blob.record;
            s_have_previous_record = true;
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to load assistant diagnostics: %s", esp_err_to_name(err));
        }
        nvs_close(handle);
    }

    assistant_diag_write_locked(0, ASSISTANT_DIAG_DETAIL_NONE, -1, 0, ESP_OK, false, false);
    xSemaphoreGive(s_diag_mutex);
    return ESP_OK;
}

void assistant_diag_capture_wake(void)
{
    if (s_diag_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(s_diag_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    assistant_diag_write_locked(0, ASSISTANT_DIAG_DETAIL_WAKE, -1, 0, ESP_OK, false, false);
    xSemaphoreGive(s_diag_mutex);
}

void assistant_diag_start_command(int command_id, uint8_t assistant_stage)
{
    if (s_diag_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(s_diag_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_record.command_id = command_id;
    assistant_diag_write_locked(assistant_stage, ASSISTANT_DIAG_DETAIL_EXEC_START, -1, 0, ESP_OK, true, false);
    xSemaphoreGive(s_diag_mutex);
}

void assistant_diag_update_detail(uint8_t assistant_stage,
                                  assistant_diag_detail_t detail_stage,
                                  int32_t weather_day,
                                  uint32_t attempt,
                                  esp_err_t last_err)
{
    if (s_diag_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(s_diag_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    assistant_diag_write_locked(assistant_stage, detail_stage, weather_day, attempt, last_err, s_record.active, s_record.timed_out);
    xSemaphoreGive(s_diag_mutex);
}

void assistant_diag_mark_timeout(uint8_t assistant_stage)
{
    if (s_diag_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(s_diag_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    assistant_diag_write_locked(assistant_stage,
                                ASSISTANT_DIAG_DETAIL_TIMEOUT,
                                s_record.weather_day,
                                s_record.attempt,
                                ESP_ERR_TIMEOUT,
                                true,
                                true);
    xSemaphoreGive(s_diag_mutex);
}

void assistant_diag_finish_command(esp_err_t last_err)
{
    if (s_diag_mutex == NULL) {
        return;
    }
    if (xSemaphoreTake(s_diag_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    assistant_diag_write_locked(0,
                                ASSISTANT_DIAG_DETAIL_FINISH,
                                s_record.weather_day,
                                s_record.attempt,
                                last_err,
                                false,
                                s_record.timed_out);
    xSemaphoreGive(s_diag_mutex);
}

bool assistant_diag_format_previous_issue(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0 || !s_have_previous_record) {
        return false;
    }

    const assistant_diag_record_t *record = &s_previous_record;
    bool suspicious_reset = s_previous_reset_reason == ESP_RST_PANIC ||
                            s_previous_reset_reason == ESP_RST_INT_WDT ||
                            s_previous_reset_reason == ESP_RST_TASK_WDT ||
                            s_previous_reset_reason == ESP_RST_WDT;
    bool notable = record->timed_out || record->active || suspicious_reset;
    if (!notable) {
        return false;
    }

    const char *prefix = record->timed_out ? "Prev timeout" : "Prev reset";
    if (record->command_id == 2) {
        snprintf(buffer, buffer_size, "%s weather today", prefix);
    } else if (record->command_id == 3) {
        snprintf(buffer, buffer_size, "%s weather tomorrow", prefix);
    } else if (record->command_id == 1) {
        snprintf(buffer, buffer_size, "%s sync groups", prefix);
    } else if (record->command_id >= 100) {
        snprintf(buffer, buffer_size, "%s hue command", prefix);
    } else if (record->active) {
        snprintf(buffer, buffer_size, "%s cmd %ld", prefix, (long)record->command_id);
    } else {
        snprintf(buffer, buffer_size, "Prev reboot %s", assistant_diag_reset_reason_name(s_previous_reset_reason));
    }
    return true;
}

void assistant_diag_log_previous_issue(void)
{
    if (!s_have_previous_record) {
        ESP_LOGI(TAG, "No previous assistant diagnostics found");
        return;
    }

    ESP_LOGI(TAG,
             "Previous assistant state: reset=%s active=%s timeout=%s command_id=%ld detail=%s err=%s attempt=%lu uptime_ms=%lu free_heap=%lu largest_block=%lu",
             assistant_diag_reset_reason_name(s_previous_reset_reason),
             s_previous_record.active ? "true" : "false",
             s_previous_record.timed_out ? "true" : "false",
             (long)s_previous_record.command_id,
             assistant_diag_detail_name(s_previous_record.detail_stage),
             esp_err_to_name((esp_err_t)s_previous_record.last_err),
             (unsigned long)s_previous_record.attempt,
             (unsigned long)s_previous_record.uptime_ms,
             (unsigned long)s_previous_record.free_heap_bytes,
             (unsigned long)s_previous_record.largest_free_block_bytes);
}

esp_reset_reason_t assistant_diag_previous_reset_reason(void)
{
    return s_previous_reset_reason;
}
