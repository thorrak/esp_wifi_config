/**
 * @file main.c
 * @brief ESP WiFi Config - sharing an HTTP server the application owns
 *
 * Every other example lets the library create the HTTP server. This one starts
 * its own first and hands it over through `.http.httpd`, so the library
 * registers its /api/wifi routes alongside the application's.
 *
 * That flow has three requirements nothing else in this repository has to
 * meet, because it is the only one where the application touches the network
 * before `wifi_cfg_init()` runs. Each is marked (1)(2)(3) below. Getting any of
 * them wrong fails in a way that does not name the cause, which is why this
 * example exists: it is the configuration CI would otherwise never build.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "esp_wifi_config.h"

static const char *TAG = "shared_httpd";

/* One route of the application's own, to show the two sets coexisting. */
static esp_err_t hello_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "hello from the application's own handler\n");
}

static const httpd_uri_t hello_uri = {
    .uri     = "/hello",
    .method  = HTTP_GET,
    .handler = hello_handler,
};

/**
 * @brief Start the application's server, sized for what the library will add.
 */
static httpd_handle_t start_my_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    /* (2) HTTPD_DEFAULT_CONFIG() allows 8 URI handlers. The library registers
     *     around 22 of its own -- the REST API, the Web UI, and the captive
     *     portal probes -- so a default-sized server runs out partway through
     *     and the rest of the API simply is not there. The library does not
     *     check each registration, so nothing is logged: you get a server that
     *     answers /api/wifi/status and 404s on /api/wifi/scan, which reads as
     *     a library bug. Leave room for its routes and your own. */
    config.max_uri_handlers = 32;

    /* (3) The Web UI registers a catch-all route -- a slash followed by a
     *     bare asterisk -- and the default matcher compares URIs exactly, so
     *     it never fires. The captive portal then answers nothing outside the
     *     handful of exact paths, and the phone's "sign in to network" sheet
     *     opens on a blank page. */
    config.uri_match_fn = httpd_uri_match_wildcard;

    /* Not required, but what almost every embedded server wants: without it
     * the 8th concurrent connection is accepted and then never answered. A
     * browser alone opens up to six to one origin. */
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &hello_uri));

    ESP_LOGI(TAG, "application server up, serving GET /hello");
    return server;
}

static void on_wifi_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    wifi_status_t status;
    if (wifi_cfg_get_status(&status) == ESP_OK) {
        ESP_LOGI(TAG, "Got IP: %s", status.ip);
        ESP_LOGI(TAG, "  application route : http://%s/hello", status.ip);
        ESP_LOGI(TAG, "  library route     : http://%s/api/wifi/status", status.ip);
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* (1) Everywhere else this is the library's job -- wifi_cfg_init() calls
     *     esp_netif_init() itself and tolerates having been beaten to it. Here
     *     it cannot: starting a server opens a socket, and that happens before
     *     wifi_cfg_init() is reached. Skip this line and the device aborts
     *     inside lwIP with
     *
     *         assert failed: tcpip_send_msg_wait_sem ... (Invalid mbox)
     *
     *     which names neither netif, nor this library, nor the line that did
     *     it. Calling it twice is harmless. */
    ESP_ERROR_CHECK(esp_netif_init());

    httpd_handle_t my_server = start_my_webserver();

    esp_event_handler_register(WIFI_CFG_EVENT, WIFI_CFG_EVENT_GOT_IP,
                               on_wifi_got_ip, NULL);

    wifi_cfg_config_t config = {
        WIFI_CFG_DEFAULTS,
        .enable_ap = true,
    };

    /* Assigned after the initialiser rather than written as
     * `.http = { .httpd = my_server }`, which would replace the whole `http`
     * sub-struct and silently drop the api_base_path and auth defaults that
     * WIFI_CFG_DEFAULTS put there. One field, set on its own. */
    config.http.httpd = my_server;

    ESP_ERROR_CHECK(wifi_cfg_init(&config));

    /* The library does not stop a server it did not start: wifi_cfg_deinit()
     * unregisters its own routes and leaves /hello answering. */
    ESP_LOGI(TAG, "wifi_cfg_init() done; both route sets are on one server");
}
