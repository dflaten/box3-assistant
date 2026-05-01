#pragma once

#include "esp_err.h"

#include "assistant_runtime.h"

esp_err_t hue_command_runtime_load_groups(assistant_runtime_t *rt);
esp_err_t hue_command_runtime_rebuild(assistant_runtime_t *rt,
                                      int sync_command_id,
                                      int weather_today_command_id,
                                      int weather_tomorrow_command_id,
                                      int set_timer_command_id,
                                      int stop_command_id,
                                      int group_command_base);
esp_err_t hue_command_runtime_sync_groups(assistant_runtime_t *rt,
                                          int sync_command_id,
                                          int weather_today_command_id,
                                          int weather_tomorrow_command_id,
                                          int set_timer_command_id,
                                          int stop_command_id,
                                          int group_command_base);
