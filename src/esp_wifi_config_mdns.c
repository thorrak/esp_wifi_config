/**
 * @file esp_wifi_config_mdns.c
 * @brief mDNS service discovery with hostname template support
 */

#include "esp_wifi_config_priv.h"
#include "mdns.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "wifi_cfg_mdns";

static bool mdns_initialized = false;
static char mdns_hostname[64] = {0};

/**
 * @brief Initialize mDNS with template hostname
 */
esp_err_t wifi_cfg_mdns_init(void)
{
    if (!g_wifi_cfg) return ESP_ERR_INVALID_STATE;

    // Check if mDNS is enabled
    if (!g_wifi_cfg->config.mdns.enable) {
        return ESP_OK;
    }

    if (mdns_initialized) {
        return ESP_OK;
    }

    // Initialize mDNS
    esp_err_t ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Get hostname template (use config or default from Kconfig)
    const char *hostname_tmpl = g_wifi_cfg->config.mdns.hostname;
    if (!hostname_tmpl || !hostname_tmpl[0]) {
#ifdef CONFIG_WIFI_CFG_MDNS_HOSTNAME
        hostname_tmpl = CONFIG_WIFI_CFG_MDNS_HOSTNAME;
#else
        hostname_tmpl = "esp32-{id}";
#endif
    }

    // Expand template
    wifi_cfg_expand_template(hostname_tmpl, mdns_hostname, sizeof(mdns_hostname));

    // Set hostname
    ret = mdns_hostname_set(mdns_hostname);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set hostname: %s", esp_err_to_name(ret));
        mdns_free();
        return ret;
    }

    // Set instance name
    const char *instance = g_wifi_cfg->config.mdns.instance_name;
    if (!instance || !instance[0]) {
        instance = mdns_hostname;
    }
    mdns_instance_name_set(instance);

    // Add HTTP service if HTTP server is running
    if (g_wifi_cfg->httpd) {
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }

    mdns_initialized = true;
    ESP_LOGI(TAG, "mDNS initialized: %s.local", mdns_hostname);

    return ESP_OK;
}

/**
 * @brief Deinitialize mDNS
 */
esp_err_t wifi_cfg_mdns_deinit(void)
{
    if (!mdns_initialized) {
        return ESP_OK;
    }

    mdns_free();
    mdns_initialized = false;
    mdns_hostname[0] = '\0';

    ESP_LOGI(TAG, "mDNS deinitialized");
    return ESP_OK;
}

/**
 * @brief Get mDNS hostname
 */
const char *wifi_cfg_mdns_get_hostname(void)
{
    return mdns_initialized ? mdns_hostname : NULL;
}
