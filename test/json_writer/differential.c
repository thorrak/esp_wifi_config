/*
 * Differential test: the streaming writer must produce byte-identical output
 * to cJSON_PrintUnformatted() for everything this library emits.
 *
 * "Looks right" is not the bar. The Web UI, tools/test_server and whatever
 * scripts users have written all consume these responses, so the only safe
 * claim is that the bytes did not change -- which is a thing a test can assert
 * and a reviewer cannot.
 *
 * Each case builds the same document twice: once through cJSON, once through
 * the writer, then memcmp.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_wifi_config_json.h"

static int failures = 0;
static int checks   = 0;

/* Streaming sink that appends, so the streamed result can be compared whole. */
typedef struct { char *buf; size_t len, cap; } acc_t;

static esp_err_t acc_sink(void *ctx, const char *data, size_t len)
{
    acc_t *a = ctx;
    if (a->len + len >= a->cap) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(a->buf + a->len, data, len);
    a->len += len;
    a->buf[a->len] = '\0';
    return ESP_OK;
}

static void compare(const char *name, cJSON *tree, const char *written)
{
    char *expect = cJSON_PrintUnformatted(tree);
    checks++;
    if (expect == NULL) {
        printf("  %-40s cJSON failed to print\n", name);
        failures++;
        return;
    }
    if (strcmp(expect, written) != 0) {
        printf("  %-40s MISMATCH\n     cJSON : %s\n     writer: %s\n",
               name, expect, written);
        failures++;
    } else {
        printf("  %-40s ok  (%zu bytes)\n", name, strlen(expect));
    }
    free(expect);
    cJSON_Delete(tree);
}

/* Run the writer with a deliberately small scratch buffer so that every case
 * also exercises flushing across chunk boundaries. 7 bytes is prime and
 * smaller than every token, which is the point. */
#define SCRATCH 7

typedef void (*build_fn)(wcfg_json_w *w);

static const char *write_with(build_fn fn, char *out, size_t out_cap)
{
    static char scratch[SCRATCH];
    acc_t acc = { .buf = out, .len = 0, .cap = out_cap };
    out[0] = '\0';
    wcfg_json_w w;
    wcfg_json_init(&w, scratch, sizeof(scratch), acc_sink, &acc);
    fn(&w);
    esp_err_t err = wcfg_json_finish(&w);
    if (err != ESP_OK) {
        snprintf(out, out_cap, "<writer error %d>", (int)err);
    }
    return out;
}

/* ------------------------------------------------------------ the six shapes */

static void b_status(wcfg_json_w *w)
{
    wcfg_json_obj_open(w, NULL);
    wcfg_json_str(w, "state", "connected");
    wcfg_json_str(w, "ssid", "MyAP");
    wcfg_json_int(w, "rssi", -67);
    wcfg_json_int(w, "quality", 78);
    wcfg_json_int(w, "channel", 6);
    wcfg_json_str(w, "ip", "192.168.1.42");
    wcfg_json_str(w, "netmask", "255.255.255.0");
    wcfg_json_str(w, "gateway", "192.168.1.1");
    wcfg_json_str(w, "dns", "192.168.1.1");
    wcfg_json_str(w, "mac", "B4:E6:2D:96:3F:99");
    wcfg_json_str(w, "hostname", "esp32-abc");
    wcfg_json_int(w, "uptime_ms", 4294967295LL);   /* uint32 max */
    wcfg_json_bool(w, "ap_active", false);
    wcfg_json_obj_close(w);
}

static cJSON *t_status(void)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "state", "connected");
    cJSON_AddStringToObject(j, "ssid", "MyAP");
    cJSON_AddNumberToObject(j, "rssi", -67);
    cJSON_AddNumberToObject(j, "quality", 78);
    cJSON_AddNumberToObject(j, "channel", 6);
    cJSON_AddStringToObject(j, "ip", "192.168.1.42");
    cJSON_AddStringToObject(j, "netmask", "255.255.255.0");
    cJSON_AddStringToObject(j, "gateway", "192.168.1.1");
    cJSON_AddStringToObject(j, "dns", "192.168.1.1");
    cJSON_AddStringToObject(j, "mac", "B4:E6:2D:96:3F:99");
    cJSON_AddStringToObject(j, "hostname", "esp32-abc");
    cJSON_AddNumberToObject(j, "uptime_ms", 4294967295.0);
    cJSON_AddBoolToObject(j, "ap_active", 0);
    return j;
}

/* SSIDs are attacker-supplied bytes. Every escape cJSON knows, in one string. */
static const char *NASTY =
    "quote\" back\\slash tab\there nl\nhere cr\rhere bs\bff\f "
    "ctrl\x01\x1f end \xc3\xa9\xe2\x82\xac";

static void b_scan(wcfg_json_w *w)
{
    wcfg_json_obj_open(w, NULL);
    wcfg_json_arr_open(w, "networks");
    wcfg_json_obj_open(w, NULL);
    wcfg_json_str(w, "ssid", NASTY);
    wcfg_json_int(w, "rssi", -100);
    wcfg_json_str(w, "auth", "WPA/WPA2");
    wcfg_json_obj_close(w);
    wcfg_json_obj_open(w, NULL);
    wcfg_json_str(w, "ssid", "");
    wcfg_json_int(w, "rssi", 0);
    wcfg_json_str(w, "auth", "OPEN");
    wcfg_json_obj_close(w);
    wcfg_json_arr_close(w);
    wcfg_json_obj_close(w);
}

static cJSON *t_scan(void)
{
    cJSON *j = cJSON_CreateObject();
    cJSON *a = cJSON_AddArrayToObject(j, "networks");
    cJSON *n1 = cJSON_CreateObject();
    cJSON_AddStringToObject(n1, "ssid", NASTY);
    cJSON_AddNumberToObject(n1, "rssi", -100);
    cJSON_AddStringToObject(n1, "auth", "WPA/WPA2");
    cJSON_AddItemToArray(a, n1);
    cJSON *n2 = cJSON_CreateObject();
    cJSON_AddStringToObject(n2, "ssid", "");
    cJSON_AddNumberToObject(n2, "rssi", 0);
    cJSON_AddStringToObject(n2, "auth", "OPEN");
    cJSON_AddItemToArray(a, n2);
    return j;
}

/* An empty array is its own shape: the comma bookkeeping differs. */
static void b_empty(wcfg_json_w *w)
{
    wcfg_json_obj_open(w, NULL);
    wcfg_json_arr_open(w, "networks");
    wcfg_json_arr_close(w);
    wcfg_json_obj_close(w);
}

static cJSON *t_empty(void)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddArrayToObject(j, "networks");
    return j;
}

/* An array followed by scalars: catches need_comma not being restored on close. */
static void b_after_array(wcfg_json_w *w)
{
    wcfg_json_obj_open(w, NULL);
    wcfg_json_bool(w, "active", true);
    wcfg_json_arr_open(w, "clients");
    wcfg_json_obj_open(w, NULL);
    wcfg_json_str(w, "mac", "AA:BB:CC:DD:EE:FF");
    wcfg_json_obj_close(w);
    wcfg_json_arr_close(w);
    wcfg_json_int(w, "sta_count", 1);
    wcfg_json_obj_close(w);
}

static cJSON *t_after_array(void)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddBoolToObject(j, "active", 1);
    cJSON *a = cJSON_AddArrayToObject(j, "clients");
    cJSON *c = cJSON_CreateObject();
    cJSON_AddStringToObject(c, "mac", "AA:BB:CC:DD:EE:FF");
    cJSON_AddItemToArray(a, c);
    cJSON_AddNumberToObject(j, "sta_count", 1);
    return j;
}

/* Integer edges, including the ones cJSON reaches by its %g branch. */
static void b_ints(wcfg_json_w *w)
{
    wcfg_json_obj_open(w, NULL);
    wcfg_json_int(w, "zero", 0);
    wcfg_json_int(w, "neg", -1);
    wcfg_json_int(w, "int_max", 2147483647LL);
    wcfg_json_int(w, "int_min", -2147483648LL);
    wcfg_json_int(w, "u32_max", 4294967295LL);
    wcfg_json_obj_close(w);
}

static cJSON *t_ints(void)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "zero", 0);
    cJSON_AddNumberToObject(j, "neg", -1);
    cJSON_AddNumberToObject(j, "int_max", 2147483647.0);
    cJSON_AddNumberToObject(j, "int_min", -2147483648.0);
    cJSON_AddNumberToObject(j, "u32_max", 4294967295.0);
    return j;
}

/* Keys are escaped too -- custom variable names come from the API. */
static void b_keys(wcfg_json_w *w)
{
    wcfg_json_obj_open(w, NULL);
    wcfg_json_arr_open(w, "vars");
    wcfg_json_obj_open(w, NULL);
    wcfg_json_str(w, "key", "a\"b");
    wcfg_json_str(w, "value", "line1\nline2");
    wcfg_json_obj_close(w);
    wcfg_json_arr_close(w);
    wcfg_json_obj_close(w);
}

static cJSON *t_keys(void)
{
    cJSON *j = cJSON_CreateObject();
    cJSON *a = cJSON_AddArrayToObject(j, "vars");
    cJSON *v = cJSON_CreateObject();
    cJSON_AddStringToObject(v, "key", "a\"b");
    cJSON_AddStringToObject(v, "value", "line1\nline2");
    cJSON_AddItemToArray(a, v);
    return j;
}

/* ---------------------------------------------------------- error behaviour */

static void expect_err(const char *name, esp_err_t got, esp_err_t want)
{
    checks++;
    if (got == want) {
        printf("  %-40s ok  (err %d)\n", name, (int)got);
    } else {
        printf("  %-40s WRONG ERROR: got %d want %d\n", name, (int)got, (int)want);
        failures++;
    }
}

static void error_cases(void)
{
    char small[8];
    wcfg_json_w w;

    /* Buffer mode with nowhere to grow must report, not truncate silently. */
    wcfg_json_init(&w, small, sizeof(small), NULL, NULL);
    wcfg_json_obj_open(&w, NULL);
    wcfg_json_str(&w, "a_long_key", "a_long_value");
    wcfg_json_obj_close(&w);
    expect_err("buffer overflow reports NO_MEM", wcfg_json_finish(&w), ESP_ERR_NO_MEM);

    /* An unbalanced document must not be sent as if it were fine. */
    char buf[64];
    wcfg_json_init(&w, buf, sizeof(buf), NULL, NULL);
    wcfg_json_obj_open(&w, NULL);
    wcfg_json_str(&w, "k", "v");
    expect_err("unclosed object reports INVALID_STATE", wcfg_json_finish(&w),
               ESP_ERR_INVALID_STATE);

    /* Closing more than was opened. */
    wcfg_json_init(&w, buf, sizeof(buf), NULL, NULL);
    wcfg_json_obj_open(&w, NULL);
    wcfg_json_obj_close(&w);
    wcfg_json_obj_close(&w);
    expect_err("over-close reports INVALID_STATE", wcfg_json_finish(&w),
               ESP_ERR_INVALID_STATE);

    /* Buffer mode success must NUL-terminate. */
    wcfg_json_init(&w, buf, sizeof(buf), NULL, NULL);
    wcfg_json_obj_open(&w, NULL);
    wcfg_json_int(&w, "n", 42);
    wcfg_json_obj_close(&w);
    expect_err("buffer mode succeeds", wcfg_json_finish(&w), ESP_OK);
    checks++;
    if (strcmp(buf, "{\"n\":42}") == 0) {
        printf("  %-40s ok\n", "buffer mode content");
    } else {
        printf("  %-40s got '%s'\n", "buffer mode content", buf);
        failures++;
    }
}

int main(void)
{
    static char out[8192];

    printf("differential: writer vs cJSON_PrintUnformatted\n");
    compare("GET /status",            t_status(),      write_with(b_status, out, sizeof(out)));
    compare("GET /scan (nasty SSID)", t_scan(),        write_with(b_scan, out, sizeof(out)));
    compare("empty array",            t_empty(),       write_with(b_empty, out, sizeof(out)));
    compare("scalar after array",     t_after_array(), write_with(b_after_array, out, sizeof(out)));
    compare("integer edges",          t_ints(),        write_with(b_ints, out, sizeof(out)));
    compare("escaped keys and values", t_keys(),       write_with(b_keys, out, sizeof(out)));

    printf("\nerror behaviour\n");
    error_cases();

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    return failures != 0;
}
