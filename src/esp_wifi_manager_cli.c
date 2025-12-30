/**
 * @file esp_wifi_manager_cli.c
 * @brief CLI interface for WiFi Manager
 */

#include "esp_wifi_manager_priv.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "wifi_mgr_cli";

// =============================================================================
// Command Handlers
// =============================================================================

static int cmd_wifi_status(int argc, char **argv)
{
    wifi_status_t status;
    esp_err_t ret = wifi_manager_get_status(&status);
    if (ret != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("State:    %s\n",
           status.state == WIFI_STATE_CONNECTED ? "connected" :
           status.state == WIFI_STATE_CONNECTING ? "connecting" : "disconnected");

    if (status.state == WIFI_STATE_CONNECTED) {
        printf("SSID:     %s\n", status.ssid);
        printf("IP:       %s\n", status.ip);
        printf("Gateway:  %s\n", status.gateway);
        printf("RSSI:     %d dBm (%d%%)\n", status.rssi, status.quality);
        printf("Channel:  %d\n", status.channel);
        printf("MAC:      %s\n", status.mac);
    }

    printf("AP:       %s\n", status.ap_active ? "active" : "inactive");
    return 0;
}

static int cmd_wifi_scan(int argc, char **argv)
{
    wifi_scan_result_t results[20];
    size_t count = 0;

    printf("Scanning...\n");
    esp_err_t ret = wifi_manager_scan(results, 20, &count);
    if (ret != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("Found %zu networks:\n", count);
    printf("%-32s %6s %s\n", "SSID", "RSSI", "Auth");
    printf("-------------------------------- ------ --------\n");

    for (size_t i = 0; i < count; i++) {
        const char *auth = "UNKNOWN";
        switch (results[i].auth) {
            case WIFI_AUTH_OPEN: auth = "OPEN"; break;
            case WIFI_AUTH_WEP: auth = "WEP"; break;
            case WIFI_AUTH_WPA_PSK: auth = "WPA"; break;
            case WIFI_AUTH_WPA2_PSK: auth = "WPA2"; break;
            case WIFI_AUTH_WPA_WPA2_PSK: auth = "WPA/WPA2"; break;
            case WIFI_AUTH_WPA3_PSK: auth = "WPA3"; break;
            default: break;
        }
        printf("%-32s %4d   %s\n", results[i].ssid[0] ? results[i].ssid : "(hidden)",
               results[i].rssi, auth);
    }
    return 0;
}

static int cmd_wifi_list(int argc, char **argv)
{
    wifi_network_t networks[CONFIG_WIFI_MGR_MAX_NETWORKS];
    size_t count = 0;

    esp_err_t ret = wifi_manager_list_networks(networks, CONFIG_WIFI_MGR_MAX_NETWORKS, &count);
    if (ret != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(ret));
        return 1;
    }

    if (count == 0) {
        printf("No saved networks\n");
        return 0;
    }

    printf("Saved networks (%zu):\n", count);
    printf("%-32s %s\n", "SSID", "Priority");
    printf("-------------------------------- --------\n");

    for (size_t i = 0; i < count; i++) {
        printf("%-32s %d\n", networks[i].ssid, networks[i].priority);
    }
    return 0;
}

// wifi add <ssid> [password] [priority]
static struct {
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_int *priority;
    struct arg_end *end;
} wifi_add_args;

static int cmd_wifi_add(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&wifi_add_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wifi_add_args.end, argv[0]);
        return 1;
    }

    wifi_network_t net = {0};
    strncpy(net.ssid, wifi_add_args.ssid->sval[0], sizeof(net.ssid) - 1);
    if (wifi_add_args.password->count > 0) {
        strncpy(net.password, wifi_add_args.password->sval[0], sizeof(net.password) - 1);
    }
    net.priority = wifi_add_args.priority->count > 0 ? wifi_add_args.priority->ival[0] : 10;

    esp_err_t ret = wifi_manager_add_network(&net);
    if (ret == ESP_ERR_INVALID_STATE) {
        printf("Network already exists\n");
        return 1;
    }
    if (ret != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("Added: %s\n", net.ssid);
    return 0;
}

// wifi del <ssid>
static struct {
    struct arg_str *ssid;
    struct arg_end *end;
} wifi_del_args;

static int cmd_wifi_del(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&wifi_del_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wifi_del_args.end, argv[0]);
        return 1;
    }

    esp_err_t ret = wifi_manager_remove_network(wifi_del_args.ssid->sval[0]);
    if (ret == ESP_ERR_NOT_FOUND) {
        printf("Network not found\n");
        return 1;
    }
    if (ret != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("Deleted: %s\n", wifi_del_args.ssid->sval[0]);
    return 0;
}

// wifi connect [ssid]
static struct {
    struct arg_str *ssid;
    struct arg_end *end;
} wifi_connect_args;

static int cmd_wifi_connect(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&wifi_connect_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wifi_connect_args.end, argv[0]);
        return 1;
    }

    const char *ssid = wifi_connect_args.ssid->count > 0 ? wifi_connect_args.ssid->sval[0] : NULL;
    esp_err_t ret = wifi_manager_connect(ssid);
    if (ret != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("Connecting%s%s...\n", ssid ? " to " : "", ssid ? ssid : "");
    return 0;
}

static int cmd_wifi_disconnect(int argc, char **argv)
{
    esp_err_t ret = wifi_manager_disconnect();
    if (ret != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("Disconnected\n");
    return 0;
}

static int cmd_wifi_ap_start(int argc, char **argv)
{
    esp_err_t ret = wifi_manager_start_ap(NULL);
    if (ret != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("AP started\n");
    return 0;
}

static int cmd_wifi_ap_stop(int argc, char **argv)
{
    esp_err_t ret = wifi_manager_stop_ap();
    if (ret != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("AP stopped\n");
    return 0;
}

static int cmd_wifi_reset(int argc, char **argv)
{
    esp_err_t ret = wifi_manager_factory_reset();
    if (ret != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("Factory reset complete. Restart recommended.\n");
    return 0;
}

// wifi var get <key>
static struct {
    struct arg_str *key;
    struct arg_end *end;
} wifi_var_get_args;

static int cmd_wifi_var_get(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&wifi_var_get_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wifi_var_get_args.end, argv[0]);
        return 1;
    }

    char value[128];
    esp_err_t ret = wifi_manager_get_var(wifi_var_get_args.key->sval[0], value, sizeof(value));
    if (ret == ESP_ERR_NOT_FOUND) {
        printf("Variable not found\n");
        return 1;
    }
    if (ret != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("%s = %s\n", wifi_var_get_args.key->sval[0], value);
    return 0;
}

// wifi var set <key> <value>
static struct {
    struct arg_str *key;
    struct arg_str *value;
    struct arg_end *end;
} wifi_var_set_args;

static int cmd_wifi_var_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&wifi_var_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wifi_var_set_args.end, argv[0]);
        return 1;
    }

    esp_err_t ret = wifi_manager_set_var(wifi_var_set_args.key->sval[0],
                                          wifi_var_set_args.value->sval[0]);
    if (ret != ESP_OK) {
        printf("Error: %s\n", esp_err_to_name(ret));
        return 1;
    }

    printf("Set %s = %s\n", wifi_var_set_args.key->sval[0], wifi_var_set_args.value->sval[0]);
    return 0;
}

// =============================================================================
// Init
// =============================================================================

esp_err_t wifi_mgr_cli_init(void)
{
    ESP_LOGI(TAG, "Registering CLI commands");

    // wifi status
    esp_console_cmd_t status_cmd = {
        .command = "wifi_status",
        .help = "Show WiFi status",
        .hint = NULL,
        .func = &cmd_wifi_status,
    };
    esp_console_cmd_register(&status_cmd);

    // wifi scan
    esp_console_cmd_t scan_cmd = {
        .command = "wifi_scan",
        .help = "Scan for networks",
        .hint = NULL,
        .func = &cmd_wifi_scan,
    };
    esp_console_cmd_register(&scan_cmd);

    // wifi list
    esp_console_cmd_t list_cmd = {
        .command = "wifi_list",
        .help = "List saved networks",
        .hint = NULL,
        .func = &cmd_wifi_list,
    };
    esp_console_cmd_register(&list_cmd);

    // wifi add
    wifi_add_args.ssid = arg_str1(NULL, NULL, "<ssid>", "Network SSID");
    wifi_add_args.password = arg_str0(NULL, NULL, "[password]", "Network password");
    wifi_add_args.priority = arg_int0("p", "priority", "<n>", "Priority (default 10)");
    wifi_add_args.end = arg_end(3);
    esp_console_cmd_t add_cmd = {
        .command = "wifi_add",
        .help = "Add network: wifi_add <ssid> [password] [-p priority]",
        .hint = NULL,
        .func = &cmd_wifi_add,
        .argtable = &wifi_add_args,
    };
    esp_console_cmd_register(&add_cmd);

    // wifi del
    wifi_del_args.ssid = arg_str1(NULL, NULL, "<ssid>", "Network SSID");
    wifi_del_args.end = arg_end(1);
    esp_console_cmd_t del_cmd = {
        .command = "wifi_del",
        .help = "Delete network",
        .hint = NULL,
        .func = &cmd_wifi_del,
        .argtable = &wifi_del_args,
    };
    esp_console_cmd_register(&del_cmd);

    // wifi connect
    wifi_connect_args.ssid = arg_str0(NULL, NULL, "[ssid]", "Network SSID (auto if omitted)");
    wifi_connect_args.end = arg_end(1);
    esp_console_cmd_t connect_cmd = {
        .command = "wifi_connect",
        .help = "Connect to network",
        .hint = NULL,
        .func = &cmd_wifi_connect,
        .argtable = &wifi_connect_args,
    };
    esp_console_cmd_register(&connect_cmd);

    // wifi disconnect
    esp_console_cmd_t disconnect_cmd = {
        .command = "wifi_disconnect",
        .help = "Disconnect from network",
        .hint = NULL,
        .func = &cmd_wifi_disconnect,
    };
    esp_console_cmd_register(&disconnect_cmd);

    // wifi ap start
    esp_console_cmd_t ap_start_cmd = {
        .command = "wifi_ap_start",
        .help = "Start access point",
        .hint = NULL,
        .func = &cmd_wifi_ap_start,
    };
    esp_console_cmd_register(&ap_start_cmd);

    // wifi ap stop
    esp_console_cmd_t ap_stop_cmd = {
        .command = "wifi_ap_stop",
        .help = "Stop access point",
        .hint = NULL,
        .func = &cmd_wifi_ap_stop,
    };
    esp_console_cmd_register(&ap_stop_cmd);

    // wifi reset
    esp_console_cmd_t reset_cmd = {
        .command = "wifi_reset",
        .help = "Factory reset (clear all saved data)",
        .hint = NULL,
        .func = &cmd_wifi_reset,
    };
    esp_console_cmd_register(&reset_cmd);

    // wifi var get
    wifi_var_get_args.key = arg_str1(NULL, NULL, "<key>", "Variable key");
    wifi_var_get_args.end = arg_end(1);
    esp_console_cmd_t var_get_cmd = {
        .command = "wifi_var_get",
        .help = "Get variable",
        .hint = NULL,
        .func = &cmd_wifi_var_get,
        .argtable = &wifi_var_get_args,
    };
    esp_console_cmd_register(&var_get_cmd);

    // wifi var set
    wifi_var_set_args.key = arg_str1(NULL, NULL, "<key>", "Variable key");
    wifi_var_set_args.value = arg_str1(NULL, NULL, "<value>", "Variable value");
    wifi_var_set_args.end = arg_end(2);
    esp_console_cmd_t var_set_cmd = {
        .command = "wifi_var_set",
        .help = "Set variable",
        .hint = NULL,
        .func = &cmd_wifi_var_set,
        .argtable = &wifi_var_set_args,
    };
    esp_console_cmd_register(&var_set_cmd);

    return ESP_OK;
}
