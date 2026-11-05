"""Census of the room story for seamless online co-op (docs/SEAMLESS.md 8).

Reads the decompiled stage scripts (work/port-research/dec of the main
checkout: gitignored, made from the user's own disc) and writes the story sets
one data file shares between the C++ client and the C# server
(data/online/story_sets.json), plus the C++ tables the engine compiles
(src/engine/online_story_sets.inc; a ctest compares the two).

The story of a record is BASE_FLAG (1024 flags), BASE_FLAGFOLDER (12 key
items) and BASE_COUNTER (16 counters). Its parts:

  phase       flags 1-6 and 86 (game cleared): one-hot, the furthest wins
              (scripts in stagML02 / stagML11 / stagBS06 step it, clearing
              the step before);
  working     the story working set gameStart.lua (stagIS10) restores from
              the save room's per-difficulty copies (stagIS12 start.lua):
              the phase plus 19-20, 67-70, 83, 89-99, 320-327, 329-333,
              335, 49, 383, 82, 334, and the key items 0-10 (copies 518 /
              530 / 542 + key); the copies are derived from the working set
              of the room's difficulty and are never taken from a member;
  persistent  the non-phase working flags plus every flag a script sets to 1
              for good: not cleared by gameStart.lua, never cleared by a
              script (outside the restore scripts and the allowed clears
              below), not one of the derived values (0, 508, 611, 900-937):
              OR. Flag 503 ("has been to the training field", set by
              stag0001's first.lua, read only by the lobby gate) is
              persistent since round G (docs/SEAMLESS.md 8.8); its one clear,
              stag0001's restart.lua before *Retry*'s NEXTSTAGE stag0001, is
              the retry's, and a barrier into stag0001 leaves it alone;
  keys        flag folder 0-10 (key items, picked up through the engine's
              item code): OR; folder 11 is the quest item (a session value).
              The key setter stages (round G): the stages whose scripts set a
              key with a constant SYSctlFlagFolder SET (K2, K3 in stagML02),
              and for the dropped keys the stages where the r25c campaign
              picked them up (MEASURED_KEY_STAGES); the server refuses a key
              from any other stage;
  counters    COUNTER 1 (junk shop donations: the engine's JUNK program,
              subwnd_shop.cpp, opened only by stagML13's junkshop.lua) and
              COUNTER 7 (stag0001's attempts, gene03.lua): additive, per
              engine line (docs/SEAMLESS.md 8.3);
  never       everything else a script writes: gameStart.lua's temporaries,
              the derived and non-monotone flags, folder 11, COUNTER 0 and
              2-6 (session values, 3.7).

It also writes the setter table for the server's plausibility check (8.4):
for every phase and persistent flag the stages whose scripts set it to 1
(constant indexes, and FLGset(SYSgetDifficulty() + N, 1) as N..N+2), the
stages that change the two counters and the key setter stages; and the
training proof of the rebase (round G, 8.8): the persistent flags whose
setter stages all lie outside stagml02, stagml11 and stag0001 (a story that
holds one went beyond the training, so 503 is set for it).

Usage: python story_census.py <dec folder> [--json data/online/story_sets.json]
       [--cpp src/engine/online_story_sets.inc] [--check]   (--check: compare
       with the files instead of writing them; exit 1 on a difference)
"""
from __future__ import annotations

import collections
import json
import re
import sys
from pathlib import Path

# gameStart.lua (stagIS10_m2__gameStart.lua): the cleared ranges (r51story flag census).
TEMP = set()
for a, b in [(23, 34), (36, 66), (71, 73), (84, 85), (100, 319), (336, 391), (415, 425), (457, 468),
             (481, 500), (514, 517), (562, 566), (571, 575), (593, 594), (598, 610)]:
    TEMP.update(range(a, b + 1))
TEMP.update([82, 328, 334, 409, 475, 477, 478, 480])

PHASE = [1, 2, 3, 4, 5, 6, 86]
# (working first, count, normal copy, hard copy, very hard copy): gameStart.lua / stagIS12 start.lua.
WORKING_GROUPS = [
    (1, 6, 700, 7, 13),
    (19, 2, 706, 741, 771),
    (67, 4, 708, 74, 78),
    (83, 1, 712, 743, 773),
    (86, 1, 713, 87, 88),
    (89, 11, 714, 744, 774),
    (320, 8, 725, 755, 785),
    (329, 5, 733, 763, 793),
    (335, 1, 738, 768, 798),
    (49, 1, 801, 802, 803),
    (383, 1, 804, 805, 806),
    (82, 1, 807, 808, 809),
    (334, 1, 810, 811, 812),
]
KEY_COUNT = 11                  # flag folder 0..10 (gameStart.lua restores these)
KEY_COPIES = [518, 530, 542]    # stagIS12 start.lua copies folder 0..11 to base + key per difficulty
QUEST_ITEM = 11                 # folder 11: a session value (3.7); its copies 529 / 541 / 553 are never read
# Derived at each lobby load or not monotone (a script clears them): never merged (8.2). (503 was here until round G,
# which made every guest of a phase-2 room play the training: docs/SEAMLESS.md 8.8.)
DERIVED = {0, 508, 611} | set(range(900, 938))
# The clears a persistent flag may have (the census stops on any other): 503's is the retry's (restart.lua clears it
# right before its NEXTSTAGE stag0001, so the intro plays again; the barrier into stag0001 leaves 503 alone).
ALLOWED_CLEARS = {503: {"stag0001_m1__restart.lua"}}
# The training flag and the stages a story may reach before and with the training (the rebase's proof, 8.8): the
# training stag0001, the title (stagIS*), every lobby stage (stagML*: ML02 / ML11 and the rooms ML12 / ML13 / ML14
# behind their doors; the moves from ML11 into ML12 / ML13, matz00 / matz01, check no phase, and their scripts set
# flags such as 35 (stagML13 n_metete_00, no condition) or 405 (stagML12 n_prmame_00, every phase but 3) in phase 1
# and 2) and the quest dungeons (dngQ*: quests are taken at the lobby's desk, stagML11 bML01013; that none is open
# before the training is not established). Review of round G-core: with only ML02 / ML11 / stag0001 an owner who
# talked to such a lobby NPC before his training got 503 at his next room start and he and his guests never played
# it. Old stories keep their proof in the phase (3 and up), COUNTER 7 (the training's) and the keys.
TRAINING_FLAG = 503
TRAINING_STAGE = "stag0001"
BEFORE_TRAINING_PREFIXES = ("stagis", "stagml", "dngq")
# The key items no script sets with a constant: drops of item table 2 (impItem, items.cpp pick_item), picked up in the
# r25c campaign's segments (work/campaign runs/seg/*/<segment>.log "picked up <name> ID" after "stage change -> <stage>";
# the names map to the keys by the impItem table's row order - Blosso, Mammothmon, Boat Key, Ophanimon ID, SkullGrey,
# Scorpio, Shogun, MR-04, Diaboro, MR-05, LordKnight, Offering - and the checkpoints' key sets agree: k2 0-2, p4 0-3,
# k5 0-5, k7 0-7, k8 0-8, k9 0-9, k10 0-10). A key not listed here and set by no script keeps the old rule (no stage
# check) and is named in the output.
MEASURED_KEY_STAGES = {
    0: ["dng101b2"],   # Blosso ID (segment new)
    1: ["dng102b3"],   # Mammothmon ID (segment new)
    4: ["dng204b2"],   # SkullGrey ID (segment p4)
    5: ["dng205b3"],   # Scorpio ID (segment p4)
    6: ["dng307b2"],   # Shogun ID (segment p5)
    7: ["dng308b3"],   # MR-04 ID (segment p5)
    8: ["dng309b4"],   # Diaboro ID (segment k7)
    9: ["dng411b3"],   # MR-05 ID (segment p6)
    10: ["dng412b4"],  # LordKnight ID (segment k9)
}
FOLDER_SET = re.compile(r'be:SYSctlFlagFolder\("SET",\s*(\d+)\)')
ADDITIVE_COUNTERS = [1, 7]
SESSION_COUNTERS = [0, 2, 3, 4, 5, 6]
# COUNTER 1 changes only in the engine's junk shop program (subwnd_shop.cpp ShopJunk), which a script opens with
# SUBWND GENERATE "JUNK/..." (found below); COUNTER 7 through COUNTERset in scripts.
JUNK_OPEN = re.compile(r'"SUBWND",\s*nil,\s*\d+,\s*"GENERATE",\s*\("JUNK/')

RESTORE_SCRIPTS = ("stagIS10_m2__gameStart.lua", "stagIS12_m1__start.lua")
WRITE = re.compile(r"be:FLGset\((\d+),\s*([^)]*)\)")
WRITE_BY_DIFFICULTY = re.compile(r"be:FLGset\(\(be:SYSgetDifficulty\(\)\s*\+\s*(\d+)\),\s*1\)")
COUNTER_SET = re.compile(r"be:COUNTERset\((\d+),")


def working_flags() -> set[int]:
    out = set()
    for first, count, *_ in WORKING_GROUPS:
        out.update(range(first, first + count))
    return out


def copy_flags() -> set[int]:
    out = set()
    for first, count, *copies in WORKING_GROUPS:
        for c in copies:
            out.update(range(c, c + count))
    for base in KEY_COPIES:
        out.update(range(base, base + KEY_COUNT + 1))
    return out


def stage_of(name: str) -> str:
    return re.sub(r"_m\d+__.*", "", name).lower()


def census(dec: Path) -> dict:
    files = sorted(dec.glob("*.lua"))
    if len(files) < 1000:
        raise SystemExit(f"{dec}: only {len(files)} scripts (the decompiled stage scripts are expected)")
    setters: dict[int, set[str]] = collections.defaultdict(set)
    clearers: dict[int, set[str]] = collections.defaultdict(set)
    other_writes: dict[int, set[str]] = collections.defaultdict(set)
    counter_setters: dict[int, set[str]] = collections.defaultdict(set)
    key_setters: dict[int, set[str]] = collections.defaultdict(set)
    for path in files:
        if path.name in RESTORE_SCRIPTS:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        stage = stage_of(path.name)
        for m in WRITE.finditer(text):
            f, value = int(m.group(1)), m.group(2).strip()
            if value == "1":
                setters[f].add(stage)
            elif value == "0":
                clearers[f].add(path.name)
            else:
                other_writes[f].add(path.name)
        for m in WRITE_BY_DIFFICULTY.finditer(text):
            base = int(m.group(1))
            for f in range(base, base + 3):
                setters[f].add(stage)
        for m in COUNTER_SET.finditer(text):
            counter_setters[int(m.group(1))].add(stage)
        if JUNK_OPEN.search(text):
            counter_setters[1].add(stage)
        for m in FOLDER_SET.finditer(text):
            key_setters[int(m.group(1))].add(stage)

    working = working_flags()
    copies = copy_flags()
    phase = set(PHASE)
    persistent = set(working - phase)
    for f in setters:
        if f in phase or f in copies or f in DERIVED:
            continue
        if f in TEMP and f not in working:
            continue
        persistent.add(f)
    # A persistent flag must never be cleared by a script (else OR would bring it back); the phase flags are cleared by
    # the phase steps on purpose.
    cleared = {f: sorted(set(clearers[f]) - ALLOWED_CLEARS.get(f, set())) for f in sorted(persistent) if clearers.get(f)}
    cleared = {f: c for f, c in cleared.items() if c}
    for f, allowed in ALLOWED_CLEARS.items():
        if f not in persistent or set(clearers.get(f, set())) != allowed:
            raise SystemExit(f"allowed clear of flag {f} no longer matches the scripts: {sorted(clearers.get(f, set()))}")
    if cleared:
        raise SystemExit(f"persistent flags cleared by scripts (not monotone): {cleared}")
    written_other = {f: sorted(other_writes[f]) for f in sorted(persistent | phase) if other_writes.get(f)}
    if written_other:
        raise SystemExit(f"story flags written with computed values: {written_other}")
    never_flags = sorted((TEMP - working) | DERIVED | {b + QUEST_ITEM for b in KEY_COPIES})
    unknown_counters = sorted(set(counter_setters) - set(ADDITIVE_COUNTERS) - set(SESSION_COUNTERS))
    if unknown_counters:
        raise SystemExit(f"counters changed by scripts that are neither story nor session values: {unknown_counters}")

    keys: dict[int, list[str]] = {}
    for k in range(KEY_COUNT):
        keys[k] = sorted(set(key_setters.get(k, set())) | set(MEASURED_KEY_STAGES.get(k, [])))
    unmeasured = [k for k in range(KEY_COUNT) if not keys[k]]
    if TRAINING_FLAG not in persistent:
        raise SystemExit(f"flag {TRAINING_FLAG} is not persistent")
    stages = {stage_of(path.name) for path in files}
    training_stages = {TRAINING_STAGE} | {s for s in stages if s.startswith(BEFORE_TRAINING_PREFIXES)}
    training_proof = sorted(f for f in persistent if setters.get(f) and not (setters[f] & training_stages))

    groups = []
    for first, count, *cps in WORKING_GROUPS:
        if first in phase:
            continue
        groups.append({"first": first, "count": count, "copies": cps})
    phase_copies = []
    for d in range(3):
        row = []
        for f in PHASE:
            for first, count, *cps in WORKING_GROUPS:
                if first <= f < first + count:
                    row.append(cps[d] + f - first)
        phase_copies.append(row)
    return {
        "format": "dw4-story-sets/1",
        "generated": "tools/online/story_census.py from the 7,168 decompiled stage scripts (docs/SEAMLESS.md 8.2)",
        "phase": PHASE,
        "phaseCopies": phase_copies,
        "working": groups,
        "persistent": sorted(persistent),
        "keys": KEY_COUNT,
        "keyCopies": KEY_COPIES,
        "counters": ADDITIVE_COUNTERS,
        "never": {"flags": never_flags, "folder": [QUEST_ITEM], "counters": SESSION_COUNTERS},
        "training": {"flag": TRAINING_FLAG, "stages": sorted(training_stages), "proof": training_proof},
        "setters": {
            "flags": {str(f): sorted(setters.get(f, set())) for f in sorted(persistent | phase)},
            "counters": {str(c): sorted(counter_setters.get(c, set())) for c in ADDITIVE_COUNTERS},
            "keys": {str(k): keys[k] for k in range(KEY_COUNT)},
        },
        "_unmeasured_keys": unmeasured,
    }


def to_json(sets: dict) -> str:
    # One list per line, readable in a diff.
    lines = ["{"]
    keys = [k for k in sets if not k.startswith("_")]
    for i, k in enumerate(keys):
        end = "," if i + 1 < len(keys) else ""
        v = sets[k]
        if k == "setters":
            lines.append(f'  "{k}": {{')
            parts = ("flags", "counters", "keys")
            for j, part in enumerate(parts):
                lines.append(f'    "{part}": {{')
                items = list(v[part].items())
                for n, (key, stages) in enumerate(items):
                    lines.append(f'      "{key}": {json.dumps(stages)}' + ("," if n + 1 < len(items) else ""))
                lines.append("    }" + ("," if j + 1 < len(parts) else ""))
            lines.append("  }" + end)
        elif k == "working":
            lines.append(f'  "{k}": [')
            for n, g in enumerate(v):
                lines.append(f"    {json.dumps(g)}" + ("," if n + 1 < len(v) else ""))
            lines.append("  ]" + end)
        else:
            lines.append(f'  "{k}": {json.dumps(v)}{end}')
    lines.append("}")
    return "\n".join(lines) + "\n"


def to_cpp(sets: dict) -> str:
    def row(values) -> str:
        return ", ".join(str(v) for v in values)

    out = [
        "// The story sets of docs/SEAMLESS.md 8.2, generated by tools/online/story_census.py",
        "// from the decompiled stage scripts (the same data as data/online/story_sets.json,",
        "// which the C# server reads; a ctest compares the two). Do not edit by hand.",
        "// Included by src/engine/online_story.cpp inside namespace dw4::engine::story_tables.",
        f"inline constexpr std::array<std::int32_t, {len(sets['phase'])}> kPhase{{{row(sets['phase'])}}};",
        f"inline constexpr std::array<std::array<std::int32_t, {len(sets['phase'])}>, 3> kPhaseCopies{{{{",
    ]
    for r in sets["phaseCopies"]:
        out.append(f"    {{{row(r)}}},")
    out.append("}};")
    out.append(f"inline constexpr std::array<WorkingGroup, {len(sets['working'])}> kWorking{{{{")
    for g in sets["working"]:
        out.append(f"    {{{g['first']}, {g['count']}, {{{row(g['copies'])}}}}},")
    out.append("}};")
    persistent = sets["persistent"]
    out.append(f"inline constexpr std::array<std::int32_t, {len(persistent)}> kPersistent{{")
    for i in range(0, len(persistent), 16):
        out.append("    " + row(persistent[i:i + 16]) + ",")
    out.append("};")
    proof = sets["training"]["proof"]
    out.append(f"inline constexpr std::int32_t kTrainingFlag = {sets['training']['flag']};")
    out.append(f"inline constexpr std::array<std::int32_t, {len(proof)}> kTrainingProof{{")
    for i in range(0, len(proof), 16):
        out.append("    " + row(proof[i:i + 16]) + ",")
    out.append("};")
    out.append(f"inline constexpr std::int32_t kKeys = {sets['keys']};")
    out.append(f"inline constexpr std::array<std::int32_t, 3> kKeyCopies{{{row(sets['keyCopies'])}}};")
    out.append(f"inline constexpr std::array<std::int32_t, {len(sets['counters'])}> kCounters{{{row(sets['counters'])}}};")
    return "\n".join(out) + "\n"


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    sets = census(Path(sys.argv[1]))
    outputs = []
    if "--json" in sys.argv:
        outputs.append((Path(sys.argv[sys.argv.index("--json") + 1]), to_json(sets)))
    if "--cpp" in sys.argv:
        outputs.append((Path(sys.argv[sys.argv.index("--cpp") + 1]), to_cpp(sets)))
    check = "--check" in sys.argv
    status = 0
    for path, text in outputs:
        if check:
            same = path.exists() and path.read_text(encoding="utf-8") == text
            print(f"{path}: {'equal' if same else 'DIFFERS'}")
            status |= 0 if same else 1
        else:
            path.write_text(text, encoding="utf-8", newline="\n")
            print(f"wrote {path}")
    setters = sets["setters"]["flags"]
    single = sum(1 for s in setters.values() if len(s) == 1)
    print(f"phase {len(sets['phase'])}, persistent {len(sets['persistent'])} (one setter stage: {single} of {len(setters)} "
          f"phase and persistent flags; none: {sum(1 for s in setters.values() if not s)}), keys {sets['keys']}, "
          f"counters {sets['counters']} changed in {sets['setters']['counters']}, never {len(sets['never']['flags'])} flags")
    print(f"key setter stages {sets['setters']['keys']}; without a measured stage (no check): {sets['_unmeasured_keys'] or 'none'}; "
          f"training proof {len(sets['training']['proof'])} flags")
    return status


if __name__ == "__main__":
    raise SystemExit(main())
