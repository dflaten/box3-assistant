#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_mn_iface.h"

#include "hue/hue_group.h"

#define ASSISTANT_MAX_SYNCED_GROUPS HUE_GROUP_MAX_COUNT

/**
 * @brief Command recognition and Hue group runtime data.
 */
typedef struct {
    /** True after the dynamic MultiNet command table has been allocated at least once. */
    bool commands_allocated;
    /** MultiNet interface selected from the ESP-SR model bundle. */
    esp_mn_iface_t *multinet;
    /** Opaque model instance owned by the selected MultiNet interface. */
    model_iface_data_t *model_data;
    /** Runtime Hue groups currently available for spoken on/off commands. */
    hue_group_t groups[ASSISTANT_MAX_SYNCED_GROUPS];
    /** Number of valid entries currently stored in groups[]. */
    size_t group_count;
} assistant_command_runtime_t;
