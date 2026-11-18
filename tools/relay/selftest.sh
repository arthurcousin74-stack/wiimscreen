#!/usr/bin/env bash
# Self-test of the "real WiiM" simulator: simulator -> relay -> mock WiiM (HTTPS, self-signed certificate, UPnP
# PlayQueue on port 49152). Checks the status, a command, certificate pinning, and the search through a WiiM queue
# (shuffle on): results in order, music before put back (paused, playing), a result played from its album, a WiiM
# that loads nothing, a search cut by a restart; WiiM failures (mock --fault-file): search queue refused, sound not
# put back, music before not put back, player status unreadable. About 2 min.
# Needs: openssl, python3, a built simulator (pio run -e simreal).
set -euo pipefail
cd "$(dirname "$0")/../.."
T=$(mktemp -d)
trap 'kill $(jobs -p) 2>/dev/null || true; rm -rf "$T"' EXIT
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$T/key.pem" -out "$T/cert.pem" -days 1 -subj "/CN=wiim-mock" 2>/dev/null
python3 tools/relay/mock_wiim.py 4443 "$T/cert.pem" "$T/key.pem" --nofill-file "$T/nofill" \
    --fault-file "$T/faults" > "$T/mock.log" 2>&1 &
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
    echo "--- relay"; cat "$T/relay.log"; echo "--- mock"; grep -v "getPlayerStatus\|getMetaInfo" "$T/mock.log"
    echo "SELFTEST FAILED: $1"; exit 1
}
# run <script lines>: one start of the simulator (same stored state), Journal in $LOG
run() {
    N=$((N + 1)); LOG="$T/sim$N.log"; MOCK0=$(wc -l < "$T/mock.log")
    printf '%b' "$1" > "$T/script.txt"
    timeout 90 .pio/build/simreal/program --headless --relay 127.0.0.1:8799 --state "$T/state" --out "$T" \
        --script "$T/script.txt" > "$LOG" 2>&1 || fail "simulator run $N"
}
# Journal assertions match tagged lines only (src/app/log.h): "<time>  [tag] key=value ... | <French sentence>".
# The French sentence is never matched, so rewording it is harmless.
HINT="a Journal tag changed or a line is no longer logged: see src/app/log.h"
# tagged <tag> [key=value regex]: grep pattern of a tagged Journal line
tagged() { printf '\\[%s\\] %s' "$1" "${2:-}"; }
# has <tag> <key=value regex> <what>: a tagged line in the Journal of the last run
has() { grep -q -- "$(tagged "$1" "$2")" "$LOG" || fail "$3: no Journal line \"[$1] $2\" ($HINT)"; }
# count <tag> <key=value regex>: number of such lines in the Journal of the last run
count() { grep -c -- "$(tagged "$1" "$2")" "$LOG" || true; }
# mock <field>: a field of the mock's getPlayerStatus (Title decoded); mock reset: back to its start state
mock() {
    python3 - "$1" <<'PY'
import json, ssl, sys, urllib.request
c = ssl.create_default_context(); c.check_hostname = False; c.verify_mode = ssl.CERT_NONE
get = lambda cmd: urllib.request.urlopen("https://127.0.0.1:4443/httpapi.asp?command=" + cmd, context=c, timeout=5).read()
if sys.argv[1] == "reset":
    get("mockReset")
else:
    d = json.loads(get("getPlayerStatus"))
    print(bytes.fromhex(d["Title"]).decode() if sys.argv[1] == "Title" else d[sys.argv[1]])
PY
}
# Mock requests since the start of the last run
mocklog() { tail -n +"$((MOCK0 + 1))" "$T/mock.log"; }
is() { [ "$(mock "$1")" = "$2" ] || fail "$3 (mock $1 = $(mock "$1"), expected $2)"; }
# Keyboard (QWERTY, 1280x720): search icon, then m i l e s, then Search
MILES='tap 50 360\nwait 400\ntap 861 541\nwait 150\ntap 975 330\nwait 150\ntap 1089 435\nwait 150\ntap 405 330\nwait 150\ntap 291 435\nwait 300\ntap 1089 647\n'

# 1. Status, presets, pause (932,348), pinning. Then, paused in a queue with shuffle on: search "miles", play the
#    second result (600,300), search again while it plays.
run "wait 3000\ntap 932 348\nwait 2000\n${MILES}wait 14000\ntap 600 300\nwait 9000\ntap 50 360\nwait 600\ntap 1089 647\nwait 16000\nquit\n"
has wiim.presets "result=ok count=2 " "presets"
has wiim.status "result=ok " "status"
grep -q "setPlayerCmd:pause" "$T/mock.log" || fail "pause command"
PIN=$(openssl x509 -in "$T/cert.pem" -outform der | sha256sum | cut -c1-64)
grep -q "^fp	$PIN" "$T/state/settings.txt" || fail "certificate pinning"
has search.before "playing=0 inqueue=1 | .*Mock Song" "paused track seen before the search"
has search.results "tracks=7 relevant=1 " "search results"
# The line right after a results count is the first result
grep -A1 -- "$(tagged search.results "tracks=7 ")" "$LOG" | grep -q -- "$(tagged search.item "n=1 | .*- So What ")" ||
    fail "results in order (shuffle off during the search): no \"[search.item] n=1 | ... So What\" right after \"[search.results] tracks=7\" ($HINT)"
has search.restore "paused=1 create=200 play=200 " "paused track put back"
has search.shuffle "result=ok " "shuffle put back"
has search.unmute "result=ok " "sound put back"
has play.result "create=[0-9]* play=200 .*| Qobuz (album) .*Freddie Freeloader" "result played from its album"
has search.before "playing=1 inqueue=1 | .*Freddie Freeloader" "playing track seen before the search"
has search.restore "paused=0 create=200 play=200 " "playing track put back"
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
has search.undo "unmute=ok shuffle=ok " "interrupted search undone at the next start"
[ "$(count search.queue "create=")" = 1 ] ||
    fail "one try only when the queue stays empty: $(count search.queue "create=") \"[search.queue]\" lines, expected 1 ($HINT)"
has search.done "created=" "empty search finished"
is mute 0 "sound on after the empty search"
is loop 3 "shuffle on after the empty search"
[ ! -e "$T/state/fs/fav/search_undo" ] || fail "note removed after the search"

# 4. WiiM failures. Search queue refused (single word: one try only, an error, not "no result") and the sound not put
#    back the first time: the note stays and the next poll puts the sound back.
rm -f "$T/nofill"; mock reset
printf -- '-1 upnp CreateQueue wiimscreen-recherche\n1 setPlayerCmd:mute:0\n' > "$T/faults"
run "wait 3000\n${MILES}wait 9000\nquit\n"
rm -f "$T/faults"
[ "$(count search.queue "create=")" = 1 ] ||
    fail "one try only when the WiiM refuses the queue: $(count search.queue "create=") \"[search.queue]\" lines, expected 1 ($HINT)"
has search.done "created=0 " "refused search queue reported"
has search.unmute "result=refused " "sound put back refused once"
has search.undo "unmute=ok " "sound put back at the next poll"
is mute 0 "sound on after a refused unmute"
is loop 3 "shuffle on after a refused unmute"
[ ! -e "$T/state/fs/fav/search_undo" ] || fail "note removed once the sound is back"

# 5. The music before cannot be put back (CreateQueue of its queue refused): the search queue is stopped before the
#    sound comes back, so no track that nobody chose is heard.
mock reset
printf -- '-1 upnp CreateQueue Mock Album\n' > "$T/faults"
run "wait 3000\n${MILES}wait 16000\nquit\n"
rm -f "$T/faults"
has search.results "tracks=7 relevant=1 " "search results (restore refused)"
has search.stop "result=ok " "search queue stopped when the restore fails"
is status stop "nothing plays after a failed restore"
is mute 0 "sound on after a failed restore"
is loop 3 "shuffle on after a failed restore"
[ ! -e "$T/state/fs/fav/search_undo" ] || fail "note removed after a failed restore"

# 6. Player status unreadable: the search does not start (no DeleteQueue/CreateQueue, nothing muted)
mock reset
printf -- '-1 getPlayerStatus\n' > "$T/faults"
run "wait 3000\n${MILES}wait 4000\nquit\n"
rm -f "$T/faults"
has search.cancelled "reason=status " "search cancelled without the player status"
! mocklog | grep -q "upnp DeleteQueue\|upnp CreateQueue\|setPlayerCmd:mute:1" || fail "no queue change without the player status"
is status play "music untouched without the player status"
is mute 0 "sound untouched without the player status"
echo "selftest OK: status, presets, command, pinning; queue search with shuffle, restore, album play, empty WiiM, restart, WiiM failures"
