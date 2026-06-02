#include "time/time_client.h"

#include <stdio.h>
#include <time.h>

#include "esp_err.h"

#include "system/time_support.h"
#include "time/time_format.h"
#include "time/time_open_meteo_provider.h"

/**
 * @brief Get the configured time lookup provider.
 * @return Pointer to the active provider descriptor.
 */
static const time_provider_t *time_client_active_provider(void) {
    return time_open_meteo_provider_get();
}

/**
 * @brief Resolve a location query and format the current local time there.
 * @param query User-provided city, region, or country query.
 * @param out_report Output receiving the formatted time report.
 * @return ESP_OK on success, or an ESP error code if lookup or formatting fails.
 */
esp_err_t time_client_fetch_location_time(const char *query, time_report_t *out_report) {
    if (query == NULL || query[0] == '\0' || out_report == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!time_support_is_synced()) {
        return ESP_ERR_INVALID_STATE;
    }

    const time_provider_t *provider = time_client_active_provider();
    if (provider == NULL || provider->fetch_location == NULL || provider->fetch_utc_offset == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    *out_report = (time_report_t) {0};
    time_location_t location = {0};
    esp_err_t err = provider->fetch_location(query, &location);
    if (err != ESP_OK) {
        return err;
    }

    int32_t utc_offset_seconds = 0;
    err = provider->fetch_utc_offset(location.latitude, location.longitude, &utc_offset_seconds);
    if (err != ESP_OK) {
        return err;
    }

    time_t now = 0;
    time(&now);
    if (!time_format_from_utc_offset(now,
                                     utc_offset_seconds,
                                     out_report->time_text,
                                     sizeof(out_report->time_text),
                                     out_report->date_text,
                                     sizeof(out_report->date_text))) {
        return ESP_FAIL;
    }

    snprintf(out_report->location, sizeof(out_report->location), "%s", location.location);
    snprintf(out_report->timezone, sizeof(out_report->timezone), "%s", location.timezone);
    return ESP_OK;
}

/**
 * @brief Cancel the active provider request if one is in progress.
 * @return ESP_OK when cancellation is requested, or an ESP error code when no provider request can be cancelled.
 */
esp_err_t time_client_cancel_active_request(void) {
    const time_provider_t *provider = time_client_active_provider();
    return (provider != NULL && provider->cancel_active_request != NULL) ? provider->cancel_active_request()
                                                                         : ESP_ERR_NOT_SUPPORTED;
}
