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

static const char *TAG = "hue-voice";

static esp_err_t add_runtime_phrase(int command_id, const char *text) {
    char *phonemes = flite_g2p(text, 1);
    if (phonemes == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_mn_commands_phoneme_add(command_id, text, phonemes);
    free(phonemes);
    return err;
}

esp_err_t hue_command_runtime_load_groups(assistant_runtime_t *rt) {
    size_t count = 0;
    ESP_RETURN_ON_ERROR(
        hue_group_store_load(rt->groups, ASSISTANT_MAX_SYNCED_GROUPS, &count), TAG, "Failed to load stored Hue groups");
    rt->group_count = count;
    return ESP_OK;
}

esp_err_t hue_command_runtime_rebuild(assistant_runtime_t *rt,
                                      int sync_command_id,
                                      int weather_today_command_id,
                                      int weather_tomorrow_command_id,
                                      int group_command_base) {
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

esp_err_t hue_command_runtime_sync_groups(assistant_runtime_t *rt,
                                          int sync_command_id,
                                          int weather_today_command_id,
                                          int weather_tomorrow_command_id,
                                          int group_command_base) {
    size_t synced_count = 0;
    ESP_RETURN_ON_ERROR(hue_client_sync_groups(rt->groups, ASSISTANT_MAX_SYNCED_GROUPS, &synced_count),
                        TAG,
                        "Failed to sync Hue groups");
    rt->group_count = synced_count;

    ESP_RETURN_ON_ERROR(hue_group_store_save(rt->groups, rt->group_count), TAG, "Failed to save Hue groups");
    ESP_RETURN_ON_ERROR(
        hue_command_runtime_rebuild(
            rt, sync_command_id, weather_today_command_id, weather_tomorrow_command_id, group_command_base),
        TAG,
        "Failed to rebuild command table after Hue sync");

    ESP_LOGI(TAG, "Synced %u usable Hue group(s)", (unsigned) rt->group_count);
    return ESP_OK;
}
