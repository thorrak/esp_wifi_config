set(SRCS
    "src/esp_wifi_manager.c"
    "src/esp_wifi_manager_network.c"
    "src/esp_wifi_manager_ap.c"
    "src/esp_wifi_manager_bus.c"
    "src/esp_wifi_manager_nvs.c"
    "src/esp_wifi_manager_http.c"
    "src/esp_wifi_manager_dns.c"
    "src/esp_wifi_manager_mdns.c"
)

set(PRIV_REQUIRES
    esp_bus
    json
    mbedtls
)

# Add CLI if enabled
if(CONFIG_WIFI_MGR_ENABLE_CLI)
    list(APPEND SRCS "src/esp_wifi_manager_cli.c")
    list(APPEND PRIV_REQUIRES console)
endif()

idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "src"
    REQUIRES
        esp_wifi
        esp_netif
        nvs_flash
        esp_http_server
        esp_event
        mdns
    PRIV_REQUIRES ${PRIV_REQUIRES}
)

