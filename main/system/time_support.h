#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

esp_err_t time_support_init(void);
bool time_support_is_synced(void);
bool time_support_format_now(char *time_buffer, size_t time_buffer_size, char *date_buffer, size_t date_buffer_size);
