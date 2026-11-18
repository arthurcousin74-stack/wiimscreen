#!/usr/bin/env bash
# Self-test of the desktop build: wiimscreen -> mock WiiM (HTTPS, self-signed certificate), direct connection (no
# relay). Checks status, presets, a command and certificate pinning. Needs openssl, python3 and pio run -e desktop.
set -euo pipefail
cd "$(dirname "$0")/../.."
T=$(mktemp -d)
trap 'kill $(jobs -p) 2>/dev/null || true; rm -rf "$T"' EXIT
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$T/key.pem" -out "$T/cert.pem" -days 1 -subj "/CN=wiim-mock" 2>/dev/null
python3 tools/relay/mock_wiim.py 4444 "$T/cert.pem" "$T/key.pem" > "$T/mock.log" 2>&1 &
# Wait until the mock WiiM listens (at most 10 s)
for _ in $(seq 50); do
    if python3 -c 'import socket; socket.create_connection(("127.0.0.1", 4444), 1).close()' 2>/dev/null; then break; fi
    sleep 0.2
done
# 1280x720: the pause button is at 932,348 (as on the Tab5)
printf 'wait 3000\ntap 932 348\nwait 2000\nquit\n' > "$T/script.txt"
timeout 60 .pio/build/desktop/program --headless --state "$T/state" --out "$T" --wiim 127.0.0.1:4444 \
    --script "$T/script.txt" > "$T/app.log" 2>&1
fail() { echo "SELFTEST FAILED: $1"; cat "$T/app.log"; echo "SELFTEST FAILED: $1"; exit 1; }
# Journal assertions match tagged lines only (src/app/log.h): "<time>  [tag] key=value ... | <French sentence>"
HINT="a Journal tag changed or a line is no longer logged: see src/app/log.h"
has() { grep -q -- "\\[$1\\] $2" "$T/app.log" || fail "$3: no Journal line \"[$1] $2\" ($HINT)"; }
has wiim.presets "result=ok count=2 " "presets"
has wiim.status "result=ok " "status"
grep -q "setPlayerCmd:pause" "$T/mock.log" || fail "pause command"
PIN=$(openssl x509 -in "$T/cert.pem" -outform der | sha256sum | cut -c1-64)
grep -q "^fp	$PIN" "$T/state/settings.txt" || fail "certificate pinning"
echo "desktop selftest OK: status, presets, command and pinning, direct connection"
