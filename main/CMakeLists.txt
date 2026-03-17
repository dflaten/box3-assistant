idf_component_register(
    SRCS
        "board_audio.c"
        "box3_assistant.c"
        "hue_client.c"
        "ui_status.c"
        "wifi_support.c"
    INCLUDE_DIRS "."
    REQUIRES espressif__esp-sr esp_http_client nvs_flash esp_netif esp_event esp_wifi esp_lcd espressif__esp-box-3_noglib
)
