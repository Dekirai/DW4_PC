"""Golden vectors of the room story (docs/SEAMLESS.md 8): data/online/story_golden.json.

A third, independent implementation of the story rules (the C++ one is
src/engine/online_story.cpp, the C# one server/DW4Online/src/DW4Online.Domain/
Story/StoryMerge.cs) computes the expected results of hand-written cases from
data/online/story_sets.json. Both test suites (tests/online_story_tests.cpp,
server/DW4Online/tests/DW4Online.Tests/StoryGoldenTests.cs) read the file and
must give the same results; a change of the rules changes all three.

  merge    a member's delta into the room story at a difficulty, with the
           growth the room already took per engine line;
  pending  an engine's values against the confirmed story;
  rebase   the working set and the keys from the copies of a difficulty;
  apply    a barrier's room story and dropped set into an engine.

Usage: python story_golden.py [--check]   (paths relative to the repository)
"""
from __future__ import annotations

import copy
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SETS = json.loads((ROOT / "data/online/story_sets.json").read_text(encoding="utf-8"))
OUT = ROOT / "data/online/story_golden.json"

PHASE = SETS["phase"]
PERSISTENT = set(SETS["persistent"])
KEYS = SETS["keys"]
COUNTERS = SETS["counters"]

# Refusal reasons and kinds (online_story.hpp StoryRefusal / StoryRefused::Kind).
NOT_STORY, LIMIT, PHASE_JUMP = 7, 8, 4
K_PHASE, K_FLAG, K_FOLDER, K_COUNTER = 0, 1, 2, 3


def room(flags=(), folder=(), counters=(), revision=0) -> dict:
    return {"revision": revision, "flags": sorted(set(flags)), "folder": sorted(set(folder)), "counters": sorted(counters)}


def to_arrays(r: dict):
    f = [0] * 1024
    for i in r["flags"]:
        f[i] = 1
    k = [0] * 12
    for i in r["folder"]:
        k[i] = 1
    c = [0] * 16
    for i, v in r["counters"]:
        c[i] = v
    return f, k, c


def from_arrays(f, k, c, revision) -> dict:
    return room([i for i in range(1024) if f[i]], [i for i in range(12) if k[i]],
                [[i, c[i]] for i in range(16) if c[i]], revision)


def phase_of(flags_get) -> int:
    for i in range(len(PHASE), 0, -1):
        if flags_get(PHASE[i - 1]):
            return i
    return 0


def derive(f, k, d):
    for i, p in enumerate(PHASE):
        f[SETS["phaseCopies"][d][i]] = 1 if f[p] else 0
    for g in SETS["working"]:
        for j in range(g["count"]):
            f[g["copies"][d] + j] = 1 if f[g["first"] + j] else 0
    for key in range(KEYS):
        f[SETS["keyCopies"][d] + key] = 1 if k[key] else 0


def rebase(r: dict, d: int) -> dict:
    f, k, c = to_arrays(r)
    for i, p in enumerate(PHASE):
        f[p] = 1 if f[SETS["phaseCopies"][d][i]] else 0
    for g in SETS["working"]:
        for j in range(g["count"]):
            f[g["first"] + j] = 1 if f[g["copies"][d] + j] else 0
    for key in range(12):
        k[key] = 1 if key < KEYS and f[SETS["keyCopies"][d] + key] else 0
    return from_arrays(f, k, c, r["revision"])


def merge(r: dict, delta: dict, d: int, lines: list) -> tuple[dict, list, bool, list]:
    f, k, c = to_arrays(r)
    taken = {(a, b, e): g for a, b, e, g in lines}
    refused, changed = [], False
    p = delta.get("phase", 0)
    if p:
        pr = phase_of(lambda x: f[x])
        if p > len(PHASE):
            refused.append([K_PHASE, p, NOT_STORY])
        elif p > pr:
            if p <= pr + 1:
                for i, flag in enumerate(PHASE):
                    f[flag] = 1 if i + 1 == p else 0
                changed = True
            else:
                refused.append([K_PHASE, p, PHASE_JUMP])
    seen = set()
    for x in delta.get("flags", []):
        if x in seen:
            continue
        seen.add(x)
        if x not in PERSISTENT:
            refused.append([K_FLAG, x, NOT_STORY])
        elif not f[x]:
            f[x] = 1
            changed = True
    seen = set()
    for x in delta.get("folder", []):
        if x in seen:
            continue
        seen.add(x)
        if not 0 <= x < KEYS:
            refused.append([K_FOLDER, x, NOT_STORY])
        elif not k[x]:
            k[x] = 1
            changed = True
    for counter, inst, visit, growth in delta.get("counters", []):
        if counter not in COUNTERS:
            refused.append([K_COUNTER, counter, NOT_STORY])
            continue
        if growth < 0:
            refused.append([K_COUNTER, counter, LIMIT])
            continue
        key = (counter, inst, visit)
        before = taken.get(key, 0)
        if growth <= before:
            taken.setdefault(key, before)
            continue
        c[counter] = min(c[counter] + growth - before, 2**31 - 1)
        taken[key] = growth
        changed = True
    revision = r["revision"]
    if changed:
        derive(f, k, d)
        revision += 1
    out_lines = sorted([a, b, e, g] for (a, b, e), g in taken.items())
    return from_arrays(f, k, c, revision), refused, changed, out_lines


def pending(confirmed: dict, engine: dict) -> dict:
    f, k, _ = to_arrays(confirmed)
    ef = dict((a, b) for a, b in engine["flags"])
    ek = dict((a, b) for a, b in engine["folder"])
    out = {"phase": 0, "flags": [], "folder": []}
    pe = phase_of(lambda x: ef.get(x, 0))
    if pe > phase_of(lambda x: f[x]):
        out["phase"] = pe
    out["flags"] = sorted(x for x, v in ef.items() if v and x in PERSISTENT and not f[x])
    out["folder"] = sorted(x for x, v in ek.items() if v and 0 <= x < KEYS and not k[x])
    return out


def apply(r: dict, dropped: dict, engine: dict) -> tuple[dict, bool]:
    f, k, c = to_arrays(r)
    flags = dict((a, b) for a, b in engine["flags"])
    folder = dict((a, b) for a, b in engine["folder"])
    counters = dict((a, b) for a, b in engine["counters"])
    removed = False

    def put(m, key, value):
        if key not in m:
            if value == 0:
                return False
            m[key] = value
            return True
        if m[key] == value:
            return False
        m[key] = value
        return True

    pe = phase_of(lambda x: flags.get(x, 0))
    pr = phase_of(lambda x: f[x])
    target = max(pe, pr)
    if pe > pr and PHASE[pe - 1] in dropped["flags"]:
        target = pr
        removed = True
    if target != pe:
        for i, p in enumerate(PHASE):
            put(flags, p, 1 if i + 1 == target else 0)
    for x in sorted(PERSISTENT):
        if f[x] and not flags.get(x, 0):
            put(flags, x, f[x])
    for x in sorted(set(dropped["flags"])):
        if x in PERSISTENT and not f[x] and put(flags, x, 0):
            removed = True
    for x in range(KEYS):
        if k[x] and not folder.get(x, 0):
            put(folder, x, k[x])
    for x in sorted(set(dropped["folder"])):
        if 0 <= x < KEYS and not k[x] and put(folder, x, 0):
            removed = True
    for x in COUNTERS:
        rv, ev = c[x], counters.get(x, 0)
        if x in dropped["counters"]:
            if ev > rv:
                removed = True
            put(counters, x, rv)
        elif rv > ev:
            put(counters, x, rv)
    out = {"flags": sorted([a, b] for a, b in flags.items()), "folder": sorted([a, b] for a, b in folder.items()),
           "counters": sorted([a, b] for a, b in counters.items())}
    return out, removed


def engine(flags=(), folder=(), counters=()) -> dict:
    return {"flags": sorted([list(x) for x in flags]), "folder": sorted([list(x) for x in folder]),
            "counters": sorted([list(x) for x in counters])}


def keys(flags=(), folder=(), counters=()) -> dict:
    return {"flags": list(flags), "folder": list(folder), "counters": list(counters)}


MERGE_CASES = [
    ("first phase into an empty room", 0, room(), [], {"phase": 1}),
    ("one phase step (normal copies follow, one-hot)", 0, room([2, 701], revision=4), [], {"phase": 3}),
    ("a phase jump is refused", 0, room([2, 701], revision=4), [], {"phase": 4}),
    ("a phase below the room's changes nothing", 0, room([5, 704], revision=9), [], {"phase": 3}),
    ("game cleared after phase 6", 0, room([6, 705], revision=2), [], {"phase": 7}),
    ("phase on very hard", 2, room(), [], {"phase": 1}),
    ("persistent flags OR, never-merged ones refused", 0, room([392], revision=1), [],
     {"flags": [19, 22, 392, 23, 0, 99, 900, 508, 19]}),
    ("hard copies of the working set and the keys", 1, room(), [], {"flags": [67, 89, 334, 426], "folder": [0, 3]}),
    ("keys OR, the quest item refused", 0, room(folder=[1]), [], {"folder": [2, 11, 2, 1]}),
    ("copies are never taken from a member", 0, room(), [], {"flags": [700, 706, 518, 7]}),
    ("counter growth per engine line", 0, room(counters=[[1, 1000]], revision=3), [[1, 4, 2, 300]],
     {"counters": [[1, 4, 2, 500], [1, 9, 1, 50], [7, 4, 2, 1], [3, 4, 2, 5]]}),
    ("a resend changes nothing", 0, room([19, 706], counters=[[7, 2]], revision=7), [[7, 4, 2, 2]],
     {"flags": [19], "counters": [[7, 4, 2, 2], [7, 4, 2, 1]]}),
    ("a negative growth is refused", 0, room(), [], {"counters": [[7, 1, 1, -3]]}),
    ("an unknown phase index is refused", 0, room(), [], {"phase": 9}),
]

PENDING_CASES = [
    ("phase, persistent flags and keys beyond the confirmed story", room([2, 392]),
     engine([(3, 1), (2, 0), (19, 1), (22, 0), (23, 1), (700, 1), (392, 1), (99, 1)], [(0, 1), (11, 1), (4, 0)])),
    ("nothing beyond: no claim", room([5, 19, 392], [0, 1]), engine([(3, 1), (19, 1), (392, 0)], [(0, 1), (1, 1)])),
    ("game cleared", room([6]), engine([(86, 1), (6, 0)])),
]

REBASE_CASES = [
    ("hard from its copies", 1, room([3, 702, 19, 706, 8, 742, 518, 532, 404], [0, 11])),
    ("normal from its copies", 0, room([3, 702, 19, 706, 8, 742, 518, 532, 404], [0, 11])),
    ("an empty difficulty clears the working set", 2, room([5, 704, 89, 714, 520, 405], [2])),
]

APPLY_CASES = [
    ("the room's phase, flags, keys and counters reach the engine", room([3, 19, 392], [1], [[1, 500]]), keys(),
     engine([(2, 1), (1, 0), (20, 0)], [], [(7, 2)])),
    ("a pending phase and flag above the room stay", room([3, 19]), keys(), engine([(4, 1), (393, 1)], [(2, 1)], [(1, 50)])),
    ("dropped values go back to the room's", room([3, 19], [1], [[7, 1]]), keys([4, 392], [5], [7]),
     engine([(4, 1), (392, 1), (19, 1)], [(5, 1), (1, 1)], [(7, 3)])),
    ("a dropped value the engine lacks changes nothing", room([3]), keys([395], [6], []), engine([(3, 1), (395, 0)])),
    ("a dropped phase the room has stays", room([4]), keys([4]), engine([(4, 1)])),
    ("only keys whose value changes are touched", room([19, 20, 22]), keys(), engine([(19, 1), (20, 0)])),
]


def build() -> dict:
    out = {"format": "dw4-story-golden/1", "generated": "tools/online/story_golden.py from data/online/story_sets.json",
           "merge": [], "pending": [], "rebase": [], "apply": []}
    for name, d, r, lines, delta in MERGE_CASES:
        full = {"phase": delta.get("phase", 0), "flags": delta.get("flags", []), "folder": delta.get("folder", []),
                "counters": delta.get("counters", [])}
        result, refused, changed, out_lines = merge(copy.deepcopy(r), full, d, lines)
        out["merge"].append({"name": name, "difficulty": d, "room": r, "lines": lines, "delta": full,
                             "expect": {"room": result, "refused": refused, "changed": changed, "lines": out_lines}})
    for name, confirmed, eng in PENDING_CASES:
        out["pending"].append({"name": name, "confirmed": confirmed, "engine": eng, "expect": pending(confirmed, eng)})
    for name, d, r in REBASE_CASES:
        out["rebase"].append({"name": name, "difficulty": d, "room": r, "expect": rebase(r, d)})
    for name, r, dropped, eng in APPLY_CASES:
        result, removed = apply(r, dropped, eng)
        out["apply"].append({"name": name, "room": r, "dropped": dropped, "engine": eng,
                             "expect": {"engine": result, "removed": removed}})
    return out


def to_text(data: dict) -> str:
    lines = ["{", f'  "format": {json.dumps(data["format"])},', f'  "generated": {json.dumps(data["generated"])},']
    parts = ["merge", "pending", "rebase", "apply"]
    for n, part in enumerate(parts):
        lines.append(f'  "{part}": [')
        for i, case in enumerate(data[part]):
            lines.append("    " + json.dumps(case, separators=(", ", ": ")) + ("," if i + 1 < len(data[part]) else ""))
        lines.append("  ]" + ("," if n + 1 < len(parts) else ""))
    lines.append("}")
    return "\n".join(lines) + "\n"


def main() -> int:
    text = to_text(build())
    if "--check" in sys.argv:
        same = OUT.exists() and OUT.read_text(encoding="utf-8") == text
        print(f"{OUT}: {'equal' if same else 'DIFFERS'}")
        return 0 if same else 1
    OUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
