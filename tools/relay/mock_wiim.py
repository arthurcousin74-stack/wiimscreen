# Mock WiiM for tests (tools/relay/selftest.sh, platform/pc/selftest.sh).
# - HTTPS httpapi.asp with a self-signed certificate, like a WiiM.
# - UPnP PlayQueue service in plain HTTP on port 49152 (the port the app expects), with the behaviours seen on a
#   real WiiM (docs/WIIM_API_NOTES.md): CreateQueue fetches the tracks of the SearchUrl step by step and starts
#   playing at once; with shuffle on, the queue is shuffled when created and once more shortly after;
#   PlayQueueWithIndex follows the order BrowseQueue shows; DeleteQueue stops playback; a seek sent while a track
#   loads is lost; BrowseQueue never gives Login_username back.
# Usage: python mock_wiim.py <port> <cert.pem> <key.pem> [--upnp-port N] [--nofill-file PATH] [--fault-file PATH]
#   [--art-url URL]
#   --upnp-port 0: no PlayQueue service. While the file PATH exists, the queues stay empty (a WiiM that loads nothing).
#   --fault-file: one rule per line, "<count> <prefix>": the next <count> requests (-1: all) whose name starts with
#   <prefix> fail. Names: the httpapi command ("setPlayerCmd:mute:0"; answered "Failed", not applied) or
#   "upnp <Action> <QueueName or ListName>" ("upnp CreateQueue Mock Album"; answered 500). Counters restart when the
#   file changes. Command "mockReset": back to the start state (Mock Song playing in the Mock Album queue, shuffle on).
import html, http.server, json, os, random, re, ssl, sys, threading, time, urllib.parse

args = sys.argv[1:]
def option(name, default):
    if name in args:
        i = args.index(name); v = args[i + 1]; del args[i:i + 2]; return v
    return default
UPNP_PORT = int(option("--upnp-port", "49152"))
NOFILL = option("--nofill-file", "")
ART_URL = option("--art-url", "un_known")  # cover of the playing track (an https URL tests the root certificates)
FAULTS = option("--fault-file", "")
PORT, CERT, KEY = int(args[0]), args[1], args[2]

# album id -> (album, artist, titles)
CATALOG = {
    "mock": ("Mock Album", "Mock Artist", ["Mock Intro", "Mock Song", "Mock Outro"]),
    "kob": ("Kind of Blue", "Miles Davis", ["So What", "Freddie Freeloader", "Blue in Green", "All Blues", "Flamenco Sketches"]),
    "ram": ("Random Access Memories", "Daft Punk", ["Give Life Back to Music", "Get Lucky", "Contact"]),
    "dream": ("Dreamland", "Robert Miles", ["Children", "Fable"]),
}
def tracks_of(url):
    """Tracks (title, artist, album, album id) a WiiM would fetch from this SearchUrl."""
    q = urllib.parse.parse_qs(urllib.parse.urlsplit(url).query)
    if "album/get" in url:
        a = CATALOG.get(q.get("album_id", [""])[0])
        return [(t, a[1], a[0], q["album_id"][0]) for t in a[2]] if a else []
    if "search" in url:
        words = q.get("query", [""])[0].lower().split()
        return [(t, artist, album, i) for i, (album, artist, titles) in CATALOG.items() for t in titles
                if any(w in (t + " " + artist + " " + album).lower() for w in words)]
    return []

lock = threading.RLock()
rng = random.Random(7)
queues = {}  # name -> {"url", "tracks" (in play order), "natural", "created", "reshuffled"}
state = {"status": "play", "vol": 25, "mute": 0, "loop": 3, "pos": 30000, "t": time.time(), "load_until": 0,
         "queue": None, "track": None}

def shuffle_on(): return state["loop"] in (2, 3, 5)
def mix(tracks, natural):
    """Shuffles in place; never leaves the track `natural` (the first without shuffle) first: a test sees the shuffle."""
    rng.shuffle(tracks)
    if len(tracks) > 1 and tracks[0] == natural: tracks[0], tracks[-1] = tracks[-1], tracks[0]
def now_playing(): return state["status"] == "play" and time.time() >= state["load_until"]
def advance():
    if now_playing(): state["pos"] += int((time.time() - max(state["t"], state["load_until"])) * 1000)
    state["t"] = time.time()
def start(name, track):
    advance()
    state.update(queue=name, track=track, status="play", pos=0, load_until=time.time() + 0.8)
def stop():
    advance()
    state.update(status="stop", queue=None, track=None, pos=0)
def visible(q):
    """Tracks loaded so far: 1 after 0.4 s, then 4 more per second. Reshuffled once, 1 s after the creation."""
    age = time.time() - q["created"]
    if shuffle_on() and not q["reshuffled"] and age > 1.0:
        mix(q["tracks"], q["natural"]); q["reshuffled"] = True
    return q["tracks"][:max(0, min(len(q["tracks"]), int((age - 0.15) * 4)))]
def create(name, url):
    stop()
    tracks = [] if NOFILL and os.path.exists(NOFILL) else tracks_of(url)
    natural = tracks[0] if tracks else None
    if shuffle_on(): mix(tracks, natural)
    queues[name] = {"url": url, "tracks": tracks, "natural": natural, "created": time.time(), "reshuffled": False}
    for old in list(queues)[:-3]: del queues[old]  # a WiiM keeps few queues
    if tracks: start(name, tracks[0])
def queue_xml(name, q):
    x = ['<?xml version="1.0"?><PlayList><ListName>%s</ListName><ListInfo><QueueVersion>2.0</QueueVersion>'
         '<SourceName>Qobuz</SourceName><SearchUrl>%s</SearchUrl><TrackNumber>%d</TrackNumber><Quality>0</Quality>'
         '<LastPlayIndex>1</LastPlayIndex></ListInfo><Tracks>' % (html.escape(name), html.escape(q["url"]), len(visible(q)))]
    for i, (title, artist, album, album_id) in enumerate(visible(q), 1):
        didl = ('<DIDL-Lite><item><song:albumid>%s</song:albumid><dc:title>%s</dc:title><upnp:artist>%s</upnp:artist>'
                '<upnp:album>%s</upnp:album></item></DIDL-Lite>' % tuple(html.escape(v) for v in (album_id, title, artist, album)))
        x.append("<Track%d><URL>mock://%s/%d</URL><Metadata>%s</Metadata></Track%d>" % (i, album_id, i, html.escape(didl), i))
    return "".join(x) + "</Tracks></PlayList>"

# What plays at the start: a known queue, shuffle on
def reset():
    queues.clear()
    queues["Mock Album"] = {"url": "https://www.qobuz.com/api.json/0.2/album/get?album_id=mock&offset=0&limit=50",
                            "tracks": tracks_of("album/get?album_id=mock"), "created": time.time() - 60, "reshuffled": True}
    state.update(status="play", vol=25, mute=0, loop=3, pos=30000, t=time.time(), load_until=0,
                 queue="Mock Album", track=queues["Mock Album"]["tracks"][1])
reset()

fault_rules = {"text": None, "left": []}
def fault(name):
    """True if this request must fail (rules of the fault file)."""
    try:
        text = open(FAULTS).read() if FAULTS else ""
    except OSError:
        text = ""
    if text != fault_rules["text"]:
        fault_rules["text"] = text
        fault_rules["left"] = [[int(n), prefix] for n, prefix in (line.split(None, 1) for line in text.splitlines() if line.strip())]
    for rule in fault_rules["left"]:
        if name.startswith(rule[1]) and rule[0] != 0:
            rule[0] -= rule[0] > 0
            print("mock fault:", name, flush=True)
            return True
    return False

def hexs(s): return s.encode().hex()
class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def reply(self, data, kind="text/html"):
        self.send_response(200); self.send_header("Content-Type", kind); self.send_header("Content-Length", str(len(data)))
        self.end_headers(); self.wfile.write(data)
    def do_GET(self):
        cmd = urllib.parse.unquote(self.path.split("command=", 1)[-1])
        with lock:
            advance()
            title, artist, album, _ = state["track"] or ("", "", "", "")
            arg = cmd.rsplit(":", 1)[-1]
            if fault(cmd):
                body = "Failed"
            elif cmd == "mockReset":
                reset(); body = "OK"
            elif cmd == "getPlayerStatus":
                status = "load" if state["status"] == "play" and not now_playing() else state["status"]
                body = {"status": status, "mode": "10", "curpos": str(state["pos"]), "totlen": "240000" if state["track"] else "0",
                        "vol": str(state["vol"]), "mute": str(state["mute"]), "loop": str(state["loop"]),
                        "Title": hexs(title), "Artist": hexs(artist), "Album": hexs(album)}
            elif cmd == "getMetaInfo":
                body = {"metaData": {"title": title or "unknow", "artist": artist or "unknow", "album": album or "unknow",
                                     "albumArtURI": ART_URL, "sampleRate": "44100", "bitDepth": "16"}}
            elif cmd == "getPresetInfo":
                body = {"preset_num": 2, "preset_list": [{"number": 1, "name": "Mock Radio", "url": "x", "source": "x", "picurl": ""},
                                                         {"number": 2, "name": "Mock Album", "url": "x", "source": "x", "picurl": ""}]}
            elif cmd == "getStatusEx":
                body = {"DeviceName": "WiiM Mock", "uuid": "uuid:mock-1"}
            else:
                body = "OK"
                if cmd.startswith("setPlayerCmd:pause") and state["status"] == "play": state["status"] = "pause"
                elif cmd.startswith("setPlayerCmd:resume") and state["status"] == "pause": state["status"] = "play"
                elif cmd.startswith("setPlayerCmd:stop"): stop()
                elif cmd.startswith("setPlayerCmd:vol:"): state["vol"] = int(arg)
                elif cmd.startswith("setPlayerCmd:mute:"): state["mute"] = int(arg)
                elif cmd.startswith("setPlayerCmd:loopmode:") and arg in ("-1", "0", "1", "2", "3", "4"): state["loop"] = int(arg)
                elif cmd.startswith("setPlayerCmd:seek:") and state["track"] and time.time() >= state["load_until"]:
                    state["pos"] = int(arg) * 1000  # lost while the track loads
        print("mock <-", cmd, "(failed)" if body == "Failed" else "", flush=True)
        self.reply((body if isinstance(body, str) else json.dumps(body)).encode())

SERVICE = "urn:schemas-wiimu-com:service:PlayQueue:1"
class Upnp(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def send(self, code, text):
        data = text.encode()
        self.send_response(code); self.send_header("Content-Type", 'text/xml; charset="utf-8"')
        self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)
    def do_GET(self):
        if self.path != "/description.xml": return self.send(404, "")
        self.send(200, '<?xml version="1.0"?><root><device><friendlyName>WiiM Mock</friendlyName><serviceList><service>'
                       "<serviceType>%s</serviceType><controlURL>/upnp/control/PlayQueue1</controlURL>"
                       "<SCPDURL>/upnp/PlayQueueSCPD.xml</SCPDURL></service></serviceList></device></root>" % SERVICE)
    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", "0"))).decode("utf-8", "replace")
        action = self.headers.get("SOAPACTION", "").strip('"').rsplit("#", 1)[-1]
        def arg(name):
            m = re.search(r"<%s>(.*?)</%s>" % (name, name), body, re.S)
            return html.unescape(m.group(1)) if m else ""
        out = ""
        with lock:
            advance()
            name = arg("QueueName")
            listName = re.search(r"<ListName>(.*?)</ListName>", arg("QueueContext"), re.S)
            if fault("upnp %s %s" % (action, name or (html.unescape(listName.group(1)) if listName else ""))):
                return self.send(500, "<s:Envelope><s:Body><s:Fault>501 Action Failed</s:Fault></s:Body></s:Envelope>")
            if action == "BrowseQueue":
                if name == "TotalQueue":
                    ctx = "<PlayQueue><TotalQueue>%d</TotalQueue><PlayListInfo>%s</PlayListInfo></PlayQueue>" % (
                        len(queues), "".join("<List%d><Name>%s</Name><ListInfo><Source>Qobuz</Source></ListInfo></List%d>"
                                             % (i, html.escape(n), i) for i, n in enumerate(queues, 1)))
                else:
                    if name == "CurrentQueue": name = state["queue"]
                    ctx = queue_xml(name, queues[name]) if name in queues else ""
                out = "<QueueContext>%s</QueueContext>" % html.escape(ctx)
            elif action == "CreateQueue":
                ctx = arg("QueueContext")
                n = re.search(r"<ListName>(.*?)</ListName>", ctx, re.S)
                u = re.search(r"<SearchUrl>(.*?)</SearchUrl>", ctx, re.S)
                if not n: return self.send(500, "<s:Envelope><s:Body><s:Fault>501 Action Failed</s:Fault></s:Body></s:Envelope>")
                create(html.unescape(n.group(1)), html.unescape(u.group(1)) if u else "")
            elif action == "PlayQueueWithIndex":
                q = queues.get(name)
                shown = visible(q) if q else []
                if shown: start(name, shown[max(1, min(len(shown), int(arg("Index") or "1"))) - 1])
            elif action == "DeleteQueue":
                stop()  # even for an unknown name
                queues.pop(name, None)
            else:
                return self.send(500, "<s:Envelope><s:Body><s:Fault>401 Invalid Action</s:Fault></s:Body></s:Envelope>")
        print("mock <- upnp", action, name, flush=True)
        self.send(200, '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body><u:%sResponse xmlns:u="%s">%s'
                       "</u:%sResponse></s:Body></s:Envelope>" % (action, SERVICE, out, action))

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(CERT, KEY)
class S(http.server.ThreadingHTTPServer):
    daemon_threads = True
    # TLS handshake in the connection's own thread (on first read): a handshake done in accept() on the main
    # thread let one stalled connection block all the others (the self-test failed about one time in two)
    def get_request(self):
        sock, addr = self.socket.accept()
        return ctx.wrap_socket(sock, server_side=True, do_handshake_on_connect=False), addr
class Plain(http.server.ThreadingHTTPServer):
    daemon_threads = True
if UPNP_PORT:
    threading.Thread(target=Plain(("127.0.0.1", UPNP_PORT), Upnp).serve_forever, daemon=True).start()
srv = S(("127.0.0.1", PORT), H)
print("mock listening", flush=True)
srv.serve_forever()
