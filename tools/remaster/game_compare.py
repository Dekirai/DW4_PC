#!/usr/bin/env python3
"""Hidden game runs for texture pack comparisons: screenshots at fixed
frames with and without a pack, side-by-side pictures with zoomed crops,
and the longest frame of a stage start.

  run      one hidden run (no window, no sound, --smoke-test) of an input
           script, screenshots at the given frames as PNG:
             game_compare.py run --exe EXE --data DATA --script work/dref/scripts/newgame_dng101b1.txt \
                 --frames 1990,2600 --out DIR [--pack PACK] [--size 3840x2160] [--length 6000]
  compare  side-by-side pictures of two runs' screenshots (and crops where
           they differ most), and whether their fingerprints agree:
             game_compare.py compare --a DIR --b DIR --out DIR [--label-a ..] [--label-b ..]
           or of more runs (e.g. Original, High, Ultra), the crops where the
           first and the last differ most, each crop at 1:1 in every run:
             game_compare.py compare --run Original=DIR --run High=DIR --run Ultra=DIR --out DIR
  timing   every stage start of the script (DW4_TRACE_LOADTIME's window: the
           load and 600 steps after the start) with the texture figures
           (DW4_TRACE_TEXTURES), --repeat runs each (default 8, in rotating
           order) without a pack, with an empty pack (the control: no
           texture work) and with each pack (--pack DIR or LABEL=DIR, again
           for more; LABEL=DIR@high draws the pack's 2x level, @original
           none). Judged per stage: the passes after the start (p99, the
           longest, the count over 8.3 ms) and the window's longest pass,
           each as median and quartiles over the runs, against the control
           with its noise floor (timing.json). The window's longest pass
           alone is no measure of the textures: it is mostly the load's
           heaviest step and moves by several ms with the machine's load.
             game_compare.py timing --exe EXE --data DATA --script SCRIPT --pack PACK --out DIR

Runs are started at below-normal priority without a console window, in
their own working folder under --out, with a fresh memory card folder;
DW4_NO_ERROR_BOX=1, 16:9 (DW4_WIDESCREEN=1), DW4_TRACE_FINGERPRINT=300 so
two runs can be checked to show the same frames. The pack reaches the game
through DW4_TEXTURE_PACK (src/game/game_main.cpp: test runs read no
settings.ini). Everything is written below --out.
"""
import argparse
import json
import os
import re
import statistics
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rmcommon  # noqa: E402

rmcommon.lower_priority()

import cv2  # noqa: E402
import numpy as np  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

from contact_sheets import font  # noqa: E402

BELOW_NORMAL = 0x00004000
NO_WINDOW = 0x08000000


def start_game(exe, data, script, out, length, size, pack=None, frames=None, extra_env=None):
    os.makedirs(out, exist_ok=True)
    card = os.path.join(out, "card")
    for slot in ("Card A", "Card B"):
        os.makedirs(os.path.join(card, slot), exist_ok=True)
    env = dict(os.environ)
    for key in ("DW4_TEXTURE_PACK", "DW4_DUMP_TEXTURES", "DW4_RENDER"):
        env.pop(key, None)
    env.update({"DW4_NO_ERROR_BOX": "1", "DW4_WINDOW": size, "DW4_WIDESCREEN": "1", "DW4_TRACE_FINGERPRINT": "300"})
    if pack:
        env["DW4_TEXTURE_PACK"] = os.path.abspath(pack)
    env.update(extra_env or {})
    cmd = [exe, "--data", data, "--smoke-test", str(length), "--card-dir", card, "--input", os.path.abspath(script)]
    if frames:
        cmd += ["--screenshot", os.path.join(out, "shot.bmp"), "--screenshot-frames", ",".join(str(f) for f in frames)]
    flags = (BELOW_NORMAL | NO_WINDOW) if os.name == "nt" else 0
    with open(os.path.join(out, "run.log"), "w", encoding="utf-8") as log:
        proc = subprocess.run(cmd, cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, creationflags=flags)
    return proc.returncode, open(os.path.join(out, "run.log"), encoding="utf-8", errors="replace").read()


def fingerprints(text):
    return [line for line in text.splitlines() if "fingerprint" in line]


def cmd_run(args):
    frames = [int(f) for f in args.frames.split(",") if f]
    extra = dict(item.split("=", 1) for item in args.env or [])
    code, text = start_game(args.exe, args.data, args.script, args.out, args.length, args.size, args.pack[-1] if args.pack else None, frames,
                            extra)
    shots = 0
    for f in frames:
        bmp = os.path.join(args.out, f"shot_{f}.bmp")
        if os.path.exists(bmp):
            Image.open(bmp).convert("RGB").save(os.path.join(args.out, f"shot_{f}.png"))
            os.remove(bmp)
            shots += 1
    fp = fingerprints(text)
    print(f"exit {code}, {shots}/{len(frames)} screenshots, {len(fp)} fingerprint lines; {text.strip().splitlines()[-2:]}")
    return 0 if code == 0 and shots == len(frames) else 1


def diff_windows(a, b, ww, wh, count):
    d = np.abs(a.astype(np.int16) - b.astype(np.int16)).sum(axis=2).astype(np.float32)
    d = cv2.GaussianBlur(d, (0, 0), 3)
    win = cv2.boxFilter(d, -1, (ww, wh), normalize=True, borderType=cv2.BORDER_CONSTANT)
    h, w = d.shape
    out = []
    for _ in range(count):
        y, x = np.unravel_index(int(np.argmax(win)), win.shape)
        if win[y, x] <= 0.5:
            break
        x0 = int(np.clip(x - ww // 2, 0, w - ww))
        y0 = int(np.clip(y - wh // 2, 0, h - wh))
        out.append((x0, y0, float(win[y, x])))
        win[max(0, y0 - wh // 2):y0 + wh + wh // 2, max(0, x0 - ww // 2):x0 + ww + ww // 2] = -1
    return out


def cmd_compare_runs(args):
    """Side-by-side pictures of 2 or more runs and crops at 1:1 where the
    first and the last differ most."""
    runs = [item.split("=", 1) for item in args.run]
    os.makedirs(args.out, exist_ok=True)
    logs = [open(os.path.join(d, "run.log"), encoding="utf-8", errors="replace").read() for _, d in runs]
    fps = [fingerprints(t) for t in logs]
    same = len(fps[0]) > 0 and all(f == fps[0] for f in fps[1:])
    print(f"fingerprints: {' / '.join(str(len(f)) for f in fps)} lines, {'identical' if same else 'DIFFERENT'}")
    f_big, f_small = font(30), font(22)
    names = sorted((n for n in os.listdir(runs[0][1]) if re.match(r"shot_\d+\.png$", n)
                    and all(os.path.exists(os.path.join(d, n)) for _, d in runs)), key=lambda n: int(re.findall(r"\d+", n)[0]))
    result = {"fingerprints_identical": same, "runs": [label for label, _ in runs], "frames": {}}
    n = len(runs)
    gap = 16
    for name in names:
        frame = int(re.findall(r"\d+", name)[0])
        imgs = [np.array(Image.open(os.path.join(d, name)).convert("RGB")) for _, d in runs]
        h, w = imgs[0].shape[:2]
        tw = 1280
        th = tw * h // w
        side = Image.new("RGB", (n * tw + (n - 1) * gap, th + 44), (16, 16, 20))
        d = ImageDraw.Draw(side)
        for i, (img, (label, _)) in enumerate(zip(imgs, runs)):
            side.paste(Image.fromarray(img).resize((tw, th), Image.LANCZOS), (i * (tw + gap), 44))
            d.text((i * (tw + gap) + 8, 6), f"{label} - frame {frame} ({w}x{h})", fill=(255, 230, 120), font=f_big)
        side.save(os.path.join(args.out, f"side_{frame}.png"))
        cs = min(args.crop_size, h // 2, w // 3)
        wins = diff_windows(imgs[0], imgs[-1], cs, cs, args.crops)
        sheet = Image.new("RGB", (n * cs + (n - 1) * gap, max(1, len(wins)) * (cs + 36)), (16, 16, 20))
        d = ImageDraw.Draw(sheet)
        for r, (x0, y0, score) in enumerate(wins):
            y = r * (cs + 36)
            for i, (img, (label, _)) in enumerate(zip(imgs, runs)):
                sheet.paste(Image.fromarray(img[y0:y0 + cs, x0:x0 + cs]), (i * (cs + gap), y + 32))
                d.text((i * (cs + gap) + 6, y + 4), f"{label}: {cs}x{cs} at ({x0},{y0}) 1:1", fill=(200, 200, 200), font=f_small)
        sheet.save(os.path.join(args.out, f"crops_{frame}.png"))
        diffs = {}
        for (label, _), img in zip(runs[1:], imgs[1:]):
            dd = np.abs(img.astype(np.int16) - imgs[0].astype(np.int16)).max(axis=2)
            diffs[label] = {"changed_share": round(float((dd > 8).mean()), 4), "mean_abs": round(float(dd.mean()), 3)}
        result["frames"][frame] = {"crops": [(x0, y0) for x0, y0, _ in wins], "against_first": diffs}
        print(f"frame {frame}: side_{frame}.png, crops_{frame}.png ({len(wins)} crops); changed against {runs[0][0]}: "
              + ", ".join(f"{k} {v['changed_share']:.1%}" for k, v in diffs.items()))
    with open(os.path.join(args.out, "compare.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=1)
    return 0


def cmd_compare(args):
    if args.run:
        if len(args.run) < 2:
            sys.exit("compare needs two --run LABEL=DIR at least")
        return cmd_compare_runs(args)
    if not args.a or not args.b:
        sys.exit("compare needs --a and --b, or --run LABEL=DIR twice or more")
    os.makedirs(args.out, exist_ok=True)
    fa = open(os.path.join(args.a, "run.log"), encoding="utf-8", errors="replace").read()
    fb = open(os.path.join(args.b, "run.log"), encoding="utf-8", errors="replace").read()
    same = fingerprints(fa) == fingerprints(fb) and len(fingerprints(fa)) > 0
    print(f"fingerprints: {len(fingerprints(fa))} / {len(fingerprints(fb))} lines, {'identical' if same else 'DIFFERENT'}")
    f_big, f_small = font(34), font(24)
    names = sorted((n for n in os.listdir(args.a) if re.match(r"shot_\d+\.png$", n) and os.path.exists(os.path.join(args.b, n))),
                   key=lambda n: int(re.findall(r"\d+", n)[0]))
    for n in names:
        frame = int(re.findall(r"\d+", n)[0])
        a = np.array(Image.open(os.path.join(args.a, n)).convert("RGB"))
        b = np.array(Image.open(os.path.join(args.b, n)).convert("RGB"))
        h, w = a.shape[:2]
        half_w, half_h = 1920, int(1920 * h / w)
        side = Image.new("RGB", (2 * half_w + 16, half_h + 50), (16, 16, 20))
        d = ImageDraw.Draw(side)
        for i, (img, label) in enumerate(((a, args.label_a), (b, args.label_b))):
            side.paste(Image.fromarray(img).resize((half_w, half_h), Image.LANCZOS), (i * (half_w + 16), 50))
            d.text((i * (half_w + 16) + 10, 6), f"{label} - frame {frame} ({w}x{h}, shrunk to half)", fill=(255, 230, 120), font=f_big)
        side.save(os.path.join(args.out, f"side_{frame}.png"))
        # Crops at full 4K resolution where the two differ most.
        cw, ch = 940, 528
        wins = diff_windows(a, b, cw, ch, args.crops)
        rows = len(wins)
        sheet = Image.new("RGB", (2 * half_w + 16, half_h + 50 + rows * (ch + 44)), (16, 16, 20))
        sheet.paste(side, (0, 0))
        d = ImageDraw.Draw(sheet)
        for r, (x0, y0, score) in enumerate(wins):
            y = half_h + 50 + r * (ch + 44)
            for i, (img, label) in enumerate(((a, args.label_a), (b, args.label_b))):
                crop = Image.fromarray(img[y0:y0 + ch, x0:x0 + cw])
                cx = i * (half_w + 16)
                d.text((cx + 10, y + 6), f"{label}: crop {cw}x{ch} at ({x0},{y0}), 1:1 and 2x", fill=(200, 200, 200), font=f_small)
                # 1:1 on the left half of the column, 2x zoom of its middle on the right.
                sheet.paste(crop, (cx, y + 40))
                mid = img[y0 + ch // 4:y0 + ch // 4 + ch // 2, x0 + cw // 4:x0 + cw // 4 + min(cw // 2, (half_w - cw) // 2)]
                z = Image.fromarray(mid).resize((mid.shape[1] * 2, mid.shape[0] * 2), Image.NEAREST)
                sheet.paste(z, (cx + cw + 8, y + 40))
        sheet.save(os.path.join(args.out, f"compare_{frame}.png"))
        print(f"frame {frame}: side_{frame}.png, compare_{frame}.png ({rows} crops)")
    with open(os.path.join(args.out, "compare.json"), "w", encoding="utf-8") as f:
        json.dump({"fingerprints_identical": same, "frames": [int(re.findall(r'\d+', n)[0]) for n in names]}, f, indent=1)
    return 0


WINDOW_LINE = re.compile(
    r"loadtime window (\S+): longest pass ([\d.]+) ms \(texture work ([\d.]+) ms\).*most texture work in a pass ([\d.]+) ms"
    r"(?:; after the start (\d+) passes: median ([\d.]+) ms, p95 ([\d.]+) ms, p99 ([\d.]+) ms, longest ([\d.]+) ms "
    r"\(texture work ([\d.]+) ms\), (\d+) over 8\.3 ms, (\d+) over 16\.7 ms)?")
# The figures of a stage start window per run (DW4_TRACE_LOADTIME): the
# longest pass of the whole window (mostly the load's heaviest step, which
# moves with the machine's load) and the distribution after the stage start.
WINDOW_KEYS = ("window_longest_ms", "window_texture_ms", "window_texture_max_ms", "after_passes", "after_median_ms", "after_p95_ms",
               "after_p99_ms", "after_longest_ms", "after_longest_texture_ms", "after_over_8ms", "after_over_16ms")
# What the comparison judges (lower is better).
JUDGED = ("after_p99_ms", "after_longest_ms", "after_over_8ms", "window_longest_ms")


def parse_windows(text):
    stages = {}
    for m in WINDOW_LINE.finditer(text):
        values = [float(m.group(i)) if m.group(i) is not None else None for i in range(2, 13)]
        stages[m.group(1)] = dict(zip(WINDOW_KEYS, values))
    return stages


def quartiles(values):
    v = sorted(values)
    if not v:
        return None, None, None
    if len(v) < 4:
        return v[0], statistics.median(v), v[-1]
    q = statistics.quantiles(v, n=4, method="inclusive")
    return q[0], q[1], q[2]


def cmd_timing(args):
    """Stage start timing: --repeat runs of each case in rotating order (no
    pack, the empty pack as the control, each pack); per stage the median
    and the quartiles over the runs of the passes after the stage start
    (p99, longest, passes over 8.3 ms) and of the window's longest pass;
    each case against the control with the noise floor (the spread of the
    control's runs, and how far 'no pack' lies from it: two cases without
    any texture work)."""
    os.makedirs(args.out, exist_ok=True)
    empty = os.path.join(args.out, "empty_pack")
    os.makedirs(empty, exist_ok=True)
    cases = [("no pack", None, {"DW4_RENDER": "1"}), ("empty pack", empty, {})]
    for item in args.pack or []:
        label, _, path = item.rpartition("=") if "=" in item else ("pack", "", item)
        path, _, quality = path.partition("@")
        cases.append((label, path, {"DW4_TEXTURE_QUALITY": quality} if quality else {}))
    extra = dict(item.split("=", 1) for item in args.env or [])
    figures = re.compile(r"^textures (\S+): game \d+ \(([\d.]+) MiB\), pack \d+ \(([\d.]+) MiB\), peak ([\d.]+) MiB.*?"
                         r"longest pass ([\d.]+) ms", re.M)
    result = {label: [] for label, _, _ in cases}
    for rep in range(args.repeat):
        # (A rotating order: a drift of the machine's load spreads over every case.)
        shift = rep % len(cases)
        for label, pack, env in cases[shift:] + cases[:shift]:
            out = os.path.join(args.out, f"{label.replace(' ', '_')}_{rep + 1}")
            env = dict(env, DW4_TRACE_LOADTIME="1", DW4_TRACE_TEXTURES="1", **extra)
            code, text = start_game(args.exe, args.data, args.script, out, args.length, args.size, pack, None, env)
            stages = parse_windows(text)
            for m in figures.finditer(text):
                if m.group(1) in stages:
                    stages[m.group(1)]["texture_peak_mib"] = float(m.group(4))
                    stages[m.group(1)]["longest_upload_pass_ms"] = float(m.group(5))
            entry = {"exit": code, "fingerprints": len(fingerprints(text)), "stages": stages}
            result[label].append(entry)
            s = stages.get(args.stage, {})
            print(f"{label} run {rep + 1}: exit {code}, {args.stage}: after the start p99 {s.get('after_p99_ms')} ms, longest "
                  f"{s.get('after_longest_ms')} ms, over 8.3 ms {s.get('after_over_8ms')}; window longest {s.get('window_longest_ms')} ms",
                  flush=True)
    summary = {"cases": [label for label, _, _ in cases], "control": "empty pack", "repeat": args.repeat, "stages": {}}
    all_stages = sorted({st for entries in result.values() for e in entries for st in e["stages"]})
    shown = JUDGED + ("after_median_ms", "after_p95_ms", "window_texture_max_ms", "texture_peak_mib", "longest_upload_pass_ms")
    for stage in all_stages:
        per_case = {}
        for label, entries in result.items():
            vals = [e["stages"][stage] for e in entries if stage in e["stages"]]
            row = {"runs": len(vals)}
            for key in shown:
                numbers = [v[key] for v in vals if v.get(key) is not None]
                q1, med, q3 = quartiles(numbers)
                row[key] = {"median": med, "q1": q1, "q3": q3, "max": max(numbers) if numbers else None}
            per_case[label] = row
        control = per_case.get("empty pack")
        base = per_case.get("no pack")
        verdicts = {}
        for label, row in per_case.items():
            if label == "empty pack" or not control:
                continue
            verdicts[label] = {}
            for key in JUDGED:
                c, r = control[key], row[key]
                if c["median"] is None or r["median"] is None:
                    continue
                # The noise floor: the control's spread (interquartile range)
                # and the distance between the two cases without texture work.
                noise = max(c["q3"] - c["q1"],
                            abs(base[key]["median"] - c["median"]) if base and base[key]["median"] is not None else 0.0)
                delta = r["median"] - c["median"]
                verdicts[label][key] = {"delta": round(delta, 3), "noise": round(noise, 3),
                                        "verdict": "within noise" if abs(delta) <= noise else ("above" if delta > 0 else "below")}
        summary["stages"][stage] = {"cases": per_case, "against_control": verdicts}
    summary["runs"] = result
    with open(os.path.join(args.out, "timing.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1)

    def fmt(cell):
        return "-" if cell["median"] is None else f"{cell['median']:.2f} ({cell['q1']:.2f}-{cell['q3']:.2f})"

    for stage in all_stages:
        s = summary["stages"][stage]
        print(f"\n{stage}: median (quartiles) over the runs; after = the 600 steps after the stage started")
        print(f"  {'case':14s} {'p99 after':>20s} {'longest after':>20s} {'>8.3 ms after':>20s} {'window longest':>20s}")
        for label, row in s["cases"].items():
            print(f"  {label:14s} {fmt(row['after_p99_ms']):>20s} {fmt(row['after_longest_ms']):>20s} "
                  f"{fmt(row['after_over_8ms']):>20s} {fmt(row['window_longest_ms']):>20s}")
        for label, v in s["against_control"].items():
            print(f"  {label} against the empty pack: "
                  + ", ".join(f"{k} {x['delta']:+.2f} (noise {x['noise']:.2f}, {x['verdict']})" for k, x in v.items()))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("run", "timing"):
        p = sub.add_parser(name)
        p.add_argument("--exe", required=True)
        p.add_argument("--data", required=True)
        p.add_argument("--script", required=True)
        p.add_argument("--out", required=True)
        p.add_argument("--pack", action="append", default=None)
        p.add_argument("--size", default="3840x2160")
        p.add_argument("--length", type=int, default=6000)
    sub.choices["run"].add_argument("--frames", required=True)
    sub.choices["run"].add_argument("--env", action="append", help="VAR=VALUE for the run (Graphics options: DW4_TEXTURE_QUALITY ...)")
    sub.choices["timing"].add_argument("--repeat", type=int, default=8)
    sub.choices["timing"].add_argument("--env", action="append", help="VAR=VALUE for every run (Graphics options ...)")
    sub.choices["timing"].add_argument("--stage", default="dng101b1")
    p = sub.add_parser("compare")
    p.add_argument("--a")
    p.add_argument("--b")
    p.add_argument("--run", action="append", help="LABEL=DIR, two or more (instead of --a / --b)")
    p.add_argument("--crop-size", type=int, default=600, help="crop edge with --run (texels, 1:1)")
    p.add_argument("--out", required=True)
    p.add_argument("--label-a", default="original textures")
    p.add_argument("--label-b", default="texture pack")
    p.add_argument("--crops", type=int, default=2)
    args = ap.parse_args()
    if args.cmd == "timing" and not args.pack:
        ap.error("timing needs --pack")
    return {"run": cmd_run, "compare": cmd_compare, "timing": cmd_timing}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
