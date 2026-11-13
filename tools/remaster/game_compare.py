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
  timing   the longest pass of a stage's load and of its start window (the
           load and 600 steps after it, DW4_TRACE_LOADTIME) with the texture
           figures (DW4_TRACE_TEXTURES), a few runs each without a pack, with
           an empty pack and with each pack (--pack DIR or LABEL=DIR, again
           for more; LABEL=DIR@high draws the pack's 2x level, @original
           none); every other stage the script starts is measured alike
           (timing.json "stages"):
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


def cmd_timing(args):
    os.makedirs(args.out, exist_ok=True)
    empty = os.path.join(args.out, "empty_pack")
    os.makedirs(empty, exist_ok=True)
    cases = [("no pack", None, {"DW4_RENDER": "1"}), ("empty pack", empty, {})]
    for item in args.pack or []:
        label, _, path = item.rpartition("=") if "=" in item else ("pack", "", item)
        path, _, quality = path.partition("@")
        cases.append((label, path, {"DW4_TEXTURE_QUALITY": quality} if quality else {}))
    pattern = re.compile(r"loadtime %s\S*: .*loop ([\d.]+) ms \(longest pass ([\d.]+) ms\)" % re.escape(args.stage))
    window = re.compile(r"loadtime window %s: longest pass ([\d.]+) ms \(texture work ([\d.]+) ms\).*most texture work in a pass ([\d.]+) ms"
                        % re.escape(args.stage))
    all_windows = re.compile(r"loadtime window (\S+): longest pass ([\d.]+) ms \(texture work ([\d.]+) ms\)"
                             r".*most texture work in a pass ([\d.]+) ms")
    figures = re.compile(r"^textures %s: game \d+ \(([\d.]+) MiB\), pack \d+ \(([\d.]+) MiB\), peak ([\d.]+) MiB" % re.escape(args.stage),
                         re.M)
    result = {}
    for rep in range(args.repeat):
        for label, pack, env in cases:
            out = os.path.join(args.out, f"{label.replace(' ', '_')}_{rep + 1}")
            env = dict(env, DW4_TRACE_LOADTIME="2", DW4_TRACE_TEXTURES="1")
            code, text = start_game(args.exe, args.data, args.script, out, args.length, args.size, pack, None, env)
            m = pattern.search(text)
            passes = sorted((float(x) for x in re.findall(r"load pass ([\d.]+) ms", text)), reverse=True)
            w = window.search(text)
            f = figures.search(text)
            stages = {m.group(1): {"window_longest_ms": float(m.group(2)), "window_texture_ms": float(m.group(3)),
                                   "window_texture_max_ms": float(m.group(4))} for m in all_windows.finditer(text)}
            entry = {"stages": stages, "exit": code, "loop_ms": float(m.group(1)) if m else None, "longest_ms": float(m.group(2)) if m else None,
                     "window_longest_ms": float(w.group(1)) if w else None, "window_texture_ms": float(w.group(2)) if w else None,
                     "window_texture_max_ms": float(w.group(3)) if w else None,
                     "texture_peak_mib": float(f.group(3)) if f else None,
                     "top_passes_ms": passes[:3], "fingerprints": len(fingerprints(text))}
            result.setdefault(label, []).append(entry)
            print(f"{label} run {rep + 1}: {dict((k, v) for k, v in entry.items() if k != 'stages')}", flush=True)
    summary = {}
    for label, entries in result.items():
        longest = [e["longest_ms"] for e in entries if e["longest_ms"] is not None]
        windowed = [e["window_longest_ms"] for e in entries if e["window_longest_ms"] is not None]
        summary[label] = {"longest_ms_median": statistics.median(longest) if longest else None,
                          "longest_ms_max": max(longest) if longest else None,
                          "window_ms_median": statistics.median(windowed) if windowed else None,
                          "window_ms_max": max(windowed) if windowed else None, "runs": entries, "stages": {}}
        for stage in sorted({st for e in entries for st in e["stages"]}):
            vals = [e["stages"][stage] for e in entries if stage in e["stages"]]
            summary[label]["stages"][stage] = {
                "window_longest_ms_median": statistics.median(v["window_longest_ms"] for v in vals),
                "window_longest_ms_max": max(v["window_longest_ms"] for v in vals),
                "window_texture_max_ms_median": statistics.median(v["window_texture_max_ms"] for v in vals),
                "runs": len(vals)}
    with open(os.path.join(args.out, "timing.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1)
    for label, s in summary.items():
        print(f"{label}: longest pass of the {args.stage} load median {s['longest_ms_median']} ms, max {s['longest_ms_max']} ms; "
              f"of its start window median {s['window_ms_median']} ms, max {s['window_ms_max']} ms")
        for stage, v in s["stages"].items():
            print(f"  {stage}: start window longest pass median {v['window_longest_ms_median']} ms, "
                  f"max {v['window_longest_ms_max']} ms; most texture work in a pass median "
                  f"{v['window_texture_max_ms_median']} ms ({v['runs']} runs)")
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
    sub.choices["timing"].add_argument("--repeat", type=int, default=3)
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
