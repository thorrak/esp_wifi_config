/**
 * @file esp_wifi_config_json.h
 * @brief Streaming JSON writer. Private to the component.
 *
 * Replaces the build-a-tree-then-serialise pattern for every response this
 * library emits. Two reasons, in order of importance:
 *
 *   1. Peak memory. A 20-result /scan used to allocate ~123 cJSON nodes and
 *      then a second buffer to serialise them into -- about 6.4 KB of heap for
 *      one response, on a part with ~230 KB free. This writes into a caller-
 *      supplied scratch buffer and flushes as it fills, so the peak is the
 *      scratch buffer. It also removes a failure mode: cJSON_PrintUnformatted()
 *      returning NULL under fragmentation used to become an HTTP 500.
 *
 *   2. Flash. Once nothing in the image calls cJSON_Print*, --gc-sections drops
 *      print_number() and with it the sscanf("%lg") round-trip cJSON uses to
 *      check a printed double survives -- which is what links newlib's floating
 *      point scanf, mprec and gdtoa. Measured at ~11 KB in builds that do not
 *      enable Network Provisioning BLE. (In prov-BLE builds the
 *      wifi_provisioning component calls cJSON_Print itself, so the code stays
 *      reachable regardless and only benefit 1 applies.)
 *
 * Output is byte-for-byte identical to cJSON_PrintUnformatted() for the value
 * types this library emits -- same escaping rules, same integer formatting.
 * test/json_writer/ asserts that against real cJSON rather than trusting it.
 *
 * There is no parser here. Requests are still parsed by cJSON, deliberately:
 * that is the path untrusted bytes arrive on, and it is not worth hand-rolling
 * for the remaining few kilobytes.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Scratch buffer size used by the HTTP handlers. */
#define WCFG_JSON_SCRATCH_SIZE 256

/** Refused beyond this. Nothing this library emits is deeper than 3. */
#define WCFG_JSON_MAX_DEPTH 8

/**
 * @brief Consumes a run of output bytes.
 * @return ESP_OK to continue; anything else aborts the write.
 */
typedef esp_err_t (*wcfg_json_sink_fn)(void *ctx, const char *data, size_t len);

typedef struct {
    wcfg_json_sink_fn sink;   /**< NULL: accumulate in buf, overflow is an error */
    void   *ctx;
    char   *buf;
    size_t  cap;
    size_t  len;
    bool    need_comma;
    uint8_t depth;
    esp_err_t err;            /**< first error; all later calls are no-ops */
} wcfg_json_w;

/**
 * @brief Start a writer.
 * @param sink NULL for buffer mode: everything accumulates in @p buf and
 *             overflowing it is ESP_ERR_NO_MEM. Non-NULL for streaming mode:
 *             @p buf is scratch and is flushed through @p sink as it fills.
 */
void wcfg_json_init(wcfg_json_w *w, char *buf, size_t cap,
                    wcfg_json_sink_fn sink, void *ctx);

/* `key` is NULL for a value inside an array, non-NULL inside an object. */
void wcfg_json_obj_open (wcfg_json_w *w, const char *key);
void wcfg_json_obj_close(wcfg_json_w *w);
void wcfg_json_arr_open (wcfg_json_w *w, const char *key);
void wcfg_json_arr_close(wcfg_json_w *w);
void wcfg_json_str      (wcfg_json_w *w, const char *key, const char *val);
void wcfg_json_int      (wcfg_json_w *w, const char *key, int64_t val);
void wcfg_json_bool     (wcfg_json_w *w, const char *key, bool val);

/**
 * @brief Flush and report. Also fails if a container was left open.
 * @return ESP_OK, ESP_ERR_NO_MEM (buffer mode overflow),
 *         ESP_ERR_INVALID_STATE (unbalanced), or whatever the sink returned.
 */
esp_err_t wcfg_json_finish(wcfg_json_w *w);

/** Bytes accumulated. Buffer mode only; meaningless while streaming. */
size_t wcfg_json_len(const wcfg_json_w *w);

#ifdef __cplusplus
}
#endif
