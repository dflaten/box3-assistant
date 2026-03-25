#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t wifi_init_sta(void);
bool wifi_is_connected(void);
