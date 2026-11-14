#!/usr/bin/env python3
"""Finishes the full run: per texture the final 4x picture, its 2x level
and a report line, from the variants upscale.py computed for classify.py's
classes.

    python -I tools/remaster/finalize.py --export E:/DW4Remaster/export \
        --classes E:/DW4Remaster/out/classes.tsv --up E:/DW4Remaster/out/full --out E:/DW4Remaster/out

Writes OUT/master4x/<hash>.png (4x RGBA), OUT/high2x/<hash>.png (2x, from
the 4x by a Lanczos3 filter on premultiplied colour, wrap-aware, cut-outs
kept binary) and OUT/report.tsv (class, variant, colour fidelity before and
after correction, seams, flags, seconds), plus OUT/report.json (counts,
palette group checks, timings).

Per texture:

* curation (bosses and portraits, `curate` 1): the class variant is the
  base; where it lost detail the alternative kept (its per-texel CIEDE2000
  against the source more than DETAIL_GAIN lower, and the base's above
  DETAIL_FLOOR), the alternative is blended in through a feathered mask;
  `--curation` (hash, choice base|alt|merge, gain) overrides it per texture;
* region (classes.tsv `region`, from texture_overrides.tsv): a rectangle
  of source texels taken from a second variant (UI symbols beside a noise
  field that only Lanczos keeps clean);
* soft rims (ui, decal): where the source alpha is partial (the soft dark
  shadow or outline of HUD digits and lettering, a decal's anti-aliased
  rim) and next to it, alpha and colour come from the Lanczos 4x of the
  source, so a half-transparent shadow stays half-transparent instead of
  the model's hard black (ui) or white sticker outline (decal); ui keeps
  the model inside the opaque glyphs, decals take the texels next to the
  rim too;
* HUD panels (stpnlwn*): the 2-texel colour tables at both sides by
  nearest neighbour, the lilac symbol field (a 3-texel "!" the model
  redrew as another glyph) from Lanczos; no soft rim (the frame keeps the
  model's look);
* alpha coverage (soft alpha): where the result shrunk back misses the
  source alpha by more than 0.08 (a model that made a half-transparent
  margin, a soft outline or shadow, solid), the Lanczos alpha takes over;
* colour fidelity: the result shrunk back 4x by area means against the
  source, as the player sees it (CIEDE2000 mean / 95th percentile, PSNR);
* colour correction where the mean exceeds FIX_FROM (not for cards, whose
  redraw look is intended): the low-passed difference to the source is
  added back (sigma 2 texels; 1 texel when that is not enough), for all
  palette variants of a group alike; FIX_FROM lies below the flag's
  FIX_ABOVE, so a result that ends up just under the flag (better edges)
  keeps its correction; not for `pre` smooth or dither (their distance to
  the source is the dequantising, which a correction would undo);
* seams (3D classes): on axes padded cyclically, the jump of the result
  across the wrap border against the source's jump there;
* flags: de_raw>3, de>3 (still, after the correction), p95>12,
  alpha<26dB, seam, curated, missing.
"""
import argparse
import collections
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rmcommon  # noqa: E402

rmcommon.lower_priority()

import cv2  # noqa: E402
import numpy as np  # noqa: E402

from rmcommon import (alpha_mode, colour_fix, downscale2x, fidelity, keep_alpha_coverage, load_rgba,  # noqa: E402
                      local_de, nearest, save_rgba, border_jump, uv_island_mask, wrap_axes)

FIX_ABOVE = 3.0  # mean CIEDE2000 above which a texture is flagged (de_raw>3, de>3)
FIX_FROM = 2.5  # ... and above which the colour is corrected
FIX_TARGET = 2.5  # the gentler correction is kept when it reaches this
DEQUANTISED = {"smooth", "dither"}  # pre steps whose distance to the source is intended: no correction
MIN_GAIN = 0.25  # a correction that lowers the mean by less is not applied
DETAIL_GAIN = 2.0  # curation: the alternative's per-texel dE this much lower ...
DETAIL_FLOOR = 4.0  # ... where the base's is above this
P95_FLAG = 12.0
ALPHA_FLAG = 26.0
SEAM_FLAG = 1.25  # border jump of the 4x result against the source's
FLAT_CLASSES = rmcommon.FLAT_CLASSES  # drawn as flat quads: no seam check
SOFT_RIM_CLASSES = {"ui", "decal"}
PANEL_PREFIX = "stpnlwn"  # HUD panels with colour tables in their outer columns
DEFAULT_CURATION = os.path.join(os.path.dirname(os.path.abspath(__file__)), "curation.tsv")

REPORT_COLUMNS = ["hash", "file", "names", "size", "format", "mode", "usa", "lang", "class", "variant", "wrap", "cells",
                  "de_raw", "p95_raw", "de", "p95", "psnr", "psnr_alpha", "colour_fix", "seam", "flags", "model_s",
                  "finish_s", "notes"]


def curated_merge(src, base, alt, mode, mask, gain):
    """Base with the alternative blended in where the base lost detail."""
    eb = local_de(src, base, mode)
    ea = local_de(src, alt, mode)
    m = np.clip((eb - ea - gain) / 4.0, 0.0, 1.0) * (eb > DETAIL_FLOOR)
    if mode in ("blend", "cutout", "additive"):
        m = m * (src[..., 3] > 0.02)
    if mask is not None:
        m = m * mask
    m = cv2.dilate(m.astype(np.float32), np.ones((3, 3), np.uint8))
    m = cv2.GaussianBlur(m, (0, 0), 1.0)
    m = np.clip(m, 0.0, 1.0)
    h, w = m.shape
    m4 = cv2.resize(m, (w * 4, h * 4), interpolation=cv2.INTER_LINEAR)[..., None]
    out = base * (1.0 - m4) + alt * m4
    if mode == "cutout":
        out[..., 3] = (out[..., 3] >= 0.5).astype(np.float32)
    return out.astype(np.float32), float(m.mean()), m


def region_paste(out, alt, rect, scale=4):
    """The rectangle (x0, y0, x1, y1 in source texels) of `alt` pasted into `out`."""
    x0, y0, x1, y1 = rect
    res = out.copy()
    res[y0 * scale:y1 * scale, x0 * scale:x1 * scale] = alt[y0 * scale:y1 * scale, x0 * scale:x1 * scale]
    return res


def lanczos_ref(src, row, mode, wrap):
    """The Lanczos 4x of the source through upscale.py's pipeline (fill under
    alpha 0, padding by the tiling decision, soft alpha within range)."""
    import upscale
    return upscale.run_picture(src, row, mode, upscale.lanczos_sr, upscale.lanczos_sr, wrap)


def _upsample_weight(m, sigma=1.5):
    w4 = nearest(m.astype(np.float32), 4)
    return np.clip(cv2.GaussianBlur(w4, (0, 0), sigma), 0.0, 1.0)


def soft_rim(src, out, lz, cls):
    """Alpha and colour from Lanczos in the source's partial-alpha band."""
    a = np.round(src[..., 3] * 255.0)
    band = ((a > 0) & (a < 255)).astype(np.float32)
    if band.max() <= 0:
        return out, 0.0
    k = np.ones((3, 3), np.uint8)
    if cls == "decal":
        band = cv2.dilate(band, k)  # the rim and the texels on both sides
    else:
        band = np.maximum(band, cv2.dilate(band, k) * (a < 255))  # not into the opaque glyphs
    w4 = _upsample_weight(band)[..., None]
    res = out.copy()
    res[..., :4] = out[..., :4] * (1.0 - w4) + lz[..., :4] * w4
    return res.astype(np.float32), float(band.mean())


def panel_fix(src, out, lz):
    """HUD panel: the 2-texel colour tables at both sides (columns 0-1 and
    the last two: pairs of rows with fixed palette values) nearest, the
    lilac symbol field with its 3-texel "!" (the model drew another glyph)
    from Lanczos."""
    res = out.copy()
    rgb, a = src[..., :3], src[..., 3]
    lilac = (rgb[..., 0] - rgb[..., 1] > 0.15) & (rgb[..., 2] - rgb[..., 1] > 0.15) & (a > 0.5)
    lilac[:, :2] = False
    lilac[:, -2:] = False
    m = np.zeros(a.shape, np.float32)
    if lilac.any():
        ys, xs = np.nonzero(lilac)
        m[ys.min():ys.max() + 1, xs.min():xs.max() + 1] = 1.0
    w4 = _upsample_weight(m, 1.0)[..., None]
    res = res * (1.0 - w4) + lz * w4
    res[:, :8] = nearest(src[:, :2], 4)
    res[:, -8:] = nearest(src[:, -2:], 4)
    return res.astype(np.float32), float(m.mean())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--export", required=True)
    ap.add_argument("--classes", required=True, help="classify.py's classes.tsv")
    ap.add_argument("--up", required=True, help="upscale.py's output folder (one subfolder per variant)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--curation", default=DEFAULT_CURATION,
                    help="TSV hash, choice (base|alt|merge), gain: per-texture curation choices")
    ap.add_argument("--only", default="", help="comma-separated hashes (default: all)")
    ap.add_argument("--masks", action="store_true", help="also write OUT/curation/<hash>_mask.png")
    args = ap.parse_args()

    manifest = rmcommon.read_manifest(args.export)
    classes = rmcommon.read_tsv(args.classes)
    only = set(s for s in args.only.split(",") if s)
    if only:
        classes = [c for c in classes if c["hash"] in only]
    choices = {}
    if args.curation and os.path.exists(args.curation):
        for r in rmcommon.read_tsv(args.curation):
            choices[r["hash"]] = (r["choice"], float(r.get("gain") or DETAIL_GAIN))
    metrics = {}
    for v in sorted({v for c in classes for v in c["variants"].split(",")}):
        path = os.path.join(args.up, v, "metrics.tsv")
        metrics[v] = {r["hash"]: r for r in rmcommon.read_tsv(path)} if os.path.exists(path) else {}

    # Palette variants are corrected alike: the group's decision.
    group_fix = collections.defaultdict(bool)
    for c in classes:
        row = manifest[c["hash"]]
        g = row["palette_group"]
        m = metrics.get(c["model"], {}).get(c["hash"])
        if g not in ("-", "") and m and c["colour_fix"] == "1" and float(m["de_mean"]) > FIX_FROM:
            group_fix[g] = True

    d4 = os.path.join(args.out, "master4x")
    d2 = os.path.join(args.out, "high2x")
    os.makedirs(d4, exist_ok=True)
    os.makedirs(d2, exist_ok=True)
    report_path = os.path.join(args.out, "report.tsv")
    old = {}
    if only and os.path.exists(report_path):
        old = {r["hash"]: r for r in rmcommon.read_tsv(report_path)}
    lines = {}
    t_all = time.perf_counter()
    for i, c in enumerate(classes):
        h = c["hash"]
        row = manifest[h]
        t0 = time.perf_counter()
        src = load_rgba(os.path.join(args.export, "textures", row["file"]))
        mode = alpha_mode(row)
        wrap = wrap_axes(row, c)
        mask = uv_island_mask(args.export, row, c["class"])
        flags, notes = [], []
        base_path = os.path.join(args.up, c["model"], row["file"])
        if not os.path.exists(base_path):
            lines[h] = {"hash": h, "file": row["file"], "names": row["names"], "class": c["class"], "variant": c["model"],
                        "flags": "missing"}
            print(f"{h}: missing {base_path}", flush=True)
            continue
        out = load_rgba(base_path)
        variant = c["model"]
        mrow = metrics.get(c["model"], {}).get(h, {})
        model_s = float(mrow.get("seconds", 0) or 0)
        if mrow.get("notes", "-") not in ("-", ""):
            notes.append(mrow["notes"])
        if c["curate"] == "1":
            alt_path = os.path.join(args.up, c["alt"], row["file"])
            choice, gain = choices.get(h, ("merge", DETAIL_GAIN))
            flags.append("curated")
            if os.path.exists(alt_path):
                alt = load_rgba(alt_path)
                model_s += float(metrics.get(c["alt"], {}).get(h, {}).get("seconds", 0) or 0)
                if choice == "alt":
                    out, variant = alt, c["alt"] + " (curated: alternative)"
                elif choice == "merge":
                    out, frac, m = curated_merge(src, out, alt, mode, mask, gain)
                    variant = f"{c['model']}+{c['alt']} ({frac:.1%} masked)"
                    if args.masks:
                        save_rgba(os.path.join(args.out, "curation", h + "_mask.png"), np.repeat(m[..., None], 3, axis=2))
                else:
                    variant = c["model"] + " (curated: base)"
            else:
                flags.append("missing_alt")
        if c.get("region", "-") not in ("-", ""):
            rvar, _, rect = c["region"].partition(":")
            x0, y0, x1, y1 = (int(v) for v in rect.split(","))
            reg_path = os.path.join(args.up, rvar, row["file"])
            if os.path.exists(reg_path):
                out = region_paste(out, load_rgba(reg_path), (x0, y0, x1, y1))
                variant += f" + {rvar} ({x0},{y0})-({x1},{y1})"
                model_s += float(metrics.get(rvar, {}).get(h, {}).get("seconds", 0) or 0)
            else:
                flags.append("missing_region")
        lz = None
        panel = row["names"].startswith(PANEL_PREFIX)
        if mode == "blend" and c["class"] in SOFT_RIM_CLASSES and not panel:
            lz = lanczos_ref(src, row, mode, wrap)
            out, share = soft_rim(src, out, lz, c["class"])
            if share > 0.0:
                notes.append(f"soft rim: Lanczos alpha and colour on {share:.1%} of texels")
        if panel:
            out, share = panel_fix(src, out, lanczos_ref(src, row, mode, wrap))
            notes.append(f"HUD panel: colour tables nearest, symbol field Lanczos ({share:.1%} of texels)")
        if mode == "blend":
            out[..., 3], share = keep_alpha_coverage(src[..., 3], out[..., 3], wrap)
            if share > 0.0:
                notes.append(f"alpha coverage: Lanczos alpha on {share:.1%} of texels")
        raw = fidelity(src, out, mode, mask)
        fix_txt = "-"
        fid = raw
        if raw["de_mean"] > FIX_ABOVE:
            flags.append("de_raw>3")
        want_fix = c["colour_fix"] == "1" and (raw["de_mean"] > FIX_FROM or group_fix[row["palette_group"]])
        if c["colour_fix"] != "1" and raw["de_mean"] > FIX_ABOVE:
            notes.append("redraw look kept (card)")
        if want_fix and c.get("pre", "-") in DEQUANTISED:
            # Its distance to the source is the intended dequantising: a
            # correction would add the steps back.
            want_fix = False
            notes.append(f"no colour correction (pre {c['pre']}: dequantised on purpose)")
        if want_fix:
            best = None
            for sigma in (2.0, 1.0):
                fixed = colour_fix(src, out, mode, wrap, mask, sigma=sigma)
                f2 = fidelity(src, fixed, mode, mask)
                if best is None or f2["de_mean"] < best[1]["de_mean"]:
                    best = (fixed, f2, sigma)
                if f2["de_mean"] <= FIX_TARGET:
                    break
            # Kept only where it helps: texel-level differences (tiny sparkly
            # effects, sharpened detail) are not a colour shift it can fix.
            if raw["de_mean"] - best[1]["de_mean"] >= MIN_GAIN:
                out, fid, sigma = best
                fix_txt = f"sigma {sigma:g}: dE {raw['de_mean']} -> {fid['de_mean']}"
            else:
                notes.append(f"colour correction without effect (dE {raw['de_mean']} -> {best[1]['de_mean']}), not applied")
        if fid["de_mean"] > FIX_ABOVE:
            flags.append("de>3")
        if fid["de_p95"] > P95_FLAG:
            flags.append("p95>12")
        if mode in ("blend", "additive") and fid["psnr_alpha"] < ALPHA_FLAG:
            flags.append("alpha<26dB")
        seam_txt = "-"
        if (wrap[0] or wrap[1]) and c["class"] not in FLAT_CLASSES:
            parts = []
            for axis, on in ((0, wrap[0]), (1, wrap[1])):
                if not on:
                    continue
                b_out = border_jump(out, mode, axis, mask)
                b_src = border_jump(src, mode, axis, mask)
                parts.append(f"{'ts'[axis]} {b_out:.3f}/{b_src:.3f}")
                if b_out > SEAM_FLAG * b_src + 0.02:
                    flags.append("seam")
            seam_txt = ", ".join(parts)
        save_rgba(os.path.join(d4, h + ".png"), out)
        save_rgba(os.path.join(d2, h + ".png"), downscale2x(out, mode, wrap))
        dt = time.perf_counter() - t0
        wy, wx = wrap
        lines[h] = {
            "hash": h, "file": row["file"], "names": row["names"], "size": f"{row['width']}x{row['height']}",
            "format": row["format"], "mode": mode, "usa": row["usa"], "lang": c["lang"], "class": c["class"],
            "variant": variant + (f" ({c['pre']})" if c.get("pre", "-") not in ("-", "") else ""),
            "wrap": ("t" if wy else "") + ("s" if wx else "") or "-", "cells": c["cells"],
            "de_raw": raw["de_mean"], "p95_raw": raw["de_p95"], "de": fid["de_mean"], "p95": fid["de_p95"],
            "psnr": fid["psnr"], "psnr_alpha": fid["psnr_alpha"], "colour_fix": fix_txt, "seam": seam_txt,
            "flags": ",".join(dict.fromkeys(flags)) or "-", "model_s": round(model_s, 3), "finish_s": round(dt, 3),
            "notes": "; ".join(notes) or "-"}
        if (i + 1) % 50 == 0 or i + 1 == len(classes):
            print(f"  {i + 1}/{len(classes)} ({time.perf_counter() - t_all:.0f} s)", flush=True)

    merged = dict(old)
    merged.update(lines)
    order = [c["hash"] for c in rmcommon.read_tsv(args.classes)]
    with open(report_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\t".join(REPORT_COLUMNS) + "\n")
        for h in order:
            if h in merged:
                f.write("\t".join(str(merged[h].get(col, "-")).replace("\t", " ") for col in REPORT_COLUMNS) + "\n")

    # Palette groups: the variants' 4x results must share their structure
    # (the index plane): equal alpha, colour differences like the sources'.
    groups = collections.defaultdict(list)
    for h in order:
        r = manifest[h]
        if h in merged and r["palette_group"] not in ("-", "") and int(r["palette_variants"] or 1) > 1:
            groups[r["palette_group"]].append(h)
    palette = {}
    for g, members in groups.items():
        outs = [load_rgba(os.path.join(d4, h + ".png")) for h in members if os.path.exists(os.path.join(d4, h + ".png"))]
        if len(outs) < 2:
            continue
        a_diff = max(float(np.abs(o[..., 3] - outs[0][..., 3]).max()) for o in outs[1:]) * 255
        palette[g] = {"members": members, "classes": sorted({merged[h]["class"] for h in members}),
                      "variants": sorted({merged[h]["variant"] for h in members}),
                      "colour_fix": sorted({merged[h]["colour_fix"] != "-" for h in members}),
                      "alpha_max_diff_4x": round(a_diff, 1)}
    rows = [merged[h] for h in order if h in merged]
    summary = {
        "textures": len(rows), "usa": sum(r.get("usa") == "1" for r in rows),
        "by_class": dict(collections.Counter(r["class"] for r in rows)),
        "flags": dict(collections.Counter(f for r in rows for f in r["flags"].split(",") if f != "-")),
        "colour_fixed": sum(r.get("colour_fix", "-") != "-" for r in rows),
        "model_seconds": round(sum(float(r.get("model_s", 0) or 0) for r in rows), 1),
        "finish_seconds_this_run": round(time.perf_counter() - t_all, 1),
        "palette_groups": palette,
    }
    with open(os.path.join(args.out, "report.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1)
    print(json.dumps({k: v for k, v in summary.items() if k != "palette_groups"}, indent=1))


if __name__ == "__main__":
    main()
