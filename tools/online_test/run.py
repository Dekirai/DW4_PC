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
    common: [--mysql-port 3431] [--ports 53100-53199] [--no-build]

Game scenarios (scenarios/<id>/scenario.json, one .script per client, expected.txt): every client is a hidden
DW4_ONLINE_DEV build of dw4_game (--smoke-test, DW4_NO_ERROR_BOX=1, below normal priority) that logs in through the pinned
HTTPS API; the first creates the room, the others join it at their start offsets; each plays its pad from its script
(--online-script, src/game/online_script.hpp) and, with a card, starts from that saved character (--online-card, the
campaign bot calls it at the title). Checked for every scenario: the net fingerprints (DW4_ONLINE_TRACE netfp lines)
of every instance are equal on every client at every tick (every 60 ticks) they both report with the same members, the
canonical snapshot hashes of every NEW (and co-arrival) are equal, the relay logs no desync, mismatch or resync, no client
was closed or crashed; then the scenario's expected log (in order per source; "!" lines must not appear; "shared: a b >=
N" asks for N ticks of equal fingerprints with both present in one instance). Network traffic stays on loopback.

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
    db = "dw4online_run_" + secrets.token_hex(4)
    subprocess.run([os.path.join(MYSQL_BIN, "mysql.exe"), "--protocol=tcp", "-h", "127.0.0.1", "-P", str(port), "-u", "root", "--password=" + pw,
                    "-e", "CREATE DATABASE %s CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci" % db], check=True, creationflags=NO_WINDOW)
    return "Server=127.0.0.1;Port=%d;Database=%s;User ID=root;Password=%s" % (port, db, pw)


def openssl():
    for cand in (r"C:\Program Files\Git\mingw64\bin\openssl.exe", r"C:\Program Files\Git\usr\bin\openssl.exe", shutil.which("openssl") or ""):
        if cand and os.path.exists(cand):
            return cand
    raise RuntimeError("openssl not found (Git for Windows ships one)")


def make_certificate(stack):
    """A throw-away P-256 certificate for 127.0.0.1 (PEM for Kestrel), its SPKI pin and its DER."""
    o = openssl()
    cert, key = os.path.join(stack.dir, "cert.pem"), os.path.join(stack.dir, "key.pem")
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


def start_server(stack, connection):
    https, relay = free_port(stack), free_port(stack)
    cert, key, pin, der = make_certificate(stack)
    diag = os.path.join(stack.dir, "relay")
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
        "Logging": {"File": {"Directory": os.path.join(stack.dir, "server-log")}},
    }
    path = os.path.join(stack.dir, "server-settings.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(settings, f, indent=1)
    stack.spawn(["dotnet", dll("src/DW4Online.Server", "DW4Online.Server.dll"), "--settings", path], os.path.join(stack.dir, "server.out"),
                env=dict(os.environ, ASPNETCORE_ENVIRONMENT="Production", DOTNET_gcServer="0"))
    if not wait_port(https, 60) or not wait_port(relay, 30):
        raise RuntimeError("the server did not start (see %s)" % os.path.join(stack.dir, "server.out"))
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


def game_scenarios():
    names = sorted(d for d in os.listdir(SCENARIO_DIR) if os.path.isfile(os.path.join(SCENARIO_DIR, d, "scenario.json"))) \
        if os.path.isdir(SCENARIO_DIR) else []
    return [dict(json.load(open(os.path.join(SCENARIO_DIR, n, "scenario.json"), encoding="utf-8")), name=n) for n in names]


def run_real(stack, sc, api, relay_port, pin, diag, lock):
    """One game scenario: hidden game clients with scripts in one room."""
    a = stack.args
    if not a.game_exe or not a.data:
        raise RuntimeError("game scenarios need --game-exe (a DW4_ONLINE_DEV build) and --data (the game data)")
    src = os.path.join(SCENARIO_DIR, sc["name"])
    out = os.path.join(stack.dir, sc["name"])
    os.makedirs(out, exist_ok=True)
    tag = sc["name"].lower() + secrets.token_hex(2)
    passwords = {}
    with lock:
        for c in sc["clients"]:
            passwords[c["name"]] = "online test " + c["name"] + " " + secrets.token_hex(4)
            s, body = api.call("POST", "/api/v1/auth/register", {"username": tag + c["name"], "password": passwords[c["name"]]})
            if s != 201:
                raise RuntimeError("register %s: %s %s" % (c["name"], s, body))
    https_port = api.base.rsplit(":", 1)[1]
    room_name = sc["name"] + " " + tag
    t0 = time.time()

    def script_for(c):
        # The client's script with every @N replaced by the scenario start + N seconds (the epoch condition: steps
        # of several clients that must happen at set times, such as two departures two seconds apart).
        text = open(os.path.join(src, c["script"]), encoding="utf-8").read()
        text = re.sub(r"@(\d+(?:\.\d+)?)", lambda m: "%.1f" % (t0 + float(m.group(1))), text)

        # {route:NAME} is the waypoint list of routes/NAME.txt, {route:NAME:X,Z} the same up to the waypoint X,Z.
        def route(m):
            lines = open(os.path.join(HERE, "routes", m.group(1) + ".txt"), encoding="utf-8").read().splitlines()
            points = " ".join(l for l in lines if l.strip() and not l.startswith("#")).split()
            return " ".join(points[:points.index(m.group(2)) + 1] if m.group(2) else points)
        text = re.sub(r"\{route:(\w+)(?::(-?\d+,-?\d+))?\}", route, text)
        path = os.path.join(out, c["name"] + ".script")
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        return path

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
        saved = c.get("card", sc.get("card"))
        if saved:
            folder = saved if os.path.isabs(saved) else os.path.join(a.cards, saved)
            if not os.path.isdir(folder):
                raise RuntimeError("no card folder %s (--cards)" % folder)
            argv += ["--online-card", folder]
            env["DW4_CAMPAIGN_LOAD"] = "1"
        return stack.spawn(argv, os.path.join(out, c["name"] + ".log"), env=env)

    clients = sorted(sc["clients"], key=lambda c: c.get("start", 0))
    procs = [(clients[0]["name"], start(clients[0], ["--online-create", room_name]))]
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
    return evaluate_real(sc, src, out, code, diag, timed_out, exits)


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
                    problems, notes = run_real(stack, sc, api, relay, pin, diag, lock)
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
