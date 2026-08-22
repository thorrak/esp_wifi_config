idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi_config nvs_flash esp_http_server esp_netif
)
