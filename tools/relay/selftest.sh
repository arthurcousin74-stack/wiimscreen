#!/usr/bin/env bash
# Self-test of the "real WiiM" simulator: simulator -> relay -> mock WiiM (HTTPS, self-signed certificate, UPnP
# PlayQueue on port 49152). Checks the status, a command, certificate pinning, and the search through a WiiM queue
# (shuffle on): results in order, music before put back (paused, playing), a result played from its album, a WiiM
# that loads nothing, a search cut by a restart. About 90 s.
# Needs: openssl, python3, a built simulator (pio run -e simreal).
set -euo pipefail
cd "$(dirname "$0")/../.."
T=$(mktemp -d)
trap 'kill $(jobs -p) 2>/dev/null || true; rm -rf "$T"' EXIT
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$T/key.pem" -out "$T/cert.pem" -days 1 -subj "/CN=wiim-mock" 2>/dev/null
python3 tools/relay/mock_wiim.py 4443 "$T/cert.pem" "$T/key.pem" --nofill-file "$T/nofill" > "$T/mock.log" 2>&1 &
python3 tools/relay/wiim_relay.py --allow-loopback --wiim 127.0.0.1 --listen 127.0.0.1:8799 > "$T/relay.log" 2>&1 &
# Wait until the mock and the relay listen (at most 10 s)
for _ in $(seq 50); do
    if python3 -c 'import socket,sys; [socket.create_connection(("127.0.0.1", p), 1).close() for p in (4443, 49152, 8799)]' 2>/dev/null; then break; fi
    sleep 0.2
done
mkdir -p "$T/state"
printf 'ssid\tSimulateur\nhost\t127.0.0.1:4443\nhttps\t1\n' > "$T/state/settings.txt"
N=0
fail() {
    echo "SELFTEST FAILED: $1"
    for f in "$T"/sim*.log; do echo "--- $f"; cat "$f"; done
    echo "--- relay"; cat "$T/relay.log"; echo "--- mock"; grep -v "getPlayerStatus\|getMetaInfo" "$T/mock.log"; exit 1
}
# run <script lines>: one start of the simulator (same stored state), Journal in $LOG
run() {
    N=$((N + 1)); LOG="$T/sim$N.log"
    printf '%b' "$1" > "$T/script.txt"
    timeout 90 .pio/build/simreal/program --headless --relay 127.0.0.1:8799 --state "$T/state" --out "$T" \
        --script "$T/script.txt" > "$LOG" 2>&1 || fail "simulator run $N"
}
has() { grep -q -- "$1" "$LOG" || fail "$2"; }
# mock <field>: a field of the mock's getPlayerStatus (Title decoded)
mock() {
    python3 - "$1" <<'PY'
import json, ssl, sys, urllib.request
c = ssl.create_default_context(); c.check_hostname = False; c.verify_mode = ssl.CERT_NONE
d = json.load(urllib.request.urlopen("https://127.0.0.1:4443/httpapi.asp?command=getPlayerStatus", context=c, timeout=5))
print(bytes.fromhex(d["Title"]).decode() if sys.argv[1] == "Title" else d[sys.argv[1]])
PY
}
is() { [ "$(mock "$1")" = "$2" ] || fail "$3 (mock $1 = $(mock "$1"), expected $2)"; }
# Keyboard (QWERTY, 1280x720): search icon, then m i l e s, then Search
MILES='tap 50 360\nwait 400\ntap 861 541\nwait 150\ntap 975 330\nwait 150\ntap 1089 435\nwait 150\ntap 405 330\nwait 150\ntap 291 435\nwait 300\ntap 1089 647\n'

# 1. Status, presets, pause (932,348), pinning. Then, paused in a queue with shuffle on: search "miles", play the
#    second result (600,300), search again while it plays.
run "wait 3000\ntap 932 348\nwait 2000\n${MILES}wait 14000\ntap 600 300\nwait 9000\ntap 50 360\nwait 600\ntap 1089 647\nwait 16000\nquit\n"
has "Presets du WiiM : OK, 2" "presets"
has "Relev.* : OK" "status"
grep -q "setPlayerCmd:pause" "$T/mock.log" || fail "pause command"
PIN=$(openssl x509 -in "$T/cert.pem" -outform der | sha256sum | cut -c1-64)
grep -q "^fp	$PIN" "$T/state/settings.txt" || fail "certificate pinning"
has "Avant la recherche : arr.*t « Mock Song » (file connue)" "paused track seen before the search"
has "7 r.*sultat(s), sans lecture, en rapport" "search results"
grep -A1 "7 r.*sultat(s)" "$LOG" | grep -q -- "- So What " || fail "results in order (shuffle off during the search)"
has "musique d'avant remise en pause (200, 200" "paused track put back"
has "atoire remise (OK)" "shuffle put back"
has "son r.*tabli (OK)" "sound put back"
has "Qobuz (album) .* « Freddie Freeloader » .*Play 200" "result played from its album"
has "Avant la recherche : lecture « Freddie Freeloader » (file connue)" "playing track seen before the search"
has "musique d'avant remise (200, 200" "playing track put back"
grep -q "setPlayerCmd:seek:" "$T/mock.log" || fail "seek to the position before"
is Title "Freddie Freeloader" "track before plays again"
is status play "track before plays again"
is mute 0 "sound on after the searches"
is loop 3 "shuffle on after the searches"
[ "$(mock curpos)" -ge 5000 ] || fail "position before put back (curpos $(mock curpos))"

# 2. The WiiM loads nothing, and the simulator stops 3 s after the search starts: the WiiM stays muted, shuffle off
touch "$T/nofill"
run "wait 3000\n${MILES}wait 3000\nquit\n"
is mute 1 "muted during the search"
is loop 4 "shuffle off during the search"
[ -s "$T/state/fs/fav/search_undo" ] || fail "note of the search changes in storage"

# 3. Next start: sound and shuffle put back. Then a search on this WiiM gives up after one empty queue.
run "wait 4000\n${MILES}wait 16000\nquit\n"
has "Recherche interrompue : son OK, lecture al.*atoire OK" "interrupted search undone at the next start"
[ "$(grep -c "Recherche « miles » .* : CreateQueue" "$LOG")" = 1 ] || fail "one try only when the queue stays empty"
has "Recherche : termin" "empty search finished"
is mute 0 "sound on after the empty search"
is loop 3 "shuffle on after the empty search"
[ ! -e "$T/state/fs/fav/search_undo" ] || fail "note removed after the search"
echo "selftest OK: status, presets, command, pinning; queue search with shuffle, restore, album play, empty WiiM, restart"
