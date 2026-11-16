#!/usr/bin/env bash
# Self-test of the "real WiiM" simulator: simulator -> relay -> mock WiiM (HTTPS, self-signed certificate).
# Checks the status, a command, and certificate pinning. Needs: openssl, python3, a built simulator (pio run -e simreal).
set -euo pipefail
cd "$(dirname "$0")/../.."
T=$(mktemp -d)
trap 'kill $(jobs -p) 2>/dev/null || true; rm -rf "$T"' EXIT
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$T/key.pem" -out "$T/cert.pem" -days 1 -subj "/CN=wiim-mock" 2>/dev/null
python3 tools/relay/mock_wiim.py 4443 "$T/cert.pem" "$T/key.pem" > "$T/mock.log" 2>&1 &
python3 tools/relay/wiim_relay.py --allow-loopback --wiim 127.0.0.1 --listen 127.0.0.1:8799 > "$T/relay.log" 2>&1 &
# Wait until the mock and the relay listen (at most 10 s)
for _ in $(seq 50); do
    if python3 -c 'import socket,sys; [socket.create_connection(("127.0.0.1", p), 1).close() for p in (4443, 8799)]' 2>/dev/null; then break; fi
    sleep 0.2
done
mkdir -p "$T/state"
printf 'ssid\tSimulateur\nhost\t127.0.0.1:4443\nhttps\t1\n' > "$T/state/settings.txt"
printf 'wait 3000\ntap 932 348\nwait 2000\nquit\n' > "$T/script.txt"
timeout 60 .pio/build/simreal/program --headless --relay 127.0.0.1:8799 --state "$T/state" --out "$T" \
    --script "$T/script.txt" > "$T/sim.log" 2>&1
fail() { echo "SELFTEST FAILED: $1"; echo "--- sim"; cat "$T/sim.log"; echo "--- relay"; cat "$T/relay.log"; echo "--- mock"; cat "$T/mock.log"; exit 1; }
grep -q "Presets du WiiM : OK, 2" "$T/sim.log" || fail "presets"
grep -q "Relev.* : OK" "$T/sim.log" || fail "status"
grep -q "setPlayerCmd:pause" "$T/mock.log" || fail "pause command"
PIN=$(openssl x509 -in "$T/cert.pem" -outform der | sha256sum | cut -c1-64)
grep -q "^fp	$PIN" "$T/state/settings.txt" || fail "certificate pinning"
echo "selftest OK: status, presets, command and pinning through the relay"
