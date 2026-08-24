/* Just enough esp_err.h to compile src/esp_wifi_config_json.c on the host.
 * The writer deliberately depends on nothing else from ESP-IDF -- that is what
 * makes it testable here against real cJSON. */
#pragma once
typedef int esp_err_t;
#define ESP_OK                  0
#define ESP_FAIL               -1
#define ESP_ERR_NO_MEM          0x101
#define ESP_ERR_INVALID_ARG     0x102
#define ESP_ERR_INVALID_STATE   0x103
