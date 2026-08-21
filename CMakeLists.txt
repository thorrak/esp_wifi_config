set(SRCS
    "src/esp_wifi_config.c"
    "src/esp_wifi_config_network.c"
    "src/esp_wifi_config_ap.c"
    "src/esp_wifi_config_event.c"
    "src/esp_wifi_config_nvs.c"
    "src/esp_wifi_config_http.c"
    "src/esp_wifi_config_dns.c"
    "src/esp_wifi_config_improv.c"
    # Improv BLE host shim — stubs out when Improv BLE is disabled.
    "src/esp_wifi_config_ble.c"
    # Network Provisioning backend — stubs out when WIFI_CFG_ENABLE_NETWORK_PROVISIONING is off.
    "src/esp_wifi_config_prov_ble.c"
)

set(PRIV_REQUIRES
    json
    mbedtls
    esp_app_format
    # driver carries driver/uart.h for esp_wifi_config_improv_serial.c. It is
    # unconditional for the same reason bt/protocomm are below: a
    # `list(APPEND PRIV_REQUIRES ...)` guarded on CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL
    # does not reach ESP-IDF's dependency scan, so the include path never
    # arrived. Until 0.2.0 that went unnoticed because esp_bus declared
    # `REQUIRES driver` publicly and this component depended on esp_bus, so
    # driver's includes came in transitively. Removing esp_bus removed the
    # accident, and every improv_serial build stopped compiling.
    driver
    # bt + protocomm + wifi_provisioning are listed unconditionally so
    # ESP-IDF's first-pass dependency scan (which runs before sdkconfig is
    # fully resolved) sees them. The implementation files
    # (esp_wifi_config_prov_ble.c, esp_wifi_config_ble*.c) wrap their
    # bodies in #ifdef CONFIG_WIFI_CFG_*, so when these components are
    # not used they contribute zero symbols and the linker drops the
    # unused archives.
    bt
    protocomm
    wifi_provisioning
    # esp_coex exposes esp_coex_preference_set(), called from the prov-BLE
    # backend to bias arbitration toward BT during the iOS prov-scan window.
    esp_coex
)

set(EMBED_FILES)

# Add Web UI if enabled
set(WIFI_CFG_WEBUI_EMBEDDED NO)
if(CONFIG_WIFI_CFG_ENABLE_WEBUI)
    list(APPEND SRCS "src/esp_wifi_config_webui.c")
    # Embed gzipped frontend files
    if(NOT CONFIG_WIFI_CFG_WEBUI_CUSTOM_PATH)
        set(WIFI_CFG_WEBUI_EMBEDDED YES)
        list(APPEND EMBED_FILES
            "frontend/dist/index.html"
            "frontend/dist/assets/app.js.gz"
            "frontend/dist/assets/index.css.gz"
        )
    endif()
endif()

# Improv Serial. `driver` is declared unconditionally above -- appending it
# here does not reach the dependency scan.
if(CONFIG_WIFI_CFG_ENABLE_IMPROV_SERIAL)
    list(APPEND SRCS "src/esp_wifi_config_improv_serial.c")
endif()

# BLE host bootstrap — only the source files are conditional on
# Improv BLE. The `bt` dependency is already declared unconditionally
# above so the first-pass component scan sees it.
if(CONFIG_BT_ENABLED AND CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE)
    if(CONFIG_BT_BLUEDROID_ENABLED)
        list(APPEND SRCS "src/esp_wifi_config_ble_bluedroid.c")
    elseif(CONFIG_BT_NIMBLE_ENABLED)
        list(APPEND SRCS "src/esp_wifi_config_ble_nimble.c")
    endif()
    list(APPEND SRCS "src/esp_wifi_config_improv_ble.c")
endif()

# IDF 6.x note: on IDF 6.x the `wifi_provisioning` component is removed
# in favour of the `espressif/network_provisioning` managed component
# declared in idf_component.yml. The compile-time switch in
# esp_wifi_config_prov_ble.c handles the include + symbol rename. The
# always-on PRIV_REQUIRES declaration above will need
# `wifi_provisioning` swapped for `network_provisioning` on 6.x — track
# https://github.com/thorrak/esp_wifi_config/issues for the IDF 6.x
# build path.

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

# Tell the C side whether the frontend assets were actually embedded.
#
# This has to come from CMake because the preprocessor cannot answer it.
# CONFIG_WIFI_CFG_WEBUI_CUSTOM_PATH is a Kconfig `string` with `default ""`,
# so it is *always* defined -- `#ifndef` on it is false even when no custom
# path is set. The webui source used exactly that guard, so in every ordinary
# build the embedded asset table and serve_embedded() were compiled out while
# the assets themselves were embedded and referenced by nothing. `/` returned
# 404, which is where all eight captive-portal handlers redirect.
#
# CMake gets it right (an empty string is falsy here), and it is the half that
# decides EMBED_FILES, so it is the half that should say so.
if(WIFI_CFG_WEBUI_EMBEDDED)
    idf_component_get_property(lib ${COMPONENT_NAME} COMPONENT_LIB)
    target_compile_definitions(${lib} PRIVATE WIFI_CFG_WEBUI_EMBEDDED=1)
endif()

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
if(CONFIG_BT_ENABLED AND
   (CONFIG_WIFI_CFG_ENABLE_IMPROV_BLE OR CONFIG_WIFI_CFG_ENABLE_NETWORK_PROVISIONING))
    idf_component_get_property(lib ${COMPONENT_NAME} COMPONENT_LIB)
    idf_component_get_property(bt_lib bt COMPONENT_LIB)
    if(bt_lib)
        target_link_libraries(${lib} PRIVATE ${bt_lib})
    endif()
endif()
