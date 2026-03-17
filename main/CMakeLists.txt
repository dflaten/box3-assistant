idf_component_register(
    SRCS "hue-controls.c"
    INCLUDE_DIRS "."
    REQUIRES espressif__esp-sr esp_http_client nvs_flash esp_netif esp_event esp_wifi esp_lcd espressif__esp-box-3_noglib
)
