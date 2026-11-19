#!/usr/bin/env bash
# Every CI gate of .github/workflows/firmware.yml, with the same commands, in the same order.
# CI calls this script once per gate; run it locally before a pull request:
#
#   tools/check.sh                 # all gates
#   tools/check.sh native preview  # some gates (names below)
#
# Gates: native sim preview asan simreal desktop tab5
#   native   unit tests of lib/core (AddressSanitizer + UBSan built in)      needs: a C++ compiler
#   sim      simulator build + scripted tour, taps on every screen, keys     needs: SDL2 headers (libsdl2-dev)
#   preview  preview build + golden screens (tools/preview/check_golden.py)   needs: SDL2 headers
#   asan     simulator tour and taps under AddressSanitizer/UBSan             needs: SDL2 headers
#   simreal  "real WiiM" simulator + relay self-test (about 2 min)            needs: SDL2 headers, openssl, python3
#   desktop  computer build + self-test against a mock WiiM                   needs: SDL2 + OpenSSL headers (libssl-dev)
#   tab5     Tab5 firmware                                                    needs: PlatformIO downloads the toolchain
#
# A gate whose prerequisites are missing is SKIPPED with a message (it still runs in CI).
# Environment:
#   CHECK_SKIP_RELAY=1  build simreal but skip tools/relay/selftest.sh (it uses fixed local ports 4443, 49152, 8799)
#   CHECK_WERROR=1      warnings in the project's own files are errors, as in CI (PC gates only; see tools/werror.py)
#   CHECK_STRICT=1      a missing prerequisite fails instead of skipping (CI sets it)
set -uo pipefail
cd "$(dirname "$0")/.."

ALL=(native sim preview asan simreal desktop tab5)
GATES=("$@")
[ ${#GATES[@]} -eq 0 ] && GATES=("${ALL[@]}")
RESULTS=()
FAILED=0

have() { command -v "$1" >/dev/null 2>&1; }
have_sdl2() { have sdl2-config || pkg-config --exists sdl2 2>/dev/null || [ -f /usr/include/SDL2/SDL.h ]; }
have_ssl_headers() { pkg-config --exists openssl 2>/dev/null || [ -f /usr/include/openssl/ssl.h ]; }
# timeout(1) is missing on a stock macOS: run without a time limit there
tmo() { if have timeout; then timeout "$@"; else shift; "$@"; fi; }
# PC builds: -Werror on the project's own files when CHECK_WERROR=1 (never for tab5: framework headers warn)
pcpio() {
    if [ "${CHECK_WERROR:-0}" = 1 ]; then PLATFORMIO_EXTRA_SCRIPTS=pre:tools/werror.py pio "$@"; else pio "$@"; fi
}

# need <gate> <message> <test...>: true if the test passes; else SKIP (or FAIL with CHECK_STRICT=1)
need() {
    local gate=$1 msg=$2
    shift 2
    if "$@"; then return 0; fi
    if [ "${CHECK_STRICT:-0}" = 1 ]; then
        echo "check.sh: $gate: $msg" >&2
        RESULTS+=("FAIL  $gate ($msg)")
        FAILED=1
    else
        echo "check.sh: $gate SKIPPED: $msg"
        RESULTS+=("SKIP  $gate ($msg)")
    fi
    return 1
}

gate_native() {
    pcpio test -e native
}

gate_sim() {
    need sim "SDL2 headers missing (Debian/Ubuntu: sudo apt install libsdl2-dev; macOS: brew install sdl2)" have_sdl2 || return 2
    pcpio run -e sim &&
        tmo 120 .pio/build/sim/program --headless --quiet --reset --script tools/sim/scripts/tour.txt &&
        sim_taps && sim_keys
}

# Taps on every element of every screen (tools/sim/scripts/taps.txt): ends with Sleep (the program stops by itself)
sim_taps() {
    local out
    out=$(tmo 180 .pio/build/sim/program --headless --reset --state .pio/taps-state --script tools/sim/scripts/taps.txt) ||
        { echo "$out"; return 1; }
    if ! grep -q "Mise en veille (demand" <<<"$out"; then
        echo "check.sh: sim: the taps.txt run did not reach Sleep:"
        echo "$out"
        return 1
    fi
}

# Scripted keys (tools/sim/scripts/keys.txt): the events that --log-keys prints, through takeKey() in loop()
sim_keys() {
    local out want
    out=$(tmo 60 .pio/build/sim/program --headless --quiet --reset --log-keys --state .pio/keys-state \
        --out .pio/keys-out --script tools/sim/scripts/keys.txt) || { echo "$out"; return 1; }
    for want in "key=playpause kind=press" "key=ok kind=press" "key=ok kind=long" "key=down kind=press" \
        "key=down kind=repeat" "key=text kind=press | Miles Davis"; do
        if ! grep -qxF "[key] $want" <<<"$out"; then
            echo "check.sh: sim: '[key] $want' missing in the keys.txt run:"
            echo "$out"
            return 1
        fi
    done
}

gate_preview() {
    need preview "SDL2 headers missing (libsdl2-dev)" have_sdl2 || return 2
    pcpio run -e preview && python3 tools/preview/check_golden.py
}

gate_asan() {
    need asan "SDL2 headers missing (libsdl2-dev)" have_sdl2 || return 2
    # Separate build folder: the normal sim build is not touched. UBSan reports inside M5GFX (libdeps) are known.
    PLATFORMIO_BUILD_DIR=.pio/build-asan \
        PLATFORMIO_BUILD_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1" pcpio run -e sim || return 1
    local script
    : > .pio/asan.log
    for script in tour taps; do
        if ! ASAN_OPTIONS=detect_leaks=0 tmo 300 .pio/build-asan/sim/program --headless --quiet --reset \
            --state .pio/asan-state --out .pio/asan-out --script tools/sim/scripts/$script.txt 2>> .pio/asan.log; then
            cat .pio/asan.log
            return 1
        fi
    done
    if grep -v "libdeps" .pio/asan.log | grep -E "ERROR: AddressSanitizer|runtime error"; then return 1; fi
}

gate_simreal() {
    need simreal "SDL2 headers missing (libsdl2-dev)" have_sdl2 || return 2
    pcpio run -e simreal || return 1
    if [ "${CHECK_SKIP_RELAY:-0}" = 1 ]; then
        echo "check.sh: simreal: relay self-test skipped (CHECK_SKIP_RELAY=1)"
        RESULTS+=("SKIP  simreal relay self-test (CHECK_SKIP_RELAY=1; build passed)")
        return 3
    fi
    need simreal "openssl or python3 missing (relay self-test)" eval 'have openssl && have python3' || return 2
    ./tools/relay/selftest.sh
}

gate_desktop() {
    need desktop "SDL2 headers missing (libsdl2-dev)" have_sdl2 || return 2
    need desktop "OpenSSL headers missing (Debian/Ubuntu: sudo apt install libssl-dev)" have_ssl_headers || return 2
    need desktop "openssl or python3 missing (self-test)" eval 'have openssl && have python3' || return 2
    pcpio run -e desktop && ./platform/pc/selftest.sh
}

gate_tab5() {
    pio run -e tab5
}

if ! have pio; then
    echo "check.sh: pio not found. Install it: pip install platformio \"click<8.2\"" >&2
    exit 1
fi
for g in "${GATES[@]}"; do
    if ! declare -F "gate_$g" >/dev/null; then
        echo "check.sh: unknown gate '$g' (gates: ${ALL[*]})" >&2
        exit 2
    fi
done

for g in "${GATES[@]}"; do
    echo
    echo "=== check.sh: $g ==="
    START=$SECONDS
    "gate_$g"
    rc=$?
    case $rc in
        0) RESULTS+=("PASS  $g ($((SECONDS - START)) s)") ;;
        2 | 3) ;;  # skipped (or failed with CHECK_STRICT=1): already in RESULTS
        *) RESULTS+=("FAIL  $g"); FAILED=1 ;;
    esac
done

echo
echo "=== check.sh: summary ==="
printf '  %s\n' "${RESULTS[@]}"
if [ $FAILED -ne 0 ]; then
    echo "check.sh: FAILED"
    exit 1
fi
echo "check.sh: OK (a SKIP line is not a pass: CI runs that gate)"
