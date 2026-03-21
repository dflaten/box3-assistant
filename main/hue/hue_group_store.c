#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "esp_check.h"
#include "esp_log.h"

#include "bsp/esp-box-3.h"

#include "hue/hue_group_store.h"

static const char *TAG = "hue-voice";
static const char *STORE_PATH = BSP_SPIFFS_MOUNT_POINT "/hue_groups.json";

static bool s_store_ready;

/**
 * @brief Mount the group storage backend if it is not already ready.
 * @return ESP_OK on success, or an ESP error code if SPIFFS mount fails.
 */
esp_err_t hue_group_store_init(void)
{
    if (s_store_ready) {
        return ESP_OK;
    }

    esp_err_t err = bsp_spiffs_mount();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to mount storage partition: %s", esp_err_to_name(err));
        return err;
    }

    s_store_ready = true;
    return ESP_OK;
}

/**
 * @brief Load previously saved Hue groups from flash storage.
 * @param groups Output array for loaded group entries.
 * @param max_groups Maximum number of entries that fit in the output array.
 * @param out_count Output for the number of loaded groups written.
 * @return ESP_OK on success, or an ESP error code if storage or parsing fails.
 */
esp_err_t hue_group_store_load(hue_group_t *groups, size_t max_groups, size_t *out_count)
{
    if (out_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_count = 0;

    ESP_RETURN_ON_ERROR(hue_group_store_init(), TAG, "Failed to initialize group storage");

    FILE *file = fopen(STORE_PATH, "r");
    if (file == NULL) {
        ESP_LOGI(TAG, "No stored Hue groups found yet");
        return ESP_OK;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return ESP_FAIL;
    }
    long len = ftell(file);
    if (len < 0) {
        fclose(file);
        return ESP_FAIL;
    }
    rewind(file);

    char *json = calloc((size_t)len + 1, 1);
    if (json == NULL) {
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t read_len = fread(json, 1, (size_t)len, file);
    fclose(file);
    json[read_len] = '\0';

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "Stored Hue groups file is invalid; ignoring it");
        return ESP_FAIL;
    }

    size_t count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, root) {
        if (count >= max_groups) {
            break;
        }
        if (!cJSON_IsObject(item)) {
            continue;
        }

        cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        if (!cJSON_IsString(id) || !cJSON_IsString(name)) {
            continue;
        }

        snprintf(groups[count].id, sizeof(groups[count].id), "%s", id->valuestring);
        snprintf(groups[count].name, sizeof(groups[count].name), "%s", name->valuestring);
        count++;
    }
    cJSON_Delete(root);

    *out_count = count;
    ESP_LOGI(TAG, "Loaded %u stored Hue group(s)", (unsigned)count);
    return ESP_OK;
}

/**
 * @brief Persist the current Hue groups list to flash storage.
 * @param groups The group array to save.
 * @param count The number of valid entries in the group array.
 * @return ESP_OK on success, or an ESP error code if serialization or writing fails.
 */
esp_err_t hue_group_store_save(const hue_group_t *groups, size_t count)
{
    ESP_RETURN_ON_ERROR(hue_group_store_init(), TAG, "Failed to initialize group storage");

    cJSON *root = cJSON_CreateArray();
    if (root == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < count; ++i) {
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) {
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }

        cJSON_AddStringToObject(item, "id", groups[i].id);
        cJSON_AddStringToObject(item, "name", groups[i].name);
        cJSON_AddItemToArray(root, item);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    FILE *file = fopen(STORE_PATH, "w");
    if (file == NULL) {
        cJSON_free(json);
        return ESP_FAIL;
    }

    size_t expected = strlen(json);
    size_t written = fwrite(json, 1, expected, file);
    fclose(file);
    cJSON_free(json);

    if (written != expected) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saved %u Hue group(s) to storage", (unsigned)count);
    return ESP_OK;
}
