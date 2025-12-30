/**
 * @file esp_wifi_manager_nvs.c
 * @brief NVS storage for WiFi Manager
 */

#include "esp_wifi_manager_priv.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "wifi_mgr_nvs";

// =============================================================================
// NVS Keys
// =============================================================================

#define NVS_KEY_NET_COUNT       "net_count"
#define NVS_KEY_NET_PREFIX      "net_"
#define NVS_KEY_VAR_COUNT       "var_count"
#define NVS_KEY_VAR_PREFIX      "var_"
#define NVS_KEY_AP_CONFIG       "ap_config"
#define NVS_KEY_AUTH_USER       "auth_user"
#define NVS_KEY_AUTH_PASS       "auth_pass"

// =============================================================================
// Init
// =============================================================================

esp_err_t wifi_mgr_nvs_init(void)
{
    // NVS có thể đã được init bởi component khác
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    // ESP_ERR_NVS_INVALID_STATE = already initialized, OK
    if (ret == ESP_OK || ret == ESP_ERR_NVS_INVALID_STATE) {
        return ESP_OK;
    }
    return ret;
}

// =============================================================================
// Networks
// =============================================================================

esp_err_t wifi_mgr_nvs_load_networks(wifi_network_t *networks, size_t max_count, size_t *count)
{
    nvs_handle_t handle;
    esp_err_t ret;
    
    ret = nvs_open(WIFI_MGR_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *count = 0;
        return ESP_OK;
    }
    if (ret != ESP_OK) return ret;
    
    uint8_t net_count = 0;
    ret = nvs_get_u8(handle, NVS_KEY_NET_COUNT, &net_count);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        net_count = 0;
        ret = ESP_OK;
    }
    
    size_t loaded = 0;
    for (uint8_t i = 0; i < net_count && loaded < max_count; i++) {
        char key[16];
        snprintf(key, sizeof(key), NVS_KEY_NET_PREFIX "%d", i);
        
        size_t len = sizeof(wifi_network_t);
        ret = nvs_get_blob(handle, key, &networks[loaded], &len);
        if (ret == ESP_OK) {
            loaded++;
        }
    }
    
    *count = loaded;
    nvs_close(handle);
    ESP_LOGI(TAG, "Loaded %zu networks from NVS", loaded);
    return ESP_OK;
}

esp_err_t wifi_mgr_nvs_save_networks(const wifi_network_t *networks, size_t count)
{
    nvs_handle_t handle;
    esp_err_t ret;
    
    ret = nvs_open(WIFI_MGR_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;
    
    // Save count
    ret = nvs_set_u8(handle, NVS_KEY_NET_COUNT, (uint8_t)count);
    if (ret != ESP_OK) goto cleanup;
    
    // Save each network
    for (size_t i = 0; i < count; i++) {
        char key[16];
        snprintf(key, sizeof(key), NVS_KEY_NET_PREFIX "%d", (int)i);
        ret = nvs_set_blob(handle, key, &networks[i], sizeof(wifi_network_t));
        if (ret != ESP_OK) goto cleanup;
    }
    
    ret = nvs_commit(handle);
    ESP_LOGI(TAG, "Saved %zu networks to NVS", count);
    
cleanup:
    nvs_close(handle);
    return ret;
}

// =============================================================================
// Variables
// =============================================================================

esp_err_t wifi_mgr_nvs_load_vars(wifi_var_t *vars, size_t max_count, size_t *count)
{
    nvs_handle_t handle;
    esp_err_t ret;
    
    ret = nvs_open(WIFI_MGR_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        *count = 0;
        return ESP_OK;
    }
    if (ret != ESP_OK) return ret;
    
    uint8_t var_count = 0;
    ret = nvs_get_u8(handle, NVS_KEY_VAR_COUNT, &var_count);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        var_count = 0;
        ret = ESP_OK;
    }
    
    size_t loaded = 0;
    for (uint8_t i = 0; i < var_count && loaded < max_count; i++) {
        char key[16];
        snprintf(key, sizeof(key), NVS_KEY_VAR_PREFIX "%d", i);
        
        size_t len = sizeof(wifi_var_t);
        ret = nvs_get_blob(handle, key, &vars[loaded], &len);
        if (ret == ESP_OK) {
            loaded++;
        }
    }
    
    *count = loaded;
    nvs_close(handle);
    ESP_LOGI(TAG, "Loaded %zu vars from NVS", loaded);
    return ESP_OK;
}

esp_err_t wifi_mgr_nvs_save_vars(const wifi_var_t *vars, size_t count)
{
    nvs_handle_t handle;
    esp_err_t ret;
    
    ret = nvs_open(WIFI_MGR_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;
    
    ret = nvs_set_u8(handle, NVS_KEY_VAR_COUNT, (uint8_t)count);
    if (ret != ESP_OK) goto cleanup;
    
    for (size_t i = 0; i < count; i++) {
        char key[16];
        snprintf(key, sizeof(key), NVS_KEY_VAR_PREFIX "%d", (int)i);
        ret = nvs_set_blob(handle, key, &vars[i], sizeof(wifi_var_t));
        if (ret != ESP_OK) goto cleanup;
    }
    
    ret = nvs_commit(handle);
    ESP_LOGI(TAG, "Saved %zu vars to NVS", count);
    
cleanup:
    nvs_close(handle);
    return ret;
}

// =============================================================================
// AP Config
// =============================================================================

esp_err_t wifi_mgr_nvs_load_ap_config(wifi_mgr_ap_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t ret;
    
    ret = nvs_open(WIFI_MGR_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (ret != ESP_OK) return ret;
    
    size_t len = sizeof(wifi_mgr_ap_config_t);
    ret = nvs_get_blob(handle, NVS_KEY_AP_CONFIG, config, &len);
    
    nvs_close(handle);
    return ret;
}

esp_err_t wifi_mgr_nvs_save_ap_config(const wifi_mgr_ap_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t ret;
    
    ret = nvs_open(WIFI_MGR_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;
    
    ret = nvs_set_blob(handle, NVS_KEY_AP_CONFIG, config, sizeof(wifi_mgr_ap_config_t));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    
    nvs_close(handle);
    ESP_LOGI(TAG, "Saved AP config to NVS");
    return ret;
}

// =============================================================================
// Auth Credentials
// =============================================================================

esp_err_t wifi_mgr_nvs_load_auth(char *username, size_t ulen, char *password, size_t plen)
{
    nvs_handle_t handle;
    esp_err_t ret;
    
    ret = nvs_open(WIFI_MGR_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (ret != ESP_OK) return ret;
    
    size_t len = ulen;
    ret = nvs_get_str(handle, NVS_KEY_AUTH_USER, username, &len);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return ret;
    }
    
    len = plen;
    ret = nvs_get_str(handle, NVS_KEY_AUTH_PASS, password, &len);
    
    nvs_close(handle);
    return ret;
}

esp_err_t wifi_mgr_nvs_save_auth(const char *username, const char *password)
{
    nvs_handle_t handle;
    esp_err_t ret;
    
    ret = nvs_open(WIFI_MGR_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) return ret;
    
    if (username) {
        ret = nvs_set_str(handle, NVS_KEY_AUTH_USER, username);
        if (ret != ESP_OK) goto cleanup;
    }
    
    if (password) {
        ret = nvs_set_str(handle, NVS_KEY_AUTH_PASS, password);
        if (ret != ESP_OK) goto cleanup;
    }
    
    ret = nvs_commit(handle);
    
cleanup:
    nvs_close(handle);
    return ret;
}

