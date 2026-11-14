#!/usr/bin/env python3
"""Classifies every texture of a `dw4_texpack export` by content and source
format and assigns the upscaling variant of the full run (the settings the
pilot ended with), as a selection TSV for upscale.py, finalize.py and
contact_sheets.py.

    python -I tools/remaster/classify.py --export E:/DW4Remaster/export \
        --out E:/DW4Remaster/out/classes.tsv [--overrides tools/remaster/class_overrides.tsv] [--usa-only]

Class (first rule that matches; `class_overrides.tsv` comes first):

  font        font00.png                                   path B (glyphs), UltraSharpV2 (button cells)
  loading     documents under loading*/                    UltraSharpV2
  title       logo, mojigc, mojips, Bandai/ESRB, stagis08/09   UltraSharpV2
  card        card<n>.dds                                  HDcube3, no colour correction (redraw look)
  character   player forms (P_<digi>0[12]), enemies (E_), NPCs (N_)   HDcube3
  boss        F_* and enemy skins of 128 x 128 or more     HDcube3, curated against UltraSharpV2
  weapon      w_*                                          HDcube3
  item        sfai*, fsxdat*, fsai*                        HDcube3
  portrait    charaicon, N_*ic, dejilficon, the 256 x 256 character renders   HDcube3, curated
  ui          windows, digits (multi-tone: num*, numdmg), cursor, panels, stagis10/20, language packs   UltraSharpV2
  effect      x_*, y_* and the other effect names           Lanczos 4x + light sharpening
  environment stage names (Z*, FD*, TA*, ML*, GF*, BS*, ...) and l?_pack sets   PBRify V4
  env_glow    C8 environment textures every draw of which is additive, or additive
              and mostly soft alpha: light shafts, sparkles, caustics, emissive
              screens   Lanczos 4x + light sharpening
  env_graphic environment textures (not cut-outs, not additive) that are flat
              graphics (many hard jumps between flat areas: panels, signs, LED
              grids, circuit patterns)   UltraSharpV2
  decal       painted 2D ornaments on stage geometry (overrides)   UltraSharpV2, dither denoised first

`texture_overrides.tsv` (hash, model, pre, wrap, region, note) then sets
the variant, the preprocessing or the tiling of single textures after the
visual reviews (a flat 16 x 16 colour strip with Lanczos, a circuit tile
with UltraSharpV2). `pre`: `smooth` (effects and glows whose soft alpha is
a ramp of at most 8 coarse levels, found automatically: the alpha is
dequantised before Lanczos and nothing is sharpened; not where the alpha's
fine detail is a pattern, the dungeons' heat haze), `dither` (also the
colour's dither blurred lightly; set per texture), `denoise` (decals: the
dither is denoised before the model), `mask_nearest` (cut-outs with square
windows one texel wide: the mask by nearest neighbour), `none` (no
preprocessing where the automatic rule would choose one: grids and
speckled bands drawn in alpha). `wrap` (`none`, `t`, `s`, `ts`; `-`
automatic) replaces the tiling decision. `region` (`VARIANT:x0,y0,x1,y1`,
source texels) takes that rectangle from a second variant, which upscale.py
then computes too (UI symbols beside a noise field in one texture).

`wrap` is the per-axis tiling decision (rmcommon.tiling_axes) that
upscale.py, finalize.py and build_pack.py pad and filter by.

Format and usage then refine it: a C8 texture drawn only additively is an
effect wherever its name puts it (weapon trails, item sparkles); an unnamed
stage texture is environment when it is BC1, an effect when it is C8 and
additive or a soft sprite (mostly partial alpha, no tiling), environment
otherwise; the overrides hold the visual check (enemy skins and painted
pictures among the unnamed ones). Palette variants (one index plane under
several palettes) always share the class and variant of their group.

`cells` is 1 where the export lists atlas cells (sprite quads, keyed UV
cells, font glyphs) besides whole-picture quads that upscale.py should cut
out one by one: 2D classes, effects and glows; 3D textures stay one pass.
"""
import argparse
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rmcommon  # noqa: E402

import cv2  # noqa: E402
import numpy as np  # noqa: E402

LANGS = ("usa", "jpn", "eng", "fra", "ger", "ita", "spn", "aeu", "ajp", "aus")
DIGI = ("agumon", "alpha", "angelr", "beelzeb", "bwargrey", "dorumon", "dukecrim", "girumon", "hekabut", "impdrafm",
        "impdrapm", "metalgaru", "susanoo", "vmon", "wargrey", "weregaru")

# class -> (variant, alternative for curation or "")
MODELS = {
    "font": ("pathb", ""),
    "loading": ("ultrasharp_v2", ""),
    "title": ("ultrasharp_v2", ""),
    "card": ("hdcube3", ""),
    "character": ("hdcube3", ""),
    "boss": ("hdcube3", "ultrasharp_v2"),
    "weapon": ("hdcube3", ""),
    "item": ("hdcube3", ""),
    "portrait": ("hdcube3", "ultrasharp_v2"),
    "ui": ("ultrasharp_v2", ""),
    "decal": ("ultrasharp_v2", ""),
    "effect": ("lanczos_sharp", ""),
    "env_glow": ("lanczos_sharp", ""),
    "env_graphic": ("ultrasharp_v2", ""),
    "environment": ("pbrify_v4", ""),
}
GRAPHIC_HARD, GRAPHIC_FLAT = 0.12, 0.35  # edge_stats from which a stage texture is a flat graphic
EXPORT_DIR = [""]
CELL_CLASSES_2D = {"font", "loading", "title", "ui", "portrait", "decal", "effect", "env_glow"}
NO_COLOUR_FIX = {"card"}  # the cards' redraw look is intended

COLUMNS = ["slot", "hash", "class", "variants", "cells", "file", "names", "width", "height", "note",
           "model", "alt", "curate", "colour_fix", "format", "mode", "usa", "lang", "reason", "wrap", "wrap_note", "pre",
           "region"]
VARIANTS = {"lanczos", "lanczos_sharp", "pbrify_v4", "ultrasharp_v2", "hdcube3", "pathb"}
PRE = {"-", "smooth", "dither", "denoise", "mask_nearest", "none"}
WRAP = {"-": None, "none": "", "t": "t", "s": "s", "ts": "ts", "st": "ts"}
# Soft alpha of 5 to 8 levels (STEPPED_LEVELS) on STEPPED_SHARE of the texels,
# in plateaus (STEPPED_PLATEAU of the soft neighbour pairs equal: steps over
# 2 texels wide) that form ramps (STEPPED_RAMP of the neighbour pairs that
# differ go to the next level: no pixel art), on pictures of at least
# STEPPED_MIN texels both ways, whose fine detail (the alpha minus its
# Gaussian blur of 1 texel) carries at most STEPPED_FINE of the alpha's
# variance (a ramp, not a pattern of single steps).
STEPPED_LEVELS, STEPPED_SHARE, STEPPED_PLATEAU, STEPPED_RAMP, STEPPED_MIN = (5, 8), 0.25, 0.55, 0.75, 32
STEPPED_FINE = 0.2


def fine_detail_share(a, wrap, sigma=1.0):
    """Share of a picture's variance in detail finer than a Gaussian of
    `sigma` texels (padded by the tiling decision)."""
    var = float(a.var())
    if var <= 0.0:
        return 0.0
    p = int(3 * sigma) + 2
    blur = cv2.GaussianBlur(rmcommon.pad(a, p, p, *wrap), (0, 0), sigma)[p:-p, p:-p]
    return float(((a - blur) ** 2).mean()) / var


def stepped_alpha(src, wrap=(False, False)):
    """A glow whose soft alpha is a staircase of a few coarse levels (the
    stagbs01 beams: Lanczos keeps every step as a hard edge, sharpening
    rings on them). Tiny sparkles are left alone, and so are patterns drawn
    in a few alpha levels (the dungeons' scrolling heat haze: dequantising
    pulls each texel half a step towards its neighbours and halves the
    pattern's contrast)."""
    if min(src.shape[:2]) < STEPPED_MIN:
        return False
    a = np.round(src[..., 3] * 255.0)
    part = (a > 0) & (a < 255)
    if part.mean() < STEPPED_SHARE or not STEPPED_LEVELS[0] <= len(np.unique(a[part])) <= STEPPED_LEVELS[1]:
        return False
    rank = np.searchsorted(np.unique(a), a)
    pairs = same = differ = ramp = 0
    for axis in (0, 1):
        n = part.shape[axis]
        both = np.take(part, range(1, n), axis=axis) & np.take(part, range(0, n - 1), axis=axis)
        pairs += int(both.sum())
        same += int((both & (np.diff(a, axis=axis) == 0)).sum())
        d = np.abs(np.diff(rank, axis=axis))
        seen = np.take(a, range(1, n), axis=axis) + np.take(a, range(0, n - 1), axis=axis) > 0
        differ += int(((d > 0) & seen).sum())
        ramp += int(((d == 1) & seen).sum())
    if not (pairs > 0 and same / pairs >= STEPPED_PLATEAU and differ > 0 and ramp / differ >= STEPPED_RAMP):
        return False
    return fine_detail_share(src[..., 3], wrap) <= STEPPED_FINE


def parse_region(value):
    """`VARIANT:x0,y0,x1,y1` of texture_overrides.tsv as (variant, (x0, y0, x1, y1)), or None for `-`."""
    if value in ("", "-", None):
        return None
    variant, _, rect = value.partition(":")
    box = tuple(int(v) for v in rect.split(","))
    if variant not in VARIANTS or len(box) != 4 or box[0] >= box[2] or box[1] >= box[3] or min(box) < 0:
        raise ValueError(f"bad region {value!r}")
    return variant, box


def apply_override(ov, model, pre, wrap, wrap_note):
    """A texture_overrides.tsv row applied to the automatic choices:
    (model, pre, wrap, wrap_note, region or None)."""
    if ov.get("model", "-") not in ("-", ""):
        model = ov["model"]
    if ov.get("pre", "-") not in ("-", ""):
        pre = "-" if ov["pre"] == "none" else ov["pre"]
    v = WRAP[ov.get("wrap", "-") or "-"]
    if v is not None:
        wrap = ("t" in v, "s" in v)
        wrap_note = f"override {v or 'none'} (auto: {wrap_note})"
    return model, pre, wrap, wrap_note, parse_region(ov.get("region", "-"))


def blends(row):
    return [b for b in row["blend"].split(",") if b and b != "-"]


def all_additive(row):
    b = blends(row)
    return bool(b) and all(x == "SRC_ALPHA/ONE" for x in b)


def any_additive(row):
    return "SRC_ALPHA/ONE" in blends(row)


def soft_fraction(row):
    n = int(row["width"]) * int(row["height"])
    return int(row["alpha_partial_texels"]) / max(1, n)


def name_class(names, sources, row):
    """The class from the image names and the documents that hold it, or None."""
    n_all = [n.lower() for n in names]
    s_all = [s.lower().removeprefix("pkg:") for s in sources]

    def any_name(pattern):
        return any(re.search(pattern, n) for n in n_all)

    def any_src(pattern):
        return any(re.search(pattern, s) for s in s_all)

    if "font00.png" in n_all:
        return "font", "font00"
    if any_name(r"bandailogo") or "rp_esrb.png" in n_all:
        return "title", "logo"
    if any_src(r"^loading"):
        return "loading", "loading document"
    if any_name(r"^(logo|mojigc|mojips)\.png$") or any_src(r"^stagis0[89]"):
        return "title", "title screen"
    if any_name(r"^card\d+\.dds$"):
        return "card", "card name"
    if any_name(r"^p_(" + "|".join(DIGI) + r")0[12]\.dds$") or any_src(r"^_common/[mpqs]_"):
        return "character", "player form"
    if any_name(r"^w_") or any_src(r"^_common/w_"):
        return "weapon", "weapon name"
    if any_name(r"^f_"):
        return "boss", "F_ boss name"
    if any_name(r"^(e_|gekoa|doril|dokutume)") or any(n.startswith("n_") and not n.endswith("ic.png") for n in n_all) \
            or any_src(r"separate/generator") or any_src(r"separate/e_"):
        if any_name(r"^e_") and int(row["width"]) * int(row["height"]) >= 128 * 128:
            return "boss", "enemy skin of 128 x 128 or more"
        return "character", "enemy/NPC name"
    if any_name(r"^(sfai|fsxdat|sfstaref|fsai)"):
        return "item", "item name"
    if any_name(r"^(x_|y_|break|ice01|ibara|yami|\d{3}[_a-z]|onpu|fdefboss)") or any_name(r"ef\d*\.png$|ef\.dds$") \
            or any_name(r"gref"):
        return "effect", "effect name"
    if any_name(r"^(charaicon\.png|dejilficon\.png|alpha8\.png)$") or any_name(r"^n_.*ic\.png$") \
            or any_name(r"^(" + "|".join(DIGI) + r")\.png$"):
        return "portrait", "portrait name"
    if any_name(r"^z5icons"):
        return "environment", "stage 5 screen texture"
    if any_name(r"^(cursor|waku|messwin|num|stcng|stpnl|stmoji|sback|base|back0|digiinfo|dungmap|bosshpmt|slot|bit\.|"
                r"black|roll|moji\.|i_evo|loading2)") or any_name(r"ic\.png$") \
            or any_src(r"^stagis(10|20)"):
        return "ui", "UI name or UI document"
    if any(f"separate/{lang}_pack" in s for s in s_all for lang in LANGS):
        return "ui", "language pack"
    if any_src(r"separate/l[hs]_pack"):
        return "environment", "stage set"
    if any_name(r"^(z\d|z0|fd\d|ta\d|ta0|t\d|kyz|plane|stage\d|ml\d|gf\d|gfbj|bs\d|p\d\d|fs|sf0|sfz|bjgr|0001ground|"
                r"wat|light|boat)"):
        return "environment", "stage name"
    return None


def classify(row, sources, overrides):
    """(class, reason) of one manifest row."""
    h = row["hash"]
    names = [n for n in row["names"].split(",") if n and n != "-"]
    if h in overrides:
        cls, note = overrides[h]
        return cls, f"override: {note}"
    found = name_class(names, sources, row)
    c8 = row["format"] == "C8"
    if found is None:
        # Unnamed (or unknown) stage textures: by format, alpha and usage.
        if not c8:
            return "environment", "unnamed BC1 stage texture"
        if all_additive(row):
            return "effect", "unnamed C8, every draw additive"
        if any_additive(row) and soft_fraction(row) > 0.25:
            return "effect", "unnamed C8, additive and soft"
        if row["alpha"] == "blend" and soft_fraction(row) > 0.30 and row["tiles"] == "no":
            return "effect", "unnamed C8 soft sprite (mostly partial alpha, no tiling)"
        return "environment", "unnamed C8 stage texture"
    cls, reason = found
    if c8 and cls in ("weapon", "item", "character") and all_additive(row):
        return "effect", f"{reason}; C8 drawn only additively"
    if c8 and cls == "environment" and (all_additive(row) or (any_additive(row) and soft_fraction(row) > 0.25)):
        # Emissive overlays (glows, caustics, and the digital stages' screens
        # of tiny glyph pixels): models dim or smear their isolated bright
        # texels, Lanczos keeps their light.
        how = "every draw additive" if all_additive(row) else "additive and soft"
        return "env_glow", f"{reason}; C8, {how}"
    return cls, reason


def refine_environment(row, cls, reason):
    """Environment textures that are flat graphics (hard jumps between flat
    areas: the digital stages' screens and panels, signs, LED grids) go to
    the 2D model; PBRify smears their pixel art into streaky noise."""
    if cls != "environment" or row["alpha"] == "cutout" or all_additive(row):
        return cls, reason  # cut-outs (foliage, fences) keep the environment model
    src = rmcommon.load_rgba(os.path.join(EXPORT_DIR[0], "textures", row["file"]))
    hard, flat = rmcommon.edge_stats(src, rmcommon.alpha_mode(row))
    if hard >= GRAPHIC_HARD and flat >= GRAPHIC_FLAT:
        return "env_graphic", f"{reason}; flat graphic (hard {hard:.2f}, flat {flat:.2f})"
    return cls, reason


def language(row):
    return "usa" if row["usa"] == "1" else row["languages"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--export", required=True, help="dw4_texpack export folder")
    ap.add_argument("--out", required=True, help="classes TSV to write")
    ap.add_argument("--overrides", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "class_overrides.tsv"))
    ap.add_argument("--usa-only", action="store_true", help="leave out textures the USA release never loads")
    ap.add_argument("--texture-overrides", default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                "texture_overrides.tsv"))
    args = ap.parse_args()

    EXPORT_DIR[0] = args.export
    manifest = rmcommon.read_manifest(args.export)
    sources = collections.defaultdict(set)
    for inst in rmcommon.read_tsv(os.path.join(args.export, "instances.tsv")):
        sources[inst["hash"]].add(inst["source"])
    overrides = {}
    if args.overrides and os.path.exists(args.overrides):
        for r in rmcommon.read_tsv(args.overrides):
            if r["class"] not in MODELS:
                sys.exit(f"{args.overrides}: unknown class {r['class']} for {r['hash']}")
            if r["hash"] not in manifest:
                sys.exit(f"{args.overrides}: hash {r['hash']} is not in the export")
            overrides[r["hash"]] = (r["class"], r.get("note", ""))
    tex_over = {}
    if args.texture_overrides and os.path.exists(args.texture_overrides):
        for r in rmcommon.read_tsv(args.texture_overrides):
            if r["hash"] not in manifest:
                sys.exit(f"{args.texture_overrides}: hash {r['hash']} is not in the export")
            if r.get("model", "-") not in VARIANTS | {"-"} or r.get("pre", "-") not in PRE \
                    or (r.get("wrap", "-") or "-") not in WRAP:
                sys.exit(f"{args.texture_overrides}: unknown model, pre or wrap for {r['hash']}")
            try:
                parse_region(r.get("region", "-"))
            except ValueError as e:
                sys.exit(f"{args.texture_overrides}: {e} for {r['hash']}")
            tex_over[r["hash"]] = r

    result = {}
    for h, row in manifest.items():
        cls, reason = classify(row, sources[h] or {row["source"]}, overrides)
        if h not in overrides:
            cls, reason = refine_environment(row, cls, reason)
        result[h] = (cls, reason)

    # Palette variants: one class (the group's most common) for the whole group.
    groups = collections.defaultdict(list)
    for h, row in manifest.items():
        if row["palette_group"] not in ("-", "") and int(row["palette_variants"] or 1) > 1:
            groups[row["palette_group"]].append(h)
    for g, members in groups.items():
        common = collections.Counter(result[h][0] for h in members).most_common(1)[0][0]
        for h in members:
            if result[h][0] != common:
                result[h] = (common, f"palette group {g} ({result[h][1]})")

    out_rows = []
    for h, row in sorted(manifest.items(), key=lambda kv: (kv[1]["usa"] != "1", kv[0])):
        if args.usa_only and row["usa"] != "1":
            continue
        cls, reason = result[h]
        model, alt = MODELS[cls]
        # Cells that remain after dropping whole-picture quads; 3D textures
        # keep one pass (a sprite rectangle inside a mesh atlas would get a
        # seam where meshes sample across its border).
        rects = rmcommon.merge_cells(rmcommon.read_cells(args.export, row), int(row["width"]), int(row["height"]))
        cells = bool(rects) and cls in CELL_CLASSES_2D
        mode = rmcommon.alpha_mode(row)
        src = rmcommon.load_rgba(os.path.join(args.export, "textures", row["file"]))
        wrap, wrap_note = rmcommon.tiling_axes(row, src, cls)
        pre = "-"
        if cls == "decal":
            pre = "denoise"
        elif model == "lanczos_sharp" and mode in ("blend", "additive") and stepped_alpha(src, wrap):
            pre = "smooth"
        note = reason
        region = None
        ov = tex_over.get(h)
        if ov:
            model, pre, wrap, wrap_note, region = apply_override(ov, model, pre, wrap, wrap_note)
            note = f"{reason}; texture override: {ov.get('note', '')}"
        variants = model + ("," + alt if alt else "")
        if region and region[0] not in variants.split(","):
            variants += "," + region[0]
        out_rows.append({
            "slot": h[:8], "hash": h, "class": cls, "variants": variants, "cells": "1" if cells else "0",
            "file": row["file"], "names": row["names"], "width": row["width"], "height": row["height"], "note": note,
            "model": model, "alt": alt or "-", "curate": "1" if alt else "0",
            "colour_fix": "0" if cls in NO_COLOUR_FIX else "1", "format": row["format"], "mode": mode,
            "usa": row["usa"], "lang": language(row), "reason": reason,
            "wrap": ("t" if wrap[0] else "") + ("s" if wrap[1] else "") or "-", "wrap_note": wrap_note, "pre": pre,
            "region": f"{region[0]}:{','.join(map(str, region[1]))}" if region else "-",
        })
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\t".join(COLUMNS) + "\n")
        for r in out_rows:
            f.write("\t".join(str(r[c]).replace("\t", " ") for c in COLUMNS) + "\n")

    counts = collections.Counter((r["class"], r["usa"]) for r in out_rows)
    print(f"{len(out_rows)} textures -> {args.out}")
    print(f"{'class':12s} {'variant':14s} {'USA':>5s} {'other':>5s}")
    for cls in MODELS:
        u, o = counts.get((cls, "1"), 0), counts.get((cls, "0"), 0)
        if u or o:
            print(f"{cls:12s} {MODELS[cls][0]:14s} {u:5d} {o:5d}")
    print(f"cells: {sum(r['cells'] == '1' for r in out_rows)}, curated: {sum(r['curate'] == '1' for r in out_rows)}, "
          f"overrides: {sum(r['reason'].startswith('override') for r in out_rows)}, "
          f"texture overrides: {sum(r['hash'] in tex_over for r in out_rows)}, "
          f"pre: {dict(collections.Counter(r['pre'] for r in out_rows))}, "
          f"wrap: {dict(collections.Counter(r['wrap'] for r in out_rows))}")


if __name__ == "__main__":
    main()
