/**
 * @file esp_wifi_manager_dns.c
 * @brief Captive Portal DNS Server - redirects all DNS queries to AP IP
 */

#include "esp_wifi_manager_priv.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <string.h>

static const char *TAG = "wifi_mgr_dns";

#define DNS_PORT 53
#define DNS_MAX_PACKET_SIZE 512

// DNS Header structure
typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

static int dns_sock = -1;
static TaskHandle_t dns_task_handle = NULL;
static bool dns_running = false;

/**
 * @brief Parse DNS query and extract domain name
 */
static int dns_parse_name(const uint8_t *data, int offset, char *name, int max_len)
{
    int pos = 0;
    int len;

    while ((len = data[offset]) != 0) {
        if (pos > 0 && pos < max_len - 1) {
            name[pos++] = '.';
        }
        offset++;
        for (int i = 0; i < len && pos < max_len - 1; i++) {
            name[pos++] = data[offset++];
        }
    }
    name[pos] = '\0';
    return offset + 1;  // Skip null terminator
}

/**
 * @brief Build DNS response with AP IP
 */
static int dns_build_response(const uint8_t *query, int query_len, uint8_t *response, uint32_t ap_ip)
{
    if (query_len < sizeof(dns_header_t)) {
        return -1;
    }

    // Copy query to response
    memcpy(response, query, query_len);

    dns_header_t *header = (dns_header_t *)response;

    // Set response flags
    header->flags = htons(0x8180);  // Response, No error
    header->ancount = htons(1);     // One answer

    // Find end of question section
    int offset = sizeof(dns_header_t);
    while (offset < query_len && response[offset] != 0) {
        offset += response[offset] + 1;
    }
    offset += 5;  // Skip null + QTYPE + QCLASS

    // Add answer section
    // Name pointer to question
    response[offset++] = 0xC0;
    response[offset++] = 0x0C;

    // Type A (1)
    response[offset++] = 0x00;
    response[offset++] = 0x01;

    // Class IN (1)
    response[offset++] = 0x00;
    response[offset++] = 0x01;

    // TTL (60 seconds)
    response[offset++] = 0x00;
    response[offset++] = 0x00;
    response[offset++] = 0x00;
    response[offset++] = 0x3C;

    // Data length (4 bytes for IPv4)
    response[offset++] = 0x00;
    response[offset++] = 0x04;

    // IP address (AP IP)
    response[offset++] = (ap_ip >> 0) & 0xFF;
    response[offset++] = (ap_ip >> 8) & 0xFF;
    response[offset++] = (ap_ip >> 16) & 0xFF;
    response[offset++] = (ap_ip >> 24) & 0xFF;

    return offset;
}

/**
 * @brief DNS server task
 */
static void dns_server_task(void *arg)
{
    uint8_t rx_buffer[DNS_MAX_PACKET_SIZE];
    uint8_t tx_buffer[DNS_MAX_PACKET_SIZE];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    ESP_LOGI(TAG, "DNS server task started");

    while (dns_running) {
        int len = recvfrom(dns_sock, rx_buffer, sizeof(rx_buffer), 0,
                          (struct sockaddr *)&client_addr, &addr_len);

        if (len < 0) {
            // EAGAIN (11) is expected on socket timeout - ignore it
            if (errno != EAGAIN && errno != EWOULDBLOCK && dns_running) {
                ESP_LOGW(TAG, "DNS recvfrom error: %d", errno);
            }
            continue;
        }

        if (len < (int)sizeof(dns_header_t)) {
            continue;
        }

        // Parse query domain for logging
        char domain[64] = {0};
        dns_parse_name(rx_buffer, sizeof(dns_header_t), domain, sizeof(domain));
        ESP_LOGD(TAG, "DNS query: %s", domain);

        // Get AP IP address
        uint32_t ap_ip = 0;
        esp_netif_ip_info_t ip_info;
        if (g_wifi_mgr && g_wifi_mgr->ap_netif &&
            esp_netif_get_ip_info(g_wifi_mgr->ap_netif, &ip_info) == ESP_OK) {
            ap_ip = ip_info.ip.addr;
        }

        if (ap_ip == 0) {
            continue;
        }

        // Build response
        int resp_len = dns_build_response(rx_buffer, len, tx_buffer, ap_ip);
        if (resp_len > 0) {
            sendto(dns_sock, tx_buffer, resp_len, 0,
                  (struct sockaddr *)&client_addr, addr_len);
        }
    }

    ESP_LOGI(TAG, "DNS server task stopped");
    vTaskDelete(NULL);
}

esp_err_t wifi_mgr_dns_start(void)
{
    if (dns_running) {
        return ESP_OK;
    }

    // Create UDP socket
    dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dns_sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %d", errno);
        return ESP_FAIL;
    }

    // Set socket options
    int opt = 1;
    setsockopt(dns_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Set receive timeout
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(dns_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Bind to DNS port
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    if (bind(dns_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket: %d", errno);
        close(dns_sock);
        dns_sock = -1;
        return ESP_FAIL;
    }

    dns_running = true;

    // Create DNS server task
    BaseType_t ret = xTaskCreate(dns_server_task, "dns_srv", 4096, NULL, 5, &dns_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DNS task");
        dns_running = false;
        close(dns_sock);
        dns_sock = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "DNS server started on port %d", DNS_PORT);
    return ESP_OK;
}

esp_err_t wifi_mgr_dns_stop(void)
{
    if (!dns_running) {
        return ESP_OK;
    }

    dns_running = false;

    // Close socket to unblock recvfrom
    if (dns_sock >= 0) {
        close(dns_sock);
        dns_sock = -1;
    }

    // Wait for task to finish
    if (dns_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        dns_task_handle = NULL;
    }

    ESP_LOGI(TAG, "DNS server stopped");
    return ESP_OK;
}
