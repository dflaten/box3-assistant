idf_component_register(
    SRCS
        "commands/assistant_command_dispatch.c"
        "commands/assistant_command_text.c"
        "board/board_audio.c"
        "board/ui_status.c"
        "assistant_diagnostics.c"
        "assistant_state.c"
        "box3_assistant.c"
        "hue/hue_command_map.c"
        "hue/hue_command_runtime.c"
        "hue/hue_client.c"
        "hue/hue_group_store.c"
        "system/time_support.c"
        "system/wifi_support.c"
        "weather/weather_client.c"
        "weather/weather_format.c"
    INCLUDE_DIRS "."
    REQUIRES espressif__esp-sr esp_http_client nvs_flash esp_netif esp_event esp_wifi esp_lcd json mbedtls espressif__esp-box-3_noglib
)
