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

Safety:
- listens on 127.0.0.1 only (same computer) unless you give --listen;
- goes only to private addresses (your network: the WiiM) and, for public hosts, to TLS on port 443 with the
  certificate checked (Qobuz, Deezer, album covers), or plain HTTP on port 80 (some covers);
- never logs what passes through (tokens are in the HTTP headers): one line per connection, host and port only.
"""

import argparse
import hashlib
import ipaddress
import json
import select
import socket
import ssl
import sys
import threading
import time
import urllib.request

MAX_LINE = 512
ALLOW_LOOPBACK = False  # --allow-loopback: tests with a mock WiiM on the same computer


def log(msg):
    print(time.strftime("%H:%M:%S"), msg, flush=True)


def is_private(host):
    """True if every address of `host` is on the local network (RFC 1918, link-local, ULA)."""
    try:
        infos = socket.getaddrinfo(host, None)
    except OSError:
        return False
    addrs = {info[4][0] for info in infos}
    return bool(addrs) and all(
        ipaddress.ip_address(a.split("%")[0]).is_private
        and (ALLOW_LOOPBACK or not ipaddress.ip_address(a.split("%")[0]).is_loopback)
        for a in addrs
    )


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
    private = is_private(host)
    if not private:
        if kind == "TLS" and port != 443:
            return None, "not on your network: TLS on port 443 only"
        if kind == "TCP" and port != 80:
            return None, "not on your network: plain HTTP on port 80 only"
    raw = socket.create_connection((host, port), timeout=8)
    if kind == "TCP":
        return raw, ""
    if private:
        # The WiiM has a self-signed certificate: the app checks its fingerprint (pinning)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
    else:
        ctx = ssl.create_default_context()
    tls = ctx.wrap_socket(raw, server_hostname=None if private else host)
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
            if addr[0] not in ips:
                ips.append(addr[0])
    finally:
        s.close()
    return ips


def wiim_identity(ip):
    """(name, uuid) from getStatusEx, or None if this is not a WiiM/LinkPlay device."""
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    for url in (f"https://{ip}/httpapi.asp?command=getStatusEx", f"http://{ip}/httpapi.asp?command=getStatusEx"):
        try:
            with urllib.request.urlopen(url, timeout=3, context=ctx if url.startswith("https") else None) as r:
                info = json.loads(r.read(65536).decode("utf-8", "replace"))
            return info.get("DeviceName", "WiiM"), info.get("uuid", "")
        except (OSError, ValueError):
            continue
    return None


def discover(wait_ms, fixed):
    found = []
    for ip in list(fixed) + ssdp_search(max(1.0, min(wait_ms, 8000) / 1000)):
        if any(d[0] == ip for d in found):
            continue
        ident = wiim_identity(ip)
        if ident:
            found.append((ip, ident[0], ident[1]))
    return found


def handle(conn, peer, args):
    target = None
    try:
        conn.settimeout(10)
        parts = read_line(conn).split()
        if len(parts) < 2 or parts[0] != "WIIMSIM/1":
            conn.sendall(b"ERR unknown protocol\n")
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
    p.add_argument("--allow-loopback", action="store_true", help=argparse.SUPPRESS)  # tests: mock WiiM on 127.0.0.1
    args = p.parse_args()
    global ALLOW_LOOPBACK
    ALLOW_LOOPBACK = args.allow_loopback
    host, _, port = args.listen.rpartition(":")
    if host not in ("127.0.0.1", "localhost", "::1"):
        log("WARNING: listening on the network; anyone on it can use this relay")
    srv = socket.create_server((host or "127.0.0.1", int(port)), reuse_port=False)
    log(f"wiimscreen relay on {host}:{port} — start the simulator: .pio/build/simreal/program")
    if args.wiim:
        log("WiiM given: " + ", ".join(args.wiim))
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
