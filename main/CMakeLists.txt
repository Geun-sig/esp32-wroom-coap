idf_component_register(
    SRCS "main.c" "wifi.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi nvs_flash esp_event esp_netif espressif__coap esp_driver_gpio esp_http_server
)
