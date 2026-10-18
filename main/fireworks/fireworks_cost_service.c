#include "fireworks/fireworks_cost_service.h"

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "board/ui_status.h"
#include "fireworks/fireworks_cost_client.h"
#include "fireworks/fireworks_cost_parse.h"

#define FIREWORKS_COST_TASK_STACK    8192
#define FIREWORKS_COST_TASK_PRIORITY 2
#define FIREWORKS_COST_RETRY_SECONDS 60

static const char *TAG = "fireworks-cost";

/**
 * @brief Periodically refresh the monthly Fireworks cost shown on the assistant display.
 * @param arg Unused FreeRTOS task parameter.
 * @return This task does not return.
 * @note Failed refreshes retry sooner than the configured interval so the display recovers after boot or a drop.
 */
static void fireworks_cost_task(void *arg) {
    (void) arg;

    while (true) {
        uint32_t delay_seconds = CONFIG_FIREWORKS_COST_REFRESH_SECONDS;

        uint32_t cents = 0;
        esp_err_t err = fireworks_cost_client_fetch_month_cents(&cents);
        if (err == ESP_OK) {
            char text[FIREWORKS_COST_TEXT_LEN];
            fireworks_cost_format(cents, text, sizeof(text));
            ESP_LOGI(TAG, "Monthly spend: %s", text);
            ui_status_set_cost_text(text);
        } else {
            ESP_LOGW(TAG, "Cost refresh failed: %s", esp_err_to_name(err));
            delay_seconds = FIREWORKS_COST_RETRY_SECONDS;
        }

        vTaskDelay(pdMS_TO_TICKS(delay_seconds * 1000U));
    }
}

void fireworks_cost_service_start(void) {
    if (!fireworks_cost_client_is_configured()) {
        ESP_LOGI(TAG, "Fireworks cost display disabled or unconfigured");
        return;
    }

    xTaskCreate(
        fireworks_cost_task, "fireworks_cost", FIREWORKS_COST_TASK_STACK, NULL, FIREWORKS_COST_TASK_PRIORITY, NULL);
}
