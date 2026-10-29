"""A loopback stand-in for DW4Online for the game client's end-to-end checks
(round 52, client lane), until the real DW4R2 hub (relay lane) is joined.

HTTPS (a throwaway self-signed certificate made with openssl at start, in a
temporary folder, deleted at exit; its SPKI pin is printed so the client can
pin it) on 127.0.0.1:<http_port>, with the REST calls the client uses:
GET /api/v1/status, POST /api/v1/auth/login, GET /api/v1/me, POST
/api/v1/rooms, POST /api/v1/rooms/<code>/join, POST /api/v1/rooms/<code>/ticket,
POST /api/v1/rooms/<code>/leave. Any user name logs in with any password.

DW4R2 relay (docs/SEAMLESS.md 6.2, 6.3; layouts in src/game/online/wire.hpp)
on 127.0.0.1:<relay_port>: the hello line and OK, PING once a second, every
BARRIER answered with BARRIER_OK (DISSOLVE into a hub, else NEW, the member
alone in slot 0), LOG and NETFP counted, PRESENCE fanned out to every other
member as PRESENCE (with the member id), ROOM joined / left. It never seals
ticks: it serves solo-led play, presence and the barrier path.

Loopback only. Stops after <seconds> and prints one summary line per member.

Usage: python dw4online_stub.py HTTP_PORT RELAY_PORT SECONDS PIN_FILE
  (PIN_FILE receives the base64 SPKI pin; the client gets it through
  DW4_ONLINE_PIN in a DW4_ONLINE_DEV build)
"""
from __future__ import annotations

import base64
import hashlib
import http.server
import json
import os
import shutil
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import time

OPENSSL = shutil.which("openssl") or r"C:\Program Files\Git\usr\bin\openssl.exe"
HUB_STAGES = {"stagml02", "stagml11", "stagml12", "stagml13", "stagml14"}

T_LOG, T_BARRIER, T_NETFP, T_STATE, T_PRESENCE, T_RATE, T_PONG, T_BYE = 0x02, 0x04, 0x0B, 0x0C, 0x0F, 0x11, 0x12, 0x13
T_SNAPSHOT, T_SNAPHASH, T_SPLIT = 0x07, 0x08, 0x06
T_BARRIER_OK, T_PRESENCE_OF, T_ROOM, T_PING = 0x44, 0x49, 0x4A, 0x4B


def make_certificate(folder: str) -> tuple[str, str, str]:
    cert, key = os.path.join(folder, "cert.pem"), os.path.join(folder, "key.pem")
    subprocess.run([OPENSSL, "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", key, "-out", cert, "-days", "1",
                    "-subj", "/CN=127.0.0.1", "-addext", "subjectAltName=IP:127.0.0.1"], check=True, capture_output=True)
    pub = subprocess.run([OPENSSL, "x509", "-in", cert, "-pubkey", "-noout"], check=True, capture_output=True).stdout
    der = subprocess.run([OPENSSL, "pkey", "-pubin", "-outform", "der"], input=pub, check=True, capture_output=True).stdout
    return cert, key, base64.b64encode(hashlib.sha256(der).digest()).decode()


class State:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.tokens: dict[str, str] = {}
        self.rooms: dict[str, dict] = {}
        self.tickets: dict[str, str] = {}  # ticket -> user
        self.members: dict[int, dict] = {}
        self.next_member = 1
        self.next_inst = 100
        self.relay_port = 0


def rest_handler(state: State):
    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *args):
            pass

        def answer(self, code: int, body: dict | None = None):
            data = json.dumps(body).encode() if body is not None else b""
            self.send_response(code)
            if body is not None:
                self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def user(self) -> str | None:
            auth = self.headers.get("Authorization", "")
            return state.tokens.get(auth[7:]) if auth.startswith("Bearer ") else None

        def body(self) -> dict:
            n = int(self.headers.get("Content-Length", "0"))
            raw = self.rfile.read(n) if n else b"{}"
            return json.loads(raw or b"{}")

        def do_GET(self):
            if self.path == "/api/v1/status":
                return self.answer(200, {"service": "stub", "client": {"minimumProtocol": 44, "currentProtocol": 44}, "maintenance": False,
                                         "relay": {"host": "127.0.0.1", "port": state.relay_port}})
            if self.path == "/api/v1/me":
                user = self.user()
                return self.answer(200, {"username": user}) if user else self.answer(401, {"title": "Unauthorized"})
            self.answer(404, {"title": "Not found"})

        def do_POST(self):
            body = self.body()
            parts = self.path.split("/")
            with state.lock:
                if self.path == "/api/v1/auth/login":
                    token = base64.b64encode(os.urandom(18)).decode()
                    state.tokens[token] = body.get("username", "")
                    return self.answer(200, {"token": token, "account": {"username": body.get("username", "")}})
                user = self.user()
                if not user:
                    return self.answer(401, {"title": "Unauthorized"})
                ticket = base64.urlsafe_b64encode(os.urandom(12)).decode()
                if self.path == "/api/v1/rooms":
                    code = "R%03d" % (len(state.rooms) + 1)
                    state.rooms[code] = {"name": body.get("name", ""), "settings": body.get("settings"), "owner": user}
                    state.tickets[ticket] = user
                    return self.answer(201, {"room": {"code": code, "name": body.get("name", "")}, "settings": body.get("settings"),
                                             "you": {"username": user}, "relay": {"host": "127.0.0.1", "port": state.relay_port},
                                             "ticket": ticket})
                if len(parts) == 6 and parts[3] == "rooms" and parts[5] in ("join", "ticket", "leave"):
                    room = state.rooms.get(parts[4])
                    if not room:
                        return self.answer(404, {"title": "Not found", "detail": "No such open room."})
                    if parts[5] == "leave":
                        return self.answer(204)
                    state.tickets[ticket] = user
                    if parts[5] == "ticket":
                        return self.answer(200, {"ticket": ticket})
                    return self.answer(200, {"room": {"code": parts[4], "name": room["name"]}, "settings": room["settings"],
                                             "you": {"username": user}, "relay": {"host": "127.0.0.1", "port": state.relay_port},
                                             "ticket": ticket})
            self.answer(404, {"title": "Not found"})

    return Handler


def frame(t: int, payload: bytes) -> bytes:
    return struct.pack("<BI", t, len(payload)) + payload


def str8(s: str) -> bytes:
    b = s.encode()
    return bytes([len(b)]) + b


def read_str8(p: bytes, at: int) -> tuple[str, int]:
    n = p[at]
    return p[at + 1:at + 1 + n].decode(errors="replace"), at + 1 + n


def relay_client(state: State, conn: socket.socket):
    conn.settimeout(1.0)
    buffer = b""
    line = b""
    member = None
    try:
        while b"\n" not in line:
            chunk = conn.recv(1)
            if not chunk:
                return
            line += chunk
        words = line.decode(errors="replace").split()
        if len(words) != 5 or words[0] != "DW4R2":
            conn.sendall(b"ERR bad_hello\n")
            return
        with state.lock:
            user = state.tickets.pop(words[1], None)
            if user is None:
                conn.sendall(b"ERR bad_ticket\n")
                return
            member = state.next_member
            state.next_member += 1
            state.members[member] = {"user": user, "conn": conn, "logs": 0, "ticks": 0, "netfp": 0, "barriers": [], "presence": 0,
                                     "presence_out": 0, "lock": threading.Lock()}
            conn.sendall(f"OK {member} {int(time.time() * 1000)}\n".encode())
            for other, m in state.members.items():
                if other != member:
                    send(m, frame(T_ROOM, bytes([0]) + struct.pack("<I", member) + str8(user) + struct.pack("<I", 0)))
                    send(state.members[member], frame(T_ROOM, bytes([0]) + struct.pack("<I", other) + str8(m["user"]) + struct.pack("<I", 0)))
        last_ping = 0.0
        nonce = 0
        while True:
            now = time.time()
            if now - last_ping >= 1.0:
                last_ping = now
                nonce += 1
                send(state.members[member], frame(T_PING, struct.pack("<IQhB", nonce, int(now * 1000), 2, 3)))
            try:
                chunk = conn.recv(65536)
            except socket.timeout:
                continue
            if not chunk:
                break
            buffer += chunk
            while len(buffer) >= 5:
                t, n = struct.unpack_from("<BI", buffer)
                if len(buffer) < 5 + n:
                    break
                payload, buffer = buffer[5:5 + n], buffer[5 + n:]
                handle(state, member, t, payload)
    except (OSError, ConnectionError):
        pass
    finally:
        conn.close()
        if member is not None:
            with state.lock:
                state.members[member]["conn"] = None
                for other, m in state.members.items():
                    if other != member:
                        send(m, frame(T_ROOM, bytes([1]) + struct.pack("<I", member) + str8("") + struct.pack("<I", 0)))


def send(m: dict, data: bytes):
    if m.get("conn") is None:
        return
    with m["lock"]:
        try:
            m["conn"].sendall(data)
        except OSError:
            pass


def handle(state: State, member: int, t: int, p: bytes):
    m = state.members[member]
    if t == T_LOG:
        inst, visit, first, count = struct.unpack_from("<IIIH", p)
        m["logs"] += 1
        m["ticks"] += count
    elif t == T_NETFP:
        m["netfp"] += 1
    elif t == T_BARRIER:
        inst, visit, tick = struct.unpack_from("<III", p)
        stage, at = read_str8(p, 12)
        m["barriers"].append(stage)
        with state.lock:
            new_inst = state.next_inst
            state.next_inst += 1
        decision = 2 if stage.lower() in HUB_STAGES or stage.lower().startswith("stagis") else 0
        ok = struct.pack("<IIBIIB", inst, visit, decision, new_inst, 1, 0) + bytes([0, 0xFF, 0xFF, 0xFF])
        ok += struct.pack("<IIII", member, 0, 0, 0) + bytes([0]) + struct.pack("<I", 0) + struct.pack("<I", 0) + struct.pack("<I", 0)
        ok += bytes([1, 1])
        send(m, frame(T_BARRIER_OK, ok))
    elif t == T_PRESENCE:
        m["presence"] += 1
        with state.lock:
            others = [o for k, o in state.members.items() if k != member]
        for o in others:
            send(o, frame(T_PRESENCE_OF, struct.pack("<I", member) + p))
            m["presence_out"] += 1


def main() -> int:
    http_port, relay_port, seconds, pin_file = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
    folder = tempfile.mkdtemp(prefix="dw4stub_")
    try:
        cert, key, pin = make_certificate(folder)
        with open(pin_file, "w", encoding="ascii") as f:
            f.write(pin)
        state = State()
        state.relay_port = relay_port
        class Server(http.server.ThreadingHTTPServer):
            def handle_error(self, request, client_address):  # WinHTTP closes kept-alive connections: not an error
                pass

        server = Server(("127.0.0.1", http_port), rest_handler(state))
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(cert, key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        relay = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        relay.bind(("127.0.0.1", relay_port))
        relay.listen(8)
        relay.settimeout(0.5)
        print(f"stub: https://127.0.0.1:{http_port} relay {relay_port} pin {pin}", flush=True)
        end = time.time() + seconds
        while time.time() < end:
            try:
                conn, _ = relay.accept()
            except socket.timeout:
                continue
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            threading.Thread(target=relay_client, args=(state, conn), daemon=True).start()
        server.shutdown()
        for member, m in state.members.items():
            print(f"stub: member {member} {m['user']}: {m['logs']} LOG batches, {m['ticks']} ticks, {m['netfp']} NETFP, "
                  f"{m['presence']} PRESENCE in, {m['presence_out']} forwarded, barriers {' '.join(m['barriers'])}", flush=True)
        return 0
    finally:
        shutil.rmtree(folder, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
