#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "hue/hue_group.h"

esp_err_t hue_client_set_group_by_id(const char *group_id, bool on);
esp_err_t hue_client_sync_groups(hue_group_t *groups, size_t max_groups, size_t *out_count);
