"""Loopback test of DW4Online (the groundwork spike's test, moved to the real service and to DW4R2).

Register, login, rate limit, rooms, room tickets and the DW4R2 relay, written from SEAMLESS.md's wire layout on its own
(standard library only, no code shared with the server): two members connect, enter one stage together (HEADING,
BARRIER, a co-arrival), the base uploads its snapshot, the co-arrival restores it from STATE_DATA, both send their
canonical hash and pads, and both receive the same SEALED ticks with both pads. Run against a server with a fresh
database, Relay:Enabled and RateLimits:LoginPerMinute 5:

    python relay_spike_test.py --base http://127.0.0.1:5280

Exit code 0 when every check passed. EndToEndTests.Spike_python_relay_test runs it against a throw-away server.
"""
import argparse
import json
import socket
import struct
import sys
import time
import urllib.error
import urllib.request

FAILURES = []

# DW4R2 frame types (SEAMLESS.md appendix)
INPUT, HEADING, BARRIER, SNAPSHOT, SNAPHASH, PONG, BYE = 0x01, 0x03, 0x04, 0x07, 0x08, 0x12, 0x13
WELCOME, SEALED, BARRIER_OK, BARRIER_WAIT, STATE_DATA, ROOM, PING = 0x80, 0x81, 0x84, 0x85, 0x86, 0x8A, 0x8B


def check(ok, name, detail=""):
    print(("ok   " if ok else "FAIL ") + name + (": " + str(detail) if detail != "" else ""), flush=True)
    if not ok:
        FAILURES.append(name)
    return ok


def call(base, method, path, body=None, token=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(base + path, data=data, method=method)
    req.add_header("Content-Type", "application/json")
    if token:
        req.add_header("Authorization", "Bearer " + token)
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            raw = r.read()
            return r.status, (json.loads(raw) if raw else None)
    except urllib.error.HTTPError as e:
        raw = e.read()
        return e.code, (json.loads(raw) if raw else None)


def read_line(sock):
    line = b""
    while not line.endswith(b"\n"):
        chunk = sock.recv(1)
        if not chunk:
            break
        line += chunk
    return line.decode().strip()


def recv_exactly(sock, count):
    got = b""
    while len(got) < count:
        chunk = sock.recv(count - len(got))
        if not chunk:
            raise EOFError("closed")
        got += chunk
    return got


def frame(kind, payload):
    return bytes([kind]) + struct.pack("<I", len(payload)) + payload


def string(s):
    b = s.encode("ascii")
    return bytes([len(b)]) + b


class Member:
    def __init__(self, address, ticket, protocol, data_hash, settings_hash):
        self.s = socket.create_connection(address, timeout=10)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.s.sendall(("DW4R2 %s %d %d %s\n" % (ticket, protocol, data_hash, settings_hash)).encode())
        self.ok = read_line(self.s)
        self.inst = self.visit = 0
        self.slot = 0
        self.frames = []

    def send(self, kind, payload):
        self.s.sendall(frame(kind, payload))

    def next(self, wanted=None, timeout=10.0):
        """The next frame (answering PING on the way); with wanted, skips to the first frame of that type."""
        end = time.time() + timeout
        while time.time() < end:
            header = recv_exactly(self.s, 5)
            kind, length = header[0], struct.unpack("<I", header[1:])[0]
            payload = recv_exactly(self.s, length) if length else b""
            if kind == PING:
                nonce = struct.unpack_from("<I", payload, 0)[0]
                self.send(PONG, struct.pack("<IQ", nonce, int(time.time() * 1000)))
                self.last_ping = time.perf_counter()
            if wanted is None or kind == wanted:
                return kind, payload
        raise TimeoutError("no frame %#x" % wanted)


def parse_welcome(p):
    member, inst, visit, slot, mode = struct.unpack_from("<BIIBB", p, 0)
    return member, inst, visit, slot, mode


def parse_barrier_ok(p):
    inst, visit, decision, new_inst, new_visit = struct.unpack_from("<IIBII", p, 0)
    pos = 17
    n = p[pos]
    stage = p[pos + 1:pos + 1 + n].decode()
    pos += 1 + n
    point, own = p[pos], p[pos + 1]
    slot_map = list(struct.unpack_from("<4b", p, pos + 2))
    mode, delay, co = p[pos + 6], p[pos + 7], p[pos + 8]
    return {"decision": decision, "inst": new_inst, "visit": new_visit, "stage": stage, "slot": own, "map": slot_map, "mode": mode,
            "delay": delay, "co_arrivals": co}


def parse_ticks(p):
    inst, visit, first, count = struct.unpack_from("<IIIH", p, 0)
    pos, ticks = 14, []
    for _ in range(count):
        mask, events = p[pos], p[pos + 1]
        pos += 2
        pads = {}
        for slot in range(4):
            if mask & (1 << slot):
                pads[slot] = p[pos:pos + 7]
                pos += 7
        for _ in range(events):
            length = struct.unpack_from("<H", p, pos + 1)[0]
            pos += 3 + length
        ticks.append((mask, pads))
    return first, ticks


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", default="http://127.0.0.1:5280")
    args = parser.parse_args()
    base = args.base.rstrip("/")

    status = None
    for _ in range(50):
        try:
            status = call(base, "GET", "/api/v1/status")
            break
        except OSError:
            time.sleep(0.2)
    check(status is not None and status[0] == 200, "status", status and status[1].get("version"))
    relay = status[1]["relay"]
    check(relay is not None and relay.get("protocol") == "DW4R2", "relay announced", relay)

    suffix = str(int(time.time() * 1000) % 1000000)
    host_user, guest_user = "host" + suffix, "guest" + suffix
    check(call(base, "POST", "/api/v1/auth/register", {"username": host_user, "password": "correct horse 1"})[0] == 201, "register host")
    check(call(base, "POST", "/api/v1/auth/register", {"username": guest_user, "password": "correct horse 2"})[0] == 201, "register guest")
    dup = call(base, "POST", "/api/v1/auth/register", {"username": host_user.upper(), "password": "correct horse 1"})
    check(dup[0] == 409 and dup[1]["code"] == "username_taken", "duplicate name refused", dup[0])
    bad = call(base, "POST", "/api/v1/auth/register", {"username": "a b", "password": "correct horse 1"})
    check(bad[0] == 422 and bad[1]["code"] == "invalid_username", "bad name refused", bad[0])

    t0 = time.perf_counter()
    s, host = call(base, "POST", "/api/v1/auth/login", {"username": host_user, "password": "correct horse 1", "deviceName": "spike"})
    check(s == 200, "login host", "%.0f ms" % ((time.perf_counter() - t0) * 1000))
    s, guest = call(base, "POST", "/api/v1/auth/login", {"username": guest_user, "password": "correct horse 2", "deviceName": "spike"})
    check(s == 200, "login guest")
    check(call(base, "GET", "/api/v1/me", token=host["token"])[1]["username"] == host_user, "me")
    check(call(base, "GET", "/api/v1/me", token="nope")[0] == 401, "bad token refused")

    room_body = {"name": "Spike room", "password": "dw4dw4", "maxPlayers": 4, "protocol": 43, "dataHash": 1234, "clientVersion": "0.52.0",
                 "settings": {"difficulty": 0, "cameraMode": 0, "lightMode": 1}}
    s, room = call(base, "POST", "/api/v1/rooms", room_body, host["token"])
    check(s == 201 and room.get("relayTicket"), "create room with a relay ticket", s)
    code = room["room"]["code"]
    settings_hash = room["settingsHash"]
    listed = call(base, "GET", "/api/v1/rooms?protocol=43&dataHash=1234")[1]
    check(any(r["code"] == code for r in listed), "listed for the same protocol")
    check(call(base, "GET", "/api/v1/rooms?protocol=40&dataHash=1234")[1] == [], "not listed for another protocol")
    join = {"password": "x", "protocol": 43, "dataHash": 1234, "clientVersion": "0.52.0"}
    wrong = call(base, "POST", "/api/v1/rooms/%s/join" % code, join, guest["token"])
    check(wrong[0] == 403 and wrong[1]["code"] == "wrong_room_password", "wrong room password refused", wrong[0])
    join["password"] = "dw4dw4"
    s, joined = call(base, "POST", "/api/v1/rooms/%s/join" % code, join, guest["token"])
    check(s == 200 and joined["settingsHash"] == settings_hash, "join", s)

    address = (relay["host"], relay["port"])
    a = Member(address, room["relayTicket"], 43, 1234, settings_hash)
    check(a.ok.startswith("OK 0 "), "relay handshake owner", a.ok)
    b = Member(address, joined["relayTicket"], 43, 1234, settings_hash)
    check(b.ok.startswith("OK 1 "), "relay handshake guest", b.ok)
    replay = Member(address, joined["relayTicket"], 43, 1234, settings_hash)
    check(replay.ok == "ERR ticket", "a used ticket is refused", replay.ok)
    replay.s.close()
    for m in (a, b):
        kind, p = m.next(WELCOME)
        member, m.inst, m.visit, m.slot, mode = parse_welcome(p)
        check(mode == 0, "welcome: a solo hub instance", "member %d instance %d" % (member, m.inst))

    # Both confirm the same exit (HEADING with the context hash), then park at their barriers: a co-arrival.
    context = 0xC0FFEE
    for m in (a, b):
        m.send(HEADING, struct.pack("<I", m.inst) + string("stag0101") + bytes([0]) + struct.pack("<Q", context))
    for m in (a, b):
        carry = m.ok.encode()
        m.send(BARRIER, struct.pack("<III", m.inst, m.visit, 0) + string("stag0101") + bytes([0, 1, 1]) + struct.pack("<Q", context)
               + struct.pack("<I", len(carry)) + carry)
    kind, p = a.next(BARRIER_WAIT)
    check(p[4] == 1, "the first waits for the announced one (co-arrival)")
    oa = parse_barrier_ok(a.next(BARRIER_OK)[1])
    ob = parse_barrier_ok(b.next(BARRIER_OK)[1])
    check(oa["decision"] == 1 and oa["mode"] == 2 and oa["co_arrivals"] == 1, "owner: NEW, sealed, one co-arrival", oa)
    check(ob["decision"] == 4 and ob["inst"] == oa["inst"] and ob["slot"] == 1, "guest: CO_ARRIVE into the owner's instance", ob)
    for m, o in ((a, oa), (b, ob)):
        m.inst, m.visit, m.slot, m.delay = o["inst"], o["visit"], o["slot"], max(1, o["delay"])

    # The base uploads its load snapshot; the co-arrival restores it from STATE_DATA; both send the canonical hash.
    snapshot = bytes(range(256)) * 80
    def pads(m):
        return frame(INPUT, struct.pack("<IIIB", m.inst, m.visit, 0, m.delay) +
                     b"".join(struct.pack("<Hbbbb", 0x10 * (m.slot + 1), m.slot, 0, 0, 0) for _ in range(m.delay)))

    # Pads for ticks 0..d-1 go out at once with the hash (in one segment), so the first sealed tick has them.
    a.send(SNAPSHOT, struct.pack("<IIII", a.inst, a.visit, len(snapshot), 0) + snapshot)
    a.s.sendall(frame(SNAPHASH, struct.pack("<IIQ", a.inst, a.visit, 42)) + pads(a))
    kind, p = b.next(STATE_DATA)
    inst, visit, total, offset = struct.unpack_from("<IIII", p, 0)
    blob = p[16:]
    while len(blob) < total:
        kind, p = b.next(STATE_DATA)
        blob += p[16:]
    purpose, slot, codec = blob[0], blob[1], blob[2]
    snap_len = struct.unpack_from("<I", blob, 3)[0]
    check(purpose == 2 and slot == 1 and codec == 0 and blob[7:7 + snap_len] == snapshot, "co-arrival state: the base's snapshot", snap_len)
    b.s.sendall(frame(SNAPHASH, struct.pack("<IIQ", b.inst, b.visit, 42)) + pads(b))

    # The sealed stream: both machines get the same ticks with both pads.
    streams = {}
    for m in (a, b):
        first, ticks = parse_ticks(m.next(SEALED)[1])
        streams[m.slot] = (first, ticks[0])
    check(streams[0] == streams[1] and streams[0][0] == 0, "both machines sealed tick 0 alike", streams[0][1][0])
    check(streams[0][1][0] == 0b11 and streams[0][1][1][0][:2] == struct.pack("<H", 0x10) and streams[0][1][1][1][:2] == struct.pack("<H", 0x20),
          "tick 0 carries both pads")

    # PING / PONG round trip through the relay.
    lat = []
    for _ in range(3):
        t = time.perf_counter()
        a.next(PING, timeout=3)
        lat.append((time.perf_counter() - t) * 1000)
    print("relay ping round trip ms: once per second (waited %.0f ms for the last)" % lat[-1], flush=True)

    # The owner leaves the room: it keeps running with the story frozen; the owner's relay connection closes.
    check(call(base, "POST", "/api/v1/rooms/%s/leave" % code, None, host["token"])[0] == 204, "owner leaves")
    s, beat = call(base, "POST", "/api/v1/rooms/%s/heartbeat" % code, None, guest["token"])
    check(s == 200 and beat["room"]["storyFrozen"], "room keeps running with the story frozen")
    for m in (a, b):
        try:
            m.send(BYE, bytes([0]))
        except OSError:
            pass
        m.s.close()

    # Rate limit: 5 logins per minute per IP (two used above, so the 4th extra one is refused).
    codes = [call(base, "POST", "/api/v1/auth/login", {"username": host_user, "password": "wrong password"})[0] for _ in range(4)]
    check(codes == [401, 401, 401, 429], "login rate limit", codes)

    print("ALL PASSED" if not FAILURES else "FAILED: " + ", ".join(FAILURES), flush=True)
    return 0 if not FAILURES else 1


if __name__ == "__main__":
    sys.exit(main())
