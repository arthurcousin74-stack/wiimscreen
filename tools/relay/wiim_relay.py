#!/usr/bin/env python3
"""wiimscreen relay: lets the simulator ("real WiiM" build) reach your WiiM, Qobuz and Deezer.

Run it on a computer of your home network (Windows, macOS, Linux, Raspberry Pi). Python 3.8+, nothing to install:

    python tools/relay/wiim_relay.py                 # finds the WiiM itself (SSDP)
    python tools/relay/wiim_relay.py --wiim 192.168.1.50

The simulator opens each connection through the relay. Its first line says where to go:
    WIIMSIM/1 TCP <host> <port>    plain TCP                answer: OK
    WIIMSIM/1 TLS <host> <port>    TLS done by the relay    answer: OK <SHA-256 of the server certificate>
    WIIMSIM/1 DISCOVER [ms]        WiiMs on the network     answer: OK, then lines "ip<TAB>name<TAB>uuid"
then the bytes go both ways unchanged. The app checks the WiiM certificate itself (pinning, like on the Tab5).
When the relay needs a secret (see below), the first line ends with " key=<secret>".

Safety:
- listens on 127.0.0.1 only (same computer) unless you give --listen. On another address, a client must send the
  secret (--secret, or a random one printed at start): without it, anyone on the network could use the relay;
- on your network, goes only to the WiiMs that the relay knows (--wiim and the ones it discovered), on ports 443,
  80, 49152 and 59152 (WiiM API and UPnP); never to any other private address or port;
- on the Internet, goes only to TLS on port 443 or plain HTTP on port 80 (some covers);
- never to this computer (loopback or its own network addresses), link-local (169.254.x), multicast or reserved
  addresses;
- checks the certificate and the name of every host name; only a WiiM given by its IP address (self-signed, pinned
  by the app) is accepted without a check; each name is resolved once (no DNS rebinding between check and use);
- discovery follows no redirect and keeps only printable text in the names it returns;
- never logs what passes through (tokens are in the HTTP headers): one line per connection, host and port only.
"""

import argparse
import hashlib
import hmac
import http.client
import ipaddress
import json
import secrets
import select
import socket
import ssl
import sys
import threading
import time

MAX_LINE = 512
ALLOW_LOOPBACK = False  # --allow-loopback: tests with a mock WiiM on the same computer (any port on loopback)
WIIM_PORTS = (443, 80, 49152, 59152)  # WiiM API (HTTPS, HTTP) and UPnP
KNOWN_WIIMS = set()  # addresses of the WiiMs: --wiim and discovery (the only private targets)
OWN_ADDRESSES = set()  # this computer's addresses (refused unless --allow-loopback)
SECRET = None  # required in the first line ("key=<secret>") when set
MAX_NAME = 64
MAX_DEVICES = 32


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def norm_ip(addr):
    """ipaddress object for an address string (scope removed, IPv4-mapped IPv6 turned into IPv4), or None."""
    try:
        ip = ipaddress.ip_address(str(addr).split("%")[0])
    except ValueError:
        return None
    if ip.version == 6 and ip.ipv4_mapped:
        ip = ip.ipv4_mapped
    return ip


def classify(addr):
    """ "loopback", "own" (an address of this computer), "private" (not on the Internet), "public", or None (never)."""
    ip = norm_ip(addr)
    if ip is None:
        return None
    if ip.is_loopback:
        return "loopback"
    # 0.0.0.0 / :: (this computer), multicast, link-local (169.254.x: cloud metadata), reserved: never
    if ip.is_unspecified or ip.is_multicast or ip.is_link_local or ip.is_reserved:
        return None
    if str(ip) in OWN_ADDRESSES:
        return "own"
    return "public" if ip.is_global else "private"  # CGNAT 100.64/10, ULA… are not public


def own_addresses():
    """This computer's addresses (host name and default routes). UDP connect() sends no packet."""
    found = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None):
            found.add(info[4][0])
    except OSError:
        pass
    for family, probe in ((socket.AF_INET, "192.0.2.1"), (socket.AF_INET6, "2001:db8::1")):
        try:
            with socket.socket(family, socket.SOCK_DGRAM) as s:
                s.connect((probe, 9))
                found.add(s.getsockname()[0])
        except OSError:
            pass
    return {str(ip) for ip in map(norm_ip, found) if ip is not None and not ip.is_loopback}


def target_allowed(addr, port, kind):
    """(reason, where): reason None if the relay may open (kind, addr, port); where is "wiim" or "public"."""
    where = classify(addr)
    ip = str(norm_ip(addr)) if where else None
    if where == "loopback":
        if not ALLOW_LOOPBACK:
            return "this computer: not allowed", None
        if ip not in KNOWN_WIIMS:
            return "not a known WiiM", None
        return None, "wiim"  # tests: mock WiiM on any port
    if where == "own" and not ALLOW_LOOPBACK:
        return "this computer: not allowed", None
    if where in ("own", "private"):
        if ip not in KNOWN_WIIMS:
            return "not a known WiiM (give it with --wiim)", None
        if port not in WIIM_PORTS:
            return "WiiM: ports 443, 80, 49152 and 59152 only", None
        return None, "wiim"
    if where == "public":
        if kind == "TLS" and port != 443:
            return "not on your network: TLS on port 443 only", None
        if kind == "TCP" and port != 80:
            return "not on your network: plain HTTP on port 80 only", None
        return None, "public"
    return "address not allowed", None


def resolve(host, port):
    """Address for `host`, resolved ONCE (the connection uses this same address: no DNS rebinding between the check
    and the connection), or None."""
    try:
        return socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)[0][4][0]
    except (OSError, IndexError, UnicodeError):
        return None


def key_ok(parts):
    """Checks and removes the trailing "key=<secret>" of the first line. True if no secret is needed or it matches."""
    given = parts.pop()[4:] if parts and parts[-1].startswith("key=") else None
    if SECRET is None:
        return True
    return given is not None and hmac.compare_digest(given.encode(), SECRET.encode())


def is_loopback_listen(host):
    if host in ("", "localhost"):
        return True
    ip = norm_ip(host.strip("[]"))
    return ip is not None and ip.is_loopback


def is_ip_literal(host):
    try:
        ipaddress.ip_address(host)
        return True
    except ValueError:
        return False


def read_line(sock):
    data = b""
    while not data.endswith(b"\n"):
        c = sock.recv(1)
        if not c or len(data) > MAX_LINE:
            raise ConnectionError("first line missing")
        data += c
    return data.decode("ascii", "replace").strip()


def bridge(a, b, idle_s=120):
    """Passes bytes both ways until one side closes, from ONE thread: reading and writing the same TLS socket from
    two threads at once is not safe (with TLS 1.3 the connection hung about one time in two)."""
    socks = (a, b)
    for s in socks:
        s.setblocking(False)

    def send_all(s, data):
        view = memoryview(data)
        while view:
            try:
                view = view[s.send(view):]
            except (ssl.SSLWantWriteError, ssl.SSLWantReadError, BlockingIOError, InterruptedError):
                select.select([s], [s], [], 5)

    while True:
        # Data already decrypted inside a TLS socket does not make select() fire: read it first
        ready = [s for s in socks if isinstance(s, ssl.SSLSocket) and s.pending()]
        if not ready:
            ready, _, _ = select.select(socks, [], [], idle_s)
            if not ready:
                return  # idle
        for s in ready:
            other = b if s is a else a
            try:
                data = s.recv(65536)
            except (ssl.SSLWantReadError, ssl.SSLWantWriteError, BlockingIOError, InterruptedError):
                continue  # a TLS record without data (session ticket…) or not complete yet
            if not data:
                return
            send_all(other, data)


def open_target(kind, host, port):
    """Connection to the target, or (None, reason)."""
    if not 0 < port < 65536:
        return None, "bad port"
    addr = resolve(host, port)
    if addr is None:
        return None, "unknown host"
    reason, where = target_allowed(addr, port, kind)
    if reason:
        return None, reason
    raw = socket.create_connection((addr, port), timeout=8)
    if kind == "TCP":
        return raw, ""
    if where == "wiim" and is_ip_literal(host):
        # The WiiM, given by its address, has a self-signed certificate: the app checks its fingerprint (pinning)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        tls = ctx.wrap_socket(raw)
    else:
        # A host name is always checked (certificate and name), even if it resolves to a private address
        tls = ssl.create_default_context().wrap_socket(raw, server_hostname=host)
    return tls, hashlib.sha256(tls.getpeercert(binary_form=True)).hexdigest()


def ssdp_search(wait_s):
    msg = (
        "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n"
        "ST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n\r\n"
    ).encode()
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
    s.settimeout(0.3)
    ips = []
    end = time.time() + wait_s
    try:
        s.sendto(msg, ("239.255.255.250", 1900))
        s.sendto(msg, ("239.255.255.250", 1900))
        while time.time() < end:
            try:
                _, addr = s.recvfrom(4096)
            except socket.timeout:
                continue
            if addr[0] not in ips and len(ips) < MAX_DEVICES:
                ips.append(addr[0])
    finally:
        s.close()
    return ips


def clean_text(value, default=""):
    """Printable text only (no tab, newline or other control character: they would break the protocol lines)."""
    if not isinstance(value, str):
        return default
    return "".join(c for c in value if c.isprintable() and c != "\t")[:MAX_NAME].strip() or default


def parse_identity(body):
    """(name, uuid) from a getStatusEx answer (bytes), or None if it is not a JSON object."""
    try:
        info = json.loads(body.decode("utf-8", "replace"))
    except ValueError:
        return None
    if not isinstance(info, dict):
        return None
    return clean_text(info.get("DeviceName"), "WiiM"), clean_text(info.get("uuid"))


def http_get(ip, port, path, tls):
    """Body of a GET (64 KiB at most) if the answer is 200, else None. No redirect, no proxy."""
    if tls:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        conn = http.client.HTTPSConnection(ip, port, timeout=3, context=ctx)
    else:
        conn = http.client.HTTPConnection(ip, port, timeout=3)
    try:
        conn.request("GET", path)
        r = conn.getresponse()
        return r.read(65536) if r.status == 200 else None
    finally:
        conn.close()


def discoverable(ip):
    """A responder that may be asked (and then used): a private address that is not this computer."""
    where = classify(ip)
    return where == "private" or (ALLOW_LOOPBACK and where in ("loopback", "own"))


def wiim_identity(ip):
    """(name, uuid) from getStatusEx, or None if this is not a WiiM/LinkPlay device."""
    if not discoverable(ip):
        return None
    for port, tls in ((443, True), (80, False)):
        try:
            body = http_get(ip, port, "/httpapi.asp?command=getStatusEx", tls)
        except (OSError, ValueError, http.client.HTTPException):
            continue
        ident = parse_identity(body) if body is not None else None
        if ident:
            return ident
    return None


def discover(wait_ms, fixed):
    found = []
    for ip in list(fixed) + ssdp_search(max(1.0, min(wait_ms, 8000) / 1000)):
        ip = str(norm_ip(ip) or "")
        if not ip or any(d[0] == ip for d in found):
            continue
        ident = wiim_identity(ip)
        if ident:
            found.append((ip, ident[0], ident[1]))
            KNOWN_WIIMS.add(ip)
    return found


def handle(conn, peer, args):
    target = None
    try:
        conn.settimeout(10)
        parts = read_line(conn).split()
        if len(parts) < 2 or parts[0] != "WIIMSIM/1":
            conn.sendall(b"ERR unknown protocol\n")
            return
        if not key_ok(parts):
            log(f"refused {peer[0]}: wrong or missing key")
            conn.sendall(b"ERR key needed (--secret of the relay)\n")
            return
        if parts[1] == "DISCOVER":
            wait_ms = int(parts[2]) if len(parts) > 2 and parts[2].isdigit() else 2500
            devices = discover(wait_ms, args.wiim)
            log(f"discovery: {len(devices)} WiiM(s)")
            conn.sendall(b"OK\n" + "".join(f"{ip}\t{name}\t{uuid}\n" for ip, name, uuid in devices).encode())
            return
        if parts[1] not in ("TCP", "TLS") or len(parts) != 4 or not parts[3].isdigit():
            conn.sendall(b"ERR bad request\n")
            return
        kind, host, port = parts[1], parts[2], int(parts[3])
        try:
            target, fingerprint = open_target(kind, host, port)
        except (OSError, ssl.SSLError) as e:
            target, fingerprint = None, f"connection failed: {e.__class__.__name__}"
        if target is None:
            log(f"refused {kind} {host}:{port} ({fingerprint})")
            conn.sendall(f"ERR {fingerprint}\n".encode())
            return
        log(f"{kind} {host}:{port}")
        conn.sendall(f"OK {fingerprint}\n".encode() if fingerprint else b"OK\n")
        bridge(conn, target)
    except (OSError, ConnectionError, ValueError):
        pass
    finally:
        for s in (target, conn):
            if s:
                try:
                    s.close()
                except OSError:
                    pass


def main():
    p = argparse.ArgumentParser(description="Relay between the wiimscreen simulator and your home network.")
    p.add_argument("--listen", default="127.0.0.1:8787", help="address:port to listen on (default 127.0.0.1:8787)")
    p.add_argument("--wiim", action="append", default=[], help="IP of a WiiM (if discovery does not find it)")
    p.add_argument("--secret", help="key that clients must send (needed with --listen on another address than "
                   "127.0.0.1; a random one is made if you give none)")
    p.add_argument("--allow-loopback", action="store_true", help=argparse.SUPPRESS)  # tests: mock WiiM on 127.0.0.1
    args = p.parse_args()
    global ALLOW_LOOPBACK, SECRET
    ALLOW_LOOPBACK = args.allow_loopback
    OWN_ADDRESSES.update(own_addresses())
    for w in args.wiim:
        ip = norm_ip(w)
        if ip is None:
            p.error(f"--wiim {w}: give an IP address")
        KNOWN_WIIMS.add(str(ip))
    host, _, port = args.listen.rpartition(":")
    host = host.strip("[]")
    if args.secret is not None:
        if len(args.secret) < 16 or any(not (c.isascii() and c.isprintable()) or c.isspace() for c in args.secret):
            p.error("--secret: 16 characters at least, printable ASCII, no space")
        SECRET = args.secret
    elif not is_loopback_listen(host):
        SECRET = secrets.token_urlsafe(24)
        # Shown once, on purpose: the user gives it to the simulator (never written to a log line afterwards)
        print(f"Relay key, for the simulator: WIIMSIM_RELAY_KEY={SECRET}", flush=True)
    if not is_loopback_listen(host):
        log("Listening on the network: only clients that send the key can use this relay")
    srv = socket.create_server((host or "127.0.0.1", int(port)), reuse_port=False)
    log(f"wiimscreen relay on {host}:{port} — start the simulator: .pio/build/simreal/program")
    if args.wiim:
        log("WiiM given: " + ", ".join(sorted(KNOWN_WIIMS)))
    else:
        found = discover(2500, [])
        log("WiiM found: " + (", ".join(f"{n} ({ip})" for ip, n, _ in found) or "none yet (use --wiim <ip>)"))
    try:
        while True:
            conn, peer = srv.accept()
            threading.Thread(target=handle, args=(conn, peer, args), daemon=True).start()
    except KeyboardInterrupt:
        log("stopped")
        sys.exit(0)


if __name__ == "__main__":
    main()
