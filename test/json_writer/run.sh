#!/usr/bin/env bash
# Host-side differential test for the streaming JSON writer.
#
#   ./test/json_writer/run.sh
#
# Needs a copy of cJSON to compare against; uses the one in ESP-IDF.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
IDF="${IDF_PATH:-${BENCH_IDF_PATH:-$HOME/esp/v5.4.3/esp-idf}}"
CJSON="$IDF/components/json/cJSON"
[[ -f "$CJSON/cJSON.c" ]] || { echo "no cJSON at $CJSON (set IDF_PATH)" >&2; exit 2; }

OUT="$(mktemp -d)"; trap 'rm -rf "$OUT"' EXIT
SAN="-fsanitize=address,undefined"

# cJSON is the reference implementation, not code under test: compile it
# permissively. macOS marks sprintf() deprecated and cJSON uses it six times,
# which -Werror would otherwise turn into a build failure in someone else's
# code.
cc -std=c99 -g $SAN -Wno-deprecated-declarations -I"$CJSON" \
   -c "$CJSON/cJSON.c" -o "$OUT/cJSON.o"

# Ours is code under test: compile it as strictly as the component build does,
# and then some.
cc -std=c99 -Wall -Wextra -Werror -g $SAN \
   -I"$HERE/shim" -I"$ROOT/src" -I"$CJSON" \
   "$HERE/differential.c" "$ROOT/src/esp_wifi_config_json.c" "$OUT/cJSON.o" \
   -lm -o "$OUT/differential"
"$OUT/differential"
