#pragma once

#include "freertos/FreeRTOS.h"

#include "esp_err.h"

#include "assistant_runtime.h"
#include "board/ui_status.h"

/**
 * @brief Convert an assistant stage enum to the matching diagnostic label.
 * @param stage Assistant stage to format.
 * @return Pointer to a static string naming the supplied stage.
 */
const char *assistant_session_stage_name(assistant_stage_t stage);

/**
 * @brief Clear the active wake/listen/execute session bookkeeping.
 * @param rt Runtime state whose watchdog-visible session fields should be reset.
 * @return This function does not return a value.
 */
void assistant_session_clear_active(assistant_runtime_t *rt);

/**
 * @brief Restore the correct idle screen after command execution or recovery completes.
 * @param rt Shared assistant runtime state used to decide which idle UI should be shown.
 * @return This function does not return a value.
 */
void assistant_session_restore_idle_ui(assistant_runtime_t *rt);

/**
 * @brief Reset the assistant pipeline and return to standby mode.
 * @param rt Shared assistant runtime state to reset back to standby.
 * @return This function does not return a value.
 */
void assistant_session_return_to_standby(assistant_runtime_t *rt);

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
                                                          TickType_t hold_time);

/**
 * @brief Initialize the speech model and AFE state for the assistant session tasks.
 * @param rt Shared assistant runtime state that receives initialized model handles.
 * @return ESP_OK on success, or an ESP error code when speech initialization fails.
 */
esp_err_t assistant_session_init_models(assistant_runtime_t *rt);

/**
 * @brief Start the assistant audio feed and speech detection tasks.
 * @param rt Shared assistant runtime state passed to both tasks.
 * @return This function does not return a value.
 */
void assistant_session_start_tasks(assistant_runtime_t *rt);
