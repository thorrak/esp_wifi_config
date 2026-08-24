#include "esp_wifi_config_json.h"

#include <string.h>

/* ------------------------------------------------------------------ output */

static void w_raw(wcfg_json_w *w, const char *s, size_t n)
{
    if (w->err != ESP_OK) {
        return;
    }
    while (n > 0) {
        size_t space = w->cap - w->len;
        if (space == 0) {
            if (w->sink == NULL) {
                /* Buffer mode: there is nowhere to put it. */
                w->err = ESP_ERR_NO_MEM;
                return;
            }
            esp_err_t err = w->sink(w->ctx, w->buf, w->len);
            if (err != ESP_OK) {
                w->err = err;
                return;
            }
            w->len = 0;
            space = w->cap;
        }
        size_t take = (n < space) ? n : space;
        memcpy(w->buf + w->len, s, take);
        w->len += take;
        s      += take;
        n      -= take;
    }
}

/*
 * Escaping, matching cJSON's print_string_ptr() exactly: the seven two-character
 * escapes, \u00xx with LOWERCASE hex for anything else below 0x20, and every
 * other byte verbatim -- including the high halves of UTF-8 sequences, which
 * cJSON also passes through untouched.
 *
 * Runs of ordinary characters are emitted in one go rather than byte at a time.
 */
static void w_escaped(wcfg_json_w *w, const char *s)
{
    static const char hex[] = "0123456789abcdef";

    if (s == NULL) {
        s = "";
    }

    const char *run = s;
    for (const char *p = s; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        char        ubuf[6];
        const char *esc = NULL;
        size_t      esc_len = 2;

        switch (c) {
            case '\\': esc = "\\\\"; break;
            case '"':  esc = "\\\""; break;
            case '\b': esc = "\\b";  break;
            case '\f': esc = "\\f";  break;
            case '\n': esc = "\\n";  break;
            case '\r': esc = "\\r";  break;
            case '\t': esc = "\\t";  break;
            default:
                if (c < 0x20) {
                    ubuf[0] = '\\';
                    ubuf[1] = 'u';
                    ubuf[2] = '0';
                    ubuf[3] = '0';
                    ubuf[4] = hex[(c >> 4) & 0x0F];
                    ubuf[5] = hex[c & 0x0F];
                    esc     = ubuf;
                    esc_len = sizeof(ubuf);
                }
                break;
        }

        if (esc != NULL) {
            if (p > run) {
                w_raw(w, run, (size_t)(p - run));
            }
            w_raw(w, esc, esc_len);
            run = p + 1;
        }
    }
    if (*run != '\0') {
        w_raw(w, run, strlen(run));
    }
}

/*
 * Hand-rolled rather than snprintf("%lld"): pulling long-long formatting into
 * newlib's vfprintf costs more than this function does, and the whole point of
 * the writer is to stop dragging printf machinery into the image.
 */
static void w_int(wcfg_json_w *w, int64_t val)
{
    char     digits[20];
    char     out[21];
    size_t   n = 0;
    size_t   j = 0;
    bool     neg = (val < 0);

    /* Negate in unsigned space so INT64_MIN does not overflow. */
    uint64_t u = neg ? (~(uint64_t)val + 1u) : (uint64_t)val;

    do {
        digits[n++] = (char)('0' + (u % 10u));
        u /= 10u;
    } while (u != 0);

    if (neg) {
        out[j++] = '-';
    }
    while (n > 0) {
        out[j++] = digits[--n];
    }
    w_raw(w, out, j);
}

/* Comma from the previous sibling, then the key if we are inside an object. */
static void w_pre(wcfg_json_w *w, const char *key)
{
    if (w->need_comma) {
        w_raw(w, ",", 1);
    }
    if (key != NULL) {
        w_raw(w, "\"", 1);
        w_escaped(w, key);
        w_raw(w, "\":", 2);
    }
    w->need_comma = true;
}

static void w_open(wcfg_json_w *w, const char *key, char brace)
{
    if (w->depth >= WCFG_JSON_MAX_DEPTH) {
        if (w->err == ESP_OK) {
            w->err = ESP_ERR_INVALID_STATE;
        }
        return;
    }
    w_pre(w, key);
    w_raw(w, &brace, 1);
    w->depth++;
    /* Nothing has been emitted inside the new container yet. */
    w->need_comma = false;
}

static void w_close(wcfg_json_w *w, char brace)
{
    if (w->depth == 0) {
        if (w->err == ESP_OK) {
            w->err = ESP_ERR_INVALID_STATE;
        }
        return;
    }
    w_raw(w, &brace, 1);
    w->depth--;
    /* The container just closed IS a value in its parent, so a sibling after
     * it needs a comma -- regardless of whether the container was empty. */
    w->need_comma = true;
}

/* --------------------------------------------------------------- public API */

void wcfg_json_init(wcfg_json_w *w, char *buf, size_t cap,
                    wcfg_json_sink_fn sink, void *ctx)
{
    w->sink       = sink;
    w->ctx        = ctx;
    w->buf        = buf;
    w->cap        = cap;
    w->len        = 0;
    w->need_comma = false;
    w->depth      = 0;
    w->err        = (buf != NULL && cap > 0) ? ESP_OK : ESP_ERR_INVALID_ARG;
}

void wcfg_json_obj_open(wcfg_json_w *w, const char *key)  { w_open(w, key, '{'); }
void wcfg_json_obj_close(wcfg_json_w *w)                  { w_close(w, '}'); }
void wcfg_json_arr_open(wcfg_json_w *w, const char *key)  { w_open(w, key, '['); }
void wcfg_json_arr_close(wcfg_json_w *w)                  { w_close(w, ']'); }

void wcfg_json_str(wcfg_json_w *w, const char *key, const char *val)
{
    w_pre(w, key);
    w_raw(w, "\"", 1);
    w_escaped(w, val);
    w_raw(w, "\"", 1);
}

void wcfg_json_int(wcfg_json_w *w, const char *key, int64_t val)
{
    w_pre(w, key);
    w_int(w, val);
}

void wcfg_json_bool(wcfg_json_w *w, const char *key, bool val)
{
    w_pre(w, key);
    if (val) {
        w_raw(w, "true", 4);
    } else {
        w_raw(w, "false", 5);
    }
}

esp_err_t wcfg_json_finish(wcfg_json_w *w)
{
    if (w->err == ESP_OK && w->depth != 0) {
        w->err = ESP_ERR_INVALID_STATE;
    }
    if (w->err == ESP_OK && w->sink != NULL && w->len > 0) {
        esp_err_t err = w->sink(w->ctx, w->buf, w->len);
        if (err != ESP_OK) {
            w->err = err;
        }
        w->len = 0;
    }
    if (w->err == ESP_OK && w->sink == NULL) {
        /* Buffer mode callers pass the buffer to something expecting a C
         * string. Only terminate if there is room; running out is an error,
         * not a silent truncation. */
        if (w->len < w->cap) {
            w->buf[w->len] = '\0';
        } else {
            w->err = ESP_ERR_NO_MEM;
        }
    }
    return w->err;
}

size_t wcfg_json_len(const wcfg_json_w *w)
{
    return w->len;
}
