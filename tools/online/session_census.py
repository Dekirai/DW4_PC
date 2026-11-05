"""Census of the session values for seamless online co-op (docs/SEAMLESS.md 3.7).

Reads the decompiled stage scripts (work/port-research/dec of the main
checkout: gitignored, made from the user's own disc) and lists every
engine-wide *temporary* value a stage script reads or writes:

  - the FLAG indexes gameStart.lua (stagIS10) clears when a character is
    called, minus the story working set it restores from its copies, plus
    the values the story merge never takes (0, 508, 611, 900-937; 503 is
    story since round G, docs/SEAMLESS.md 8.8);
  - COUNTER 0 and 2..6 (COUNTER 1 and 7 are story counters);
  - flag folder 11 (the quest item; folder 0..10 are key items);
  - GATESTAGE name / number (the gate disc's return point).

For each value it records who writes it (hub / UI stage or field stage, the
script kind) and who reads it (load-time scripts - boot, func, start,
first, startnpc - or run-time scripts) in the joinable stages (every stage
except stagML02, stagML11-14 and stagIS*). A value is then classed

  context   written by a hub program (where every member is a solo
            instance) and read by a joinable stage, or written by a hub
            program and by a joinable stage and read by a hub (the quest
            state the lobby reads back, 409 / 424 / 571): two members who
            arrive at the same stage can differ in it, and the stage (or the
            lobby they return to) would behave differently for them, so JOIN
            and co-arrival need equal values;
  personal  GATESTAGE (read only in stagML11, the gate disc's return), and
            every value only hub stages write and no joinable stage reads
            (lobby talks and shops): nothing in a shared instance touches
            them, so they travel with the member (kept aside at a join,
            written back at DISSOLVE) instead of being adopted from the
            instance's leader;
  instance  everything else a joinable stage writes or reads: it belongs to
            the engine line (a fork keeps it on both sides, a joiner adopts
            the instance's).

Computed indexes (FLGset(L0 + 166, ...)) are reported per script, not per
value (they are the dungeon flags 100-299 of the dungeon start scripts).

Usage: python session_census.py <dec folder> [--out session_values.txt] [--report]
"""
from __future__ import annotations

import collections
import re
import sys
from pathlib import Path

# gameStart.lua (stagIS10_m2__gameStart.lua): cleared ranges (r51story flag census).
TEMP = set()
for a, b in [(23, 34), (36, 66), (71, 73), (84, 85), (100, 319), (336, 391), (415, 425), (457, 468),
             (481, 500), (514, 517), (562, 566), (571, 575), (593, 594), (598, 610)]:
    TEMP.update(range(a, b + 1))
TEMP.update([82, 328, 334, 409, 475, 477, 478, 480])
# The story working set gameStart.lua restores from the difficulty copies.
STORY_GROUPS = [(1, 6), (19, 2), (67, 4), (83, 1), (86, 1), (89, 11), (320, 8), (329, 5), (335, 1), (49, 1), (383, 1),
                (82, 1), (334, 1)]
STORY = set()
for first, count in STORY_GROUPS:
    STORY.update(range(first, first + count))
# Never merged although not cleared by gameStart.lua (docs/SEAMLESS.md 8.2).
NEVER_MERGED = {0, 508, 611} | set(range(900, 938))
TEMPORARY_FLAGS = (TEMP - STORY) | NEVER_MERGED
TEMPORARY_COUNTERS = {0, 2, 3, 4, 5, 6}

LOAD_KINDS = ("boot", "func", "start", "first", "startnpc")

FLAG_READ = re.compile(r"be:FLGisOn\((\d+)\)")
FLAG_WRITE = re.compile(r"be:FLGset\((\d+),\s*([^)]*)\)")
FLAG_COMPUTED = re.compile(r"be:FLG(?:isOn|set)\(\(")
COUNTER = re.compile(r"be:COUNTER(set|get)\((\d+)")
FOLDER = re.compile(r'be:SYSctlFlagFolder\("(SET|CLR|IS)",\s*(\d+)\)')
GATE_SET = re.compile(r"be:SYSsetGateStage\(")
GATE_GET = re.compile(r"be:SYSgetGateStage(?:Name|No)\(")


def stage_of(name: str) -> str:
    return re.sub(r"_m\d+__.*", "", name)


def kind_of(name: str) -> str:
    script = re.sub(r"^[^_]+_m\d+__", "", name)[:-4]
    head = re.match(r"[A-Za-z]+", script)
    return head.group(0) if head else script


def script_of(name: str) -> str:
    return re.sub(r"^[^_]+_m\d+__", "", name)[:-4]


def is_hub(stage: str) -> bool:
    s = stage.lower()
    return s in ("stagml02", "stagml11", "stagml12", "stagml13", "stagml14") or s.startswith("stagis")


class Value:
    def __init__(self, key: str):
        self.key = key
        self.writers = collections.defaultdict(set)   # "hub" / "field" -> {stage:script}
        self.readers = collections.defaultdict(set)   # "hub" / "load" / "run" -> {stage:script}

    def cls(self) -> str:
        if self.key.startswith("gatestage"):
            return "personal"
        hub_written = bool(self.writers["hub"])
        field_written = bool(self.writers["field"])
        field_read = bool(self.readers["load"] or self.readers["run"])
        hub_read = bool(self.readers["hub"])
        if hub_written and (field_read or (field_written and hub_read)):
            return "context"
        if field_written or field_read:
            return "instance"
        return "personal"


def census(dec: Path):
    values: dict[str, Value] = {}
    computed = collections.defaultdict(int)

    def value(key: str) -> Value:
        if key not in values:
            values[key] = Value(key)
        return values[key]

    for path in sorted(dec.glob("*.lua")):
        text = path.read_text(encoding="utf-8", errors="replace")
        stage, kind, script = stage_of(path.name), kind_of(path.name), script_of(path.name)
        hub = is_hub(stage)
        where_read = "hub" if hub else ("load" if kind in LOAD_KINDS else "run")
        where_write = "hub" if hub else "field"
        tag = f"{stage}:{script}"
        for m in FLAG_READ.finditer(text):
            f = int(m.group(1))
            if f in TEMPORARY_FLAGS:
                value(f"flag {f}").readers[where_read].add(tag)
        for m in FLAG_WRITE.finditer(text):
            f = int(m.group(1))
            if f in TEMPORARY_FLAGS:
                value(f"flag {f}").writers[where_write].add(tag)
        computed[tag] += len(FLAG_COMPUTED.findall(text))
        for m in COUNTER.finditer(text):
            c = int(m.group(2))
            if c not in TEMPORARY_COUNTERS:
                continue
            v = value(f"counter {c}")
            (v.writers[where_write] if m.group(1) == "set" else v.readers[where_read]).add(tag)
        for m in FOLDER.finditer(text):
            if int(m.group(2)) != 11:
                continue
            v = value("folder 11")
            (v.readers[where_read] if m.group(1) == "IS" else v.writers[where_write]).add(tag)
        if GATE_SET.search(text):
            value("gatestage").writers[where_write].add(tag)
        if GATE_GET.search(text):
            value("gatestage").readers[where_read].add(tag)
    return values, {k: n for k, n in computed.items() if n}


def sort_key(key: str):
    kind, _, number = key.partition(" ")
    order = {"flag": 0, "counter": 1, "folder": 2, "gatestage": 3}[kind]
    return (order, int(number) if number.isdigit() else 0)


def stages(tags) -> str:
    return " ".join(sorted({t.split(":")[0] for t in tags}))


def write_table(values: dict[str, Value], out: Path) -> None:
    lines = [
        "# Session values of seamless online co-op (docs/SEAMLESS.md 3.7).",
        "# Generated by tools/online/session_census.py from the decompiled stage scripts;",
        "# checked in. One value per line: <class> <kind> <index>, then the stages",
        "# that write it (w:), the joinable stages that read it at load time (l:)",
        "# or at run time (r:) and the hub / UI stages that read it (h:).",
        "#   context  - hashed into Engine::session_context_hash (JOIN / co-arrival only",
        "#              between equal hashes)",
        "#   personal - travels with the member (PlayerCarry::personal), written back at DISSOLVE",
        "#   instance - belongs to the engine line (forks keep it, joiners adopt it)",
    ]
    for key in sorted(values, key=sort_key):
        v = values[key]
        writers = v.writers["hub"] | v.writers["field"]
        lines.append(f"{v.cls():8} {key:12} w: {stages(writers) or '-'} | l: {stages(v.readers['load']) or '-'} | "
                     f"r: {stages(v.readers['run']) or '-'} | h: {stages(v.readers['hub']) or '-'}")
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    dec = Path(sys.argv[1])
    values, computed = census(dec)
    if "--out" in sys.argv:
        write_table(values, Path(sys.argv[sys.argv.index("--out") + 1]))
    by_class = collections.defaultdict(list)
    for key in sorted(values, key=sort_key):
        by_class[values[key].cls()].append(key)
    for c in ("context", "personal", "instance"):
        print(f"{c}: {len(by_class[c])}")
        if c == "context" or "--report" in sys.argv:
            for key in by_class[c]:
                v = values[key]
                print(f"  {key:12} hub writers: {' '.join(sorted(v.writers['hub'])) or '-'}")
                print(f"  {'':12} field writers: {stages(v.writers['field']) or '-'}")
                print(f"  {'':12} load readers: {' '.join(sorted(v.readers['load'])) or '-'}")
                print(f"  {'':12} run readers: {stages(v.readers['run']) or '-'}")
                print(f"  {'':12} hub readers: {stages(v.readers['hub']) or '-'}")
    print(f"computed flag indexes in {len(computed)} scripts: " + " ".join(sorted({t.split(':')[0] for t in computed})))
    return 0


if __name__ == "__main__":
    sys.exit(main())
