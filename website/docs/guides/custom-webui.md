---
sidebar_position: 5
title: Custom Web UI
description: Ship your own captive-portal frontend from LittleFS / SPIFFS instead of the embedded Preact UI
---

# Custom Web UI

The library can serve a captive-portal frontend from a filesystem
partition (LittleFS or SPIFFS) instead of the embedded Preact UI. This
lets you ship your own branding, framework, and content without
recompiling firmware — just rewrite the filesystem image and re-flash.

## Three Serving Modes

The Web UI source is selected at build time by two Kconfig keys:

| Mode | Kconfig | Source of files |
|---|---|---|
| **Embedded Web UI** (default when enabled) | `WIFI_CFG_ENABLE_WEBUI=y`, `WIFI_CFG_WEBUI_CUSTOM_PATH=""` | Bundled Preact app (~10 KB gzipped) linked into the firmware via `EMBED_FILES` |
| **Custom filesystem UI** | `WIFI_CFG_ENABLE_WEBUI=y`, `WIFI_CFG_WEBUI_CUSTOM_PATH="/littlefs"` | Files on the configured filesystem path |
| **Simple fallback page** | `WIFI_CFG_ENABLE_WEBUI=n` | A built-in minimal HTML page with inline JS, just enough to add a network and connect |

:::caution Custom path replaces embedded — no fallback
When `WIFI_CFG_WEBUI_CUSTOM_PATH` is set, the embedded Preact assets are
**excluded from the build entirely** (the CMake `EMBED_FILES` list
drops them and the corresponding C handler is `#ifndef`-gated). If a
file isn't found on the filesystem, the request fails — there is no
runtime fallback to embedded content. Test your filesystem image
contains the expected files before flashing.
:::

## Required File Layout

The HTTP server serves exactly three URL paths. Your filesystem image
must provide files that match:

| URL | Filesystem path (under `WEBUI_CUSTOM_PATH`) | Notes |
|---|---|---|
| `/` | `/index.html` | Main HTML document. `/` is internally remapped to `/index.html`. |
| `/assets/app.js` | `/assets/app.js` or `/assets/app.js.gz` | JS bundle. Single-file output required (no code splitting). |
| `/assets/index.css` | `/assets/index.css` or `/assets/index.css.gz` | CSS stylesheet. |

### Gzip handling

The server checks for both the plain file and a `.gz` sibling for every
request:

- If only the plain file exists → serve plain.
- If only the gzipped file exists → serve gzipped, add `Content-Encoding: gzip`.
- If **both** exist → prefer the gzipped variant (smaller response, same `Content-Type`).

Gzipping is recommended for `app.js` and `index.css` — the embedded
Preact build ships both as `.js.gz` and `.css.gz` and saves around 70%
on the wire.

### Content types

The server auto-sets `Content-Type` from the file extension. Supported
extensions: `.html`, `.css`, `.js`, `.json`, `.svg`, `.png`. Unknown
extensions fall back to `application/octet-stream`.

## Minimal HTML Template

A custom frontend can use any framework or none at all. The only hard
requirements are:

- Mount JS at `/assets/app.js`.
- Mount CSS at `/assets/index.css`.
- Output a single JS bundle (no dynamic imports / code splitting).

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>My Device Setup</title>
  <link rel="stylesheet" href="/assets/index.css">
  <script type="module" crossorigin src="/assets/app.js"></script>
</head>
<body>
  <div id="app"></div>
</body>
</html>
```

The DOM structure inside `<body>` is entirely up to you — the library
serves the files but does not require any particular markup.

## Vite Build Configuration

If your frontend uses Vite (the bundled Preact app does), the relevant
rollup output settings are:

```typescript
// vite.config.ts
import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';
import { compression } from 'vite-plugin-compression2';

export default defineConfig({
  plugins: [
    preact(),
    compression({ algorithm: 'gzip' }),  // emit .gz alongside originals
  ],
  build: {
    rollupOptions: {
      output: {
        inlineDynamicImports: true,      // force single JS bundle
        entryFileNames: 'assets/app.js', // fixed filename, no hash
        assetFileNames: 'assets/[name].[ext]',  // index.css (no hash)
      },
    },
  },
});
```

The non-negotiable bits are:

- **`inlineDynamicImports: true`** — the HTTP server only serves three
  fixed paths; dynamic chunks would be unreachable.
- **Fixed `app.js` / `index.css` output names** — no content hashes,
  since the server has hardcoded handlers for those exact URLs.

The bundled frontend lives at [`frontend/`](https://github.com/thorrak/esp_wifi_config/tree/main/frontend);
its `vite.config.ts` is the canonical reference.

## Deployment: Custom Frontend on LittleFS

A complete worked example lives at
[`examples/with_webui_customize/`](https://github.com/thorrak/esp_wifi_config/tree/main/examples/with_webui_customize)
— this section summarises the moving parts.

### 1. sdkconfig

```
CONFIG_WIFI_CFG_ENABLE_WEBUI=y
CONFIG_WIFI_CFG_WEBUI_CUSTOM_PATH="/littlefs"
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

### 2. Partition table

Add a LittleFS data partition (here 512 KB at the tail of flash):

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 1M,
storage,  data, littlefs,,        512K,
```

Adjust `factory` size for your firmware and pick a `storage` size that
comfortably fits your assets (compressed). The bundled Preact UI is
~10 KB gzipped, but a richer custom app can easily reach 100–200 KB.

### 3. Place frontend output

```
www/
├── index.html
└── assets/
    ├── app.js.gz
    └── index.css.gz
```

### 4. Wire LittleFS into CMake

In the project's top-level `CMakeLists.txt`:

```cmake
littlefs_create_partition_image(storage www FLASH_IN_PROJECT)
```

`storage` must match the partition name from `partitions.csv`. The
component `joltwallet/littlefs` (or the in-tree LittleFS port) needs to
be in `idf_component.yml` for this CMake function to exist.

### 5. Build and flash

```bash
idf.py build flash
```

The library mounts the partition automatically when the HTTP server
starts and serves files from `/littlefs/...` for any request matching
the three fixed URLs.

## Iterating on the Frontend

Because the filesystem image is independent of the firmware binary,
frontend-only changes don't require rebuilding firmware. After editing
your frontend:

```bash
# Rebuild frontend → www/
npm --prefix frontend run build

# Re-flash just the LittleFS partition
idf.py littlefs-flash
```

(The exact target name depends on your LittleFS component; some expose
`storage-flash` or similar.)

## Captive-Portal Behaviour

Captive-portal detection probes are handled regardless of which Web UI
mode is active:

| Path | Platform |
|---|---|
| `/generate_204`, `/gen_204` | Android |
| `/hotspot-detect.html`, `/library/test/success.html` | iOS / macOS |
| `/ncsi.txt`, `/connecttest.txt` | Windows |
| `/success.txt`, `/canonical.html` | Firefox |

All probes return an HTTP 302 redirect to `http://<AP_IP>/`, which the
phone/laptop then renders inside its captive-portal popup. This works
whether `/` is served from the embedded UI, your custom filesystem UI,
or the simple fallback page.

## Talking to the Backend

The custom frontend communicates with the device over the REST API
documented in [REST API Reference](../api/rest-api). Base path defaults
to `/api/wifi` and is configurable through `wifi_cfg_http_config_t`:

```c
.http = {
    .api_base_path = "/api/wifi",   // default
    .enable_auth   = false,         // set true for HTTP Basic Auth
    .pre_request_hook = my_hook,    // optional; see HTTP Server Sharing
},
```

If you need to host other endpoints alongside the WiFi Config API on
the same server, see [HTTP Server Sharing](./http-server-sharing).

## Reference Example

The full deployment lives at [`examples/with_webui_customize/`](https://github.com/thorrak/esp_wifi_config/tree/main/examples/with_webui_customize) —
it copies the bundled Preact frontend into the example's `www/`
directory and demonstrates flashing both firmware and LittleFS image
from a single `idf.py build flash`.
