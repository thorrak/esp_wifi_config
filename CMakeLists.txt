set(SRCS
    "src/esp_wifi_config.c"
    "src/esp_wifi_config_network.c"
    "src/esp_wifi_config_ap.c"
    "src/esp_wifi_config_bus.c"
    "src/esp_wifi_config_nvs.c"
    "src/esp_wifi_config_http.c"
    "src/esp_wifi_config_dns.c"
)

set(PRIV_REQUIRES
    esp_bus
    json
    mbedtls
)

set(EMBED_FILES)

# Add CLI if enabled
if(CONFIG_WIFI_CFG_ENABLE_CLI)
    list(APPEND SRCS "src/esp_wifi_config_cli.c")
    list(APPEND PRIV_REQUIRES console)
endif()

# Add Web UI if enabled
if(CONFIG_WIFI_CFG_ENABLE_WEBUI)
    list(APPEND SRCS "src/esp_wifi_config_webui.c")
    # Embed gzipped frontend files
    set(EMBED_FILES
        "frontend/dist/index.html"
        "frontend/dist/assets/app.js.gz"
        "frontend/dist/assets/index.css.gz"
    )
endif()

# Add BLE support if enabled (custom BLE or Improv BLE — both need the backend)
if(CONFIG_BT_ENABLED AND CONFIG_WIFI_CFG_ENABLE_BLE)
    list(APPEND SRCS "src/esp_wifi_config_ble.c")
    list(APPEND PRIV_REQUIRES bt)
    if(CONFIG_BT_BLUEDROID_ENABLED)
        list(APPEND SRCS "src/esp_wifi_config_ble_bluedroid.c")
    elseif(CONFIG_BT_NIMBLE_ENABLED)
        list(APPEND SRCS "src/esp_wifi_config_ble_nimble.c")
    endif()
endif()

# Add Improv WiFi support
if(CONFIG_WIFI_CFG_ENABLE_IMPROV)
    list(APPEND SRCS "src/esp_wifi_config_improv.c")
    if(CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE)
        list(APPEND SRCS "src/esp_wifi_config_improv_ble.c")
    endif()
    if(CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL)
        list(APPEND SRCS "src/esp_wifi_config_improv_serial.c")
        list(APPEND PRIV_REQUIRES driver)
    endif()
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
    PRIV_REQUIRES ${PRIV_REQUIRES}
    EMBED_FILES ${EMBED_FILES}
)
