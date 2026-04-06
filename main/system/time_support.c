#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"

#include "system/time_support.h"

static const char *TAG = "hue-voice";
static const time_t MIN_VALID_UNIX_TIME = 1704067200; // 2024-01-01 00:00:00 UTC

static bool s_time_support_initialized;

/**
 * @brief Convert a weather/API timezone name into a POSIX TZ string for newlib.
 * @param timezone_name IANA-style timezone identifier from config.
 * @return A POSIX TZ string understood by tzset(), or the original input as fallback.
 */
static const char *timezone_to_posix(const char *timezone_name)
{
    if (timezone_name == NULL || timezone_name[0] == '\0') {
        return "UTC0";
    }

    if (strcmp(timezone_name, "America/New_York") == 0) {
        return "EST5EDT,M3.2.0,M11.1.0";
    }
    if (strcmp(timezone_name, "America/Chicago") == 0) {
        return "CST6CDT,M3.2.0,M11.1.0";
    }
    if (strcmp(timezone_name, "America/Denver") == 0) {
        return "MST7MDT,M3.2.0,M11.1.0";
    }
    if (strcmp(timezone_name, "America/Phoenix") == 0) {
        return "MST7";
    }
    if (strcmp(timezone_name, "America/Los_Angeles") == 0) {
        return "PST8PDT,M3.2.0,M11.1.0";
    }
    if (strcmp(timezone_name, "UTC") == 0 || strcmp(timezone_name, "Etc/UTC") == 0) {
        return "UTC0";
    }

    return timezone_name;
}

esp_err_t time_support_init(void)
{
    if (s_time_support_initialized) {
        return ESP_OK;
    }

    const char *posix_tz = timezone_to_posix(CONFIG_WEATHER_TIMEZONE);
    if (setenv("TZ", posix_tz, 1) != 0) {
        ESP_LOGE(TAG, "Failed to set timezone to %s", posix_tz);
        return ESP_FAIL;
    }
    tzset();

    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.google.com");
    sntp_config.wait_for_sync = false;

    esp_err_t err = esp_netif_sntp_init(&sntp_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SNTP: %s", esp_err_to_name(err));
        return err;
    }

    s_time_support_initialized = true;
    ESP_LOGI(TAG, "Time sync started for timezone %s (TZ=%s)", CONFIG_WEATHER_TIMEZONE, posix_tz);
    return ESP_OK;
}

bool time_support_is_synced(void)
{
    time_t now = 0;
    time(&now);
    return now >= MIN_VALID_UNIX_TIME;
}

bool time_support_format_now(char *time_buffer,
                             size_t time_buffer_size,
                             char *date_buffer,
                             size_t date_buffer_size)
{
    if (time_buffer == NULL || date_buffer == NULL || time_buffer_size == 0 || date_buffer_size == 0) {
        return false;
    }

    time_buffer[0] = '\0';
    date_buffer[0] = '\0';

    if (!time_support_is_synced()) {
        return false;
    }

    time_t now = 0;
    struct tm local_time = { 0 };
    time(&now);
    localtime_r(&now, &local_time);

    if (strftime(time_buffer, time_buffer_size, "%I:%M %p", &local_time) == 0 ||
        strftime(date_buffer, date_buffer_size, "%a %b %d", &local_time) == 0) {
        return false;
    }

    if (time_buffer[0] == '0' && time_buffer[1] != '\0') {
        memmove(time_buffer, time_buffer + 1, strlen(time_buffer));
    }

    return true;
}
