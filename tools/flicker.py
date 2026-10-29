#!/usr/bin/env python3
"""Temporal flicker detector for the dev picture dumps of dw4_game (DW4_DEV_DUMP, src/game/dev_present.hpp).

    python tools/flicker.py DUMP_DIR [--out DIR] [--thr 32] [--tol 10] [--max-len 3] [--block 16]
                            [--mode pulse|reversal|static] [--from-frame N] [--to-frame M] [--top 12]

DUMP_DIR holds present_NNNNNN.png (every presented picture), presents.csv and, with DW4_DEV_IDS=1,
ids_NNNNNN.png + items_NNNNNN.txt (the draw item that wrote each pixel).

Modes (events are counted per pixel and present):
  pulse     (default) a pixel leaves its value for 1..--max-len presents and comes back: the presents
            t..t+m-1 all differ from the one before (t-1) by at least --thr (0..255 luminance) in the
            same direction while the one after (t+m) is within --tol of t-1. A surface sliding past
            (motion, interpolated or not) seldom comes back to the same value that fast; something that
            blinks for a picture or a 60 Hz step (2-3 presents at 144 Hz) does.
            With item-id pictures also "owner pulses": the pixel's draw item (stable key: node and
            geometry address and occurrence, not the list index) is another one for 1..max-len
            presents and then the first one again, with a luminance change of at least --thr / 2:
            z-fighting, an item blinking out, draw order swaps.
  reversal  luminance goes up then down (or down then up) by at least --thr over t-k, t, t+k for
            k = 1..max-len (catches moving textures too: use it with a static scene).
  static    every change of at least --thr between consecutive presents (nothing should move).

Output in --out (default DUMP_DIR/flicker[_mode]):
  heat.png       the middle picture darkened with the event counts on top (yellow..red), regions boxed
  heat_raw.png   the event counts alone (white = most)
  report.txt     totals, the busiest presents, the hot regions (blocks of --block px joined) with the
                 draw items owning their event pixels: at the event and the present before it
                 ('A -> B' = another item covered the pixel during the event)
  events.csv     present, frame, steps, alpha, luminance events, owner events
The owner of a pixel is the last item that wrote it; under a blended owner ([B...] flags) the change
can come from an item below it. Confirm a suspect with an A/B run that leaves it out
(DW4_DEV_HIDE=<node or texture name>): its region's events must vanish. Under a moving camera fine
texture detail sliding past also comes back within a few presents; judge such windows against the
same run with DW4_DEV_SMOOTH=0 and with tools/blend_report.py.
Needs numpy and Pillow.
"""
import argparse
import collections
import csv
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw


def luminance(path):
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.int32)
    return ((a[..., 0] * 299 + a[..., 1] * 587 + a[..., 2] * 114) // 1000).astype(np.int16)


def read_ids(path):
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.int32)
    return a[..., 0] | (a[..., 1] << 8) | (a[..., 2] << 16)


class Keys:
    """Stable draw item keys across presents: (node address, geometry address, placement, occurrence) -> int, and a name."""

    def __init__(self):
        self.ids = {}
        self.names = ["(nothing drawn)"]

    def table(self, path):
        """items_NNNNNN.txt -> array list index -> key number (0 = nothing)."""
        rows = [0]
        seen = collections.Counter()
        if os.path.exists(path):
            with open(path, encoding="utf-8", errors="replace") as f:
                for line in f:
                    if line.startswith("#"):
                        continue
                    p = line.rstrip("\n").split("\t")
                    if len(p) < 11:
                        continue
                    index, node, kind, origin, texture, flags = p[:6]
                    # (The placement, "inst ..." in newer dumps, keeps the
                    # instances of a shared node apart as the blend does.)
                    inst = next((c.split()[1] for c in p[11:] if c.startswith("inst ")), "")
                    pair = (p[9], p[10], inst)
                    occurrence = seen[pair]
                    seen[pair] += 1
                    key = (pair, occurrence)
                    if key not in self.ids:
                        self.ids[key] = len(self.names)
                        self.names.append(f"{origin} node={node} {kind} tex={texture} [{flags}]"
                                          + (f" occ{occurrence}" if occurrence else ""))
                    while len(rows) < int(index):
                        rows.append(0)
                    rows.append(self.ids[key])
        return np.array(rows, dtype=np.int64)

    def image(self, folder, present):
        ids = read_ids(os.path.join(folder, "ids_%06d.png" % present))
        table = self.table(os.path.join(folder, "items_%06d.txt" % present))
        ids = np.where(ids < len(table), ids, 0)
        return table[ids]


def regions(block_counts, min_events):
    """Connected 8-neighbour regions of blocks with at least min_events events."""
    hot = block_counts >= min_events
    seen = np.zeros_like(hot)
    out = []
    h, w = hot.shape
    for y in range(h):
        for x in range(w):
            if not hot[y, x] or seen[y, x]:
                continue
            stack = [(y, x)]
            seen[y, x] = True
            cells = []
            while stack:
                cy, cx = stack.pop()
                cells.append((cy, cx))
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        ny, nx = cy + dy, cx + dx
                        if 0 <= ny < h and 0 <= nx < w and hot[ny, nx] and not seen[ny, nx]:
                            seen[ny, nx] = True
                            stack.append((ny, nx))
            ys = [c[0] for c in cells]
            xs = [c[1] for c in cells]
            total = int(sum(block_counts[c] for c in cells))
            out.append({"cells": cells, "y0": min(ys), "y1": max(ys), "x0": min(xs), "x1": max(xs), "events": total})
    out.sort(key=lambda r: -r["events"])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dump")
    ap.add_argument("--out")
    ap.add_argument("--mode", choices=["pulse", "reversal", "static"], default="pulse")
    ap.add_argument("--thr", type=int, default=32)
    ap.add_argument("--tol", type=int, default=10)
    ap.add_argument("--max-len", type=int, default=3)
    ap.add_argument("--block", type=int, default=16)
    ap.add_argument("--from-frame", type=int, default=None)
    ap.add_argument("--to-frame", type=int, default=None)
    ap.add_argument("--top", type=int, default=12)
    ap.add_argument("--no-ids", action="store_true", help="ignore item-id pictures")
    ap.add_argument("--min-region", type=int, default=0, help="events per block for a region (default: auto)")
    args = ap.parse_args()

    out = args.out or os.path.join(args.dump, "flicker" if args.mode == "pulse" else "flicker_" + args.mode)
    os.makedirs(out, exist_ok=True)
    M = args.max_len

    meta = {}
    csv_path = os.path.join(args.dump, "presents.csv")
    if os.path.exists(csv_path):
        with open(csv_path) as f:
            for row in csv.DictReader(f):
                meta[int(row["present"])] = row
    frame_of = lambda p: int(meta[p]["frame"]) if p in meta else -1
    presents = sorted(int(m.group(1)) for m in (re.match(r"present_(\d+)\.png$", n) for n in os.listdir(args.dump)) if m)
    if args.from_frame is not None:
        presents = [p for p in presents if frame_of(p) >= args.from_frame]
    if args.to_frame is not None:
        presents = [p for p in presents if 0 <= frame_of(p) <= args.to_frame]
    need = {"pulse": M + 2, "reversal": 2 * M + 1, "static": 2}[args.mode]
    if len(presents) < need:
        sys.exit("not enough presents")
    use_ids = (not args.no_ids and args.mode == "pulse"
               and os.path.exists(os.path.join(args.dump, "ids_%06d.png" % presents[0])))
    keys = Keys()

    buf = collections.deque()
    counts = None
    owner_counts = None
    lum_per_present, own_per_present = {}, {}
    samples = []  # (y array, x array, key during the event, key before it, weight, owner event?)
    rng = np.random.default_rng(1)

    def sample(ev, k_now, k_before, owner):
        ys, xs = np.nonzero(ev)
        if not len(ys):
            return
        step = max(1, len(ys) // 3000)
        sel = rng.permutation(len(ys))[: len(ys) // step] if step > 1 else slice(None)
        ys, xs = ys[sel], xs[sel]
        if k_now is not None:
            samples.append((ys, xs, k_now[ys, xs], k_before[ys, xs], step, owner))
        else:
            samples.append((ys, xs, None, None, step, owner))

    for p in presents:
        lum = luminance(os.path.join(args.dump, "present_%06d.png" % p))
        key = keys.image(args.dump, p) if use_ids else None
        if buf and buf[-1][0] != p - 1:
            buf.clear()  # another dump range: no events across the gap
        buf.append((p, lum, key))
        if len(buf) > need:
            buf.popleft()
        if len(buf) < need:
            continue
        if counts is None:
            counts = np.zeros(lum.shape, dtype=np.int32)
            owner_counts = np.zeros(lum.shape, dtype=np.int32)
        if args.mode == "static":
            t, lt, _ = buf[-1]
            ev = np.abs(lt - buf[-2][1]) >= args.thr
            lum_per_present[t] = int(ev.sum())
            counts += ev
            continue
        if args.mode == "reversal":
            t, lt, _ = buf[M]
            mask = np.zeros(lt.shape, dtype=bool)
            for k in range(1, M + 1):
                da = lt - buf[M - k][1]
                db = buf[M + k][1] - lt
                mask |= (np.sign(da) * np.sign(db) < 0) & (np.minimum(np.abs(da), np.abs(db)) >= args.thr)
            lum_per_present[t] = int(mask.sum())
            counts += mask
            sample(mask, None, None, False)
            continue
        # pulse: buf[0] = t-1, buf[1..m] = the pulse, buf[m+1] = the return
        t = buf[1][0]
        base = buf[0][1]
        lum_ev = np.zeros(base.shape, dtype=bool)
        own_ev = np.zeros(base.shape, dtype=bool)
        up = np.ones(base.shape, dtype=bool)
        down = np.ones(base.shape, dtype=bool)
        other = np.ones(base.shape, dtype=bool) if use_ids else None
        lum_big = np.ones(base.shape, dtype=bool)
        for m in range(1, M + 1):
            d = buf[m][1] - base
            up &= d >= args.thr
            down &= d <= -args.thr
            back = np.abs(buf[m + 1][1] - base) <= args.tol
            lum_ev |= (up | down) & back
            if use_ids:
                other &= buf[m][2] != buf[0][2]
                lum_big &= np.abs(d) >= args.thr // 2
                own_ev |= other & lum_big & (buf[m + 1][2] == buf[0][2])
            if not (up.any() or down.any() or (use_ids and other.any())):
                break
        # Count each pulse once: at its first present.
        lum_per_present[t] = int(lum_ev.sum())
        counts += lum_ev
        if use_ids:
            own_per_present[t] = int(own_ev.sum())
            owner_counts += own_ev
            sample(lum_ev & ~own_ev, buf[1][2], buf[0][2], False)
            sample(own_ev, buf[1][2], buf[0][2], True)
        else:
            sample(lum_ev, None, None, False)
    if counts is None:
        sys.exit("no events computed")
    total_counts = counts + owner_counts
    h, w = counts.shape
    bs = args.block
    bh, bw = (h + bs - 1) // bs, (w + bs - 1) // bs
    padded = np.zeros((bh * bs, bw * bs), dtype=np.int64)
    padded[:h, :w] = total_counts
    blocks = padded.reshape(bh, bs, bw, bs).sum(axis=(1, 3))
    total = int(counts.sum())
    owner_total = int(owner_counts.sum())
    nz = blocks[blocks > 0]
    min_region = args.min_region or (max(8, int(np.percentile(nz, 90))) if len(nz) else 8)
    regs = regions(blocks, min_region)[: args.top]

    region_of = np.full((bh, bw), -1, dtype=np.int32)
    for i, r in enumerate(regs):
        for c in r["cells"]:
            region_of[c] = i
    owners = [collections.Counter() for _ in regs]
    changes = [collections.Counter() for _ in regs]
    kinds = [collections.Counter() for _ in regs]
    for ys, xs, k_now, k_before, weight, owner in samples:
        reg = region_of[ys // bs, xs // bs]
        for j in range(len(ys)):
            r = reg[j]
            if r < 0:
                continue
            kinds[r]["owner" if owner else "luminance"] += weight
            if k_now is None:
                continue
            a, b = int(k_now[j]), int(k_before[j])
            owners[r][keys.names[a]] += weight
            if a != b:
                changes[r][f"{keys.names[b]}  ->  {keys.names[a]}"] += weight

    mid = presents[len(presents) // 2]
    base_img = np.asarray(Image.open(os.path.join(args.dump, "present_%06d.png" % mid)).convert("RGB"), dtype=np.float32) * 0.35
    top = float(np.percentile(total_counts[total_counts > 0], 99)) if (total_counts > 0).any() else 1.0
    level = np.clip(total_counts / max(1.0, top), 0, 1)
    heat = base_img.copy()
    on = level > 0
    heat[on, 0] = 255
    heat[on, 1] = 255 * (1 - level[on])
    heat[on, 2] = 0
    img = Image.fromarray(heat.astype(np.uint8))
    d = ImageDraw.Draw(img)
    for i, r in enumerate(regs):
        box = (r["x0"] * bs, r["y0"] * bs, (r["x1"] + 1) * bs, (r["y1"] + 1) * bs)
        d.rectangle(box, outline=(0, 255, 255), width=2)
        d.text((box[0] + 3, box[1] + 2), f"R{i}", fill=(0, 255, 255))
    img.save(os.path.join(out, "heat.png"))
    Image.fromarray((level * 255).astype(np.uint8)).save(os.path.join(out, "heat_raw.png"))

    with open(os.path.join(out, "events.csv"), "w") as f:
        f.write("present,frame,steps,alpha,luminance_events,owner_events\n")
        for p in sorted(lum_per_present):
            r = meta.get(p, {})
            f.write(f"{p},{r.get('frame', -1)},{r.get('steps', '')},{r.get('alpha', '')},{lum_per_present[p]},"
                    f"{own_per_present.get(p, 0)}\n")
    n = max(1, len(lum_per_present))
    lines = [f"dump {args.dump}: {len(presents)} presents {presents[0]}..{presents[-1]} "
             f"(frames {frame_of(presents[0])}..{frame_of(presents[-1])}), {w}x{h}, mode {args.mode}, thr {args.thr}, "
             f"tol {args.tol}, max-len {M}" + (", owner keys" if use_ids else "")]
    lines.append(f"luminance events {total} ({total / n:.1f}/present), owner events {owner_total} ({owner_total / n:.1f}/present), "
                 f"pixels with events {int((total_counts > 0).sum())}")
    both = {p: lum_per_present[p] + own_per_present.get(p, 0) for p in lum_per_present}
    burst = sorted(both.items(), key=lambda kv: -kv[1])[:8]
    lines.append("busiest presents: " + ", ".join(f"{p}(f{frame_of(p)}):{v}" for p, v in burst))
    lines.append(f"regions (blocks of {bs} px with >= {min_region} events):")
    for i, r in enumerate(regs):
        kind = ", ".join(f"{k} {v}" for k, v in kinds[i].most_common())
        lines.append(f"R{i}: x {r['x0'] * bs}..{(r['x1'] + 1) * bs} y {r['y0'] * bs}..{(r['y1'] + 1) * bs}  events {r['events']}"
                     f" ({100.0 * r['events'] / max(1, total + owner_total):.1f} %; sampled {kind})")
        tot = sum(owners[i].values()) or 1
        for name, v in owners[i].most_common(4):
            lines.append(f"    {100.0 * v / tot:5.1f} %  {name}")
        ch = sum(changes[i].values())
        if ch:
            lines.append(f"    another item during the event for {100.0 * ch / tot:.1f} %:")
            for name, v in changes[i].most_common(3):
                lines.append(f"      {100.0 * v / tot:5.1f} %  {name}")
    text = "\n".join(lines)
    with open(os.path.join(out, "report.txt"), "w", encoding="utf-8") as f:
        f.write(text + "\n")
    print(text)


if __name__ == "__main__":
    main()
