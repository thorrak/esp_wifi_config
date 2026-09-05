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
#: Returned by run_bounded when it had to kill the command. 124 is what
#: timeout(1) uses, which this stands in for on a host that has none.
TIMED_OUT=124

#: Seconds a sanitizer probe may take. It runs six instructions; anything
#: slower than this is the runtime deadlocking, not the program working.
PROBE_TIMEOUT_S=10

#: Seconds the differential test may take. Generous -- it is the real test and
#: a sanitized build is slow -- but finite, which is the point.
DIFF_TIMEOUT_S="${JSON_WRITER_TIMEOUT_S:-120}"

# Run a command with a wall-clock bound, killing the command itself if it
# overruns. Returns its exit status, or $TIMED_OUT if it had to be killed.
#
# The obvious shape does not work, and this script shipped it:
#
#     ( "$cmd"; echo $? > "$rc" ) & pid=$!   # <-- $pid is the SUBSHELL
#     kill -9 "$pid"
#
# Two commands inside the parentheses stop bash from exec-ing the subshell, so
# the command is the subshell's *child*. Killing the subshell orphans it, and
# a program wedged in ASan's initialiser then spins forever. Measured
# 2026-08-31 on this repo: seven such processes -- five probes and two
# differential runs from before the probe existed -- at roughly 3100 CPU
# minutes each, the top consumers on the machine, holding unlinked inodes from
# temp directories the EXIT trap had already removed.
#
# Nothing reported it, which is the worse half: `wait` reaps the subshell, the
# function returns "timed out" exactly as designed, and the fallback message
# prints correctly. The script believed it had bounded something it had not.
#
# Backgrounding the command directly makes $! the command, so the kill lands.
# This bounds a single process; a command that forked children of its own would
# want `set -m` and `kill -9 -"$pid"` for the group.
run_bounded() {
    local limit="$1"; shift
    local pid n=0 rc=0
    "$@" & pid=$!
    while (( n < limit )); do
        if ! kill -0 "$pid" 2>/dev/null; then
            wait "$pid" 2>/dev/null || rc=$?
            return $rc
        fi
        sleep 1
        n=$((n+1))
    done
    # It may have finished during that last sleep.
    if ! kill -0 "$pid" 2>/dev/null; then
        wait "$pid" 2>/dev/null || rc=$?
        return $rc
    fi
    kill -9 "$pid" 2>/dev/null || true
    # `wait` after the kill reaps the job, which is what stops bash printing
    # its own "Killed: 9" line into the middle of whatever prints next.
    wait "$pid" 2>/dev/null || true
    return $TIMED_OUT
}

probe_sanitizer() {
    # Compile and RUN a trivial program under $1, bounded. macOS ships no
    # timeout(1), hence run_bounded. Prints nothing; returns 0 if usable.
    local flags="$1" probe="$OUT/sanprobe" rc=0
    printf '#include <stdlib.h>\nint main(void){volatile int*p=malloc(4);*p=1;int r=*p;free((void*)p);return r-1;}\n' \
        > "$OUT/sanprobe.c"
    cc -std=c99 $flags "$OUT/sanprobe.c" -o "$probe" 2>/dev/null || return 1
    run_bounded "$PROBE_TIMEOUT_S" "$probe" >/dev/null 2>&1 || rc=$?
    # Only a hang disqualifies a sanitizer. A non-zero exit means the runtime
    # started and had an opinion, which is all this is asking.
    [[ $rc -ne $TIMED_OUT ]]
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

# Bounded for the same reason the probe is. Until 2026-08-31 the throwaway
# probe was bounded and the actual test was not, which is backwards: a hang
# here is the one that costs you a CI job or an afternoon.
rc=0
run_bounded "$DIFF_TIMEOUT_S" "$OUT/differential" || rc=$?
if [[ $rc -eq $TIMED_OUT ]]; then
    echo "FAIL: differential did not finish within ${DIFF_TIMEOUT_S}s and was killed." >&2
    echo "      A hang is not a slow test. Either the writer is looping, or the" >&2
    echo "      sanitizer runtime deadlocked before main() -- run with" >&2
    echo "      JSON_WRITER_SAN='' to tell those apart, and raise the bound with" >&2
    echo "      JSON_WRITER_TIMEOUT_S if this host is simply slow." >&2
    exit 1
fi
exit $rc
