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
    if(NOT CONFIG_WIFI_CFG_WEBUI_CUSTOM_PATH)
        list(APPEND EMBED_FILES
            "frontend/dist/index.html"
            "frontend/dist/assets/app.js.gz"
            "frontend/dist/assets/index.css.gz"
        )
    endif()
endif()

# Improv Serial needs driver component
if(CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL)
    list(APPEND SRCS "src/esp_wifi_config_improv_serial.c")
    list(APPEND PRIV_REQUIRES driver)
endif()

# BLE stack is needed if either custom BLE or Improv BLE is enabled.
if(CONFIG_BT_ENABLED AND (CONFIG_WIFI_CFG_ENABLE_CUSTOM_BLE OR CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE))
    list(APPEND PRIV_REQUIRES bt)
    # Shared BLE layer (command routing, JSON protocol, init/deinit)
    list(APPEND SRCS "src/esp_wifi_config_ble.c")
    # Stack-specific backend
    if(CONFIG_BT_BLUEDROID_ENABLED)
        list(APPEND SRCS "src/esp_wifi_config_ble_bluedroid.c")
    elseif(CONFIG_BT_NIMBLE_ENABLED)
        list(APPEND SRCS "src/esp_wifi_config_ble_nimble.c")
    endif()
    # Improv BLE transport
    if(CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE)
        list(APPEND SRCS "src/esp_wifi_config_improv_ble.c")
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

# Add CLI support after component registration.
# console include paths are propagated via target_link_libraries.
if(CONFIG_WIFI_CFG_ENABLE_CLI)
    idf_component_get_property(lib ${COMPONENT_NAME} COMPONENT_LIB)
    target_sources(${lib} PRIVATE "src/esp_wifi_config_cli.c")
    idf_component_get_property(console_lib console COMPONENT_LIB)
    target_link_libraries(${lib} PRIVATE ${console_lib})
endif()

# Belt-and-suspenders BLE include/link propagation.
# PRIV_REQUIRES bt (above) handles ESP-IDF's dep graph and final-exe link,
# but some build flows (e.g. PlatformIO's espressif32 platform) don't
# always forward bt's INCLUDE_DIRS onto our compile commands. Explicitly
# linking against bt's COMPONENT_LIB target makes CMake propagate
# INTERFACE_INCLUDE_DIRECTORIES regardless.
if(CONFIG_BT_ENABLED AND (CONFIG_WIFI_CFG_ENABLE_CUSTOM_BLE OR CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE))
    idf_component_get_property(lib ${COMPONENT_NAME} COMPONENT_LIB)
    idf_component_get_property(bt_lib bt COMPONENT_LIB)
    if(bt_lib)
        target_link_libraries(${lib} PRIVATE ${bt_lib})
    endif()
endif()

