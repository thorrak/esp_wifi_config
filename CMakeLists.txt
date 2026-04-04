set(SRCS
    "src/esp_wifi_config.c"
    "src/esp_wifi_config_network.c"
    "src/esp_wifi_config_ap.c"
    "src/esp_wifi_config_bus.c"
    "src/esp_wifi_config_nvs.c"
    "src/esp_wifi_config_http.c"
    "src/esp_wifi_config_dns.c"
    "src/esp_wifi_config_improv.c"
)

set(PRIV_REQUIRES
    esp_bus
    json
    mbedtls
)

set(EMBED_FILES)

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

# Improv Serial needs driver component
if(CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL)
    list(APPEND SRCS "src/esp_wifi_config_improv_serial.c")
    list(APPEND PRIV_REQUIRES driver)
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

# Add CLI support after component registration.
# console include paths are propagated via target_link_libraries.
if(CONFIG_WIFI_CFG_ENABLE_CLI)
    idf_component_get_property(lib ${COMPONENT_NAME} COMPONENT_LIB)
    target_sources(${lib} PRIVATE "src/esp_wifi_config_cli.c")
    idf_component_get_property(console_lib console COMPONENT_LIB)
    target_link_libraries(${lib} PRIVATE ${console_lib})
endif()

# Add BLE support after component registration.
# bt include paths are propagated via target_link_libraries.
if(CONFIG_BT_ENABLED AND CONFIG_WIFI_CFG_ENABLE_BLE)
    idf_component_get_property(lib ${COMPONENT_NAME} COMPONENT_LIB)
    # Shared BLE layer (command routing, JSON protocol)
    target_sources(${lib} PRIVATE "src/esp_wifi_config_ble.c")
    # Stack-specific backend
    if(CONFIG_BT_BLUEDROID_ENABLED)
        target_sources(${lib} PRIVATE "src/esp_wifi_config_ble_bluedroid.c")
    elseif(CONFIG_BT_NIMBLE_ENABLED)
        target_sources(${lib} PRIVATE "src/esp_wifi_config_ble_nimble.c")
    endif()
    # Improv BLE transport
    if(CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE)
        target_sources(${lib} PRIVATE "src/esp_wifi_config_improv_ble.c")
    endif()
    idf_component_get_property(bt_lib bt COMPONENT_LIB)
    target_link_libraries(${lib} PRIVATE ${bt_lib})
endif()
