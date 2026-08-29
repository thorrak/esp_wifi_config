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

# ---------------------------------------------------------------------------
# Sanitizer selection
#
# The sanitizers are the point of this test as much as the assertions are: a
# writer that produces the right bytes by reading one past its buffer has not
# passed. So the default is address+undefined and it is only reduced when the
# host cannot run it -- loudly, never silently.
#
# It has to be *probed* rather than assumed, because a broken ASan does not
# fail, it HANGS, before main(). On macOS 26 with Apple clang 17 the ASan
# runtime deadlocks in its own initialiser:
#
#   AsanInitInternal -> InitializeShadowMemory -> MemoryRangeIsAvailable
#     -> get_dyld_hdr -> dyld_shared_cache_iterate_text_swift -> _Block_copy
#       -> malloc -> __sanitizer_mz_malloc      (back into ASan, mid-init)
#
# dyld allocates while ASan is still wiring up its malloc interceptor. That is
# an interceptor-vs-dyld version mismatch in the toolchain, not anything this
# test does, and no amount of test-side change fixes it. A CI job that
# inherited that combination would hang forever rather than report.
#
# UBSan is unaffected -- it installs no allocator -- so the fallback keeps it
# and loses only the memory-safety half.
#
# Override with JSON_WRITER_SAN to force a choice, e.g. JSON_WRITER_SAN=''.
probe_sanitizer() {
    # Compile and RUN a trivial program under $1, bounded. macOS ships no
    # timeout(1), hence the poll loop. Prints nothing; returns 0 if usable.
    local flags="$1" probe="$OUT/sanprobe" rc="$OUT/sanprobe.rc" pid n=0
    printf '#include <stdlib.h>\nint main(void){volatile int*p=malloc(4);*p=1;int r=*p;free((void*)p);return r-1;}\n' \
        > "$OUT/sanprobe.c"
    cc -std=c99 $flags "$OUT/sanprobe.c" -o "$probe" 2>/dev/null || return 1
    rm -f "$rc"
    ( "$probe" >/dev/null 2>&1; echo $? > "$rc" ) & pid=$!
    while [[ $n -lt 10 ]]; do [[ -f "$rc" ]] && break; sleep 1; n=$((n+1)); done
    if [[ -f "$rc" ]]; then return 0; fi
    # `wait` after the kill reaps the job, which is what stops bash printing
    # its own "Killed: 9" line into the middle of the warning below.
    kill -9 "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    return 1
}

if [[ -n "${JSON_WRITER_SAN+set}" ]]; then
    SAN="$JSON_WRITER_SAN"
    echo "sanitizers: '$SAN' (forced via JSON_WRITER_SAN)"
elif probe_sanitizer "-fsanitize=address,undefined"; then
    SAN="-fsanitize=address,undefined"
    echo "sanitizers: address+undefined"
elif probe_sanitizer "-fsanitize=undefined"; then
    SAN="-fsanitize=undefined"
    echo "WARNING: this host's AddressSanitizer runtime hangs before main()," >&2
    echo "         so it is disabled and only UndefinedBehaviorSanitizer runs." >&2
    echo "         The differential assertions below are unaffected, but nothing" >&2
    echo "         is checking for out-of-bounds or use-after-free in this run." >&2
    echo "         Toolchain: $(cc --version | head -1)" >&2
else
    SAN=""
    echo "WARNING: no working sanitizer on this host; running unsanitized." >&2
    echo "         Toolchain: $(cc --version | head -1)" >&2
fi

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
