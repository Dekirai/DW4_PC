"""Loopback test of DW4Online (the groundwork spike's test, moved to the real service).

Register, login, rate limit, rooms, stage instances and the TCP relay splice with netplay-style frames.
Standard library only. Run against a server with a fresh database, Relay:Enabled and RateLimits:LoginPerMinute 5:

    python relay_spike_test.py --base http://127.0.0.1:5280

Exit code 0 when every check passed. EndToEndTests.Spike_python_relay_test runs it against a throw-away server.
"""
import argparse
import json
import socket
import sys
import time
import urllib.error
import urllib.request

FAILURES = []


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
            break
        got += chunk
    return got


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
    check(relay is not None, "relay announced", relay)

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

    room_body = {"name": "Spike room", "password": "dw4dw4", "maxPlayers": 4, "protocol": 41, "dataHash": 1234, "clientVersion": "0.48.0"}
    s, room = call(base, "POST", "/api/v1/rooms", room_body, host["token"])
    check(s == 201, "create room", s)
    code = room["room"]["code"]
    listed = call(base, "GET", "/api/v1/rooms?protocol=41&dataHash=1234")[1]
    check(any(r["code"] == code for r in listed), "listed for the same protocol")
    check(call(base, "GET", "/api/v1/rooms?protocol=40&dataHash=1234")[1] == [], "not listed for another protocol")
    join = {"password": "x", "protocol": 41, "dataHash": 1234, "clientVersion": "0.48.0"}
    wrong = call(base, "POST", "/api/v1/rooms/%s/join" % code, join, guest["token"])
    check(wrong[0] == 403 and wrong[1]["code"] == "wrong_room_password", "wrong room password refused", wrong[0])
    join["password"] = "dw4dw4"
    check(call(base, "POST", "/api/v1/rooms/%s/join" % code, join, guest["token"])[0] == 200, "join")

    s, host_stage = call(base, "POST", "/api/v1/rooms/%s/stage" % code, {"stage": "stagD101"}, host["token"])
    check(s == 200 and host_stage["role"] == "host", "host enters the stage", host_stage.get("channel"))
    s, guest_stage = call(base, "POST", "/api/v1/rooms/%s/stage" % code, {"stage": "stagD101"}, guest["token"])
    check(s == 200 and guest_stage["role"] == "guest", "guest joins the instance", guest_stage.get("channel"))
    channel = host_stage["channel"]
    address = (relay["host"], relay["port"])

    # Relay: host control, guest arrives, host opens the data socket, bytes flow both ways.
    ctl = socket.create_connection(address, timeout=10)
    ctl.sendall(("DW4R1 HOST %s\n" % host_stage["ticket"]).encode())
    ok = read_line(ctl)
    check(ok.startswith("OK "), "relay host control", ok[:3])
    g = socket.create_connection(address, timeout=10)
    g.sendall(("DW4R1 GUEST %s\n" % guest_stage["ticket"]).encode())
    notice = read_line(ctl).split()
    check(len(notice) == 3 and notice[0] == "GUEST", "host told about the guest", " ".join(notice))
    d = socket.create_connection(address, timeout=10)
    d.sendall(("DW4R1 DATA %s %s %s\n" % (channel, notice[1], ok[3:])).encode())
    check(read_line(g) == "OK", "guest spliced")
    for s_ in (g, d):
        s_.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    # A netplay-style frame: type u8 + u32 LE size + payload.
    hello = bytes([1]) + (5).to_bytes(4, "little") + b"HELLO"
    g.sendall(hello)
    check(recv_exactly(d, len(hello)) == hello, "host data received the guest frame")
    welcome = bytes([2]) + (1).to_bytes(4, "little") + bytes([1])
    d.sendall(welcome)
    check(recv_exactly(g, len(welcome)) == welcome, "guest received the host frame")
    lat = []
    for _ in range(200):
        t = time.perf_counter()
        g.sendall(b"x" * 35)
        got = recv_exactly(d, 35)
        d.sendall(got)
        recv_exactly(g, 35)
        lat.append((time.perf_counter() - t) * 1000)
    lat.sort()
    print("relay round trip ms: median %.3f p99 %.3f" % (lat[len(lat) // 2], lat[int(len(lat) * 0.99)]), flush=True)

    # The owner leaves the room: it keeps running with the story frozen.
    check(call(base, "POST", "/api/v1/rooms/%s/leave" % code, None, host["token"])[0] == 204, "owner leaves")
    s, beat = call(base, "POST", "/api/v1/rooms/%s/heartbeat" % code, None, guest["token"])
    check(s == 200 and beat["room"]["storyFrozen"], "room keeps running with the story frozen")
    for s_ in (g, d, ctl):
        s_.close()

    # Rate limit: 5 logins per minute per IP (two used above, so the 4th extra one is refused).
    codes = [call(base, "POST", "/api/v1/auth/login", {"username": host_user, "password": "wrong password"})[0] for _ in range(4)]
    check(codes == [401, 401, 401, 429], "login rate limit", codes)

    print("ALL PASSED" if not FAILURES else "FAILED: " + ", ".join(FAILURES), flush=True)
    return 0 if not FAILURES else 1


if __name__ == "__main__":
    sys.exit(main())
