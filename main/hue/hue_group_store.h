#pragma once

#include <stddef.h>

#include "esp_err.h"

#include "hue/hue_group.h"

esp_err_t hue_group_store_init(void);
esp_err_t hue_group_store_load(hue_group_t *groups, size_t max_groups, size_t *out_count);
esp_err_t hue_group_store_save(const hue_group_t *groups, size_t count);
