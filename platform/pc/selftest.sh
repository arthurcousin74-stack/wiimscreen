#!/usr/bin/env bash
# Self-test of the desktop build: wiimscreen -> mock WiiM (HTTPS, self-signed certificate), direct connection (no
# relay). Checks status, presets, a command and certificate pinning. Needs openssl, python3 and pio run -e desktop.
# Windows build: PROGRAM=.pio/build/windows/wiimscreen.exe platform/pc/selftest.sh (in MSYS2, or in WSL: the exe
# then reaches the mock WiiM of WSL on localhost and gets its folders as Windows paths).
# SELFTEST_ART_URL=https://...jpg: the mock WiiM gives this cover; the program must download it over HTTPS with a
# verified certificate (root certificates: system store, or cacert.pem beside wiimscreen.exe). Needs the Internet.
set -euo pipefail
cd "$(dirname "$0")/../.."
PROGRAM=${PROGRAM:-.pio/build/desktop/program}
# A folder as the program reads it: Windows path for a .exe started from WSL or MSYS2
native() {
    case "$PROGRAM" in
        *.exe) if command -v wslpath >/dev/null; then wslpath -w "$1"; elif command -v cygpath >/dev/null; then cygpath -m "$1"; else echo "$1"; fi ;;
        *) echo "$1" ;;
    esac
}
T=$(mktemp -d)
trap 'kill $(jobs -p) 2>/dev/null || true; rm -rf "$T"' EXIT
# (MSYS2: no path conversion of the subject)
MSYS2_ARG_CONV_EXCL=/CN= openssl req -x509 -newkey rsa:2048 -nodes -keyout "$T/key.pem" -out "$T/cert.pem" -days 1 -subj "/CN=wiim-mock" 2>/dev/null
ART=${SELFTEST_ART_URL:-}
python3 tools/relay/mock_wiim.py 4444 "$T/cert.pem" "$T/key.pem" ${ART:+--art-url "$ART"} > "$T/mock.log" 2>&1 &
# Wait until the mock WiiM listens (at most 10 s)
for _ in $(seq 50); do
    if python3 -c 'import socket; socket.create_connection(("127.0.0.1", 4444), 1).close()' 2>/dev/null; then break; fi
    sleep 0.2
done
# 1280x720: the pause button is at 932,348 (as on the Tab5)
# With a cover over HTTPS: more time for its download (a first connection to a CDN can take seconds)
printf 'wait 3000\ntap 932 348\nwait %d\nquit\n' "$([ -n "$ART" ] && echo 10000 || echo 2000)" > "$T/script.txt"
timeout 60 "$PROGRAM" --headless --state "$(native "$T/state")" --out "$(native "$T")" --wiim 127.0.0.1:4444 \
    --script "$(native "$T/script.txt")" > "$T/app.log" 2>&1
fail() { echo "SELFTEST FAILED: $1"; cat "$T/app.log"; echo "SELFTEST FAILED: $1"; exit 1; }
# Journal assertions match tagged lines only (src/app/log.h): "<time>  [tag] key=value ... | <French sentence>"
HINT="a Journal tag changed or a line is no longer logged: see src/app/log.h"
has() { grep -q -- "\\[$1\\] $2" "$T/app.log" || fail "$3: no Journal line \"[$1] $2\" ($HINT)"; }
has wiim.presets "result=ok count=2 " "presets"
has wiim.status "result=ok " "status"
grep -q "setPlayerCmd:pause" "$T/mock.log" || fail "pause command"
PIN=$(openssl x509 -in "$T/cert.pem" -outform der | sha256sum | cut -c1-64)
grep -q "^fp	$PIN" "$T/state/settings.txt" || fail "certificate pinning"
if [ -n "$ART" ]; then
    ART_HOST=$(echo "$ART" | sed -E 's|^https://([^/:]+).*|\1|')
    grep -qF "[tls] host=$ART_HOST verified=1 " "$T/app.log" || fail "cover over HTTPS: certificate of $ART_HOST not verified"
    if grep -E "pochette :|\[W\] TLS" "$T/app.log"; then fail "cover over HTTPS"; fi
    echo "desktop selftest: cover over HTTPS from $ART_HOST, certificate verified"
fi
echo "desktop selftest OK: status, presets, command and pinning, direct connection"
