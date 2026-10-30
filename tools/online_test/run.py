"""The local online test stack (SEAMLESS.md 16).

Starts a private MariaDB (C:/xampp/mysql/bin/mysqld.exe with its own data directory under tools/online_test/.work and
its own port), the DW4Online server on 127.0.0.1 with a throw-away HTTPS certificate (openssl, pinned by the clients)
and the DW4R2 relay on a free port, registers accounts through the API, creates and joins a room, starts N hidden
clients with input scripts, optionally each behind the latency / jitter / loss proxy, collects every client's net
fingerprints and membership log and the relay's membership log, compares them per instance and checks each scenario's
expected events. It stops only the processes it started (by their handles), never anything by name.

    python run.py [SCENARIO ...] --game-exe EXE --data DATA [--cards DIR] [--parallel N] [--list] [--keep]
                                                           the scenarios with hidden game clients (scenarios/<id>)
    python run.py --fake [SCENARIO ...] [--impair OW,JITTER,LOSS]
                                                           the same ideas with fake DW4R2 clients (tests/DW4Online.FakeClient)
    python run.py matrix [--seconds 40] [--warmup 20]      the loss and jitter matrix (in-process hub, SEAMLESS.md 6.4)
    python run.py G1 --game-exe EXE --data DATA [--offset 8] [--frames 22000] [--sealed-ticks 3000]
                                                           two hidden game clients with the campaign bot from a new game
    python run.py dev --game-exe EXE --data DATA [--hidden [--frames N]] [--dev-card DIR]
                                                           the local stack and two clients in one room (visible windows
                                                           for a person, scripts/online-dev.ps1; --hidden for agents)
    python run.py ui [SCENARIO ...] --game-exe EXE --data DATA [--cards DIR]
                                                           the account and room screens (ui/<id>): hidden clients driven by
                                                           --online-ui scripts through the client's own code paths, with
                                                           screenshots (PNG) of every screen in the run folder
    common: [--mysql-port 3431] [--ports 53100-53199] [--no-build]

Game scenarios (scenarios/<id>/scenario.json, one .script per client, expected.txt): every client is a hidden
DW4_ONLINE_DEV build of dw4_game (--smoke-test, DW4_NO_ERROR_BOX=1, below normal priority) that logs in through the pinned
HTTPS API; the first creates the room, the others join it at their start offsets; each plays its pad from its script
(--online-script, src/game/online_script.hpp) and, with a card, starts from that saved character (--online-card, the
campaign bot calls it at the title). Checked for every scenario: the net fingerprints (DW4_ONLINE_TRACE netfp lines)
of every instance are equal on every client at every tick (every 60 ticks) they both report with the same members, the
canonical snapshot hashes of every NEW (and co-arrival) are equal, the relay logs no desync, mismatch or resync, no client
was closed or crashed; then the scenario's expected log (in order per source; "!" lines must not appear; "shared: a b >=
N" asks for N ticks of equal fingerprints with both present in one instance); a scenario's "achievements" asks the
server for each client account's online achievements afterwards (check_achievements). Network traffic stays on loopback. A
scenario.json with "server": {...} gets its own server with those settings merged over the stack's and its own database
(S14h / S14a: {"Rooms": {"StoryWhileOwnerAway": "hold"}}); "min_shared_ticks" lowers the 10,000 shared ticks a scenario
needs (S14: one member plays); "seed_story": true starts the room story from the room creator's card (its online_story
row, SEAMLESS.md 8.4), so the members' card stories are not new to the room (the title gives every member the room
story at game start).

Without a scenario every game scenario runs. G1 is the integration's first game run (a new game from the title, both
meet in the first field).
"""
import argparse
import base64
import re
import threading
import hashlib
import json
import os
import secrets
import shutil
import socket
import ssl
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
SERVER = os.path.join(REPO, "server", "DW4Online")
WORK = os.path.join(HERE, ".work")
MYSQL_BIN = r"C:\xampp\mysql\bin"
BELOW_NORMAL = 0x00004000
NO_WINDOW = 0x08000000
PROTOCOL, DATA_HASH = 43, 1

# The stand-in engine's timing for every client of a scenario (simulation: all must agree).
ENTRY, DEPART = 120, 120


def scenario(name, desc, clients, duration, checks, entry=ENTRY, depart=DEPART):
    return {"name": name, "desc": desc, "clients": clients, "duration": duration, "checks": checks, "entry": entry, "depart": depart}


def client(name, script, quest=0, start=0.0, rate=3000):
    return {"name": name, "script": script, "quest": quest, "start": start, "rate": rate}


def ex(at, stage, group=False, point=0):
    return {"at": at, "do": "group_exit" if group else "exit", "stage": stage, "point": point}


SCENARIOS = [
    scenario("S1", "two members in one stage for 10,000+ ticks, own cameras (fake: the same sealed instance throughout)",
             [client("a", [ex(1.0, "stag0102")]), client("b", [ex(1.0, "stag0102")])], 180,
             [("sealed_together", ["a", "b"], "stag0102"), ("fingerprint_ticks", 10000)]),
    scenario("S2", "a leaves by an exit (fork), b follows later (join by catch-up), both change stage together",
             [client("a", [ex(1.0, "stag0101"), ex(10.0, "stag0102"), ex(32.0, "stag0201", group=True)]),
              client("b", [ex(1.4, "stag0101"), ex(18.0, "stag0102")])], 45,
             [("decision", "a", "New", "stag0101"), ("fork_seen", "a"), ("decision", "b", "Join", "stag0102"),
              ("sealed_together", ["a", "b"], "stag0201")]),
    scenario("S3", "three clients: two joins mid-stage one at a time, then a common stage change",
             [client("a", [ex(1.0, "stag0101"), ex(30.0, "stag0102", group=True)]), client("b", [ex(8.0, "stag0101")]),
              client("c", [ex(16.0, "stag0101")])], 45,
             [("decision", "b", "Join", "stag0101"), ("decision", "c", "Join", "stag0101"), ("sealed_together", ["a", "b", "c"], "stag0102")]),
    scenario("S5", "everybody falls: game over, the group dissolves into solo lobbies",
             [client("a", [ex(1.0, "stag0101"), ex(10.0, "stagML11", group=True)]), client("b", [ex(1.0, "stag0101")])], 18,
             [("sealed_together", ["a", "b"], "stag0101"), ("decision", "a", "Dissolve", "stagML11"), ("decision", "b", "Dissolve", "stagML11")]),
    scenario("S16", "quest and story in stag0401: other quests stay apart, the same quest shares one instance",
             [client("a", [ex(1.0, "stag0401")], quest=1), client("b", [ex(6.0, "stag0401")], quest=0),
              client("c", [ex(9.0, "stag0401")], quest=1)], 22,
             [("decision", "b", "New", "stag0401", "OtherContext"), ("decision", "c", "Join", "stag0401"), ("sealed_together", ["a", "c"], "stag0401")]),
    scenario("S17a", "two members take the gate 1.5 s apart: one co-arrival, one entry scene",
             [client("a", [ex(1.0, "stag0101")]), client("b", [ex(2.5, "stag0101")])], 14,
             [("decision", "a", "New", "stag0101"), ("decision", "b", "CoArrive", "stag0101"), ("sealed_together", ["a", "b"], "stag0101")]),
    scenario("S17b", "two members take the gate 8 s apart: the later waits for the entry scene, then joins",
             [client("a", [ex(1.0, "stag0101")]), client("b", [ex(9.0, "stag0101")])], 30,
             [("waited", "b", "NotJoinable"), ("decision", "b", "Join", "stag0101"), ("sealed_together", ["a", "b"], "stag0101")], entry=600),
    scenario("S22a", "a joiner whose target leaves the stage during the catch-up falls back to NEW",
             [client("a", [ex(1.0, "stag0101"), ex(15.5, "stag0102")]), client("b", [ex(12.0, "stag0101")], rate=90)], 26,
             [("decision", "b", "Join", "stag0101"), ("decision", "b", "New", "stag0101", "HoldExpired")]),
    scenario("S22b", "a joiner whose target forks during the catch-up follows the instance id",
             [client("a", [ex(1.0, "stag0101")]), client("c", [ex(1.0, "stag0101"), ex(15.0, "stag0102")]),
              client("b", [ex(12.0, "stag0101")], rate=90)], 40,
             [("decision", "b", "Join", "stag0101"), ("fork_seen", "a"), ("sealed_together", ["a", "b"], "stag0101")]),
]


class Stack:
    """The processes this run starts; stop() ends exactly those."""

    def __init__(self, args):
        self.args = args
        self.procs = []
        self.ports = []
        self.dir = os.path.join(WORK, time.strftime("run_%Y%m%d_%H%M%S"))
        os.makedirs(self.dir, exist_ok=True)

    def spawn(self, argv, log, env=None, flags=BELOW_NORMAL | NO_WINDOW):
        f = open(log, "w", encoding="utf-8")
        p = subprocess.Popen(argv, stdout=f, stderr=subprocess.STDOUT, env=env, creationflags=flags)
        self.procs.append((p, f))
        return p

    def stop(self):
        if getattr(self, "mysql", None):
            port, pw = self.mysql
            subprocess.run([os.path.join(MYSQL_BIN, "mysql.exe"), "--protocol=tcp", "-h", "127.0.0.1", "-P", str(port), "-u", "root",
                            "--password=" + pw, "-e", "SHUTDOWN"], capture_output=True, creationflags=NO_WINDOW)
            self.mysql = None
        for p, f in reversed(self.procs):
            if p.poll() is None:
                p.terminate()
                try:
                    p.wait(10)
                except subprocess.TimeoutExpired:
                    p.kill()
            f.close()
        self.procs.clear()


def used_ports():
    out = subprocess.run(["netstat", "-ano", "-p", "TCP"], capture_output=True, text=True, creationflags=NO_WINDOW).stdout
    used = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and ":" in parts[1]:
            try:
                used.add(int(parts[1].rsplit(":", 1)[1]))
            except ValueError:
                pass
    return used


def free_port(stack):
    lo, hi = (int(x) for x in stack.args.ports.split("-"))
    used = used_ports()
    for p in range(lo, hi + 1):
        if p in used or p in stack.ports:
            continue
        s = socket.socket()
        try:
            s.bind(("127.0.0.1", p))
        except OSError:
            continue
        finally:
            s.close()
        stack.ports.append(p)
        return p
    raise RuntimeError("no free port in " + stack.args.ports)


def wait_port(port, seconds):
    end = time.time() + seconds
    while time.time() < end:
        try:
            socket.create_connection(("127.0.0.1", port), timeout=0.5).close()
            return True
        except OSError:
            time.sleep(0.3)
    return False


def start_mysql(stack):
    port = stack.args.mysql_port
    probe = socket.socket()
    try:
        probe.bind(("127.0.0.1", port))  # a listener there (not just TIME_WAIT entries) refuses the bind
    except OSError:
        raise RuntimeError("port %d is in use (another mysqld?): pass --mysql-port" % port)
    finally:
        probe.close()
    data = os.path.join(WORK, "mysql-data")
    pw_file = os.path.join(WORK, "mysql-root.txt")
    if not os.path.isdir(data):
        pw = secrets.token_urlsafe(18)
        with open(pw_file, "w") as f:
            f.write(pw)
        subprocess.run([os.path.join(MYSQL_BIN, "mysql_install_db.exe"), "--datadir=" + data, "--port=%d" % port, "--password=" + pw, "--silent"],
                       check=True, creationflags=NO_WINDOW)
    pw = open(pw_file).read().strip()
    stack.spawn([os.path.join(MYSQL_BIN, "mysqld.exe"), "--defaults-file=" + os.path.join(data, "my.ini"), "--bind-address=127.0.0.1",
                 "--port=%d" % port, "--character-set-server=utf8mb4", "--collation-server=utf8mb4_unicode_ci", "--default-time-zone=+00:00",
                 "--innodb-use-native-aio=0", "--log-error=" + os.path.join(stack.dir, "mysqld.err")], os.path.join(stack.dir, "mysqld.out"))
    if not wait_port(port, 40):
        raise RuntimeError("mysqld did not open port %d" % port)
    stack.mysql = (port, pw)
    return create_database(stack)


def create_database(stack):
    """A new database in the run's mysqld; its connection string."""
    port, pw = stack.mysql
    db = "dw4online_run_" + secrets.token_hex(4)
    subprocess.run([os.path.join(MYSQL_BIN, "mysql.exe"), "--protocol=tcp", "-h", "127.0.0.1", "-P", str(port), "-u", "root", "--password=" + pw,
                    "-e", "CREATE DATABASE %s CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci" % db], check=True, creationflags=NO_WINDOW)
    return "Server=127.0.0.1;Port=%d;Database=%s;User ID=root;Password=%s" % (port, db, pw)


def openssl():
    for cand in (r"C:\Program Files\Git\mingw64\bin\openssl.exe", r"C:\Program Files\Git\usr\bin\openssl.exe", shutil.which("openssl") or ""):
        if cand and os.path.exists(cand):
            return cand
    raise RuntimeError("openssl not found (Git for Windows ships one)")


def make_certificate(folder):
    """A throw-away P-256 certificate for 127.0.0.1 (PEM for Kestrel), its SPKI pin and its DER."""
    o = openssl()
    cert, key = os.path.join(folder, "cert.pem"), os.path.join(folder, "key.pem")
    subprocess.run([o, "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes", "-keyout", key, "-out", cert,
                    "-days", "2", "-subj", "/CN=127.0.0.1", "-addext", "subjectAltName=IP:127.0.0.1"], check=True, capture_output=True,
                   creationflags=NO_WINDOW)
    der = subprocess.run([o, "x509", "-in", cert, "-outform", "der"], check=True, capture_output=True, creationflags=NO_WINDOW).stdout
    pub = subprocess.run([o, "x509", "-in", cert, "-pubkey", "-noout"], check=True, capture_output=True, creationflags=NO_WINDOW).stdout
    spki = subprocess.run([o, "pkey", "-pubin", "-outform", "der"], input=pub, check=True, capture_output=True, creationflags=NO_WINDOW).stdout
    return cert, key, base64.b64encode(hashlib.sha256(spki).digest()).decode(), der


def build():
    for proj in ("src/DW4Online.Server/DW4Online.Server.csproj", "tests/DW4Online.FakeClient/DW4Online.FakeClient.csproj"):
        subprocess.run(["dotnet", "build", os.path.join(SERVER, proj), "-nologo", "-v", "q", "--disable-build-servers"], check=True,
                       creationflags=BELOW_NORMAL | NO_WINDOW)


def dll(project, name):
    return os.path.join(SERVER, project, "bin", "Debug", "net10.0", name)


class Api:
    def __init__(self, base, der):
        self.base = base
        self.ctx = ssl.create_default_context()
        self.ctx.check_hostname = False
        self.ctx.verify_mode = ssl.CERT_NONE
        self.der = der

    def call(self, method, path, body=None, token=None):
        req = urllib.request.Request(self.base + path, data=json.dumps(body).encode() if body is not None else None, method=method)
        req.add_header("Content-Type", "application/json")
        if token:
            req.add_header("Authorization", "Bearer " + token)
        # The pin: the server must present exactly the certificate this run made.
        host, port = self.base.split("//")[1].split(":")
        with socket.create_connection((host, int(port))) as raw, self.ctx.wrap_socket(raw) as tls:
            if tls.getpeercert(binary_form=True) != self.der:
                raise RuntimeError("the server presented another certificate")
        try:
            with urllib.request.urlopen(req, timeout=15, context=self.ctx) as r:
                raw = r.read()
                return r.status, json.loads(raw) if raw else None
        except urllib.error.HTTPError as e:
            raw = e.read()
            return e.code, json.loads(raw) if raw else None


def merged(base, extra):
    """base with extra's keys deep-merged into it (nested objects key by key)."""
    out = dict(base)
    for k, v in (extra or {}).items():
        out[k] = merged(out[k], v) if isinstance(v, dict) and isinstance(out.get(k), dict) else v
    return out


def start_server(stack, connection, extra=None, name=""):
    """The DW4Online server (API and relay) on loopback; extra: settings merged over the test stack's (a scenario's own
    server, in its own folder and database: SEAMLESS.md 8.6's Rooms:StoryWhileOwnerAway, for example)."""
    folder = os.path.join(stack.dir, "server-" + name) if name else stack.dir
    os.makedirs(folder, exist_ok=True)
    https, relay = free_port(stack), free_port(stack)
    cert, key, pin, der = make_certificate(folder)
    diag = os.path.join(folder, "relay")
    settings = {
        "Database": {"ConnectionString": connection, "AutoMigrate": True},
        "Kestrel": {"Endpoints": {"Http": {"Url": "http://127.0.0.1:%d" % free_port(stack)},
                                  "Https": {"Url": "https://127.0.0.1:%d" % https, "Certificate": {"Path": cert, "KeyPath": key}}}},
        "Service": {"RelayHost": "127.0.0.1"},
        "Accounts": {"PasswordIterations": 1000},
        "RateLimits": {k: 100000 for k in ("GlobalPerMinute", "LoginPerMinute", "RegisterPerHour", "RoomCreatesPerMinute", "RoomJoinsPerMinute",
                                           "StageChangesPerMinute")},
        "Relay": {"Enabled": True, "ListenAddress": "127.0.0.1", "Port": relay, "TicketKey": base64.b64encode(secrets.token_bytes(32)).decode(),
                  "DiagnosticsDirectory": diag},
        "Web": {"DataProtectionKeysDirectory": "memory"},
        "Logging": {"File": {"Directory": os.path.join(folder, "server-log")}},
    }
    settings = merged(settings, extra)
    path = os.path.join(folder, "server-settings.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(settings, f, indent=1)
    stack.spawn(["dotnet", dll("src/DW4Online.Server", "DW4Online.Server.dll"), "--settings", path], os.path.join(folder, "server.out"),
                env=dict(os.environ, ASPNETCORE_ENVIRONMENT="Production", DOTNET_gcServer="0"))
    if not wait_port(https, 60) or not wait_port(relay, 30):
        raise RuntimeError("the server did not start (see %s)" % os.path.join(folder, "server.out"))
    return "https://127.0.0.1:%d" % https, relay, pin, der, diag


def run_scenario(stack, sc, api, relay_port, pin, diag):
    tag = sc["name"].lower() + secrets.token_hex(2)
    names = [c["name"] for c in sc["clients"]]
    tokens = {}
    for n in names:
        s, body = api.call("POST", "/api/v1/auth/register", {"username": tag + n, "password": "online test " + n})
        if s != 201:
            raise RuntimeError("register %s: %s %s" % (n, s, body))
        tokens[n] = body["token"]
    s, room = api.call("POST", "/api/v1/rooms", {"name": sc["name"], "maxPlayers": 10, "protocol": PROTOCOL, "dataHash": DATA_HASH,
                                                 "clientVersion": "0.52.0"}, tokens[names[0]])
    code, settings_hash = room["room"]["code"], room["settingsHash"]
    for n in names[1:]:
        s, _ = api.call("POST", "/api/v1/rooms/%s/join" % code, {"protocol": PROTOCOL, "dataHash": DATA_HASH, "clientVersion": "0.52.0"}, tokens[n])
        if s != 200:
            raise RuntimeError("join %s: %s" % (n, s))
    out = os.path.join(stack.dir, sc["name"])
    os.makedirs(out, exist_ok=True)
    procs, t0 = [], time.time()
    impair = stack.args.impair
    for k, c in enumerate(sorted(sc["clients"], key=lambda c: c["start"])):
        while time.time() - t0 < c["start"]:
            time.sleep(0.05)
        port = relay_port
        if impair:
            ow, jitter, loss = impair.split(",")
            port = free_port(stack)
            stack.spawn(["dotnet", dll("tests/DW4Online.FakeClient", "DW4Online.FakeClient.dll"), "proxy", "--listen", str(port), "--target",
                         str(relay_port), "--one-way", ow, "--jitter", jitter, "--loss", loss], os.path.join(out, c["name"] + ".proxy.out"))
            wait_port(port, 20)
        script = os.path.join(out, c["name"] + ".script.json")
        with open(script, "w") as f:
            json.dump([{**a, "at": a["at"] - c["start"]} for a in c["script"]], f)
        argv = ["dotnet", dll("tests/DW4Online.FakeClient", "DW4Online.FakeClient.dll"), "client", "--port", str(port), "--api", api.base,
                "--token", tokens[c["name"]], "--room", code, "--pin", pin, "--protocol", str(PROTOCOL), "--data-hash", str(DATA_HASH),
                "--settings-hash", settings_hash, "--name", c["name"], "--quest", str(c["quest"]), "--seed", str(k + 1), "--script", script,
                "--duration", str(sc["duration"] - c["start"]), "--replay-rate", str(c["rate"]), "--entry-ticks", str(sc["entry"]),
                "--depart-ticks", str(sc["depart"]), "--log", os.path.join(out, c["name"] + ".jsonl"), "--stats", os.path.join(out, c["name"] + ".stats.json")]
        procs.append((c["name"], stack.spawn(argv, os.path.join(out, c["name"] + ".out"))))
    for name, p in procs:
        p.wait(sc["duration"] + 60)
    return evaluate(sc, out, code, diag)


def load_jsonl(path):
    if not os.path.exists(path):
        return []
    return [json.loads(line) for line in open(path, encoding="utf-8") if line.strip()]


def evaluate(sc, out, code, diag):
    logs = {c["name"]: load_jsonl(os.path.join(out, c["name"] + ".jsonl")) for c in sc["clients"]}
    stats = {}
    for c in sc["clients"]:
        p = os.path.join(out, c["name"] + ".stats.json")
        stats[c["name"]] = json.load(open(p)) if os.path.exists(p) else {"fingerprints": []}
    problems, notes = [], []
    # Fingerprints: equal for every (instance, visit, tick) two clients both reported.
    table = {}
    for n, st in stats.items():
        for fp in st.get("fingerprints", []):
            table.setdefault((fp["inst"], fp["visit"], fp["tick"]), {})[n] = fp["fp"]
    compared = mismatched = 0
    ticks_by_instance = {}
    for key, per in table.items():
        if len(per) >= 2:
            compared += 1
            if len(set(per.values())) > 1:
                mismatched += 1
                if mismatched <= 3:
                    problems.append("fingerprints differ at instance %s visit %s tick %s: %s" % (key[0], key[1], key[2], per))
            ticks_by_instance[(key[0], key[1])] = max(ticks_by_instance.get((key[0], key[1]), 0), key[2])
    notes.append("%d fingerprint ticks compared, %d differ" % (compared, mismatched))
    # Membership logs: every client whose fingerprints of an instance span an event's tick logged the same event
    # (joins, leaves and forks at the same tick with the same slots).
    span = {}
    for n, st in stats.items():
        for fp in st.get("fingerprints", []):
            k = (n, fp["inst"], fp["visit"])
            lo, hi = span.get(k, (fp["tick"], fp["tick"]))
            span[k] = (min(lo, fp["tick"]), max(hi, fp["tick"]))
    events = {}
    for n, lines in logs.items():
        for e in lines:
            if e["ev"] in ("join_applied", "leave_applied", "fork"):
                events.setdefault((e["inst"], e["visit"], e["ev"], e.get("at"), e.get("slots", e.get("leavers"))), set()).add(n)
    membership = 0
    for key, who in events.items():
        inst, visit, ev, at, slots = key
        should = {n for n in logs if (n, inst, visit) in span and span[(n, inst, visit)][0] <= at and span[(n, inst, visit)][1] >= at + 60}
        missing = should - who
        membership += 1
        if missing:
            problems.append("membership event %s logged by %s, not by %s" % (key, sorted(who), sorted(missing)))
    notes.append("%d membership events compared" % membership)
    # Expected events.
    for check in sc["checks"]:
        kind = check[0]
        if kind == "decision":
            _, who, decision, stage = check[:4]
            notice = check[4] if len(check) > 4 else None
            ok = any(e["ev"] == "decision" and e["decision"] == decision and e.get("to") == stage
                     and (notice is None or e.get("notice") == notice) for e in logs[who])
            if not ok:
                problems.append("%s: no %s decision%s for %s" % (who, decision, " (" + notice + ")" if notice else "", stage))
        elif kind == "fork_seen":
            if not any(e["ev"] == "fork" for e in logs[check[1]]):
                problems.append("%s saw no fork" % check[1])
        elif kind == "waited":
            if not any(e["ev"] == "wait" and e.get("reason") == check[2] for e in logs[check[1]]):
                problems.append("%s did not wait (%s)" % (check[1], check[2]))
        elif kind == "sealed_together":
            _, who, stage = check
            insts = []
            for n in who:
                fps = [fp for fp in stats[n].get("fingerprints", [])]
                ins = {(fp["inst"], fp["visit"]) for fp in fps}
                staged = {(e["inst"], e["visit"]) for e in logs[n] if e.get("stage") == stage}
                insts.append(ins & staged)
            common = set.intersection(*insts) if insts else set()
            if not common:
                problems.append("%s never shared a logged instance in %s" % ("+".join(who), stage))
            else:
                best = max(common, key=lambda k: ticks_by_instance.get(k, 0))
                notes.append("%s shared instance %s visit %s in %s up to tick %d" % ("+".join(who), best[0], best[1], stage, ticks_by_instance.get(best, 0)))
        elif kind == "fingerprint_ticks":
            longest = max(ticks_by_instance.values() or [0])
            if longest < check[1]:
                problems.append("fingerprints compared only up to tick %d (< %d)" % (longest, check[1]))
            else:
                notes.append("fingerprints identical up to tick %d" % longest)
    relay_log = load_jsonl(os.path.join(diag, "room_%s.jsonl" % code))
    notes.append("relay: %d membership events (%s)" % (len(relay_log), ", ".join(sorted({e["ev"] for e in relay_log}))))
    for n, st in stats.items():
        if st.get("CloseReason") and not str(st["CloseReason"]).startswith("Left"):
            problems.append("%s was closed: %s" % (n, st["CloseReason"]))
    return problems, notes


SCENARIO_DIR = os.path.join(HERE, "scenarios")

# The card format (docs/SAVES.md, src/save_data.cpp): a Dolphin GCI file is a 64-byte directory entry and the 90,112-byte
# NDMWSaveData file, which holds two copies (at 0x2000 and 0xC000) of a 40,960-byte block: a checksum (the sum of the
# big-endian words after it), the version word 4 and four character records 0x2000 apart from offset 8; a record's
# BASE_FLAG bytes start at 792 (one byte a flag).
GCI_ENTRY, CARD_COPIES, CARD_COPY_SIZE, RECORD_FLAGS = 64, (0x2000, 0xC000), 40960, 792


def derive_card(src, dst, flags):
    """A copy of the card folder src (its .gci files, or those of its "Card A") in dst with the story flags
    {flag: value} set in the first character record of both copies (checksums updated). A scenario's precondition
    that has nothing to do with the online play (S16: Digi-Elf 0 freed in stag0103's boat ride, which the lobby
    Digi-Elf's first quest asks for and the r25c campaign never did: flag 89, stored for the normal difficulty as 714,
    which stagIS10 gameStart.lua copies back into 89 when the game starts); the run folder keeps the copy."""
    if os.path.isdir(os.path.join(src, "Card A")):
        src = os.path.join(src, "Card A")
    os.makedirs(dst, exist_ok=True)
    done = 0
    for name in os.listdir(src):
        if not name.endswith(".gci"):
            continue
        data = bytearray(open(os.path.join(src, name), "rb").read())
        for copy in CARD_COPIES:
            base = GCI_ENTRY + copy
            if struct.unpack(">I", data[base + 4:base + 8])[0] != 4 or not data[base + 8]:
                raise RuntimeError("%s: copy at 0x%x has no character in its first record" % (name, copy))
            for flag, value in flags.items():
                data[base + 8 + RECORD_FLAGS + int(flag)] = int(value)
            words = struct.unpack(">%dI" % ((CARD_COPY_SIZE - 4) // 4), bytes(data[base + 4:base + CARD_COPY_SIZE]))
            data[base:base + 4] = struct.pack(">I", sum(words) & 0xFFFFFFFF)
        open(os.path.join(dst, name), "wb").write(bytes(data))
        done += 1
    if not done:
        raise RuntimeError("no .gci file in " + src)
    return dst


def card_story(folder):
    """The story of the first character record of the card folder's DW4 save (the first copy that passes its
    checksum, as the game reads it): BASE_FLAG (1024 bytes at 792), BASE_FLAGFOLDER (12 at 1816), BASE_COUNTER
    (16 x i32 big endian at 1828)."""
    if os.path.isdir(os.path.join(folder, "Card A")):
        folder = os.path.join(folder, "Card A")
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".gci"):
            continue
        data = open(os.path.join(folder, name), "rb").read()
        for copy in CARD_COPIES:
            base = GCI_ENTRY + copy
            words = struct.unpack(">%dI" % ((CARD_COPY_SIZE - 4) // 4), data[base + 4:base + CARD_COPY_SIZE])
            if struct.unpack(">I", data[base:base + 4])[0] != sum(words) & 0xFFFFFFFF or not data[base + 8]:
                continue
            rec = data[base + 8:base + 8 + 3316]
            return rec[792:1816], rec[1816:1828], struct.unpack(">16i", rec[1828:1892])
    raise RuntimeError("no valid DW4 save in " + folder)


def seed_story(stack, connection, account_id, folder):
    """The account's online story (online_story, SEAMLESS.md 8.4) from the card's story: the room story of a room this
    account opens starts from it, as if the online character had played that far (test runs that start from saved
    cards; the title gives every member the room story at game start)."""
    flags, folder_bytes, counters = card_story(folder)
    db = dict(kv.split("=", 1) for kv in connection.split(";") if "=" in kv)["Database"]
    port, pw = stack.mysql
    sql = ("REPLACE INTO online_story (account_id, revision, flags, folder, counters, updated_at) VALUES (%d, 1, X'%s', X'%s', X'%s', "
           "UTC_TIMESTAMP(3))" % (int(account_id), flags.hex(), folder_bytes.hex(), struct.pack("<16i", *counters).hex()))
    subprocess.run([os.path.join(MYSQL_BIN, "mysql.exe"), "--protocol=tcp", "-h", "127.0.0.1", "-P", str(port), "-u", "root", "--password=" + pw,
                    db, "-e", sql], check=True, capture_output=True, creationflags=NO_WINDOW)


def stored_story_flag(stack, connection, account_id, flag):
    """(revision, value of `flag`) of the account's online_story row as the database holds it now; (0, 0) without one."""
    db = dict(kv.split("=", 1) for kv in connection.split(";") if "=" in kv)["Database"]
    port, pw = stack.mysql
    sql = "SELECT revision, ORD(SUBSTRING(flags, %d, 1)) FROM online_story WHERE account_id = %d" % (int(flag) + 1, int(account_id))
    out = subprocess.run([os.path.join(MYSQL_BIN, "mysql.exe"), "--protocol=tcp", "-h", "127.0.0.1", "-P", str(port), "-u", "root", "--password=" + pw,
                          "-N", "-B", db, "-e", sql], check=True, capture_output=True, text=True, creationflags=NO_WINDOW).stdout.split()
    return (int(out[0]), int(out[1])) if len(out) >= 2 else (0, 0)


def watch_story_saved(stack, sc, out, connection, owner_id, result, stop):
    """scenario.json "story_saved": {"client", "after", "flag", "seconds"}: the time from the client's log line that
    contains "after" (the script step that saw the flag set in its engine; step lines are flushed at once) until the
    room owner's online_story row in the database holds the flag (SEAMLESS.md 17 S2: within ten seconds). The log is
    read every 0.1 s, the database every 0.2 s; result["seconds"] is that time, None if it never came within 60 s."""
    spec = sc["story_saved"]
    log = os.path.join(out, spec["client"] + ".log")
    seen, at, text = None, 0, ""
    while not stop.is_set():
        if seen is None:
            if os.path.exists(log):
                with open(log, encoding="utf-8", errors="replace") as f:
                    f.seek(at)
                    chunk = f.read()
                    at = f.tell()
                text = (text + chunk)[-4096:]
                if spec["after"] in text:
                    seen = time.time()
                    result["before"] = stored_story_flag(stack, connection, owner_id, spec["flag"])
                    continue
            time.sleep(0.1)
            continue
        revision, value = stored_story_flag(stack, connection, owner_id, spec["flag"])
        if value:
            result["seconds"], result["revision"] = time.time() - seen, revision
            return
        if time.time() - seen > 60:
            result["seconds"] = None
            return
        time.sleep(0.2)


def game_scenarios():
    names = sorted(d for d in os.listdir(SCENARIO_DIR) if os.path.isfile(os.path.join(SCENARIO_DIR, d, "scenario.json"))) \
        if os.path.isdir(SCENARIO_DIR) else []
    return [dict(json.load(open(os.path.join(SCENARIO_DIR, n, "scenario.json"), encoding="utf-8")), name=n) for n in names]


def run_real(stack, sc, api, relay_port, pin, diag, lock, connection):
    """One game scenario: hidden game clients with scripts in one room."""
    a = stack.args
    if not a.game_exe or not a.data:
        raise RuntimeError("game scenarios need --game-exe (a DW4_ONLINE_DEV build) and --data (the game data)")
    src = os.path.join(SCENARIO_DIR, sc["name"])
    out = os.path.join(stack.dir, sc["name"])
    os.makedirs(out, exist_ok=True)
    tag = sc["name"].lower() + secrets.token_hex(2)
    passwords, accounts = {}, {}
    with lock:
        for c in sc["clients"]:
            passwords[c["name"]] = "online test " + c["name"] + " " + secrets.token_hex(4)
            s, body = api.call("POST", "/api/v1/auth/register", {"username": tag + c["name"], "password": passwords[c["name"]]})
            if s != 201:
                raise RuntimeError("register %s: %s %s" % (c["name"], s, body))
            accounts[c["name"]] = body["account"]["id"]
    https_port = api.base.rsplit(":", 1)[1]
    room_name = sc["name"] + " " + tag
    t0 = time.time()

    def script_for(c):
        # The client's script with every @N replaced by the scenario start + N seconds (the epoch condition: steps
        # of several clients that must happen at set times, such as two departures two seconds apart).
        text = open(os.path.join(src, c["script"]), encoding="utf-8").read()
        text = re.sub(r"@(\d+(?:\.\d+)?)", lambda m: "%.1f" % (t0 + float(m.group(1))), text)

        # {route:NAME} is the waypoint list of routes/NAME.txt, {route:NAME:X,Z} the same up to the waypoint X,Z,
        # {route:NAME>X,Z} the same from the waypoint X,Z on (S14: on along the route after a detour).
        def route(m):
            lines = open(os.path.join(HERE, "routes", m.group(1) + ".txt"), encoding="utf-8").read().splitlines()
            points = " ".join(l for l in lines if l.strip() and not l.startswith("#")).split()
            if m.group(3):
                return " ".join(points[points.index(m.group(3)):])
            return " ".join(points[:points.index(m.group(2)) + 1] if m.group(2) else points)
        text = re.sub(r"\{route:(\w+)(?::(-?\d+,-?\d+)|>(-?\d+,-?\d+))?\}", route, text)
        path = os.path.join(out, c["name"] + ".script")
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        return path

    def saved_card(c):
        # The client's card folder (scenario.json's "card", with its "card_flags" in a copy), or None.
        saved = c.get("card", sc.get("card"))
        if not saved:
            return None
        folder = saved if os.path.isabs(saved) else os.path.join(a.cards, saved)
        if not os.path.isdir(folder):
            raise RuntimeError("no card folder %s (--cards)" % folder)
        if c.get("card_flags"):
            folder = derive_card(folder, os.path.join(out, "card_from_" + c["name"]), c["card_flags"])
        return folder

    def start(c, room_args):
        card = os.path.join(out, "card_" + c["name"])
        for sub_dir in ("Card A", "Card B"):
            os.makedirs(os.path.join(card, sub_dir), exist_ok=True)
        env = dict(os.environ, DW4_NO_ERROR_BOX="1", DW4_ONLINE_SERVER="127.0.0.1:" + https_port, DW4_ONLINE_PIN=pin,
                   DW4_ONLINE_PASSWORD=passwords[c["name"]], DW4_ONLINE_TRACE="1")
        env.update(sc.get("env", {}))
        env.update(c.get("env", {}))
        argv = [a.game_exe, "--data", a.data, "--smoke-test", str(c.get("frames", sc.get("frames", 30000))), "--card-dir", card,
                "--online-login", tag + c["name"], "--online-script", script_for(c)] + room_args
        folder = saved_card(c)
        if folder:
            argv += ["--online-card", folder]
            env["DW4_CAMPAIGN_LOAD"] = "1"
        return stack.spawn(argv, os.path.join(out, c["name"] + ".log"), env=env)

    clients = sorted(sc["clients"], key=lambda c: c.get("start", 0))
    if sc.get("seed_story"):
        # The room's creator is its owner: the room story starts from the owner's card.
        owner_card = saved_card(clients[0])
        if not owner_card:
            raise RuntimeError("seed_story needs a card for the room's creator %s" % clients[0]["name"])
        with lock:
            seed_story(stack, connection, accounts[clients[0]["name"]], owner_card)
    procs = [(clients[0]["name"], start(clients[0], ["--online-create", room_name]))]
    story_saved, stop_watch = {}, threading.Event()
    if sc.get("story_saved"):
        watcher = threading.Thread(target=watch_story_saved, args=(stack, sc, out, connection, accounts[clients[0]["name"]], story_saved, stop_watch),
                                   daemon=True)
        watcher.start()
    code = None
    while time.time() - t0 < 90 and code is None:
        if procs[0][1].poll() is not None:
            raise RuntimeError("the first client ended before its room existed (see %s)" % os.path.join(out, clients[0]["name"] + ".log"))
        with lock:
            s, rooms = api.call("GET", "/api/v1/rooms")
        for r in (rooms if isinstance(rooms, list) else (rooms or {}).get("rooms", [])):
            if r.get("name") == room_name:
                code = r.get("code")
        time.sleep(0.2)
    if not code:
        raise RuntimeError("the first client's room did not appear")
    for c in clients[1:]:
        while time.time() - t0 < c.get("start", 0):
            time.sleep(0.05)
        procs.append((c["name"], start(c, ["--online-join", code])))
    limit = sc.get("timeout", 1200)
    for name, p in procs:
        try:
            p.wait(max(1, limit - (time.time() - t0)))
        except subprocess.TimeoutExpired:
            pass
    timed_out = [name for name, p in procs if p.poll() is None]
    exits = {name: p.poll() for name, p in procs}
    for name, p in procs:
        if p.poll() is None:
            p.terminate()
    stop_watch.set()
    problems, notes = evaluate_real(sc, src, out, code, diag, timed_out, exits)
    if sc.get("story_saved"):
        spec = sc["story_saved"]
        seconds = story_saved.get("seconds")
        if "before" not in story_saved:
            problems.append("story_saved: %s never logged '%s'" % (spec["client"], spec["after"]))
        elif story_saved["before"][1]:
            problems.append("story_saved: the owner's online_story had flag %s before %s set it (%s)" % (spec["flag"], spec["client"],
                                                                                                    story_saved["before"]))
        elif seconds is None:
            problems.append("story_saved: flag %s never reached the owner's online_story" % spec["flag"])
        else:
            # The log is read every 0.1 s: the flag may have been set up to 0.1 s before the line was seen.
            notes.append("story_saved: flag %s in the owner's online_story (revision %d) %.1f s after %s set it (at most %s s)" % (
                spec["flag"], story_saved["revision"], seconds, spec["client"], spec["seconds"]))
            if seconds + 0.1 > spec["seconds"]:
                problems.append("story_saved: flag %s took %.1f s to reach the owner's online_story (> %s s)" % (spec["flag"], seconds,
                                                                                                             spec["seconds"]))
    if sc.get("achievements"):
        check_achievements(sc, api, tag, passwords, lock, problems, notes)
    return problems, notes


def check_achievements(sc, api, tag, passwords, lock, problems, notes):
    """The online account's achievements (SEAMLESS.md 11) a scenario asks for in "achievements": per client, the codes the
    server must hold ("unlocked", in the "ruleset", standard by default, with "players" per code where given), the codes it
    must not ("locked"), and the least progress of stat achievements ("progress": code -> the counter's least value)."""
    for name, want in sorted(sc["achievements"].items()):
        with lock:
            s, body = api.call("POST", "/api/v1/auth/login", {"username": tag + name, "password": passwords[name], "deviceName": "online_test",
                                                              "protocol": 44})
        if s != 200 or not body or not body.get("token"):
            problems.append("%s: no login for the achievement check (%s %s)" % (name, s, body))
            continue
        with lock:
            s, items = api.call("GET", "/api/v1/achievements", token=body["token"])
        if s != 200 or not isinstance(items, list):
            problems.append("%s: GET /achievements answered %s" % (name, s))
            continue
        by_code = {a["code"]: a for a in items}
        held = sorted(code for code, a in by_code.items() if a.get("unlocks"))
        notes.append("%s holds online: %s" % (name, ", ".join(held) or "-"))
        for code in want.get("unlocked", []):
            unlocks = (by_code.get(code) or {}).get("unlocks") or []
            if not unlocks:
                problems.append("%s: the server holds no %s" % (name, code))
                continue
            u = unlocks[0]
            if u.get("ruleset") != want.get("ruleset", "standard"):
                problems.append("%s: %s in ruleset %s" % (name, code, u.get("ruleset")))
            players = want.get("players", {}).get(code)
            if players is not None and u.get("players") != players:
                problems.append("%s: %s with players %s, expected %s" % (name, code, u.get("players"), players))
            if u.get("flagged"):
                problems.append("%s: %s is flagged" % (name, code))
        for code in want.get("locked", []):
            if code in held:
                problems.append("%s: %s must not be unlocked" % (name, code))
        for code, least in want.get("progress", {}).items():
            progress = (by_code.get(code) or {}).get("progress")
            notes.append("%s: %s progress %s" % (name, code, progress))
            if progress is None or progress < least:
                problems.append("%s: %s progress %s, expected at least %s" % (name, code, progress, least))


def parse_client_log(path):
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines() if os.path.exists(path) else []
    fps, snaps = {}, {}
    for line in lines:
        if line.startswith("online-trace: netfp "):
            kv = dict(x.split("=", 1) for x in line[len("online-trace: netfp "):].split())
            fps[(int(kv["inst"]), int(kv["visit"]), int(kv["tick"]), int(kv["present"], 16))] = kv["fp"]
        elif line.startswith("online-trace: snaphash "):
            parts = line[len("online-trace: snaphash "):].split()
            kv = dict(x.split("=", 1) for x in parts if "=" in x)
            snaps[(int(kv["inst"]), int(kv["visit"]))] = kv["hash"]
    return lines, fps, snaps


def evaluate_real(sc, src, out, code, diag, timed_out, exits):
    problems, notes = [], []
    names = [c["name"] for c in sc["clients"]]
    logs, fps, snaps = {}, {}, {}
    for n in names:
        logs[n], fps[n], snaps[n] = parse_client_log(os.path.join(out, n + ".log"))
    for n in timed_out:
        problems.append("%s did not finish within %s s" % (n, sc.get("timeout", 1200)))
    for n, code_ in exits.items():
        if code_ not in (0, None):
            problems.append("%s exited with %s" % (n, code_))
    for n in names:
        if not any(l.startswith("online-script: done") for l in logs[n]):
            problems.append("%s: the script did not finish" % n)
        for l in logs[n]:
            if l.startswith("online-trace: close") or l.startswith("error:") or "inputs stopped arriving" in l or l.startswith("online-script: FAIL"):
                problems.append("%s: %s" % (n, l))
    # Net fingerprints: equal wherever two clients report the same instance, visit, tick and members.
    table = {}
    for n in names:
        for k, fp in fps[n].items():
            table.setdefault(k, {})[n] = fp
    compared = [k for k, per in table.items() if len(per) >= 2]
    differ = [k for k in compared if len(set(table[k].values())) > 1]
    notes.append("%d fingerprint ticks reported by two or more clients, %d differ" % (len(compared), len(differ)))
    for k in sorted(differ)[:4]:
        problems.append("net fingerprints differ at instance %d visit %d tick %d (present %x): %s" % (k[0], k[1], k[2], k[3], table[k]))
    spans = {}
    for k in compared:
        who = tuple(sorted(table[k]))
        spans.setdefault((k[0], k[1], who), []).append(k[2])
    for (inst, visit, who), ticks in sorted(spans.items()):
        notes.append("instance %d visit %d: %s equal from tick %d to %d (%d comparisons)" % (inst, visit, "+".join(who), min(ticks), max(ticks), len(ticks)))
    # Every scenario plays at least min_shared_ticks (10,000) ticks in shared instances, compared every 60 ticks.
    per_visit = {}
    for k in compared:
        lo, hi = per_visit.get((k[0], k[1]), (k[2], k[2]))
        per_visit[(k[0], k[1])] = (min(lo, k[2]), max(hi, k[2]))
    shared_total = sum(hi - lo for lo, hi in per_visit.values())
    need = sc.get("min_shared_ticks", 10000)
    notes.append("%d ticks in shared instances compared (%d needed)" % (shared_total, need))
    if shared_total < need:
        problems.append("only %d ticks in shared instances compared (< %d)" % (shared_total, need))
    # Canonical snapshot hashes of every NEW / co-arrival.
    stab = {}
    for n in names:
        for k, h in snaps[n].items():
            stab.setdefault(k, {})[n] = h
    snap_multi = [k for k, per in stab.items() if len(per) >= 2]
    notes.append("%d snapshot hashes reported by two or more clients" % len(snap_multi))
    for k in snap_multi:
        if len(set(stab[k].values())) > 1:
            problems.append("canonical snapshot hashes differ at instance %d visit %d: %s" % (k[0], k[1], stab[k]))
    relay = load_jsonl(os.path.join(diag, "room_%s.jsonl" % code))
    kinds = {}
    for e in relay:
        kinds[e["ev"]] = kinds.get(e["ev"], 0) + 1
    notes.append("relay: " + ", ".join("%s %d" % kv for kv in sorted(kinds.items())))
    for bad in ("desync", "snaphash_mismatch", "split_mismatch", "barrier_mismatch", "barrier_split", "resync", "ready_mismatch"):
        if kinds.get(bad):
            problems.append("relay logged %d x %s" % (kinds[bad], bad))
    relay_lines = [" ".join([e["ev"]] + ["%s=%s" % (k, v) for k, v in e.items() if k != "ev"]) for e in relay]
    # The expected log.
    expected = os.path.join(src, "expected.txt")
    cursors = {}
    for raw in (open(expected, encoding="utf-8").read().splitlines() if os.path.exists(expected) else []):
        line = raw.split(" #", 1)[0].strip()
        if not line or line.startswith("#"):
            continue
        negate = line.startswith("!")
        if negate:
            line = line[1:]
        source, _, pattern = line.partition(":")
        source, pattern = source.strip(), pattern.strip()
        if source == "shared":
            m = re.match(r"(\S+)\s+(\S+)\s*>=\s*(\d+)", pattern)
            who = tuple(sorted([m.group(1), m.group(2)]))
            best = 0
            for (inst, visit, w), ticks in spans.items():
                if set(who) <= set(w):
                    best = max(best, max(ticks) - min(ticks))
            if best < int(m.group(3)):
                problems.append("expected %s and %s to share an instance with equal fingerprints for %s ticks, got %d" % (who[0], who[1], m.group(3), best))
            else:
                notes.append("%s+%s shared %d ticks with equal fingerprints" % (who[0], who[1], best))
            continue
        lines = relay_lines if source == "relay" else logs.get(source)
        if lines is None:
            problems.append("expected.txt: unknown source %s" % source)
            continue
        rx = re.compile(pattern)
        if negate:
            hit = next((l for l in lines if rx.search(l)), None)
            if hit:
                problems.append("%s logged what it must not (%s): %s" % (source, pattern, hit[:200]))
            continue
        at = cursors.get(source, 0)
        while at < len(lines) and not rx.search(lines[at]):
            at += 1
        if at >= len(lines):
            problems.append("%s: expected /%s/ (in order) not found" % (source, pattern))
        else:
            cursors[source] = at + 1
    return problems, notes


def run_game(stack, api, relay_port, pin, diag):
    """G1: two hidden game clients (DW4_ONLINE_DEV) in one room and one stage, sealed, equal net fingerprints."""
    a = stack.args
    if not a.game_exe or not a.data:
        raise RuntimeError("G1 needs --game-exe (a DW4_ONLINE_DEV build) and --data (the game data)")
    out = os.path.join(stack.dir, "G1")
    os.makedirs(out, exist_ok=True)
    tag = "g" + secrets.token_hex(3)
    names = ["kim", "lee"]
    passwords = {}
    for n in names:
        passwords[n] = "online test " + n + " " + secrets.token_hex(4)
        s, body = api.call("POST", "/api/v1/auth/register", {"username": tag + n, "password": passwords[n]})
        if s != 201:
            raise RuntimeError("register %s: %s %s" % (n, s, body))
    with open(os.path.join(out, "campaign.txt"), "w") as f:
        f.write("0 SEEK 200000 campaign\n")
    https_port = api.base.rsplit(":", 1)[1]
    room_name = "G1 " + tag

    def start(name, room_args):
        card = os.path.join(out, "card_" + name)
        for sub in ("Card A", "Card B"):
            os.makedirs(os.path.join(card, sub), exist_ok=True)
        env = dict(os.environ, DW4_NO_ERROR_BOX="1", DW4_ONLINE_SERVER="127.0.0.1:" + https_port, DW4_ONLINE_PIN=pin,
                   DW4_ONLINE_PASSWORD=passwords[name], DW4_ONLINE_TRACE="1")
        argv = [a.game_exe, "--data", a.data, "--smoke-test", str(a.frames), "--card-dir", card, "--input", os.path.join(out, "campaign.txt"),
                "--online-login", tag + name] + room_args
        return stack.spawn(argv, os.path.join(out, name + ".log"), env=env)

    t0 = time.time()
    first = start(names[0], ["--online-create", room_name])
    code = None
    while time.time() - t0 < 90 and code is None:
        if first.poll() is not None:
            raise RuntimeError("the first client ended before its room existed (see %s)" % os.path.join(out, names[0] + ".log"))
        s, rooms = api.call("GET", "/api/v1/rooms")
        for r in (rooms if isinstance(rooms, list) else (rooms or {}).get("rooms", [])):
            if r.get("name") == room_name:
                code = r.get("code")
        time.sleep(0.5)
    if not code:
        raise RuntimeError("the first client's room did not appear")
    created = time.time()
    while time.time() - created < a.offset:
        time.sleep(0.1)
    second = start(names[1], ["--online-join", code])
    limit = a.frames / 59.94 + 240
    for p in (first, second):
        try:
            p.wait(max(1, limit - (time.time() - t0)))
        except subprocess.TimeoutExpired:
            pass
    return evaluate_game(out, names, code, diag, a.sealed_ticks)


def evaluate_game(out, names, code, diag, sealed_ticks):
    problems, notes = [], []
    fps, decisions, members, closes, finals = {}, {}, {}, {}, {}
    for n in names:
        path = os.path.join(out, n + ".log")
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines() if os.path.exists(path) else []
        fps[n], decisions[n], members[n] = {}, [], []
        for line in lines:
            if line.startswith("online-trace: netfp "):
                kv = dict(x.split("=", 1) for x in line[len("online-trace: netfp "):].split())
                fps[n][(int(kv["inst"]), int(kv["visit"]), int(kv["tick"]))] = (kv["fp"], int(kv["present"], 16), kv["mode"])
            elif line.startswith("online-trace: decision "):
                decisions[n].append(line[len("online-trace: "):])
            elif line.startswith("online-trace: join applied") or line.startswith("online-trace: member") or line.startswith("online-trace: ready"):
                members[n].append(line[len("online-trace: "):])
            elif line.startswith("online-trace: close"):
                closes[n] = line
            elif line.startswith("online: final mode"):
                finals[n] = line
        notes.append("%s: %d netfp lines, %d decisions, last: %s" % (n, len(fps[n]), len(decisions[n]), finals.get(n, "(no final line)")))
        for m in members[n][:6]:
            notes.append("   %s %s" % (n, m))
    common = set(fps[names[0]]) & set(fps[names[1]])
    mismatched = [k for k in sorted(common) if fps[names[0]][k][0] != fps[names[1]][k][0]]
    shared = [k for k in sorted(common) if fps[names[0]][k][1] & fps[names[0]][k][1] - 1]  # two or more present
    by_inst = {}
    for k in shared:
        by_inst.setdefault((k[0], k[1]), []).append(k[2])
    notes.append("%d netfp ticks both reported, %d with two members present, %d differ" % (len(common), len(shared), len(mismatched)))
    for k in mismatched[:3]:
        problems.append("net fingerprints differ at instance %d visit %d tick %d: %s / %s" % (k[0], k[1], k[2], fps[names[0]][k][0], fps[names[1]][k][0]))
    best = max(by_inst.items(), key=lambda kv: max(kv[1]) - min(kv[1]), default=None)
    if not best:
        problems.append("the two clients never shared an instance (no netfp tick with both present)")
    else:
        span = max(best[1]) - min(best[1])
        notes.append("shared instance %d visit %d: equal net fingerprints from tick %d to %d (%d ticks, %d comparisons)" %
                     (best[0][0], best[0][1], min(best[1]), max(best[1]), span, len(best[1])))
        if span < sealed_ticks:
            problems.append("only %d sealed ticks compared (< %d)" % (span, sealed_ticks))
    relay_log = load_jsonl(os.path.join(diag, "room_%s.jsonl" % code))
    kinds = {}
    for e in relay_log:
        kinds[e["ev"]] = kinds.get(e["ev"], 0) + 1
    notes.append("relay: " + ", ".join("%s %d" % kv for kv in sorted(kinds.items())))
    for bad in ("desync", "snaphash_mismatch", "split_mismatch", "barrier_mismatch", "barrier_split", "resync", "ready_mismatch"):
        if kinds.get(bad):
            problems.append("relay logged %d x %s" % (kinds[bad], bad))
    for n, line in closes.items():
        problems.append("%s was closed: %s" % (n, line))
    return problems, notes


def run_dev(stack, api, pin):
    """dev: the local stack and two game clients in one room (scripts/online-dev.ps1). Visible windows for a person at the
    PC; --hidden runs both hidden for --frames frames with the campaign bot from the title (the check agents run)."""
    a = stack.args
    if not a.game_exe or not a.data:
        raise RuntimeError("dev needs --game-exe (a DW4_ONLINE_DEV build) and --data (the game data)")
    out = os.path.join(stack.dir, "dev")
    os.makedirs(out, exist_ok=True)
    tag = "dev" + secrets.token_hex(2)
    names = ["kim", "lee"]
    passwords = {}
    for n in names:
        passwords[n] = "online dev " + n + " " + secrets.token_hex(6)
        s, body = api.call("POST", "/api/v1/auth/register", {"username": tag + n, "password": passwords[n]})
        if s != 201:
            raise RuntimeError("register %s: %s %s" % (n, s, body))
    https_port = api.base.rsplit(":", 1)[1]
    room_name = "Dev room " + tag
    campaign = os.path.join(out, "campaign.txt")
    with open(campaign, "w") as f:
        f.write("0 SEEK 200000 campaign\n")

    def start(n, room_args):
        card = os.path.join(out, "card_" + n)
        for sub in ("Card A", "Card B"):
            os.makedirs(os.path.join(card, sub), exist_ok=True)
        env = dict(os.environ, DW4_ONLINE_SERVER="127.0.0.1:" + https_port, DW4_ONLINE_PIN=pin, DW4_ONLINE_PASSWORD=passwords[n],
                   DW4_ONLINE_TRACE="1")
        argv = [a.game_exe, "--data", a.data, "--card-dir", card, "--online-login", tag + n] + room_args
        if a.dev_card:
            argv += ["--online-card", a.dev_card]
        if a.hidden:
            env["DW4_NO_ERROR_BOX"] = "1"
            argv += ["--smoke-test", str(a.frames), "--input", campaign]
            if a.dev_card:
                env["DW4_CAMPAIGN_LOAD"] = "1"
            return stack.spawn(argv, os.path.join(out, n + ".log"), env=env)
        # Visible windows for the person at the PC: normal priority, their own windows.
        return stack.spawn(argv, os.path.join(out, n + ".log"), env=env, flags=0)

    first = start(names[0], ["--online-create", room_name])
    code, t0 = None, time.time()
    while time.time() - t0 < 120 and code is None:
        if first.poll() is not None:
            raise RuntimeError("the first client ended before its room existed (see %s)" % os.path.join(out, names[0] + ".log"))
        s, rooms = api.call("GET", "/api/v1/rooms")
        for r in (rooms if isinstance(rooms, list) else (rooms or {}).get("rooms", [])):
            if r.get("name") == room_name:
                code = r.get("code")
        time.sleep(0.5)
    if not code:
        raise RuntimeError("the first client's room did not appear")
    print("room %s (code %s): %s created it, %s joins; logs in %s" % (room_name, code, tag + names[0], tag + names[1], out), flush=True)
    second = start(names[1], ["--online-join", code])
    if a.hidden:
        limit = a.frames / 59.94 + 240
        for p in (first, second):
            try:
                p.wait(max(1, limit - (time.time() - t0)))
            except subprocess.TimeoutExpired:
                pass
    else:
        print("close both game windows to stop the local server", flush=True)
        for p in (first, second):
            p.wait()
    problems, notes = [], []
    for n, p in zip(names, (first, second)):
        lines = open(os.path.join(out, n + ".log"), encoding="utf-8", errors="replace").read().splitlines()
        final = next((l for l in reversed(lines) if l.startswith("online: final")), None)
        decisions = sum(1 for l in lines if l.startswith("online-trace: decision"))
        notes.append("%s: exit %s, %d barrier decisions, %s" % (n, p.poll(), decisions, final or "(no final line)"))
        if not final:
            problems.append("%s: no final online line" % n)
        problems += ["%s: %s" % (n, l) for l in lines if l.startswith("online-trace: close") or l.startswith("error:")]
    return problems, notes


UI_DIR = os.path.join(HERE, "ui")


def ui_scenarios():
    names = sorted(d for d in os.listdir(UI_DIR) if os.path.isfile(os.path.join(UI_DIR, d, "scenario.json"))) if os.path.isdir(UI_DIR) else []
    return [dict(json.load(open(os.path.join(UI_DIR, n, "scenario.json"), encoding="utf-8")), name=n) for n in names]


def run_ui(stack, sc, connection, servers):
    """One UI scenario (ui/<id>): hidden game clients that drive the account and room screens with --online-ui scripts
    (src/game/online_ui_script.hpp) against the local server, each with its own card folder and login folder, screenshots
    at 1920x1009 widescreen with MSAA 4 (the round 50 / 54 look). The scripts read their account names, passwords and the
    room name from UI_<CLIENT>_NAME, UI_<CLIENT>_PASSWORD, UI_ROOM and UI_ROOM_PASSWORD (made up per run, never in the
    repository). Checked: every client ends with exit code 0 and its script complete; no client printed an error."""
    a = stack.args
    if not a.game_exe or not a.data:
        raise RuntimeError("ui scenarios need --game-exe (a DW4_ONLINE_DEV build) and --data (the game data)")
    key = json.dumps(sc.get("server", {}), sort_keys=True)
    if key not in servers:
        servers[key] = start_server(stack, connection, sc.get("server"), "server%d" % len(servers))
    base, relay, pin, der, diag = servers[key]
    api = Api(base, der)
    src = os.path.join(UI_DIR, sc["name"])
    out = os.path.join(stack.dir, sc["name"])
    os.makedirs(out, exist_ok=True)
    tag = "ui" + secrets.token_hex(3)
    env = dict(os.environ, DW4_NO_ERROR_BOX="1", DW4_ONLINE_SERVER="127.0.0.1:" + base.rsplit(":", 1)[1], DW4_ONLINE_PIN=pin,
               DW4_WINDOW=a.ui_window, DW4_WIDESCREEN="1", DW4_MSAA="4", DW4_ONLINE_TRACE="1",
               UI_ROOM="Room " + tag, UI_ROOM_PASSWORD="pw" + secrets.token_hex(4))
    for c in sc["clients"]:
        up = c["name"].upper()
        env["UI_%s_NAME" % up] = tag + c["name"]
        env["UI_%s_PASSWORD" % up] = "ui test " + c["name"] + " " + secrets.token_hex(6)
        if c.get("registered"):
            s, body = api.call("POST", "/api/v1/auth/register", {"username": env["UI_%s_NAME" % up], "password": env["UI_%s_PASSWORD" % up]})
            if s != 201:
                raise RuntimeError("register %s: %s %s" % (c["name"], s, body))
    campaign = os.path.join(out, "campaign.txt")
    with open(campaign, "w") as f:
        f.write("0 SEEK 400000 campaign\n")
    t0 = time.time()
    procs = []
    for c in sorted(sc["clients"], key=lambda c: c.get("start", 0)):
        while time.time() - t0 < c.get("start", 0):
            time.sleep(0.1)
        d = os.path.join(out, c["name"])
        for sub in ("cards/Card A", "cards/Card B", "online"):
            os.makedirs(os.path.join(d, sub), exist_ok=True)
        argv = [a.game_exe, "--data", a.data, "--smoke-test", str(c.get("frames", 400000)), "--card-dir", os.path.join(d, "cards"),
                "--online-folder", os.path.join(d, "online"), "--online-ui", os.path.join(src, c["script"]),
                "--screenshot", os.path.join(d, "shot.bmp")] + c.get("args", [])
        cenv = dict(env)
        cenv.update(c.get("env", {}))
        if c.get("card"):
            folder = c["card"] if os.path.isabs(c["card"]) else os.path.join(a.cards, c["card"])
            if not os.path.isdir(folder):
                raise RuntimeError("no card folder %s (--cards)" % folder)
            argv += ["--online-card", folder]
            cenv["DW4_CAMPAIGN_LOAD"] = "1"
        if c.get("campaign"):
            argv += ["--input", campaign]
            if sc.get("checkpoints"):
                # The campaign bot saves like a player at these checkpoints (the save Digi-Elf, the save room stagIS12).
                cenv["DW4_CAMPAIGN_CHECKPOINTS"] = os.path.join(d, "checkpoints")
                cenv["DW4_CAMPAIGN_CHECKPOINT_LIST"] = sc["checkpoints"]
        procs.append((c["name"], stack.spawn(argv, os.path.join(d, "log.txt"), env=cenv)))
    limit = sc.get("timeout", 300)
    for name, p in procs:
        try:
            p.wait(max(1, limit - (time.time() - t0)))
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
    problems, notes = [], []
    for name, p in procs:
        d = os.path.join(out, name)
        lines = open(os.path.join(d, "log.txt"), encoding="utf-8", errors="replace").read().splitlines()
        steps = [l for l in lines if l.startswith("online-ui: [")]
        shots = []
        for f in sorted(os.listdir(d)):
            if f.endswith(".bmp"):
                try:
                    from PIL import Image
                    Image.open(os.path.join(d, f)).save(os.path.join(d, f[:-4] + ".png"))
                    os.remove(os.path.join(d, f))
                    shots.append(f[:-4])
                except Exception as e:  # keep the BMP
                    shots.append(f[:-4] + " (bmp: %s)" % e)
        notes.append("%s: exit %s, %d script steps, shots: %s" % (name, p.poll(), len(steps), ", ".join(shots) or "none"))
        if p.poll() != 0:
            problems.append("%s: exit code %s" % (name, p.poll()))
        problems += ["%s: %s" % (name, l) for l in lines if l.startswith("error:") or "online-ui line" in l]
        expected = sum(1 for l in open(os.path.join(src, sc["clients"][[c["name"] for c in sc["clients"]].index(name)]["script"]),
                                       encoding="utf-8") if l.split("#")[0].strip())
        if len(steps) < expected:
            problems.append("%s: %d of %d script lines ran (log %s)" % (name, len(steps), expected, os.path.join(d, "log.txt")))
    if sc.get("characters"):
        check_characters(sc, api, env, out, problems, notes)
    return problems, notes


UI_RECORD_SIZE, UI_STORY = 3316, (792, 1892)  # the character record; its story bytes (BASE_FLAG .. BASE_COUNTER, SEAMLESS.md 8.1)


def gci_record(path, box=0):
    """Box `box` of the first data copy of a DW4 save file whose version and checksum hold (save_data.cpp)."""
    data = open(path, "rb").read()
    for copy in CARD_COPIES:
        base = GCI_ENTRY + copy
        block = data[base:base + CARD_COPY_SIZE]
        words = struct.unpack(">%dI" % ((CARD_COPY_SIZE - 4) // 4), block[4:])
        if struct.unpack(">I", block[4:8])[0] == 4 and struct.unpack(">I", block[0:4])[0] == sum(words) & 0xFFFFFFFF:
            at = 8 + box * 0x2000
            return block[at:at + UI_RECORD_SIZE]
    raise RuntimeError("no valid data copy in " + path)


def check_characters(sc, api, env, out, problems, notes):
    """scenario.json "characters" (SEAMLESS.md 9 and the S2 done-when): per client, its online character as the client
    logged it ("online-character: ..." lines) and as the server holds it. Every client entered without a character (a new
    game), its first stage change created revision 1, the save room uploaded (card_written), the run's last save is the
    automatic save when it left the room (an upload with "leave_upload": true; otherwise also "unchanged since" the last
    uploaded revision), nothing was refused; GET /online-character (logged in through the API) returns the last uploaded
    revision, and its record equals the client's card cache (online_card.dat, nothing pending) outside the story
    bytes, which the server composes from the account's own online story (8.4). "own_story": "none" asks for a record
    whose story bytes are all zero (a guest's own story is untouched by playing in someone else's room); "phase": N for
    the story phase flag N set in the record (the room owner's online story holds the room's progress)."""
    for name, want in sorted(sc["characters"].items()):
        up = name.upper()
        account, password = env["UI_%s_NAME" % up], env["UI_%s_PASSWORD" % up]
        log = os.path.join(out, name, "log.txt")
        lines = open(log, encoding="utf-8", errors="replace").read().splitlines() if os.path.exists(log) else []
        ch = [l[len("online-character: "):] for l in lines if l.startswith("online-character: ")]
        if not any(l.startswith("enter without a character") for l in ch):
            problems.append("%s: the room entry did not start without a character (a new game)" % name)
        saves = [(m.group(1), int(m.group(2))) for m in (re.match(r"saved \((.+)\) revision (\d+)$", l) for l in ch) if m]
        notes.append("%s uploads: %s" % (name, ", ".join("%s r%d" % s for s in saves) or "none"))
        reasons = [r for r, _ in saves]
        if not saves or saves[0] != ("stage change", 1):
            problems.append("%s: the first stage change did not create the online character as revision 1" % name)
        if "save room" not in reasons:
            problems.append("%s: no upload from the save room" % name)
        # The automatic save when leaving: the last save line of the run. It uploads what changed since the last upload
        # ("leave_upload": true asks for that) and finds nothing to do when the server holds the character as it is.
        leave = [l for l in ch if l.startswith(("save (", "saved (", "upload ("))][-1:]
        left_unchanged = re.match(r"save \(leaving the room\) unchanged since revision (\d+)$", leave[0]) if leave else None
        if want.get("leave_upload") and (not reasons or reasons[-1] != "leaving the room" or not leave or not leave[0].startswith("saved")):
            problems.append("%s: the automatic save when leaving the room did not upload last (%s)" % (name, leave))
        elif not want.get("leave_upload") and not (left_unchanged or (leave and leave[0].startswith("saved (leaving the room)"))):
            problems.append("%s: the run did not end with the automatic save when leaving the room (%s)" % (name, leave))
        if left_unchanged and saves and int(left_unchanged.group(1)) != saves[-1][1]:
            problems.append("%s: the leaving save compared with revision %s, the last upload was %d" % (name, left_unchanged.group(1), saves[-1][1]))
        if leave:
            notes.append("%s left the room: %s" % (name, leave[0]))
        for l in ch:
            if l.startswith(("conflict", "refused")) or "not uploaded" in l:
                problems.append("%s: %s" % (name, l))
        s, body = api.call("POST", "/api/v1/auth/login", {"username": account, "password": password, "deviceName": "online_test", "protocol": 45})
        if s != 200 or not body or not body.get("token"):
            problems.append("%s: no login for the character check (%s)" % (name, s))
            continue
        s, body = api.call("GET", "/api/v1/online-character", token=body["token"])
        if s != 200 or not body:
            problems.append("%s: GET /online-character answered %s" % (name, s))
            continue
        record = base64.b64decode(body["record"])
        summary = body.get("summary") or {}
        notes.append("%s on the server: revision %s, %s level %s, play %s s" % (name, body["revision"], summary.get("digimon"), summary.get("level"),
                                                                              body.get("playSeconds")))
        if saves and body["revision"] != saves[-1][1]:
            problems.append("%s: the server holds revision %s, the client saved revision %s last" % (name, body["revision"], saves[-1][1]))
        folder = os.path.join(out, name, "online", re.sub(r"[^a-z0-9_-]", "_", account.lower()))
        cache, state_file = os.path.join(folder, "online_card.dat"), os.path.join(folder, "online_card.state")
        state = open(state_file).read() if os.path.exists(state_file) else ""
        if "pending=0" not in state:
            problems.append("%s: the card cache has a pending save: %s" % (name, state.replace("\n", " ")))
        box = int(re.search(r"box=(\d)", state).group(1)) if "box=" in state else 0
        if not os.path.exists(cache):
            problems.append("%s: no card cache %s" % (name, cache))
        else:
            mine = gci_record(cache, box)
            if record[:UI_STORY[0]] + record[UI_STORY[1]:] != mine[:UI_STORY[0]] + mine[UI_STORY[1]:]:
                problems.append("%s: the server's record differs from the card cache's outside the story bytes" % name)
        story = record[UI_STORY[0]:UI_STORY[1]]
        if want.get("own_story") == "none" and any(story):
            problems.append("%s: the record's story bytes are not empty (a guest's own story must stay untouched)" % name)
        if "phase" in want and record[UI_STORY[0] + int(want["phase"])] != 1:
            problems.append("%s: the record's story lacks the phase flag %s" % (name, want["phase"]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenarios", nargs="*")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--mysql-port", type=int, default=3431)
    ap.add_argument("--ports", default="53100-53199")
    ap.add_argument("--impair", default="", help="one-way ms,jitter ms,loss (each client behind the proxy)")
    ap.add_argument("--keep", action="store_true", help="keep the run folder")
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--seconds", default="40")
    ap.add_argument("--warmup", default="20")
    ap.add_argument("--game-exe", default="", help="G1: a DW4_ONLINE_DEV build of dw4_game.exe")
    ap.add_argument("--data", default="", help="G1: the game data (disc image or extracted folder, read only)")
    ap.add_argument("--offset", type=float, default=8.0, help="G1: seconds between the room's creation and the second client")
    ap.add_argument("--frames", type=int, default=22000, help="G1: frames each client runs (--smoke-test)")
    ap.add_argument("--sealed-ticks", type=int, default=3000, help="G1: sealed ticks with equal net fingerprints needed")
    ap.add_argument("--fake", action="store_true", help="the fake-client scenarios")
    ap.add_argument("--cards", default=os.environ.get("DW4_ONLINE_TEST_CARDS", ""),
                    help="game scenarios: the folder of the saved characters a scenario names (a campaign checkpoint set)")
    ap.add_argument("--parallel", type=int, default=1, help="game scenarios run side by side (one room each)")
    ap.add_argument("--hidden", action="store_true", help="dev: hidden clients with the campaign bot for --frames frames")
    ap.add_argument("--dev-card", default="", help="dev: a card folder whose saves both online cards start with")
    ap.add_argument("--ui-window", default="1920x1009", help="ui: the hidden window's size")
    a = ap.parse_args()
    if a.list:
        for sc in (SCENARIOS if a.fake else game_scenarios()):
            print("%-5s %s" % (sc["name"], sc["desc"]))
        return 0
    if not a.no_build:
        build()
    if a.scenarios == ["matrix"]:
        lo, hi = a.ports.split("-")
        out = os.path.join(WORK, "loss_matrix")
        os.makedirs(out, exist_ok=True)
        return subprocess.run(["dotnet", dll("tests/DW4Online.FakeClient", "DW4Online.FakeClient.dll"), "matrix", "--ports", a.ports,
                               "--warmup", a.warmup, "--seconds", a.seconds, "--out", os.path.join(out, "matrix.json"), "--md",
                               os.path.join(out, "matrix.md")], creationflags=BELOW_NORMAL | NO_WINDOW).returncode
    if a.scenarios[:1] == ["ui"]:
        wanted_ui = [sc for sc in ui_scenarios() if len(a.scenarios) == 1 or sc["name"] in a.scenarios[1:]]
        unknown = [n for n in a.scenarios[1:] if n not in {sc["name"] for sc in ui_scenarios()}]
        if unknown:
            print("unknown ui scenario(s): " + " ".join(unknown))
            return 2
        stack = Stack(a)
        failed = 0
        try:
            connection = start_mysql(stack)
            servers = {}
            for sc in wanted_ui:
                t = time.time()
                try:
                    problems, notes = run_ui(stack, sc, connection, servers)
                except Exception as e:
                    problems, notes = ["%s: %s" % (type(e).__name__, e)], []
                print("%s ui %-10s %s (%.0f s)" % ("PASS" if not problems else "FAIL", sc["name"], sc.get("desc", ""), time.time() - t), flush=True)
                for n in notes:
                    print("       " + n, flush=True)
                for p in problems:
                    print("     ! " + p, flush=True)
                failed += bool(problems)
        finally:
            stack.stop()
        print("run folder (screenshots): " + stack.dir)
        print("%d of %d ui scenarios passed" % (len(wanted_ui) - failed, len(wanted_ui)))
        return 1 if failed else 0
    game = "G1" in a.scenarios
    dev = a.scenarios == ["dev"]
    if dev:
        stack = Stack(a)
        try:
            connection = start_mysql(stack)
            base, relay, pin, der, diag = start_server(stack, connection)
            print("local server %s, relay 127.0.0.1:%d" % (base, relay), flush=True)
            problems, notes = run_dev(stack, Api(base, der), pin)
        finally:
            stack.stop()
        print(("PASS" if not problems else "FAIL") + " dev: two clients in one room", flush=True)
        for n in notes:
            print("       " + n, flush=True)
        for p in problems:
            print("     ! " + p, flush=True)
        print("run folder: " + stack.dir)
        return 1 if problems else 0
    if a.fake:
        wanted, real = [s for s in SCENARIOS if not a.scenarios or s["name"] in a.scenarios], []
    else:
        wanted = []
        real = [s for s in game_scenarios() if (not a.scenarios and not game) or s["name"] in a.scenarios]
        unknown = [n for n in a.scenarios if n != "G1" and n not in {s["name"] for s in real}]
        if unknown:
            print("unknown scenario(s): " + " ".join(unknown))
            return 2
    stack = Stack(a)
    failed = 0
    try:
        connection = start_mysql(stack)
        base, relay, pin, der, diag = start_server(stack, connection)
        api = Api(base, der)
        s, status = api.call("GET", "/api/v1/status")
        print("server %s, relay %s:%d (%s), pin %s" % (base, status["relay"]["host"], status["relay"]["port"], status["relay"]["protocol"], pin), flush=True)
        if real:
            lock = threading.Lock()
            results = {}

            def one(sc):
                t = time.time()
                try:
                    if sc.get("server"):
                        # Its own server (settings of scenario.json's "server") with its own database: a server closes the
                        # rooms its database lists as open when it starts.
                        with lock:
                            own_db = create_database(stack)
                            own = start_server(stack, own_db, sc["server"], sc["name"])
                        problems, notes = run_real(stack, sc, Api(own[0], own[3]), own[1], own[2], own[4], lock, own_db)
                    else:
                        problems, notes = run_real(stack, sc, api, relay, pin, diag, lock, connection)
                except Exception as e:  # a broken scenario is a failure, the others go on
                    problems, notes = ["%s: %s" % (type(e).__name__, e)], []
                with lock:
                    results[sc["name"]] = (problems, notes)
                    print("%s %-5s %s (%.0f s)" % ("PASS" if not problems else "FAIL", sc["name"], sc["desc"], time.time() - t), flush=True)
                    for n in notes:
                        print("       " + n, flush=True)
                    for p in problems:
                        print("     ! " + p, flush=True)

            pending = list(real)
            running = []
            while pending or running:
                running = [th for th in running if th.is_alive()]
                while pending and len(running) < max(1, a.parallel):
                    th = threading.Thread(target=one, args=(pending.pop(0),))
                    th.start()
                    running.append(th)
                time.sleep(0.5)
            failed += sum(1 for p, _ in results.values() if p)
            wanted = wanted + real
        for sc in wanted[:len(wanted) - len(real)]:
            t = time.time()
            problems, notes = run_scenario(stack, sc, api, relay, pin, diag)
            print("%s %-5s %s (%.0f s)" % ("PASS" if not problems else "FAIL", sc["name"], sc["desc"], time.time() - t), flush=True)
            for n in notes:
                print("       " + n, flush=True)
            for p in problems:
                print("     ! " + p, flush=True)
            failed += bool(problems)
        if game:
            t = time.time()
            problems, notes = run_game(stack, api, relay, pin, diag)
            print("%s G1    two hidden game clients in one stage, sealed, equal net fingerprints (%.0f s)" % ("PASS" if not problems else "FAIL",
                                                                                                          time.time() - t), flush=True)
            for n in notes:
                print("       " + n, flush=True)
            for p in problems:
                print("     ! " + p, flush=True)
            failed += bool(problems)
            wanted = wanted + [{"name": "G1"}]
    finally:
        stack.stop()
        if not a.keep and not failed:
            shutil.rmtree(stack.dir, ignore_errors=True)
        else:
            print("run folder: " + stack.dir)
    print("%d of %d scenarios passed" % (len(wanted) - failed, len(wanted)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
