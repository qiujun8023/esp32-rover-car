idf_component_register(
    SRCS
        "main.c"
        "motor.c"
        "led.c"
        "wifi_ap.c"
        "display/car_ui.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_lcd
        esp_http_server
        led_strip
)
