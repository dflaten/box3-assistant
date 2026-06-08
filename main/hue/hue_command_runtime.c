#include <stdio.h>
#include <stdlib.h>

#include "flite_g2p.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_mn_speech_commands.h"

#include "commands/assistant_commands.h"
#include "hue/hue_client.h"
#include "hue/hue_command_map.h"
#include "hue/hue_command_runtime.h"
#include "hue/hue_group_store.h"
#include "volume/volume_command_map.h"

static const char *TAG = "hue-voice";

/**
 * @brief Convert and register one spoken phrase in the active MultiNet command table.
 * @param command_id Command id associated with the phrase.
 * @param text Spoken phrase to register.
 * @return ESP_OK on success, or an ESP error code when conversion or registration fails.
 */
static esp_err_t add_runtime_phrase(int command_id, const char *text) {
    char *phonemes = flite_g2p(text, 1);
    if (phonemes == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_mn_commands_phoneme_add(command_id, text, phonemes);
    free(phonemes);
    return err;
}

/**
 * @brief Load saved Hue groups into the assistant runtime.
 * @param rt Shared assistant runtime that receives persisted groups.
 * @return ESP_OK on success, or an ESP error code when persisted groups cannot be loaded.
 */
esp_err_t hue_command_runtime_load_groups(assistant_runtime_t *rt) {
    size_t count = 0;
    ESP_RETURN_ON_ERROR(
        hue_group_store_load(rt->groups, ASSISTANT_MAX_SYNCED_GROUPS, &count), TAG, "Failed to load stored Hue groups");
    rt->group_count = count;
    return ESP_OK;
}

/**
 * @brief Rebuild the MultiNet table with built-in, volume, and dynamic Hue commands.
 * @param rt Shared assistant runtime containing the MultiNet instance and Hue groups.
 * @param sync_command_id Command id for Hue synchronization.
 * @param weather_today_command_id Command id for today's weather.
 * @param weather_tomorrow_command_id Command id for tomorrow's weather.
 * @param set_timer_command_id Command id for starting a timer.
 * @param stop_command_id Command id for stopping an active alarm.
 * @param time_now_command_id Command id for local time.
 * @param time_in_location_command_id Command id for remote time lookup.
 * @param volume_up_command_base First command id in the ten-entry volume increase range.
 * @param volume_down_command_base First command id in the ten-entry volume decrease range.
 * @param group_command_base First command id reserved for dynamic Hue groups.
 * @return ESP_OK on success, or an ESP error code when command registration fails.
 */
esp_err_t hue_command_runtime_rebuild(assistant_runtime_t *rt,
                                      int sync_command_id,
                                      int weather_today_command_id,
                                      int weather_tomorrow_command_id,
                                      int set_timer_command_id,
                                      int stop_command_id,
                                      int time_now_command_id,
                                      int time_in_location_command_id,
                                      int volume_up_command_base,
                                      int volume_down_command_base,
                                      int group_command_base) {
    static const char *number_words[ASSISTANT_CMD_VOLUME_STEP_COUNT] = {
        "one",
        "two",
        "three",
        "four",
        "five",
        "six",
        "seven",
        "eight",
        "nine",
        "ten",
    };
    if (!rt->commands_allocated) {
        ESP_RETURN_ON_ERROR(
            esp_mn_commands_alloc(rt->multinet, rt->model_data), TAG, "Failed to allocate command table");
        rt->commands_allocated = true;
    } else {
        ESP_RETURN_ON_ERROR(esp_mn_commands_clear(), TAG, "Failed to clear command table");
    }

    ESP_RETURN_ON_ERROR(
        add_runtime_phrase(sync_command_id, "update groups from hue"), TAG, "Failed to add sync command");
    ESP_RETURN_ON_ERROR(
        add_runtime_phrase(weather_today_command_id, "weather today"), TAG, "Failed to add weather command");
    ESP_RETURN_ON_ERROR(add_runtime_phrase(weather_tomorrow_command_id, "weather tomorrow"),
                        TAG,
                        "Failed to add tomorrow weather command");
    ESP_RETURN_ON_ERROR(add_runtime_phrase(set_timer_command_id, "set a timer"), TAG, "Failed to add timer command");
    ESP_RETURN_ON_ERROR(add_runtime_phrase(set_timer_command_id, "set timer"), TAG, "Failed to add timer command");
    ESP_RETURN_ON_ERROR(add_runtime_phrase(stop_command_id, "stop"), TAG, "Failed to add stop command");
    ESP_RETURN_ON_ERROR(add_runtime_phrase(time_now_command_id, "what time is it"), TAG, "Failed to add time command");
    ESP_RETURN_ON_ERROR(
        add_runtime_phrase(time_in_location_command_id, "current time in"), TAG, "Failed to add remote time command");
    for (int i = 0; i < ASSISTANT_CMD_VOLUME_STEP_COUNT; ++i) {
        char volume_up_phrase[48];
        char volume_down_phrase[48];
        snprintf(volume_up_phrase, sizeof(volume_up_phrase), "volume up by %s", number_words[i]);
        snprintf(volume_down_phrase, sizeof(volume_down_phrase), "volume down by %s", number_words[i]);
        ESP_RETURN_ON_ERROR(
            add_runtime_phrase(volume_command_id(volume_up_command_base, (uint8_t) (i + 1)), volume_up_phrase),
            TAG,
            "Failed to add volume up command");
        ESP_RETURN_ON_ERROR(
            add_runtime_phrase(volume_command_id(volume_down_command_base, (uint8_t) (i + 1)), volume_down_phrase),
            TAG,
            "Failed to add volume down command");
    }

    for (size_t i = 0; i < rt->group_count; ++i) {
        char on_phrase[96];
        char off_phrase[96];
        snprintf(on_phrase, sizeof(on_phrase), "turn on %s", rt->groups[i].name);
        snprintf(off_phrase, sizeof(off_phrase), "turn off %s", rt->groups[i].name);

        ESP_RETURN_ON_ERROR(add_runtime_phrase(hue_group_command_id(group_command_base, i, true), on_phrase),
                            TAG,
                            "Failed to add on command");
        ESP_RETURN_ON_ERROR(add_runtime_phrase(hue_group_command_id(group_command_base, i, false), off_phrase),
                            TAG,
                            "Failed to add off command");
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

/**
 * @brief Refresh Hue groups, persist them, and rebuild the MultiNet command table.
 * @param rt Shared assistant runtime that receives synchronized groups.
 * @param sync_command_id Command id for Hue synchronization.
 * @param weather_today_command_id Command id for today's weather.
 * @param weather_tomorrow_command_id Command id for tomorrow's weather.
 * @param set_timer_command_id Command id for starting a timer.
 * @param stop_command_id Command id for stopping an active alarm.
 * @param time_now_command_id Command id for local time.
 * @param time_in_location_command_id Command id for remote time lookup.
 * @param volume_up_command_base First command id in the ten-entry volume increase range.
 * @param volume_down_command_base First command id in the ten-entry volume decrease range.
 * @param group_command_base First command id reserved for dynamic Hue groups.
 * @return ESP_OK on success, or an ESP error code when synchronization, persistence, or rebuilding fails.
 */
esp_err_t hue_command_runtime_sync_groups(assistant_runtime_t *rt,
                                          int sync_command_id,
                                          int weather_today_command_id,
                                          int weather_tomorrow_command_id,
                                          int set_timer_command_id,
                                          int stop_command_id,
                                          int time_now_command_id,
                                          int time_in_location_command_id,
                                          int volume_up_command_base,
                                          int volume_down_command_base,
                                          int group_command_base) {
    size_t synced_count = 0;
    ESP_RETURN_ON_ERROR(hue_client_sync_groups(rt->groups, ASSISTANT_MAX_SYNCED_GROUPS, &synced_count),
                        TAG,
                        "Failed to sync Hue groups");
    rt->group_count = synced_count;

    ESP_RETURN_ON_ERROR(hue_group_store_save(rt->groups, rt->group_count), TAG, "Failed to save Hue groups");
    ESP_RETURN_ON_ERROR(hue_command_runtime_rebuild(rt,
                                                    sync_command_id,
                                                    weather_today_command_id,
                                                    weather_tomorrow_command_id,
                                                    set_timer_command_id,
                                                    stop_command_id,
                                                    time_now_command_id,
                                                    time_in_location_command_id,
                                                    volume_up_command_base,
                                                    volume_down_command_base,
                                                    group_command_base),
                        TAG,
                        "Failed to rebuild command table after Hue sync");

    ESP_LOGI(TAG, "Synced %u usable Hue group(s)", (unsigned) rt->group_count);
    return ESP_OK;
}
