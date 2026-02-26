/**
 * @file esp_wifi_manager_webui.c
 * @brief Embedded Web UI serving for WiFi Manager
 */

#include "esp_wifi_manager_priv.h"
#include "esp_log.h"
#include <string.h>
#include <sys/stat.h>

#ifdef CONFIG_WIFI_MGR_ENABLE_WEBUI

static const char *TAG = "wifi_mgr_webui";

/**
 * @brief Get the filesystem base path for custom WebUI files
 *
 * Returns the CONFIG_WIFI_MGR_WEBUI_CUSTOM_PATH if set, NULL otherwise.
 */
static const char *get_fs_base_path(void)
{
#ifdef CONFIG_WIFI_MGR_WEBUI_CUSTOM_PATH
    const char *path = CONFIG_WIFI_MGR_WEBUI_CUSTOM_PATH;
    if (path && path[0]) {
        return path;
    }
#endif
    return NULL;
}

/**
 * @brief Determine MIME content type from file path
 */
static const char *get_content_type(const char *filepath)
{
    if (strstr(filepath, ".html")) return "text/html";
    if (strstr(filepath, ".js"))   return "application/javascript";
    if (strstr(filepath, ".css"))  return "text/css";
    if (strstr(filepath, ".json")) return "application/json";
    if (strstr(filepath, ".png"))  return "image/png";
    if (strstr(filepath, ".svg"))  return "image/svg+xml";
    if (strstr(filepath, ".ico"))  return "image/x-icon";
    if (strstr(filepath, ".jpg") || strstr(filepath, ".jpeg")) return "image/jpeg";
    if (strstr(filepath, ".woff2")) return "font/woff2";
    if (strstr(filepath, ".woff")) return "font/woff";
    if (strstr(filepath, ".ttf"))  return "font/ttf";
    if (strstr(filepath, ".gif"))  return "image/gif";
    if (strstr(filepath, ".webp")) return "image/webp";
    return "text/plain";
}

/**
 * @brief Try to serve file from custom filesystem path (SPIFFS/LittleFS)
 */
static bool serve_from_filesystem(httpd_req_t *req, const char *filepath)
{
    const char *base_path = get_fs_base_path();
    if (!base_path) {
        return false;
    }

    char fullpath[128];
    snprintf(fullpath, sizeof(fullpath), "%s%s", base_path, filepath);

    // Check if file exists
    struct stat st;
    if (stat(fullpath, &st) != 0) {
        return false;
    }

    FILE *f = fopen(fullpath, "r");
    if (!f) {
        return false;
    }

    httpd_resp_set_type(req, get_content_type(filepath));

    // Check for gzipped version
    char gzpath[132];
    snprintf(gzpath, sizeof(gzpath), "%s.gz", fullpath);
    if (stat(gzpath, &st) == 0) {
        fclose(f);
        f = fopen(gzpath, "r");
        if (f) {
            httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        }
    }

    // Stream file
    char buf[512];
    size_t read_bytes;
    while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
        httpd_resp_send_chunk(req, buf, read_bytes);
    }
    httpd_resp_send_chunk(req, NULL, 0);

    fclose(f);
    ESP_LOGD(TAG, "Served from filesystem: %s", filepath);
    return true;
}

#ifndef CONFIG_WIFI_MGR_WEBUI_CUSTOM_PATH
// Embedded files (linked via CMakeLists.txt EMBED_FILES)
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t app_js_gz_start[] asm("_binary_app_js_gz_start");
extern const uint8_t app_js_gz_end[] asm("_binary_app_js_gz_end");
extern const uint8_t index_css_gz_start[] asm("_binary_index_css_gz_start");
extern const uint8_t index_css_gz_end[] asm("_binary_index_css_gz_end");

static const struct {
    const char *uri;
    const uint8_t *start;
    const uint8_t *end;
    bool gzipped;
} embedded_assets[] = {
    { "/index.html",       index_html_start,   index_html_end,   false },
    { "/assets/app.js",    app_js_gz_start,    app_js_gz_end,    true  },
    { "/assets/index.css", index_css_gz_start, index_css_gz_end, true  },
};

static bool serve_embedded(httpd_req_t *req, const char *filepath)
{
    for (size_t i = 0; i < sizeof(embedded_assets) / sizeof(embedded_assets[0]); i++) {
        if (strcmp(filepath, embedded_assets[i].uri) == 0) {
            httpd_resp_set_type(req, get_content_type(filepath));
            if (embedded_assets[i].gzipped) {
                httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
            }
            httpd_resp_send(req, (const char *)embedded_assets[i].start,
                            embedded_assets[i].end - embedded_assets[i].start);
            return true;
        }
    }
    return false;
}
#endif // !CONFIG_WIFI_MGR_WEBUI_CUSTOM_PATH

/**
 * @brief Unified handler for all Web UI static files
 *
 * Serves files by trying the filesystem first, then falling back to
 * embedded content (when available). Remaps "/" to "/index.html".
 */
static esp_err_t handler_webui_static(httpd_req_t *req)
{
    const char *filepath = req->uri;

    if (strcmp(filepath, "/") == 0) {
        filepath = "/index.html";
    }

    if (serve_from_filesystem(req, filepath)) {
        return ESP_OK;
    }

#ifndef CONFIG_WIFI_MGR_WEBUI_CUSTOM_PATH
    if (serve_embedded(req, filepath)) {
        return ESP_OK;
    }
#endif

    ESP_LOGW(TAG, "404 Not Found: uri=%s filepath=%s", req->uri, filepath);
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, NULL);
    return ESP_FAIL;
}

/**
 * @brief Initialize Web UI handlers
 */
esp_err_t wifi_mgr_webui_init(httpd_handle_t httpd)
{
    if (!httpd) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing Web UI (fs_path: %s)",
             get_fs_base_path() ? get_fs_base_path() : "(embedded only)");

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handler_webui_static,
    };
    httpd_register_uri_handler(httpd, &index_uri);

    httpd_uri_t wildcard_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = handler_webui_static,
    };
    httpd_register_uri_handler(httpd, &wildcard_uri);

#ifndef CONFIG_WIFI_MGR_WEBUI_CUSTOM_PATH
    size_t total_size = (index_html_end - index_html_start) +
                        (app_js_gz_end - app_js_gz_start) +
                        (index_css_gz_end - index_css_gz_start);
    ESP_LOGI(TAG, "Web UI registered (embedded size: %zu bytes)", total_size);
#else
    ESP_LOGI(TAG, "Web UI registered (serving from filesystem: %s)",
             CONFIG_WIFI_MGR_WEBUI_CUSTOM_PATH);
#endif

    return ESP_OK;
}

#else

esp_err_t wifi_mgr_webui_init(httpd_handle_t httpd)
{
    (void)httpd;
    return ESP_OK;
}

#endif // CONFIG_WIFI_MGR_ENABLE_WEBUI
