"""The local online test stack (SEAMLESS.md 16, the world of round I1: 19.24).

Starts a private MariaDB (C:/xampp/mysql/bin/mysqld.exe with its own data directory under tools/online_test/.work and
its own port), the DW4Online server on 127.0.0.1 with a throw-away HTTPS certificate (openssl, pinned by the clients)
and the DW4R2 relay on a free port, registers accounts through the API, starts N hidden clients with input scripts that
enter the world with --online-play (the world has no rooms: every scenario gets a FRESH server process with its own
database, so no scenario sees another's instances), optionally each behind the latency / jitter / loss proxy, collects
every client's net fingerprints and membership log and the relay's world membership log (world_<run>.jsonl, a wid on
every line), compares them per instance and checks each scenario's expected events. It stops only the processes it
started (by their handles), never anything by name.

World keys of scenario.json (round I1a): "autochoice" (first | new: DW4_ONLINE_AUTOCHOICE of every client, the development
build's answer to the arrival prompt; default first; a client's own "autochoice" wins), a client's "box" (--online-play
BOX), a client's "cut" ({"at": S, "seconds": N, "mode": "close" | "half"}: its own proxy cuts its link at the scenario's
start + S for N seconds; close drops the connection and refuses reconnects, half holds every byte back without a FIN),
"server_restart" ({"at": S, "kill": false, "down": N}: the scenario's server stops at start + S, gracefully (Ctrl+Break:
the service's ordered stop) or killed, and starts again on the same ports N seconds later), "db_checks" ([{"sql": ...,
"expect": REGEX, "after": true}]: a query of the scenario's database after the run whose tab-separated rows must match),
"world_log" ([REGEX ...]: lines that must appear in the relay's world log in order, as "relay:" lines of expected.txt),
"own_story" (as in round G-core: the clients whose online card and server story must stay their own), a client's
"prefs" (PUT /me/online-prefs before it starts: visibility, host settings), a client's "role" (moderator / admin before it
starts), "api_steps" ([{"at": S, "as": CLIENT | "admin", "method", "path", "body"}]: timed API calls, e.g. a story reset or
a staff restore; "admin" is an admin account the harness makes; {id:NAME}, {user:NAME} and {pw:NAME} name a client's
account and password),
"api_checks" ([{"at": S | "after", "as": ..., "method", "path", "status", "expect": REGEX}]), a client's "crash"
({"at": S, "restart": N, "script": FILE}: its process is killed at start + S and started again N seconds later with that
script), "allow_close" (the world's CLOSEs of a server restart or an ended instance are expected), "counter_proof" (the
scenario must FAIL: a client hook switches the checked behaviour off; its expected problems are listed as notes) and
"requires" ([CAPABILITY ...]: the scenario runs only with --capabilities naming them, e.g. fake_world_clients for the
fake presence senders; else it is reported as SKIP with the reason).

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
N" asks for N ticks of equal fingerprints with both present in one instance). A scenario with "allow_resync" (S11a, S11b:
a client's DW4_ONLINE_TEST_DESYNC injects a desync) may differ up to its instance's last resync READY and must then be
equal for "min_after_resync" ticks (the relay's desync, resync, statehash_mismatch and checkpoint_suspect are allowed
there); its expected log checks the votes and resyncs. A scenario with "expect_relay" ({"ready_mismatch": 1,
"checkpoint_suspect": 1}: JS, a READY from a checkpoint the members' fingerprints contradict) needs exactly that count of
that relay event instead of none. A scenario's "achievements" asks the
server for each client account's online achievements afterwards (check_achievements). Network traffic stays on loopback. A
scenario.json with "server": {...} gets its own server with those settings merged over the stack's and its own database
(S14h / S14a: {"Rooms": {"StoryWhileOwnerAway": "hold"}}); "min_shared_ticks" lowers the 10,000 shared ticks a scenario
needs (S14: one member plays); "seed_story": true starts the room story from the room creator's card (its online_story
row, SEAMLESS.md 8.4), so the members' card stories are not new to the room (the title gives every member the room
story at game start); "shots": true (scenario or client) lets the scripts' "shot NAME" steps write NAME.bmp into
the run folder (the client then draws its pictures in its hidden window).

Without a scenario every game scenario runs. G1 is the integration's first game run (a new game from the title, both
meet in the first field).
"""
import argparse
import base64
import re
import threading
import glob
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
NEW_GROUP = 0x00000200  # CREATE_NEW_PROCESS_GROUP: the server's own group (the harness never shares its console signals)
PROTOCOL, DATA_HASH = 43, 1
WORLD_PROTOCOL = 49  # the world's protocol (SEAMLESS.md 19.18): the fake world clients and the --fake scenarios speak it

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
        self.servers = {}
        self.dir = os.path.join(WORK, time.strftime("run_%Y%m%d_%H%M%S"))
        os.makedirs(self.dir, exist_ok=True)

    def spawn(self, argv, log, env=None, flags=BELOW_NORMAL | NO_WINDOW):
        f = open(log, "w", encoding="utf-8")
        p = subprocess.Popen(argv, stdout=f, stderr=subprocess.STDOUT, env=env, creationflags=flags)
        self.procs.append((p, f))
        return p

    def stop(self):
        # The clients and the server first, then the mysqld: SQL SHUTDOWN, and its process waited for until it is gone. A
        # terminate() right after SHUTDOWN cut its shutdown short (mysqld.err ended at "FTS optimize thread exiting", no
        # "Shutdown complete", a stale .pid left): the way mysql.global_priv was damaged before (the r71 review).
        mysqld = getattr(self, "mysqld", None)
        for p, f in reversed(self.procs):
            if p is not mysqld and p.poll() is None:
                p.terminate()
                try:
                    p.wait(10)
                except subprocess.TimeoutExpired:
                    p.kill()
        if getattr(self, "mysql", None):
            port, pw = self.mysql
            subprocess.run([os.path.join(MYSQL_BIN, "mysql.exe"), "--protocol=tcp", "-h", "127.0.0.1", "-P", str(port), "-u", "root",
                            "--password=" + pw, "-e", "SHUTDOWN"], capture_output=True, creationflags=NO_WINDOW)
            self.mysql = None
            if mysqld is not None:
                try:
                    mysqld.wait(120)
                except subprocess.TimeoutExpired:
                    print("mysqld did not shut down within 120 s after SHUTDOWN; terminated", file=sys.stderr)
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
    stack.mysqld = stack.spawn([os.path.join(MYSQL_BIN, "mysqld.exe"), "--defaults-file=" + os.path.join(data, "my.ini"),
                                "--bind-address=127.0.0.1", "--port=%d" % port, "--character-set-server=utf8mb4",
                                "--collation-server=utf8mb4_unicode_ci", "--default-time-zone=+00:00", "--innodb-use-native-aio=0",
                                "--log-error=" + os.path.join(stack.dir, "mysqld.err")], os.path.join(stack.dir, "mysqld.out"))
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


# --server-root: the server (API and relay) of another DW4Online checkout, built there (the latency scenarios run the
# deployed server's code, DW4Online 0.1.0 = main 890e802, against this client); the fake clients and the proxy stay this
# checkout's.
SERVER_DLL = None
SERVER_HAS_STORY = False  # --server-root of a checkout whose server keeps the room story (round 56 and later)


def server_dll():
    return SERVER_DLL or dll("src/DW4Online.Server", "DW4Online.Server.dll")


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
                                           "StageChangesPerMinute", "WorldEntersPerMinute", "SocialPerMinute")},
        # (Every client of the stack connects from 127.0.0.1: W19's 35 fake presence senders and the game clients.)
        "Relay": {"Enabled": True, "ListenAddress": "127.0.0.1", "Port": relay, "TicketKey": base64.b64encode(secrets.token_bytes(32)).decode(),
                  "DiagnosticsDirectory": diag, "MaxConnectionsPerIp": 128},
        "Web": {"DataProtectionKeysDirectory": "memory"},
        "Logging": {"File": {"Directory": os.path.join(folder, "server-log")}},
        # The ordered stop of restart_server (a process without a console gets no Ctrl+C): the service stops once this exists.
        "Testing": {"StopFile": os.path.join(folder, "stop.request")},
    }
    if getattr(stack.args, "play_protocol", 0):
        # The deployed server's configuration (Play:MinimumProtocol / CurrentProtocol, the field's 43).
        settings["Play"] = {"MinimumProtocol": stack.args.play_protocol, "CurrentProtocol": stack.args.play_protocol}
    settings = merged(settings, extra)
    path = os.path.join(folder, "server-settings.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(settings, f, indent=1)
    argv, env = ["dotnet", server_dll(), "--settings", path], dict(os.environ, ASPNETCORE_ENVIRONMENT="Production", DOTNET_gcServer="0")
    proc = stack.spawn(argv, os.path.join(folder, "server.out"), env=env, flags=BELOW_NORMAL | NO_WINDOW | NEW_GROUP)
    if not wait_port(https, 60) or not wait_port(relay, 30):
        raise RuntimeError("the server did not start (see %s)" % os.path.join(folder, "server.out"))
    base = "https://127.0.0.1:%d" % https
    stack.servers[base] = {"argv": argv, "env": env, "folder": folder, "proc": proc, "https": https, "relay": relay, "starts": 1}
    return base, relay, pin, der, diag


def restart_server(stack, base, kill=False, down=0.0):
    """Stops the server of `base` (gracefully: the stop file of Testing:StopFile, the host's ordered stop, which closes
    the world's connections with CLOSE server stopping; kill: TerminateProcess, no CLOSE, as a crash) and starts it again
    on the same ports with the same settings `down` seconds later (W24, W25: the clients reconnect to the new process)."""
    srv = stack.servers[base]
    proc = srv["proc"]
    if proc.poll() is None:
        if kill:
            proc.kill()
        else:
            with open(os.path.join(srv["folder"], "stop.request"), "w") as f:
                f.write("stop")
            try:
                proc.wait(60)
            except subprocess.TimeoutExpired:
                print("the server did not stop within 60 s of its stop file; killed", file=sys.stderr)
                proc.kill()
    proc.wait(30)
    time.sleep(max(0.0, down))
    srv["starts"] += 1
    srv["proc"] = stack.spawn(srv["argv"], os.path.join(srv["folder"], "server%d.out" % srv["starts"]), env=srv["env"],
                              flags=BELOW_NORMAL | NO_WINDOW | NEW_GROUP)
    if not wait_port(srv["https"], 60) or not wait_port(srv["relay"], 30):
        raise RuntimeError("the restarted server did not start (see %s)" % srv["folder"])


def run_scenario(stack, sc, api, relay_port, pin, diag):
    """A --fake scenario in the world: its own server (a fresh process and database, as every game scenario), one account
    per fake client, each enters the world with a ticket of POST /world/ticket (FakeClient's client mode, protocol 49) at
    the title (stage "") and plays its script's exits; the relay's matching makes the instances (a NEW, a co-arrival, the
    automatic join of I1a)."""
    tag = sc["name"].lower() + secrets.token_hex(2)
    names = [c["name"] for c in sc["clients"]]
    tokens = {}
    for n in names:
        s, body = api.call("POST", "/api/v1/auth/register", {"username": tag + n, "password": "online test " + n})
        if s != 201:
            raise RuntimeError("register %s: %s %s" % (n, s, body))
        tokens[n] = body["token"]
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
                "--token", tokens[c["name"]], "--pin", pin, "--protocol", str(WORLD_PROTOCOL), "--data-hash", str(DATA_HASH),
                "--name", c["name"], "--quest", str(c["quest"]), "--seed", str(k + 1), "--script", script,
                "--duration", str(sc["duration"] - c["start"]), "--replay-rate", str(c["rate"]), "--entry-ticks", str(sc["entry"]),
                "--depart-ticks", str(sc["depart"]), "--log", os.path.join(out, c["name"] + ".jsonl"), "--stats", os.path.join(out, c["name"] + ".stats.json")]
        procs.append((c["name"], stack.spawn(argv, os.path.join(out, c["name"] + ".out"))))
    for name, p in procs:
        p.wait(sc["duration"] + 60)
    return evaluate(sc, out, None, diag)


def load_world_log(diag):
    """The relay's world membership log of a server (one world_<run>.jsonl per relay process, in start order; a restarted
    server writes a second file): every line with its wid (0 at home)."""
    lines = []
    for path in sorted(glob.glob(os.path.join(diag, "world_*.jsonl")), key=os.path.getmtime):
        lines += load_jsonl(path)
    return lines


def world_notes(relay):
    """The world's membership log per instance: its events by kind (the notes of a scenario)."""
    per = {}
    for e in relay:
        per.setdefault(int(e.get("wid", 0) or 0), {}).setdefault(e.get("ev", "?"), 0)
        per[int(e.get("wid", 0) or 0)][e.get("ev", "?")] += 1
    return ["world log, instance %s: %s" % (wid if wid else "home", ", ".join("%s %d" % kv for kv in sorted(evs.items())))
            for wid, evs in sorted(per.items())]


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
    relay_log = load_world_log(diag)
    notes.append("relay: %d membership events (%s)" % (len(relay_log), ", ".join(sorted({e["ev"] for e in relay_log}))))
    notes.extend(world_notes(relay_log))
    for bad in ("desync", "snaphash_mismatch", "split_mismatch", "barrier_mismatch", "barrier_split"):
        if any(e.get("ev") == bad for e in relay_log):
            problems.append("relay logged %s" % bad)
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


STORY_SETS = os.path.join(os.path.dirname(os.path.dirname(HERE)), "data", "online", "story_sets.json")


def production_story(flags, folder_bytes, counters, difficulty=0, with_training=False):
    """The story a server's merges leave (round G-core, SEAMLESS.md 16 "Round G"): only the merge sets of a card's story -
    the phase one-hot, the persistent flags, K0-K10, COUNTER 1 and 7 - and the copies of `difficulty` derived from them,
    as the keeper does. Flag 503 only with `with_training`: stories saved before round G never hold it (it was never
    merged), and the rebase at the room's open has to derive it. (The first form copied all 1024 flag bytes, 503 and
    the temporaries included, which hid the tutorial bug of 8.8.)"""
    sets = json.load(open(STORY_SETS, encoding="utf-8"))
    phase = sets["phase"]
    out = bytearray(1024)
    index = max((i + 1 for i, f in enumerate(phase) if flags[f]), default=0)
    if index:
        out[phase[index - 1]] = 1
    for f in sets["persistent"]:
        if flags[f] and (f != sets["training"]["flag"] or with_training):
            out[f] = 1
    keys = bytearray(12)
    for k in range(sets["keys"]):
        keys[k] = 1 if folder_bytes[k] else 0
    for i, f in enumerate(phase):
        out[sets["phaseCopies"][difficulty][i]] = out[f]
    for g in sets["working"]:
        for j in range(g["count"]):
            out[g["copies"][difficulty] + j] = out[g["first"] + j]
    for k in range(sets["keys"]):
        out[sets["keyCopies"][difficulty] + k] = keys[k]
    kept = [counters[i] if i in sets["counters"] else 0 for i in range(16)]
    return bytes(out), bytes(keys), kept


def seed_story(stack, connection, account_id, folder, difficulty=0, with_training=False, full=False):
    """The account's online story (online_story, SEAMLESS.md 8.4) from the card's story: the room story of a room this
    account opens starts from it, as if the online character had played that far (test runs that start from saved
    cards; the title gives every member the room story at game start). Production-like since round G-core
    (production_story); `full` (scenario.json "seed_story": "full") copies the card's bytes as before."""
    flags, folder_bytes, counters = card_story(folder)
    if not full:
        flags, folder_bytes, counters = production_story(flags, folder_bytes, counters, difficulty, with_training)
    db = dict(kv.split("=", 1) for kv in connection.split(";") if "=" in kv)["Database"]
    port, pw = stack.mysql
    sql = ("REPLACE INTO online_story (account_id, revision, flags, folder, counters, updated_at) VALUES (%d, 1, X'%s', X'%s', X'%s', "
           "UTC_TIMESTAMP(3))" % (int(account_id), flags.hex(), folder_bytes.hex(), struct.pack("<16i", *counters).hex()))
    subprocess.run([os.path.join(MYSQL_BIN, "mysql.exe"), "--protocol=tcp", "-h", "127.0.0.1", "-P", str(port), "-u", "root", "--password=" + pw,
                    db, "-e", sql], check=True, capture_output=True, creationflags=NO_WINDOW)


def sql_rows(stack, connection, sql):
    """The rows of a query of the scenario's database (tab-separated text per row; NULL as "NULL")."""
    db = dict(kv.split("=", 1) for kv in connection.split(";") if "=" in kv)["Database"]
    port, pw = stack.mysql
    out = subprocess.run([os.path.join(MYSQL_BIN, "mysql.exe"), "--protocol=tcp", "-h", "127.0.0.1", "-P", str(port), "-u", "root", "--password=" + pw,
                          "-N", "-B", db, "-e", sql], check=True, capture_output=True, text=True, creationflags=NO_WINDOW).stdout
    return [l for l in out.splitlines() if l.strip()]


def stored_story(stack, connection, account_id):
    """(revision, the story bytes as a record holds them: flags 1024, folder 12, counters 16 x i32 big endian) of the
    account's online_story row; (0, 1100 zero bytes) without one (round G-core: the guest's own story)."""
    db = dict(kv.split("=", 1) for kv in connection.split(";") if "=" in kv)["Database"]
    port, pw = stack.mysql
    sql = "SELECT revision, HEX(flags), HEX(folder), HEX(counters) FROM online_story WHERE account_id = %d" % int(account_id)
    out = subprocess.run([os.path.join(MYSQL_BIN, "mysql.exe"), "--protocol=tcp", "-h", "127.0.0.1", "-P", str(port), "-u", "root", "--password=" + pw,
                          "-N", "-B", db, "-e", sql], check=True, capture_output=True, text=True, creationflags=NO_WINDOW).stdout.split()
    if len(out) < 4:
        return 0, bytes(1100)
    flags = bytes.fromhex(out[1]).ljust(1024, b"\0")[:1024]
    folder = bytes.fromhex(out[2]).ljust(12, b"\0")[:12]
    counters = struct.unpack("<16i", bytes.fromhex(out[3]).ljust(64, b"\0")[:64])
    return int(out[0]), flags + folder + struct.pack(">16i", *counters)


def card_boxes_story(path):
    """The story bytes (792..1891) of every used box of every valid data copy of a GCI file: [(copy, box, bytes)]."""
    data = open(path, "rb").read()
    found = []
    for copy in CARD_COPIES:
        base = GCI_ENTRY + copy
        if len(data) < base + CARD_COPY_SIZE:
            continue
        words = struct.unpack(">%dI" % ((CARD_COPY_SIZE - 4) // 4), data[base + 4:base + CARD_COPY_SIZE])
        if struct.unpack(">I", data[base:base + 4])[0] != sum(words) & 0xFFFFFFFF:
            continue
        for box in range(4):
            rec = base + 8 + box * 0x2000
            if data[rec]:
                found.append((copy, box, data[rec + 792:rec + 1892]))
    return found


def own_story_checks(stack, connection, sc, out, accounts, seeded, problems, notes):
    """Round G-core (G3, G4, G10): a guest's online card, its backups and its local card hold its own story, never the
    owner's; its own online_story row keeps its seeded revision."""
    for name in sc.get("own_story", []):
        revision, own = stored_story(stack, connection, accounts[name])
        if name in seeded and revision != seeded[name]:
            problems.append("%s: its own online_story moved from revision %d to %d" % (name, seeded[name], revision))
        files = glob.glob(os.path.join(out, "online_" + name, "*", "online_card.dat")) + \
            glob.glob(os.path.join(out, "online_" + name, "*", "backups", "*.dat"))
        if not files:
            problems.append("%s: no online card file" % name)
        boxes = 0
        for path in files:
            for copy, box, story in card_boxes_story(path):
                boxes += 1
                if story != own:
                    problems.append("%s: %s copy 0x%x box %d does not hold its own story" % (name, os.path.basename(path), copy, box))
        notes.append("%s: %d used boxes in %d online card files hold its own story (online_story revision %d)" % (name, boxes, len(files), revision))
        local = os.path.join(out, "card_" + name)
        left = [os.path.join(r, f) for r, _, fs in os.walk(local) for f in fs]
        if left:
            problems.append("%s: the local card folder got files: %s" % (name, ", ".join(os.path.relpath(p, local) for p in left)))


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


def start_fake_world_client(stack, api, relay_port, pin, out, tag, name, script, duration, seed, lock):
    """A fake DW4R2 client in the world (tests/DW4Online.FakeClient's client mode after the relay lane: POST /world/ticket,
    protocol 49, PRESENCE every 200 ms, its script's exits as BARRIERs): a new account, its process and its account id. The
    fake's engine is the stand-in (FakeEngine), never a real game: it must never share a stage group with a game client
    (the fingerprints would differ), so W29's fake guests arrive where nobody of the host's instance is (a guest's NEW)."""
    with lock:
        s, body = api.call("POST", "/api/v1/auth/register", {"username": tag + name, "password": "online test fake " + secrets.token_hex(4)})
    if s != 201:
        raise RuntimeError("register %s: %s %s" % (name, s, body))
    path = os.path.join(out, name + ".script.json")
    with open(path, "w") as f:
        json.dump(script, f)
    argv = ["dotnet", dll("tests/DW4Online.FakeClient", "DW4Online.FakeClient.dll"), "client", "--port", str(relay_port), "--api", api.base,
            "--token", body["token"], "--pin", pin, "--protocol", str(WORLD_PROTOCOL), "--name", name, "--seed", str(seed), "--script", path,
            "--duration", str(duration), "--log", os.path.join(out, name + ".jsonl"), "--stats", os.path.join(out, name + ".stats.json")]
    return stack.spawn(argv, os.path.join(out, name + ".out")), body["account"]["id"]


def fake_world_notes(out, names):
    """What the fake world clients did: their decisions (from their own logs) and how they ended."""
    notes, problems = [], []
    for n in names:
        log = load_jsonl(os.path.join(out, n + ".jsonl"))
        decisions = ["%s %s" % (e.get("decision"), e.get("to") or e.get("stage")) for e in log if e.get("ev") == "decision"]
        welcome = any(e.get("ev") == "welcome" for e in log)
        st = os.path.join(out, n + ".stats.json")
        close = json.load(open(st)).get("CloseReason") if os.path.exists(st) else None
        notes.append("fake %s: %s%s%s" % (n, "welcome" if welcome else "no welcome", (", " + "; ".join(decisions)) if decisions else "",
                                          (", closed: %s" % close) if close else ""))
        if not welcome:
            problems.append("fake %s never got a WELCOME (see %s.out)" % (n, n))
    return notes, problems


def frame_notes(out, names):
    """The clients' frame times (W19's "frame time recorded"): the host lines of each client's online log ("host: N
    passes/s, P pictures/s, longest pass L ms"), every 10 s: the pictures per second (a hidden client draws only with
    "shots") and the longest pass."""
    notes = []
    rx = re.compile(r"host: ([0-9.]+) passes/s, ([0-9.]+) pictures/s, longest pass (\d+) ms")
    for n in names:
        path = os.path.join(out, n + ".online.log")
        rows = [tuple(float(x) for x in m.groups()) for m in (rx.search(l) for l in open(path, encoding="utf-8", errors="replace"))
                if m] if os.path.exists(path) else []
        if rows:
            pics = [r[1] for r in rows]
            notes.append("%s frame times: %d samples, pictures/s min %.1f avg %.1f, longest pass max %d ms (median of the 10 s maxima %d ms)" % (
                n, len(rows), min(pics), sum(pics) / len(pics), max(r[2] for r in rows), sorted(r[2] for r in rows)[len(rows) // 2]))
    return notes


def run_real(stack, sc, api, relay_port, pin, diag, lock, connection):
    """One game scenario: hidden game clients with scripts that enter the world (--online-play) on the scenario's own
    server (main gives every scenario a fresh one)."""
    a = stack.args
    if not a.game_exe or not a.data:
        raise RuntimeError("game scenarios need --game-exe (a DW4_ONLINE_DEV build) and --data (the game data)")
    src = os.path.join(SCENARIO_DIR, sc.get("dir", sc["name"]))
    out = os.path.join(stack.dir, sc["name"])
    os.makedirs(out, exist_ok=True)
    tag = re.sub(r"[^a-z0-9]", "", sc["name"].lower())[:8] + secrets.token_hex(2)
    passwords, accounts, tokens = {}, {}, {}
    with lock:
        for c in sc["clients"]:
            passwords[c["name"]] = "online test " + c["name"] + " " + secrets.token_hex(4)
            s, body = api.call("POST", "/api/v1/auth/register", {"username": tag + c["name"], "password": passwords[c["name"]]})
            if s != 201:
                raise RuntimeError("register %s: %s %s" % (c["name"], s, body))
            accounts[c["name"]] = body["account"]["id"]
            tokens[c["name"]] = body.get("token")
        if sc.get("api_steps") or sc.get("api_checks") or any(c.get("role") for c in sc["clients"]):
            passwords["admin"] = "online test admin " + secrets.token_hex(4)
            s, body = api.call("POST", "/api/v1/auth/register", {"username": tag + "admin", "password": passwords["admin"]})
            accounts["admin"], tokens["admin"] = body["account"]["id"], body.get("token")
            sql_rows(stack, connection, "UPDATE accounts SET role = 'admin' WHERE id = %d" % int(accounts["admin"]))
            s, body = api.call("POST", "/api/v1/auth/login", {"username": tag + "admin", "password": passwords["admin"]})
            tokens["admin"] = body.get("token")
        for c in sc["clients"]:
            if c.get("role"):
                sql_rows(stack, connection, "UPDATE accounts SET role = '%s' WHERE id = %d" % (c["role"], int(accounts[c["name"]])))
                s, body = api.call("POST", "/api/v1/auth/login", {"username": tag + c["name"], "password": passwords[c["name"]]})
                tokens[c["name"]] = body.get("token")
            if c.get("prefs"):
                s, body = api.call("PUT", "/api/v1/me/online-prefs", c["prefs"], tokens[c["name"]])
                if s != 200:
                    raise RuntimeError("prefs of %s: %s %s" % (c["name"], s, body))
    https_port = api.base.rsplit(":", 1)[1]

    def expand(text):
        for n, i in accounts.items():
            text = text.replace("{id:%s}" % n, str(i)).replace("{user:%s}" % n, tag + n).replace("{pw:%s}" % n, passwords.get(n, ""))
        return text.replace("{tag}", tag)

    def api_call(step):
        body = json.loads(expand(json.dumps(step.get("body")))) if step.get("body") is not None else None
        with lock:
            return api.call(step.get("method", "GET"), expand(step["path"]), body, tokens.get(step.get("as", "admin")))

    def api_check(check, problems, notes):
        status, body = api_call(check)
        text = json.dumps(body)
        if check.get("status") and status != check["status"]:
            problems.append("api %s %s as %s: %s, expected %s (%s)" % (check.get("method", "GET"), check["path"], check.get("as", "admin"), status,
                                                                       check["status"], text[:160]))
        elif check.get("expect") and not re.search(check["expect"], text):
            problems.append("api %s %s: %s does not match /%s/" % (check.get("method", "GET"), check["path"], text[:200], check["expect"]))
        else:
            notes.append("api %s %s as %s: %s" % (check.get("method", "GET"), check["path"], check.get("as", "admin"), status))
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
        if a.compat_scripts:
            # (--compat-scripts: a client before round G-core, a counter-proof on main, knows no key:N / difficulty
            # condition; they leave its scripts, so the scenario's other checks still run.)
            cond = r"(?:key:\d+|difficulty)(?:>=|<=|=|<|>)\d+"
            lines = []
            for line in text.splitlines():
                if not line.startswith("#"):
                    line = re.sub("," + cond, "", line)
                    line = re.sub("(?<= )" + cond + ",", "", line)
                    line = re.sub(" (?:until|fail) " + cond + "(?= |$)", "", line)
                lines.append(line)
            text = "\n".join(lines) + "\n"
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

    def start(c, play_args):
        card = os.path.join(out, "card_" + c["name"])
        for sub_dir in ("Card A", "Card B"):
            os.makedirs(os.path.join(card, sub_dir), exist_ok=True)
        env = dict(os.environ, DW4_NO_ERROR_BOX="1", DW4_ONLINE_SERVER="127.0.0.1:" + https_port, DW4_ONLINE_PIN=pin,
                   DW4_ONLINE_PASSWORD=passwords[c["name"]], DW4_ONLINE_TRACE="1")
        # The development build's answer to the arrival prompt (19.24): I1a's automatic join takes the first row.
        env["DW4_ONLINE_AUTOCHOICE"] = c.get("autochoice", sc.get("autochoice", "first"))
        env.update(sc.get("env", {}))
        env.update(c.get("env", {}))
        env["DW4_ONLINE_LOG"] = os.path.join(out, c["name"] + ".online.log")
        # The client's link (SEAMLESS.md 6.4, the latency scenarios): "impair" {"up": "MS,JITTER,LOSS", "down": ...} of the
        # client, else of the scenario, else --net (all clients): its own impairment proxy in front of the relay.
        link = c.get("impair") or sc.get("impair") or net_override()
        cut = c.get("cut")
        if link or cut:
            link = link or {"up": "0,0,0"}
            port = free_port(stack)
            extra = ["--cut", "%.3f,%s,%s" % (t0 + float(cut["at"]), cut["seconds"], cut.get("mode", "close"))] if cut else []
            stack.spawn(["dotnet", dll("tests/DW4Online.FakeClient", "DW4Online.FakeClient.dll"), "proxy", "--listen", str(port), "--target",
                         str(relay_port), "--up", link.get("up", "0,0,0"), "--down", link.get("down", link.get("up", "0,0,0"))] + extra,
                        os.path.join(out, c["name"] + ".proxy.out"))
            if not wait_port(port, 20):
                raise RuntimeError("the proxy of %s did not start" % c["name"])
            env["DW4_ONLINE_RELAY_PORT"] = str(port)
        exe = c.get("exe", a.game_exe).replace("{old_exe}", a.old_game_exe or "{old_exe}")
        argv = [exe, "--data", a.data, "--smoke-test", str(c.get("frames", sc.get("frames", 30000))), "--card-dir", card,
                "--online-login", tag + c["name"], "--online-script", script_for(c)] + play_args +             [x.replace("{out}", out) for x in c.get("args", [])]  # (a client's extra game arguments; {out}: the scenario's folder)
        # (Round G-core: each client's online card cache in the run folder, for the own story checks.)
        argv += ["--online-folder", os.path.join(out, "online_" + c["name"])]
        # "shots": the script's shot steps write NAME.bmp into the run folder (hidden 1920x1009 widescreen MSAA 4
        # pictures, as a player sees them; P1, the ghost figures).
        if c.get("shots", sc.get("shots")):
            argv += ["--screenshot", os.path.join(out, c["name"] + "_shot.bmp")]
            for k, v in (("DW4_RENDER", "1"), ("DW4_WINDOW", "1920x1009"), ("DW4_WIDESCREEN", "1"), ("DW4_MSAA", "4"), ("DW4_LABELS", "all")):
                env.setdefault(k, v)
        folder = saved_card(c)
        if folder and c.get("upload_card"):
            # (Round G-core: the card's character is the account's online character on the server already; the world
            # entry's GET brings it with the account's own story, the bot calls it at the title.)
            env["DW4_CAMPAIGN_LOAD"] = "1"
        elif folder:
            argv += ["--online-card", folder]
            env["DW4_CAMPAIGN_LOAD"] = "1"
        return stack.spawn(argv, os.path.join(out, c["name"] + ".log"), env=env)

    clients = sorted(sc["clients"], key=lambda c: c.get("start", 0))
    if sc.get("seed_story") and (not SERVER_DLL or SERVER_HAS_STORY):  # (an older server, --server-root, has no online_story)
        # The room's creator is its owner: the room story starts from the owner's card.
        owner_card = saved_card(clients[0])
        if not owner_card:
            raise RuntimeError("seed_story needs a card for the room's creator %s" % clients[0]["name"])
        with lock:
            seed_story(stack, connection, accounts[clients[0]["name"]], owner_card, int(sc.get("seed_difficulty", 0)),
                       bool(sc.get("seed_503")), sc.get("seed_story") == "full")
        # A guest's own online story (round G-core: G2, G3, G10 seed the guest's from its card too).
        for c in clients[1:]:
            if c.get("seed_story"):
                guest_card = saved_card(c)
                if not guest_card:
                    raise RuntimeError("seed_story of %s needs a card" % c["name"])
                with lock:
                    seed_story(stack, connection, accounts[c["name"]], guest_card)
    seeded = {}
    for c in clients:
        if c.get("upload_card"):
            # The card's first record as the account's online character (PUT, If-None-Match: *): the server stores it
            # with the account's own story (8.4).
            card_dir = saved_card(c)
            if not card_dir:
                raise RuntimeError("upload_card of %s needs a card" % c["name"])
            gci = [f for f in os.listdir(os.path.join(card_dir, "Card A") if os.path.isdir(os.path.join(card_dir, "Card A")) else card_dir)
                   if f.endswith(".gci")]
            base_dir = os.path.join(card_dir, "Card A") if os.path.isdir(os.path.join(card_dir, "Card A")) else card_dir
            data = open(os.path.join(base_dir, gci[0]), "rb").read()
            record = data[GCI_ENTRY + CARD_COPIES[0] + 8:GCI_ENTRY + CARD_COPIES[0] + 8 + 3316]
            import character_test
            with lock:
                s, body = character_test.put_character(api, tokens[c["name"]], record, 0, 3600)
            if s not in (200, 201):
                raise RuntimeError("upload_card %s: %s %s" % (c["name"], s, body))
    for c in clients:
        if c.get("seed_story") or (c is clients[0] and sc.get("seed_story")):
            with lock:
                seeded[c["name"]] = stored_story(stack, connection, accounts[c["name"]])[0]
    story_saved, stop_watch = {}, threading.Event()
    if sc.get("story_saved"):
        watcher = threading.Thread(target=watch_story_saved, args=(stack, sc, out, connection, accounts[clients[0]["name"]], story_saved, stop_watch),
                                   daemon=True)
        watcher.start()
    # The world: every client enters with --online-play at its start offset (no room to create or join); the scenario's
    # scripts take them through the lobby gate, where I1a's automatic join (DW4_ONLINE_AUTOCHOICE) groups them.
    procs = []
    if sc.get("server_restart"):
        spec = sc["server_restart"]

        def restarter():
            while time.time() - t0 < float(spec["at"]):
                time.sleep(0.05)
            restart_server(stack, api.base, bool(spec.get("kill")), float(spec.get("down", 0)))
        threading.Thread(target=restarter, daemon=True).start()
    timed_problems, timed_notes = [], []
    for step in sc.get("api_steps", []) + [dict(ch, check=True) for ch in sc.get("api_checks", []) if ch.get("at") != "after"]:
        def timed(step=step):
            while time.time() - t0 < float(step.get("at", 0)):
                time.sleep(0.05)
            if step.get("check"):
                api_check(step, timed_problems, timed_notes)
            else:
                status, body = api_call(step)
                timed_notes.append("api step %s %s as %s: %s" % (step.get("method", "GET"), step["path"], step.get("as", "admin"), status))
        threading.Thread(target=timed, daemon=True).start()
    for c in clients:
        while time.time() - t0 < c.get("start", 0):
            time.sleep(0.05)
        # (A client's "play_args" replace --online-play: W22's protocol 48 client knows only --online-create / --online-join.)
        procs.append((c["name"], start(c, c.get("play_args") or (["--online-play"] + ([str(c["box"])] if c.get("box") else [])))))
        if c.get("crash"):
            def crash(c=c, index=len(procs) - 1):
                while time.time() - t0 < float(c["crash"]["at"]):
                    time.sleep(0.05)
                procs[index][1].kill()
                time.sleep(float(c["crash"].get("restart", 5)))
                again = dict(c, script=c["crash"].get("script", c["script"]), name=c["name"])
                os.replace(os.path.join(out, c["name"] + ".log"), os.path.join(out, c["name"] + ".crashed.log"))
                procs[index] = (c["name"], start(again, ["--online-play"]))
            threading.Thread(target=crash, daemon=True).start()
    # The fake world clients (requires fake_world_clients): "fake_presence" ({"count": N, "stage": S}: N presence senders
    # that go from the title to the hub stage S at once and stay there, W19) and "fake_guests" ({"count": N, "after":
    # "CLIENT: REGEX", "stage": S}: N fakes that go home and then take the way to S once CLIENT's log shows REGEX; W29's
    # two guests of a's instance, a guest's NEW where nobody of it is). Their accounts are {id:fp1}, {id:fg1}, ...
    fakes, fake_names = [], []
    fake_problems = []
    limit = sc.get("timeout", 1200)
    if sc.get("fake_presence"):
        spec = sc["fake_presence"]
        for k in range(int(spec.get("count", 1))):
            name = "fp%d" % (k + 1)
            p, accounts[name] = start_fake_world_client(stack, api, relay_port, pin, out, tag, name,
                                                        [{"at": 1.0 + 0.05 * k, "do": "exit", "stage": spec.get("stage", "stagML11")}], limit, 100 + k, lock)
            fakes.append(p)
            fake_names.append(name)
    if sc.get("fake_guests"):
        spec = sc["fake_guests"]
        source, _, pattern = spec["after"].partition(":")
        rx = re.compile(pattern.strip())

        def guests():
            log_path = os.path.join(out, source.strip() + ".log")
            while time.time() - t0 < limit:
                text = open(log_path, encoding="utf-8", errors="replace").read() if os.path.exists(log_path) else ""
                if rx.search(text):
                    break
                time.sleep(0.5)
            else:
                fake_problems.append("fake_guests: %s never logged /%s/" % (source.strip(), pattern.strip()))
                return
            for k in range(int(spec.get("count", 1))):
                name = "fg%d" % (k + 1)
                script = [{"at": 1.0, "do": "exit", "stage": "stagML11"}, {"at": 4.0 + 0.2 * k, "do": "exit", "stage": spec.get("stage", "stag0101")}]
                p, accounts[name] = start_fake_world_client(stack, api, relay_port, pin, out, tag, name, script, max(10, limit - (time.time() - t0)),
                                                            200 + k, lock)
                fakes.append(p)
                fake_names.append(name)
        threading.Thread(target=guests, daemon=True).start()
    code = None
    if any(c.get("crash") for c in clients):
        # (The crashed client's second process replaces the first in procs: wait for the crash and restart to pass.)
        time.sleep(max(0.0, max(float(c["crash"]["at"]) + float(c["crash"].get("restart", 5)) + 2 for c in clients if c.get("crash"))
                       - (time.time() - t0)))
    for name, p in list(procs):
        try:
            p.wait(max(1, limit - (time.time() - t0)))
        except subprocess.TimeoutExpired:
            pass
    procs = list(procs)
    timed_out = [name for name, p in procs if p.poll() is None]
    exits = {name: p.poll() for name, p in procs}
    for name, p in procs:
        if p.poll() is None:
            p.terminate()
    for p in list(fakes):
        if p.poll() is None:
            p.terminate()
    stop_watch.set()
    problems, notes = evaluate_real(sc, src, out, code, diag, timed_out, exits)
    problems += timed_problems + fake_problems
    notes += timed_notes
    if sc.get("frame_times"):
        notes += frame_notes(out, [c["name"] for c in sc["clients"]])
    if fake_names:
        fn, fp = fake_world_notes(out, fake_names)
        notes += fn
        problems += fp
    for check in sc.get("api_checks", []):
        if check.get("at") == "after":
            api_check(check, problems, notes)
    if sc.get("own_story"):
        with lock:
            own_story_checks(stack, connection, sc, out, accounts, seeded, problems, notes)
    latency_summary(sc, out, notes)
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
    # The scenario's database afterwards (the world's tables: instance_history's end reason, its members, prefs, ...).
    for check in sc.get("db_checks", []):
        sql = check["sql"].replace("{tag}", tag)
        for n, i in accounts.items():
            sql = sql.replace("{id:%s}" % n, str(i))
        with lock:
            rows = sql_rows(stack, connection, sql)
        text = "\n".join(rows)
        if re.search(check["expect"], text):
            notes.append("db: %s -> %s" % (check.get("name", sql[:60]), text.replace("\t", " ")[:120]))
        else:
            problems.append("db: %s gave %r, expected /%s/" % (check.get("name", sql[:80]), text[:200], check["expect"]))
    return problems, notes


NET_OVERRIDE = None


def net_override():
    """--net UP[/DOWN]: every client of a game scenario behind its own proxy (MS,JITTER,LOSS per direction)."""
    if not NET_OVERRIDE:
        return None
    up, _, down = NET_OVERRIDE.partition("/")
    return {"up": up, "down": down or up}


def latency_summary(sc, out, notes):
    """The latency scenarios (SEAMLESS.md 6.4): every client's input delay changes, its latency windows ("online-trace:
    net ..."), its final latency line and its mode changes, as notes and as latency.json in the scenario's folder."""
    summary = {}
    for c in sc["clients"]:
        path = os.path.join(out, c["name"] + ".log")
        if not os.path.exists(path):
            continue
        delays, windows, modes, final, backlogs = [], [], [], None, 0
        for line in open(path, encoding="utf-8", errors="replace"):
            line = line.rstrip()
            if line.startswith("online-trace: delay "):
                delays.append(line[len("online-trace: "):])
            elif line.startswith("online-trace: net "):
                kv = dict(x.split("=", 1) for x in line[len("online-trace: net "):].split() if "=" in x)
                windows.append(kv)
            elif line.startswith("online-trace: mode "):
                modes.append(line[len("online-trace: "):])
            elif line.startswith("online-trace: backlog "):
                backlogs += 1
            elif line.startswith("online: final latency"):
                final = line[len("online: "):]
        sealed = [w for w in windows if w.get("mode") == "sealed" and int(w.get("samples", "0")) > 0]
        summary[c["name"]] = {"delays": delays, "modes": modes, "final": final, "backlogs": backlogs, "windows": windows}
        if sealed:
            # The sealed windows after the first 15 s of sealed play (the join's fills and the delay's settling first).
            settled = sealed[3:] or sealed
            n = sum(int(w["samples"]) for w in settled)
            dropped = sum(int(w["dropped"]) for w in settled)
            exec_avg = sum(float(w["exec_avg"]) * int(w["samples"]) for w in settled) / max(1, n)
            ds = sorted({int(w["d"]) for w in sealed})
            notes.append("%s: sealed windows %d, d %s (first %s, last %s), exec avg %.1f ms (settled), max %.1f ms, dropped %d of %d, backlogs %d" % (
                c["name"], len(sealed), ",".join(map(str, ds)), sealed[0]["d"], sealed[-1]["d"], exec_avg,
                max(float(w["exec_max"]) for w in settled), dropped, n, backlogs))
        for d in delays[:12]:
            notes.append("   %s %s" % (c["name"], d))
        if final:
            notes.append("   %s %s" % (c["name"], final))
    with open(os.path.join(out, "latency.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1)


def check_achievements(sc, api, tag, passwords, lock, problems, notes):
    """The online account's achievements (SEAMLESS.md 11) a scenario asks for in "achievements": per client, the codes the
    server must hold ("unlocked", in the "ruleset", standard by default, with "players" per code where given), the codes it
    must not ("locked"), and the least progress of stat achievements ("progress": code -> the counter's least value)."""
    for name, want in sorted(sc["achievements"].items()):
        with lock:
            s, body = api.call("POST", "/api/v1/auth/login", {"username": tag + name, "password": passwords[name], "deviceName": "online_test",
                                                              "protocol": WORLD_PROTOCOL})
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
    # A client with "refused": true must be turned away at its start (W22: the server refuses its protocol): its error
    # exit is the expected outcome (expected.txt names the text), never a problem; running its script would be one.
    refused = {c["name"] for c in sc["clients"] if c.get("refused")}
    for n in refused:
        if exits.get(n) in (0, None):
            problems.append("%s was not refused (exit %s)" % (n, exits.get(n)))
        if any(l.startswith("online-script: done") for l in logs[n]):
            problems.append("%s ran its script: the server let it in" % n)
    for n, code_ in exits.items():
        if code_ not in (0, None) and n not in refused:
            problems.append("%s exited with %s" % (n, code_))
    for n in names:
        if n in refused:
            continue
        if not any(l.startswith("online-script: done") for l in logs[n]):
            problems.append("%s: the script did not finish" % n)
        for l in logs[n]:
            if l.startswith("online-trace: close") and sc.get("allow_close"):
                continue  # (a server restart, an ended instance: the scenario expects the world's CLOSE)
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
    relay = load_world_log(diag)
    notes.extend(world_notes(relay))
    if sc.get("allow_resync"):
        # A resync scenario (S11a, S11b): the fingerprints may differ from the injected desync up to the instance's last
        # resync READY; from there on they must be equal for min_after_resync ticks.
        ready = {}
        for e in relay:
            if e.get("ev") == "resync_ready":
                key = (int(e["inst"]), int(e["visit"]))
                ready[key] = max(ready.get(key, -1), int(e.get("tick", -1)))
        healed = [k for k in differ if k[2] <= ready.get((k[0], k[1]), -1)]
        notes.append("%d differing ticks before the last resync READY (allowed), resync READY at %s" % (len(healed), ready))
        differ = [k for k in differ if k not in healed]
        after = 0
        for (inst, visit), t in ready.items():
            equal_after = [k[2] for k in compared if (k[0], k[1]) == (inst, visit) and k[2] > t and k not in differ]
            if equal_after:
                after = max(after, max(equal_after) - t)
        notes.append("equal net fingerprints for %d ticks after the resync" % after)
        if not ready:
            problems.append("no resync ended (no resync_ready in the relay log)")
        elif after < sc.get("min_after_resync", 3000):
            problems.append("only %d ticks of equal net fingerprints after the resync (< %d)" % (after, sc.get("min_after_resync", 3000)))
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
    kinds = {}
    for e in relay:
        kinds[e["ev"]] = kinds.get(e["ev"], 0) + 1
    notes.append("relay: " + ", ".join("%s %d" % kv for kv in sorted(kinds.items())))
    # (statehash_mismatch and checkpoint_suspect since lane jready's review: S3 logged both, a rebuilt joiner's STATEHASH
    # right after its JOIN, and passed; a suspect path makes every later join of the visit a classic one.)
    bad_kinds = ("desync", "snaphash_mismatch", "split_mismatch", "barrier_mismatch", "barrier_split", "resync", "ready_mismatch",
                 "statehash_mismatch", "checkpoint_suspect")
    resync_kinds = ("desync", "resync", "statehash_mismatch", "checkpoint_suspect")  # what an injected desync brings
    # "expect_relay" ({"ready_mismatch": 1}, JS): a scenario that provokes one of these on purpose names it with its exact
    # count; that count is then required (fewer or more is a problem), every other one stays an error.
    expect_relay = sc.get("expect_relay", {})
    for ev, count in sorted(expect_relay.items()):
        if ev not in bad_kinds or ev in ("desync", "resync"):
            problems.append("scenario.json: expect_relay names %s (only %s; desync and resync go by allow_resync)" %
                            (ev, ", ".join(k for k in bad_kinds if k not in ("desync", "resync"))))
        elif kinds.get(ev, 0) != count:
            problems.append("relay logged %d x %s, the scenario expects exactly %d" % (kinds.get(ev, 0), ev, count))
        else:
            notes.append("relay logged %d x %s as the scenario expects" % (count, ev))
    for bad in bad_kinds:
        if sc.get("allow_resync") and bad in resync_kinds and bad not in expect_relay:
            continue  # (the resync scenarios inject them; their expected.txt checks how they end)
        if bad in expect_relay:
            continue  # (checked above)
        if kinds.get(bad):
            problems.append("relay logged %d x %s" % (kinds[bad], bad))
    relay_lines = [" ".join([e["ev"]] + ["%s=%s" % (k, v) for k, v in e.items() if k != "ev"]) for e in relay]
    # scenario.json "world_log": lines of the world log that must appear in order (as expected.txt's "relay:" lines).
    at = 0
    for pattern in sc.get("world_log", []):
        rx = re.compile(pattern)
        while at < len(relay_lines) and not rx.search(relay_lines[at]):
            at += 1
        if at >= len(relay_lines):
            problems.append("world log: expected /%s/ (in order) not found" % pattern)
        else:
            at += 1
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
    def start(name, play_args):
        card = os.path.join(out, "card_" + name)
        for sub in ("Card A", "Card B"):
            os.makedirs(os.path.join(card, sub), exist_ok=True)
        env = dict(os.environ, DW4_NO_ERROR_BOX="1", DW4_ONLINE_SERVER="127.0.0.1:" + https_port, DW4_ONLINE_PIN=pin,
                   DW4_ONLINE_PASSWORD=passwords[name], DW4_ONLINE_TRACE="1", DW4_ONLINE_AUTOCHOICE="first")
        argv = [a.game_exe, "--data", a.data, "--smoke-test", str(a.frames), "--card-dir", card, "--input", os.path.join(out, "campaign.txt"),
                "--online-login", tag + name] + play_args
        return stack.spawn(argv, os.path.join(out, name + ".log"), env=env)

    t0 = time.time()
    # The world (round I1): both enter with --online-play, the second --offset seconds later; the campaign bot takes both
    # through the lobby gate into stag0101, where I1a's automatic join (DW4_ONLINE_AUTOCHOICE first) puts the second
    # into the first's instance.
    first = start(names[0], ["--online-play"])
    code = None
    while time.time() - t0 < a.offset:
        if first.poll() is not None:
            raise RuntimeError("the first client ended early (see %s)" % os.path.join(out, names[0] + ".log"))
        time.sleep(0.1)
    second = start(names[1], ["--online-play"])
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
    relay_log = load_world_log(diag)
    notes.extend(world_notes(relay_log))
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

    first = start(names[0], ["--online-play"])
    t0 = time.time()
    time.sleep(5)
    print("the world: %s and %s pressed Play; logs in %s" % (tag + names[0], tag + names[1], out), flush=True)
    second = start(names[1], ["--online-play"])
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
        if c.get("online_script"):  # (the game's pad script beside the UI script, in the scenario's folder)
            argv += ["--online-script", os.path.join(src, c["online_script"])]
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
        # A client's "log_expect": regular expressions its log must match (each one somewhere).
        for pattern in next(c for c in sc["clients"] if c["name"] == name).get("log_expect", []):
            if not any(re.search(pattern, l) for l in lines):
                problems.append("%s: no log line matches %s" % (name, pattern))
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


def guest_card_problems(name, folder, notes):
    """Round G-core: a guest without an own story (the screens' way into a room, ui/newchars' b) keeps empty story bytes in
    every used box of its online card cache, the cache's .bak and its backups (the server's record alone does not show it:
    the server composes the own story into every upload). Before the room-owner fix the screens' way left every guest the
    owner, and the save room wrote the room story into the guest's card (from there into savedata.dat by F1 > Saves >
    Import and the cloud backup). Returns the problems."""
    problems, boxes = [], 0
    files = [p for p in [os.path.join(folder, "online_card.dat"), os.path.join(folder, "online_card.dat.bak")] if os.path.exists(p)]
    files += glob.glob(os.path.join(folder, "backups", "*.dat"))
    if not files:
        problems.append("%s: no online card file in %s" % (name, folder))
    for path in files:
        for copy, box, story in card_boxes_story(path):
            boxes += 1
            if any(story):
                problems.append("%s: %s copy 0x%x box %d holds story bytes (the room story; its own story is empty)" %
                                (name, os.path.basename(path), copy, box))
    notes.append("%s: %d used boxes in %d online card files checked for an empty own story" % (name, boxes, len(files)))
    return problems


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
        # A guest (own_story "none"; round G-core, 8.8) keeps its own story in every record: a save room save that moved
        # only the room story on is unchanged since its last upload (before the room-owner fix of the title's screens the
        # guest played as the owner and that save uploaded the room story's change).
        guest_unchanged = want.get("own_story") == "none" and any(re.match(r"save \(save room\) unchanged since revision \d+$", l) for l in ch)
        if "save room" not in reasons and not guest_unchanged:
            problems.append("%s: no upload from the save room" % name)
        if guest_unchanged and "save room" not in reasons:
            notes.append("%s (a guest): its save room save was unchanged since its last upload (its own story, nothing else moved)" % name)
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
        s, body = api.call("POST", "/api/v1/auth/login", {"username": account, "password": password, "deviceName": "online_test", "protocol": WORLD_PROTOCOL})
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
        if want.get("own_story") == "none":
            problems.extend(guest_card_problems(name, folder, notes))
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
    ap.add_argument("--server-root", default="", help="the DW4Online folder of another checkout whose server runs (built there)")
    ap.add_argument("--play-protocol", type=int, default=0, help="the server's Play:MinimumProtocol / CurrentProtocol (the field: 43)")
    ap.add_argument("--server-has-story", action="store_true",
                    help="--server-root's server keeps the room story (round 56 and later, e.g. DW4Online 0.1.2): seed it as for this one")
    ap.add_argument("--net", default="", help="game scenarios: every client behind its own proxy, UP[/DOWN] as MS,JITTER,LOSS")
    ap.add_argument("--capabilities", default="",
                    help="game scenarios: what this build offers beyond the client and relay, comma-separated (fake_world_clients, old_client)")
    ap.add_argument("--old-game-exe", default="", help="W22: a client of the release before the world (protocol 48), for {old_exe}")
    ap.add_argument("--compat-scripts", action="store_true",
                    help="game scenarios: drop the key:N and difficulty conditions of round G-core (a counter-proof client before it)")
    a = ap.parse_args()
    global SERVER_DLL, NET_OVERRIDE, SERVER_HAS_STORY
    if a.server_root:
        SERVER_DLL = os.path.join(a.server_root, "src", "DW4Online.Server", "bin", "Debug", "net10.0", "DW4Online.Server.dll")
        SERVER_HAS_STORY = a.server_has_story
    NET_OVERRIDE = a.net or None
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
        # NAME@UP[~DOWN] (the latency scenarios): the scenario with every client behind its own proxy, MS,JITTER,LOSS per
        # direction (DOWN defaults to UP); the run folder is named after the whole argument.
        by_name = {s["name"]: s for s in game_scenarios()}
        for n in a.scenarios:
            base, at, link = n.partition("@")
            if at and base in by_name:
                up, _, down = link.partition("~")
                real.append(dict(by_name[base], name=n, dir=base, impair={"up": up, "down": down or up}))
        unknown = [n for n in a.scenarios if n != "G1" and n not in {s["name"] for s in real}]
        if unknown:
            print("unknown scenario(s): " + " ".join(unknown))
            return 2
    stack = Stack(a)
    failed = 0
    # A scenario that takes pictures ("shots": P1, P2) keeps the run folder: the pictures are what a person looks at.
    pictures = any(s.get("shots") or any(c.get("shots") for c in s.get("clients", [])) for s in real)
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
                missing = [r for r in sc.get("requires", []) if r not in {x.strip() for x in a.capabilities.split(",") if x.strip()}]
                if missing:
                    with lock:
                        results[sc["name"]] = ([], ["SKIP: needs " + ", ".join(missing)])
                        print("SKIP %-5s %s (needs %s)" % (sc["name"], sc["desc"], ", ".join(missing)), flush=True)
                    return
                try:
                    # Every game scenario gets a fresh server process with its own database (19.24's harness lane): in the
                    # world every scenario on one server would see the others (Public by default, the automatic join,
                    # the ghost figures). scenario.json's "server" settings are merged over the stack's.
                    with lock:
                        own_db = create_database(stack)
                        own = start_server(stack, own_db, sc.get("server"), sc["name"])
                    problems, notes = run_real(stack, sc, Api(own[0], own[3]), own[1], own[2], own[4], lock, own_db)
                except Exception as e:  # a broken scenario is a failure, the others go on
                    problems, notes = ["%s: %s" % (type(e).__name__, e)], []
                if sc.get("counter_proof"):
                    # A counter-proof (W1x, W30x): a client hook switches the checked behaviour off, so the checks must fail.
                    problems, notes = (["the counter-proof passed: the checks do not catch the switched-off behaviour"] if not problems
                                       else []), notes + ["counter-proof failed as it must: " + p for p in problems]
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
            # (The world: a fresh server per scenario, as for the game scenarios.)
            try:
                own_db = create_database(stack)
                own = start_server(stack, own_db, None, sc["name"])
                problems, notes = run_scenario(stack, sc, Api(own[0], own[3]), own[1], own[2], own[4])
            except Exception as e:
                problems, notes = ["%s: %s" % (type(e).__name__, e)], []
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
        if not a.keep and not failed and not pictures:
            shutil.rmtree(stack.dir, ignore_errors=True)
        else:
            print("run folder: " + stack.dir)
    print("%d of %d scenarios passed" % (len(wanted) - failed, len(wanted)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
