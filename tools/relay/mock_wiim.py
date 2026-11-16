# Mock WiiM for tests (tools/relay/selftest.sh): HTTPS httpapi.asp with a self-signed certificate, like a WiiM.
# Usage: python mock_wiim.py <port> <cert.pem> <key.pem>
import http.server, json, ssl, sys, time
state = {"play": True, "vol": 25, "pos": 30000, "t": time.time()}
def hexs(s): return s.encode().hex()
class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def do_GET(self):
        cmd = self.path.split("command=", 1)[-1]
        if state["play"]: state["pos"] += int((time.time() - state["t"]) * 1000)
        state["t"] = time.time()
        if cmd == "getPlayerStatus":
            body = {"status": "play" if state["play"] else "pause", "mode": "10", "curpos": str(state["pos"]),
                    "totlen": "240000", "vol": str(state["vol"]), "mute": "0",
                    "Title": hexs("Mock Song"), "Artist": hexs("Mock Artist"), "Album": hexs("Mock Album")}
        elif cmd == "getMetaInfo":
            body = {"metaData": {"title": "Mock Song", "artist": "Mock Artist", "album": "Mock Album",
                                 "albumArtURI": "un_known", "sampleRate": "44100", "bitDepth": "16"}}
        elif cmd == "getPresetInfo":
            body = {"preset_num": 2, "preset_list": [{"number": 1, "name": "Mock Radio", "url": "x", "source": "x", "picurl": ""},
                                                     {"number": 2, "name": "Mock Album", "url": "x", "source": "x", "picurl": ""}]}
        elif cmd == "getStatusEx":
            body = {"DeviceName": "WiiM Mock", "uuid": "uuid:mock-1"}
        elif cmd.startswith("setPlayerCmd:pause"): state["play"] = False; body = "OK"
        elif cmd.startswith("setPlayerCmd:resume"): state["play"] = True; body = "OK"
        elif cmd.startswith("setPlayerCmd:vol:"): state["vol"] = int(cmd.split(":")[-1]); body = "OK"
        else: body = "OK"
        print("mock <-", cmd, flush=True)
        data = (body if isinstance(body, str) else json.dumps(body)).encode()
        self.send_response(200); self.send_header("Content-Type", "text/html"); self.send_header("Content-Length", str(len(data)))
        self.end_headers(); self.wfile.write(data)
srv = http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), H)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(sys.argv[2], sys.argv[3])
srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
srv.serve_forever()
