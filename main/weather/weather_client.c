#include "weather/weather_client.h"

#include "weather/weather_open_meteo_provider.h"

static const weather_provider_t *weather_client_active_provider(void) {
    return weather_open_meteo_provider_get();
}

const char *weather_client_provider_name(void) {
    const weather_provider_t *provider = weather_client_active_provider();
    return (provider != NULL && provider->name != NULL) ? provider->name : "Weather";
}

esp_err_t weather_client_fetch_today(weather_report_t *out_report) {
    const weather_provider_t *provider = weather_client_active_provider();
    return (provider != NULL && provider->fetch_forecast != NULL)
             ? provider->fetch_forecast(WEATHER_FORECAST_TODAY, out_report)
             : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t weather_client_fetch_tomorrow(weather_report_t *out_report) {
    const weather_provider_t *provider = weather_client_active_provider();
    return (provider != NULL && provider->fetch_forecast != NULL)
             ? provider->fetch_forecast(WEATHER_FORECAST_TOMORROW, out_report)
             : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t weather_client_cancel_active_request(void) {
    const weather_provider_t *provider = weather_client_active_provider();
    return (provider != NULL && provider->cancel_active_request != NULL) ? provider->cancel_active_request()
                                                                         : ESP_ERR_NOT_SUPPORTED;
}
