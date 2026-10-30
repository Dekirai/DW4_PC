"""The canonical snapshot check of seamless online co-op (docs/SEAMLESS.md 6.8).

Compares the stage snapshots that test runs dumped with DW4_SNAPSHOT_DUMP
(<serial>_<stage>.raw and .canon per stage load) between runs of the same
session in different presentation modes (headless, with the audio mixer,
in a hidden window). For every stage load in all runs:

  - the canonical bytes must be equal;
  - the raw bytes may differ, but only where a run's raw bytes differ from
    its own canonical bytes (the presentation-only fields the canonical form
    zeroes, today the streams' frames_read).

Usage: python canonical_check.py <dump dir> <dump dir> [...]
Exit code 0 when every load passes.
"""
from __future__ import annotations

import sys
from pathlib import Path


def presentation_positions(raw: bytes, canon: bytes) -> set[int]:
    if len(raw) != len(canon):
        return set(range(max(len(raw), len(canon))))
    return {i for i, (a, b) in enumerate(zip(raw, canon)) if a != b}


def main() -> int:
    dirs = [Path(d) for d in sys.argv[1:]]
    if len(dirs) < 2:
        print(__doc__)
        return 2
    names = [sorted(p.stem for p in d.glob("*.canon")) for d in dirs]
    common = sorted(set(names[0]).intersection(*names[1:]))
    failures = 0
    raw_equal = raw_presentation = 0
    differing_bytes = 0
    for n, d in zip(names, dirs):
        if n != names[0]:
            print(f"{d}: {len(n)} loads, the first run {len(names[0])} (only the common ones are compared)")
    for stem in common:
        canons = [(d / f"{stem}.canon").read_bytes() for d in dirs]
        raws = [(d / f"{stem}.raw").read_bytes() for d in dirs]
        if any(c != canons[0] for c in canons[1:]):
            failures += 1
            print(f"{stem}: canonical bytes differ")
            continue
        allowed = set()
        for raw, canon in zip(raws, canons):
            allowed |= presentation_positions(raw, canon)
        if all(r == raws[0] for r in raws[1:]):
            raw_equal += 1
            continue
        bad = False
        for r in raws[1:]:
            if len(r) != len(raws[0]):
                bad = True
                break
            diff = {i for i, (a, b) in enumerate(zip(r, raws[0])) if a != b}
            differing_bytes += len(diff)
            if not diff <= allowed:
                bad = True
        if bad:
            failures += 1
            print(f"{stem}: raw bytes differ outside the presentation fields")
        else:
            raw_presentation += 1
    print(f"{len(common)} stage loads in {len(dirs)} runs: canonical equal {len(common) - failures if not failures else len(common) - failures}, "
          f"raw equal {raw_equal}, raw differing only in presentation fields {raw_presentation} ({differing_bytes} bytes), "
          f"failures {failures}")
    return 0 if failures == 0 and common else 1


if __name__ == "__main__":
    sys.exit(main())
