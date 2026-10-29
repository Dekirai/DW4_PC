#!/usr/bin/env python3
"""Summary of the smooth motion blend diagnostics of a dev dump (DW4_DEV_DUMP, src/game/dev_present.hpp).

    python tools/blend_report.py DUMP_DIR [--from-frame N] [--to-frame M] [--reasons jump,swap,far,...] [--top 30]

Reads DUMP_DIR/presents.csv and blend_NNNNNN.txt (written for every dumped present that was blended;
DW4_DEV_PICTURES=0 writes only these, fast). Groups the flagged draw items by reason, node name and
texture and prints how often and at which 60 Hz steps (frames) they were flagged:
  wrap   a transform sequence above the item wrapped or stepped, or its clock went back
         (DrawItem::phase / sequence_time): drawn as it is (blended, the stagML11 ring walls turned
         backwards for a step every 240 steps: a blink at > 60 presents per second)
  jump   blended across a change far larger than the step before that no sequence marked (a snap
         turn or an animation change drawn over one step; look for an unmarked discontinuity)
  swap   blended from another occurrence of the same placement, node and geometry; since the
         placements have their own keys (DrawItem::instance) only repeats within one placement
         remain. 'near' swaps (< 40 units, so blended) slide the item toward another occurrence's
         place for one step, 'far' ones are drawn as they are
  far    too far from the previous step: drawn as it is (warps, sudden turns)
  texture_jump  a texture matrix that changed but could not be blended while its model was (scroll
                wrapped, atlas step); items drawn as they are (wrap, far) keep their texture matrix unflagged
  palette_*  skinning palettes that could not be blended (palette_far: bones drawn as they are)
"""
import argparse
import collections
import csv
import os
import re


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump")
    ap.add_argument("--from-frame", type=int, default=None)
    ap.add_argument("--to-frame", type=int, default=None)
    ap.add_argument("--reasons", default="wrap,jump,swap,texture_jump,palette_far,palette_resized,palette_shared")
    ap.add_argument("--top", type=int, default=30)
    args = ap.parse_args()
    reasons = set(args.reasons.split(","))
    rows = {int(r["present"]): r for r in csv.DictReader(open(os.path.join(args.dump, "presents.csv")))}
    blended = sum(1 for r in rows.values() if r["blended"] == "1")
    camera_off = [int(r["frame"]) for r in rows.values() if r["blended"] == "1" and r.get("camera") == "0"]
    count = collections.Counter()
    frames = collections.defaultdict(set)
    example = {}
    for name in sorted(os.listdir(args.dump)):
        m = re.match(r"blend_(\d+)\.txt$", name)
        if not m:
            continue
        p = int(m.group(1))
        frame = int(rows[p]["frame"]) if p in rows else -1
        if args.from_frame is not None and frame < args.from_frame:
            continue
        if args.to_frame is not None and frame > args.to_frame:
            continue
        lines = [l.rstrip("\n").split("\t") for l in open(os.path.join(args.dump, name)) if not l.startswith("#")]
        far_items = {(c[8], c[9], c[5], c[6], c[7]) for c in lines if c[0] == "far"}
        for c in lines:
            reason = c[0]
            if reason not in reasons:
                continue
            if reason == "swap":
                reason = "swap(far)" if (c[8], c[9], c[5], c[6], c[7]) in far_items else "swap(near)"
            key = (reason, c[2], c[4])
            count[key] += 1
            frames[key].add(frame)
            example.setdefault(key, c)
    print(f"{args.dump}: {len(rows)} presents, {blended} blended, camera not blended in {len(set(camera_off))} steps")
    for key, n in count.most_common(args.top):
        c = example[key]
        fs = sorted(frames[key])
        gaps = sorted({b - a for a, b in zip(fs, fs[1:])})
        turn = f" turn {c[10]} (before {c[11]})" if len(c) > 11 else ""
        print(f"{n:5d} presents {len(fs):4d} steps  {key[0]:<11} node={key[1]} tex={key[2]}  at ({c[5]}, {c[6]}, {c[7]})"
              f" moved {c[1]}{turn}")
        print(f"            steps {fs[:16]}{' ...' if len(fs) > 16 else ''}" + (f"  gaps {gaps[:6]}" if gaps else ""))


if __name__ == "__main__":
    main()
