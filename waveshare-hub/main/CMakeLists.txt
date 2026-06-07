idf_component_register(
    SRCS
        "main.cpp"
        "lvgl_port.cpp"
        "wifi_ap.cpp"
        "stream_client.cpp"
        "jpeg_decoder.cpp"
        "weather.cpp"
        "ui_login.cpp"
        "ui_dashboard.cpp"
        "ui_settings.cpp"
        "ui_toast.cpp"
        "hmac_auth.cpp"
        "power_mgmt.cpp"
        "settings.cpp"
        "i18n.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_lcd
        esp_wifi
        esp_event
        esp_netif
        esp_http_client
        nvs_flash
        driver
        freertos
        esp_timer
        esp_system
        log
        lvgl__lvgl
        espressif__esp_lcd_touch
        espressif__esp_lcd_touch_gt911
        mbedtls
        lwip
)

target_compile_options(__idf_main PRIVATE -Wno-error=missing-field-initializers -Wno-error=reorder)
