"""The online character on the local stack (SEAMLESS.md 9, 16: S15 and the character checks of round S2).

Starts the private MariaDB and the DW4Online server of run.py (its Stack, certificate and API helpers), then hidden
DW4_ONLINE_DEV game clients that log in, open a room each and play their pad from a script (--online-script):

  C1  a new account (no online character: 404, the original new game flow): the campaign bot makes a new character
      at the title; the first stage change creates the online character (If-None-Match: *), the save room writes it
      (the campaign's checkpoint p2: card_written -> upload with If-Match), the story goes on and the member leaves
      the room (the automatic save when leaving). GET /online-character returns the last saved record and the
      client's own save store (--card-dir) stays untouched.
  C2  S15: an account whose online character exists (a campaign checkpoint's record, uploaded through the API): the
      client builds the virtual card from it and calls it at the title (DW4_CAMPAIGN_LOAD), plays the story; after its
      first upload "another PC" saves the character (a PUT through the API); the client's next upload gets 409, shows
      the message, keeps its card cache in backups/, reloads the server's character and never overwrites it.
  P1  F1 Saves > Cloud backup outside rooms (no room, the panel's own login through DW4_MENU_CLOUD): an existing
      backup (another PC's, uploaded through the API) is shown, the local save (a GCI folder card) is uploaded over
      it (the revision rises, the SHA-256 is the local file's), a kept revision made the latest again (API) is
      downloaded into the local save (the old one copied to backups/ first); a picture of the panel at 1920x1009
      (widescreen, MSAA 4) is kept in the run folder.

    python character_test.py [C1 C2 P1] --game-exe EXE --data DATA --cards CHECKPOINTS
                             [--mysql-port 3455] [--ports 54250-54299] [--keep] [--no-build]

The clients run hidden (--smoke-test, DW4_NO_ERROR_BOX=1, below normal priority); network traffic stays on loopback.
It stops only the processes it started.
"""
import argparse
import base64
import os
import re
import secrets
import shutil
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run as stack_tools  # noqa: E402  (the stack of run.py)

GCI_ENTRY, COPIES, RECORD_SIZE = 64, (0x2000, 0xC000), 3316


def record_of(gci_path, box=0):
    """Box `box` of the first data copy whose checksum and version hold (save_data.cpp)."""
    data = open(gci_path, "rb").read()
    for copy in COPIES:
        base = GCI_ENTRY + copy
        block = data[base:base + 40960]
        words = [int.from_bytes(block[i:i + 4], "big") for i in range(4, 40960, 4)]
        if int.from_bytes(block[4:8], "big") == 4 and int.from_bytes(block[0:4], "big") == sum(words) & 0xFFFFFFFF:
            at = 8 + box * 0x2000
            return block[at:at + RECORD_SIZE]
    raise RuntimeError("no valid data copy in " + gci_path)


def put_character(api, token, record, revision, play_seconds):
    """PUT /online-character through the API (another PC): If-None-Match: * for revision 0."""
    import json
    import urllib.error
    import urllib.request
    body = json.dumps({"record": base64.b64encode(record).decode(), "playSeconds": play_seconds,
                       "context": {"players": 1, "protocol": 44, "clientVersion": "0.56.0"}}).encode()
    req = urllib.request.Request(api.base + "/api/v1/online-character", data=body, method="PUT")
    req.add_header("Content-Type", "application/json")
    req.add_header("Authorization", "Bearer " + token)
    if revision:
        req.add_header("If-Match", '"%d"' % revision)
    else:
        req.add_header("If-None-Match", "*")
    try:
        with urllib.request.urlopen(req, timeout=15, context=api.ctx) as r:
            return r.status, json.loads(r.read() or b"null")
    except urllib.error.HTTPError as e:
        raw = e.read()
        return e.code, json.loads(raw) if raw else None


def register(api, name):
    password = "character test " + secrets.token_hex(6)
    s, body = api.call("POST", "/api/v1/auth/register", {"username": name, "password": password})
    if s != 201:
        raise RuntimeError("register %s: %s %s" % (name, s, body))
    return password, body["token"]


def start_client(st, args, out, name, account, password, https_port, pin, script, frames, env_extra):
    card = os.path.join(out, "own_card_" + name)  # the player's own saves: must stay untouched
    for sub in ("Card A", "Card B"):
        os.makedirs(os.path.join(card, sub), exist_ok=True)
    path = os.path.join(out, name + ".script")
    with open(path, "w", encoding="utf-8") as f:
        f.write(script)
    env = dict(os.environ, DW4_NO_ERROR_BOX="1", DW4_ONLINE_SERVER="127.0.0.1:" + https_port, DW4_ONLINE_PIN=pin,
               DW4_ONLINE_PASSWORD=password, DW4_ONLINE_TRACE="1")
    env.update(env_extra)
    argv = [args.game_exe, "--data", args.data, "--smoke-test", str(frames), "--card-dir", card, "--online-login", account,
            "--online-script", path, "--online-create", "%s %s" % (name, secrets.token_hex(2))]
    return st.spawn(argv, os.path.join(out, name + ".log"), env=env), card


def log_lines(path):
    return open(path, encoding="utf-8", errors="replace").read().splitlines() if os.path.exists(path) else []


def character_lines(lines):
    return [l[len("online-character: "):] for l in lines if l.startswith("online-character: ")]


def card_folder(args, account):
    """saves/online/<account> beside the game executable (online_card_folder)."""
    return os.path.join(os.path.dirname(os.path.abspath(args.game_exe)), "saves", "online", re.sub(r"[^a-z0-9_-]", "_", account.lower()))


def common_checks(problems, lines, own_card, exit_code):
    if exit_code not in (0, None):
        problems.append("the client exited with %s" % exit_code)
    if not any(l.startswith("online-script: done") for l in lines):
        problems.append("the script did not finish")
    for l in lines:
        if l.startswith("error:") or l.startswith("online-script: FAIL") or l.startswith("online-trace: close"):
            problems.append(l)
    for sub in ("Card A", "Card B"):
        if os.listdir(os.path.join(own_card, sub)):
            problems.append("the player's own card %s was written: %s" % (sub, os.listdir(os.path.join(own_card, sub))))


def run_c1(st, args, api, https_port, pin, out):
    """A new character: created by the first stage change, saved in the save room, saved when leaving."""
    account = "c1" + secrets.token_hex(3)
    password, token = register(api, account)
    script = "\n".join([
        "# C1: a new game from the title; the campaign's checkpoint p2 saves in the save room (stagIS12)",
        "campaign until stage=stagis12",
        "log in the save room",
        "campaign until stage=stagml12,control",
        "log saved, back in the lobby",
        "campaign until frames>=%d" % args.c1_more,
        "log leaving the room",
        "idle until frames>=30",
    ]) + "\n"
    env = {"DW4_CAMPAIGN_CHECKPOINTS": os.path.join(out, "c1_checkpoints"), "DW4_CAMPAIGN_CHECKPOINT_LIST": "p2"}
    proc, own = start_client(st, args, out, "c1", account, password, https_port, pin, script, args.c1_frames, env)
    proc.wait(args.c1_frames / 59.94 + 300)
    lines = log_lines(os.path.join(out, "c1.log"))
    ch = character_lines(lines)
    problems, notes = [], []
    common_checks(problems, lines, own, proc.poll())
    if not any(l.startswith("enter without a character") for l in ch):
        problems.append("the first entry did not start without a character (404)")
    saves = [re.match(r"saved \((.+)\) revision (\d+)", l) for l in ch]
    saves = [(m.group(1), int(m.group(2))) for m in saves if m]
    notes.append("uploads: " + ", ".join("%s r%d" % s for s in saves))
    reasons = [r for r, _ in saves]
    if not saves or saves[0][0] != "stage change" or saves[0][1] != 1:
        problems.append("the first stage change did not create the character as revision 1")
    if "save room" not in reasons:
        problems.append("no upload from the save room (card_written)")
    if "leaving the room" not in reasons:
        problems.append("no automatic save when leaving the room")
    if reasons and reasons[-1] != "leaving the room":
        problems.append("the last upload is not the leaving save")
    if any(l.startswith("conflict") or l.startswith("refused") for l in ch):
        problems.append("an upload was refused: " + "; ".join(l for l in ch if l.startswith(("conflict", "refused"))))
    s, body = api.call("GET", "/api/v1/online-character", token=token)
    if s != 200:
        problems.append("GET /online-character answered %s" % s)
    else:
        if saves and body["revision"] != saves[-1][1]:
            problems.append("the server has revision %s, the client saved %s last" % (body["revision"], saves[-1][1]))
        cache = os.path.join(card_folder(args, account), "online_card.dat")
        state = open(os.path.join(card_folder(args, account), "online_card.state")).read() if os.path.exists(cache) else ""
        if "pending=0" not in state:
            problems.append("the card cache still has a pending save: " + state.replace("\n", " "))
        box = int(re.search(r"box=(\d)", state).group(1)) if "box=" in state else 0
        if os.path.exists(cache) and base64.b64decode(body["record"]) != record_of(cache, box):
            problems.append("the server's record differs from the card cache's")
        summary = body.get("summary") or {}
        notes.append("server: revision %s, %s Lv %s, play %s s" % (body["revision"], summary.get("digimon"), summary.get("level"),
                                                                 body.get("playSeconds")))
    s, history = api.call("GET", "/api/v1/online-character/history", token=token)
    if s == 200:
        notes.append("history: %d revisions" % len(history))
    return problems, notes


def run_c2(st, args, api, https_port, pin, out):
    """S15: a called character from the server, then another PC saves it: 409, message, reload, no overwrite."""
    account = "c2" + secrets.token_hex(3)
    password, token = register(api, account)
    gci = os.path.join(args.cards, args.c2_card, "Card A", "B2-GDJE-NDMWSaveData.gci")
    record = record_of(gci, 0)
    s, body = put_character(api, token, record, 0, 3600)
    if s != 201:
        raise RuntimeError("PUT the starting character: %s %s" % (s, body))
    script = "\n".join([
        "# C2: call the server's character at the title, play the story; another PC saves meanwhile",
        "campaign until stage=stagml11,control",
        "log in the lobby",
        "campaign until frames>=%d" % args.c2_frames_play,
        "log leaving the room",
        "idle until frames>=30",
    ]) + "\n"
    proc, own = start_client(st, args, out, "c2", account, password, https_port, pin, script, args.c2_frames, {"DW4_CAMPAIGN_LOAD": "1"})
    log = os.path.join(out, "c2.log")
    other_revision = None
    deadline = time.time() + args.c2_frames / 59.94 + 300
    while proc.poll() is None and time.time() < deadline:
        if other_revision is None:
            saved = [re.match(r"saved \((.+)\) revision (\d+)", l) for l in character_lines(log_lines(log))]
            saved = [m for m in saved if m]
            if saved:
                # Another PC saves the character now (the server's current record, a little richer).
                s, cur = api.call("GET", "/api/v1/online-character", token=token)
                rec = bytearray(base64.b64decode(cur["record"]))
                bits = int.from_bytes(rec[96:100], "big") + 7
                rec[96:100] = bits.to_bytes(4, "big")
                s, put = put_character(api, token, bytes(rec), cur["revision"], cur["playSeconds"] + 5)
                if s != 200:
                    raise RuntimeError("the second PC's PUT: %s %s" % (s, put))
                other_revision = put["revision"]
                print("       C2: another PC saved revision %d after the client's revision %s" % (other_revision, saved[-1].group(2)), flush=True)
        time.sleep(0.5)
    if proc.poll() is None:
        proc.wait(60)
    lines = log_lines(log)
    ch = character_lines(lines)
    problems, notes = [], []
    common_checks(problems, lines, own, proc.poll())
    if not any(l.startswith("enter revision 1 ") for l in ch):
        problems.append("the entry did not build the card from revision 1")
    if not any("campaign: game began" in l for l in lines):
        notes.append("(no 'game began' line from the campaign)")
    if other_revision is None:
        problems.append("the client never uploaded, so the second PC never saved")
    else:
        conflict = [l for l in ch if l.startswith("conflict")]
        if not conflict:
            problems.append("no 409 after the second PC's save (the client did not upload again?)")
        if not any(l.startswith("notice Your online character was saved on another PC") for l in ch):
            problems.append("the player was not told about the conflict")
        if not any(l.startswith("reloaded revision %d" % other_revision) for l in ch):
            problems.append("the server's revision %d was not reloaded" % other_revision)
        s, body = api.call("GET", "/api/v1/online-character", token=token)
        if s != 200 or body["revision"] != other_revision:
            problems.append("the server's character was overwritten: revision %s, expected %s" % (body.get("revision") if body else s,
                                                                                                  other_revision))
        else:
            cache = os.path.join(card_folder(args, account), "online_card.dat")
            if base64.b64decode(body["record"]) != record_of(cache, 0):
                problems.append("the card cache does not hold the reloaded character")
        backups = os.path.join(card_folder(args, account), "backups")
        if not (os.path.isdir(backups) and os.listdir(backups)):
            problems.append("the local card cache was not kept in backups/")
        blocked = [l for l in ch if "not uploaded: blocked" in l]
        notes.append("409 handled; %d later save(s) not uploaded (blocked)" % len(blocked))
    notes.append("character lines: " + " | ".join(ch[:14]))
    return problems, notes


def cloud_put(api, token, data, revision):
    import hashlib
    import json
    import urllib.error
    import urllib.request
    req = urllib.request.Request(api.base + "/api/v1/cloud-save", data=data, method="PUT")
    req.add_header("Content-Type", "application/octet-stream")
    req.add_header("Authorization", "Bearer " + token)
    req.add_header("X-Sha256", hashlib.sha256(data).hexdigest())
    req.add_header("X-Device-Name", "other-pc")
    if revision:
        req.add_header("If-Match", '"%d"' % revision)
    else:
        req.add_header("If-None-Match", "*")
    try:
        with urllib.request.urlopen(req, timeout=15, context=api.ctx) as r:
            return r.status, json.loads(r.read() or b"null")
    except urllib.error.HTTPError as e:
        raw = e.read()
        return e.code, json.loads(raw) if raw else None


def panel_run(st, args, out, name, https_port, pin, cloud, card, frames, dump):
    """A hidden client without a room: the F1 Saves tab open, the cloud panel logs in and runs its test action."""
    pictures = os.path.join(out, name + "_pictures")
    os.makedirs(pictures, exist_ok=True)
    env = dict(os.environ, DW4_NO_ERROR_BOX="1", DW4_ONLINE_SERVER="127.0.0.1:" + https_port, DW4_ONLINE_PIN=pin, DW4_OPEN_MENU="1",
               DW4_MENU_TAB="Saves", DW4_MENU_CLOUD=cloud, DW4_RENDER="1", DW4_DEV_PRESENT="60", DW4_DEV_WINDOW="1920x1009",
               DW4_DEV_WIDESCREEN="1", DW4_DEV_MSAA="4", DW4_DEV_DUMP="%d-%d" % (dump, dump), DW4_DEV_DUMP_DIR=pictures)
    argv = [args.game_exe, "--data", args.data, "--smoke-test", str(frames), "--card-dir", card]
    proc = st.spawn(argv, os.path.join(out, name + ".log"), env=env)
    proc.wait(600)
    return proc.poll(), pictures


def run_p1(st, args, api, https_port, pin, out):
    """The cloud backup panel: show, upload over another PC's backup, download a restored revision."""
    import hashlib
    account = "p1" + secrets.token_hex(3)
    password, token = register(api, account)
    other = open(os.path.join(args.cards, "k10", "Card A", "B2-GDJE-NDMWSaveData.gci"), "rb").read()
    mine = open(os.path.join(args.cards, args.c2_card, "Card A", "B2-GDJE-NDMWSaveData.gci"), "rb").read()
    s, body = cloud_put(api, token, other, 0)
    if s != 201:
        raise RuntimeError("the other PC's cloud backup: %s %s" % (s, body))
    card = os.path.join(out, "local_card")
    os.makedirs(os.path.join(card, "Card A"), exist_ok=True)
    os.makedirs(os.path.join(card, "Card B"), exist_ok=True)
    local = os.path.join(card, "Card A", "B2-GDJE-NDMWSaveData.gci")
    open(local, "wb").write(mine)
    problems, notes = [], []
    code, pictures = panel_run(st, args, out, "upload", https_port, pin, "%s:%s:upload" % (account, password), card, 2400, 2390)
    lines = log_lines(os.path.join(out, "upload.log"))
    if code not in (0, None):
        problems.append("the upload run exited with %s" % code)
    if not any(l.startswith("cloud backup test: upload") for l in lines):
        problems.append("the panel did not run its upload (login or overview failed?)")
    s, meta = api.call("GET", "/api/v1/cloud-save/meta", token=token)
    if s != 200 or meta["latest"]["revision"] != 2 or meta["latest"]["sha256"].lower() != hashlib.sha256(mine).hexdigest():
        problems.append("the cloud backup is not this PC's save as revision 2: %s %s" % (s, meta and meta.get("latest")))
    shots = sorted(f for f in os.listdir(pictures) if f.endswith(".png"))
    notes.append("upload run: %s" % (", ".join(shots) or "no picture"))
    # Another PC makes revision 1 (k10) the latest again; this PC downloads it.
    s, _ = api.call("POST", "/api/v1/cloud-save/restore/1", token=token)
    if s != 200:
        problems.append("restore through the API answered %s" % s)
    code, pictures2 = panel_run(st, args, out, "download", https_port, pin, "%s:%s:download" % (account, password), card, 2400, 2390)
    if code not in (0, None):
        problems.append("the download run exited with %s" % code)
    if open(local, "rb").read() != other:
        problems.append("the local save is not the downloaded revision")
    backups = os.path.join(card, "backups")
    kept = [f for f in os.listdir(backups)] if os.path.isdir(backups) else []
    if not any(open(os.path.join(backups, f), "rb").read() == mine for f in kept):
        problems.append("the replaced local save was not copied to backups/ (%s)" % kept)
    notes.append("download run: local save = revision 3 (k10), backups %s" % kept)
    notes.append("pictures: %s, %s" % (pictures, pictures2))
    return problems, notes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenarios", nargs="*")
    ap.add_argument("--game-exe", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--cards", default=os.environ.get("DW4_ONLINE_TEST_CARDS", ""))
    ap.add_argument("--mysql-port", type=int, default=3455)
    ap.add_argument("--ports", default="54250-54299")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--c1-frames", type=int, default=30000)
    ap.add_argument("--c1-more", type=int, default=27000, help="C1: the frame the story play ends and the member leaves")
    ap.add_argument("--c2-card", default="k2")
    ap.add_argument("--c2-frames", type=int, default=16000)
    ap.add_argument("--c2-frames-play", type=int, default=14000)
    args = ap.parse_args()
    args.impair = ""
    wanted = args.scenarios or ["C1", "C2", "P1"]
    if ("C2" in wanted or "P1" in wanted) and not args.cards:
        print("C2 needs --cards (a campaign checkpoint set such as work/campaign/checkpoints/r25c)")
        return 2
    if not args.no_build:
        stack_tools.build()
    st = stack_tools.Stack(args)
    failed = 0
    try:
        connection = stack_tools.start_mysql(st)
        base, relay, pin, der, diag = stack_tools.start_server(st, connection)
        api = stack_tools.Api(base, der)
        https_port = base.rsplit(":", 1)[1]
        print("server %s, relay %d" % (base, relay), flush=True)
        results = {}

        def one(name, fn):
            out = os.path.join(st.dir, name)
            os.makedirs(out, exist_ok=True)
            t = time.time()
            try:
                problems, notes = fn(st, args, api, https_port, pin, out)
            except Exception as e:  # a broken scenario is a failure
                problems, notes = ["%s: %s" % (type(e).__name__, e)], []
            results[name] = (problems, notes, time.time() - t)

        threads = [threading.Thread(target=one, args=(n, {"C1": run_c1, "C2": run_c2, "P1": run_p1}[n])) for n in wanted]
        for th in threads:
            th.start()
        for th in threads:
            th.join()
        for name in wanted:
            problems, notes, seconds = results[name]
            print("%s %s (%.0f s)" % ("PASS" if not problems else "FAIL", name, seconds), flush=True)
            for n in notes:
                print("       " + n, flush=True)
            for p in problems:
                print("     ! " + p, flush=True)
            failed += bool(problems)
    finally:
        st.stop()
        if not args.keep and not failed:
            shutil.rmtree(st.dir, ignore_errors=True)
        else:
            print("run folder: " + st.dir)
    print("%d of %d character scenarios passed" % (len(wanted) - failed, len(wanted)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
