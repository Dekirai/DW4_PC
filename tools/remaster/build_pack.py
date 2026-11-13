#!/usr/bin/env python3
"""Builds the release form of the remaster pack (format 2, docs/TEXTURES.md)
from the full run's results (finalize.py): per texture one DDS file whose
level 0 is the 4x picture (master4x, quality Ultra) and level 1 the 2x
picture (high2x, quality High), with the rest of the mip chain down to
1 x 1, the manifest dw4pack.tsv and a README.txt.

    python -I tools/remaster/build_pack.py --final E:/DW4Remaster/out --export E:/DW4Remaster/export \
        --out E:/DW4Remaster/pack/DW4Remaster --report E:/DW4Remaster/pack/verify \
        --texconv "C:/Program Files/.../texconv.exe"

The mip chain is built here; texconv only compresses (it keeps levels it is
given when -m names their count):

* level 0 master4x, level 1 high2x as finalize.py wrote them (the 2x is a
  Lanczos3 filter of the 4x on premultiplied colour, each texel clamped to
  the range of its 4 x 4 footprint); levels 2.. by the same filter from the
  level above, cyclic on the axes the export's `tiles` / `uv_edges` name
  (the texture repeats there: a tiling floor's small levels must match
  across its border) and edge-clamped elsewhere, each axis halved only while
  it is above 1 texel;
* colour premultiplied where alpha is seen (blend, cut-out, additive), the
  straight filtered colour under alpha 0 (no black seeps in where the GPU
  filters across an edge);
* cut-outs (alpha 0 or 255 only, alpha-tested) stay binary in every level,
  the threshold chosen per level so the share of drawn texels stays that of
  level 0 (the coverage; a fixed 0.5 thins fences and foliage level by
  level). The material's reference (GREATER 0.1, ...) then cuts every level
  at the same place between texels;
* stored values everywhere, no sRGB conversion: the renderer samples the
  values as stored (GX had no sRGB), and the GPU's own bilinear and
  trilinear filtering mixes them as stored values, so the levels are
  filtered the same way (BC7_UNORM, never BC7_UNORM_SRGB).

Encodings: BC7_UNORM by texconv (DX10 header); R8G8B8A8 with a legacy header
(written here) for the hashes given with --rgba8, the font atlas and the
title's PRESS START, whose texels the game reads back.

Checks: every file is read back, its header (format, size, level count, file
length) and every level decoded by texconv against the level that went in
(PSNR of the visible colour, the mean colour shift a colour space
conversion would show, the cut-out coverage). REPORT gets levels.tsv (one
line per level), coverage.tsv (every texture of the export: packed or
skipped, and why) and build.json. --resume goes on from REPORT/done.tsv.

Runs at below-normal priority, texconv without a window. Everything it
writes is game-derived: keep OUT and REPORT outside the repository and out
of any public package (README.txt in the pack says so).
"""
import argparse
import collections
import datetime
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rmcommon  # noqa: E402

rmcommon.lower_priority()

import numpy as np  # noqa: E402

MARKER = ".dw4_build_pack"
DEFAULT_RGBA8 = "faccbeda501eefc1,ac566f5a15c2a226"  # font00.png, mojigc.png (PRESS START)
NO_WINDOW = 0x08000000
BELOW_NORMAL = 0x00004000
BC7_RETRY = 38.0  # level 0 or 1 below this (visible colour, dB): BC7 again with its 3-subset modes
MIN_PSNR = 34.0  # still below: RGBA8 (BC7 is typically 42-55 dB here)
RGBA8_MAX_TEXELS = 1024 * 1024  # the RGBA8 fallback only up to a 1024 x 1024 level 0
MAX_SHIFT = 2.0  # mean colour shift in 1/255 (a colour space conversion shifts by 10+)

# The models behind the variants (tools/remaster/README.md, the models' MANIFEST.txt).
MODELS = {
    "pbrify_v4": ("4x-PBRify_UpscalerV4", "Kim2091", "CC0-1.0",
                  "https://github.com/Kim2091/Kim2091-Models/releases/tag/4x-PBRify_UpscalerV4"),
    "ultrasharp_v2": ("4x-UltraSharpV2", "Kim2091", "CC BY-NC-SA 4.0 (non-commercial)",
                      "https://huggingface.co/Kim2091/UltraSharpV2"),
    "hdcube3": ("4xHDcube3_500k", "Venomalia",
                ["unclear: the repository's own \"HDcube Model License\" (use and",
                 "  redistribute unmodified, no claims on outputs); OpenModelDB lists",
                 "  CC BY-NC-SA 4.0. Treated as non-commercial."],
                "https://github.com/Venomalia/HDcube/releases/tag/3.0"),
}


# ------------------------------------------------------------------ mip chain

def _half(x, wrap):
    """Half size on each axis above 1 texel (Lanczos3, clamped to the
    footprint; rmcommon.down2 per axis)."""
    wy, wx = wrap
    if x.shape[0] > 1:
        x = rmcommon._down2_axis(x, 0, wy)
    if x.shape[1] > 1:
        x = rmcommon._down2_axis(x, 1, wx)
    return x


def coverage(alpha):
    return float((alpha >= 0.5).mean()) if alpha.size else 0.0


def binary_threshold(key, share):
    """The threshold on `key` that keeps `share` of the texels drawn
    (key >= t). Texels the filter left at alpha 0 stay off."""
    n = key.size
    k = int(round(share * n))
    if k <= 0:
        return 2.0
    return float(np.sort(key.ravel())[::-1][min(k, n) - 1])


def area_coverage(alpha0, shape):
    """The share of drawn level-0 texels under each texel of a level of
    `shape` (box filter of the binary level 0)."""
    h0, w0 = alpha0.shape
    h, w = shape
    return (alpha0 >= 0.5).astype(np.float32).reshape(h, h0 // h, w, w0 // w).mean(axis=(1, 3))


def next_level(img, mode, wrap, share=None, alpha0=None):
    """The next mip level of a float RGBA level (see the module docstring).
    Cut-outs: with `share` the threshold keeps that share drawn; ties of the
    filtered alpha (a binary picture filtered gives many equal values) are
    broken by the drawn share of level 0 under each texel (`alpha0`), then
    by a fixed pseudo-random order, so the share holds exactly."""
    rgb, a = img[..., :3], img[..., 3:4]
    if mode in ("opaque", "ignore"):
        out = _half(img, wrap)
        if mode == "opaque":
            out[..., 3] = 1.0
        return np.clip(out, 0.0, 1.0).astype(np.float32)
    a2 = np.clip(_half(a, wrap), 0.0, 1.0)
    pm = _half(rgb * a, wrap)
    straight = _half(rgb, wrap)
    rgb2 = np.where(a2 > 1.0 / 255.0, pm / np.maximum(a2, 1.0 / 255.0), straight)
    if mode == "cutout":
        if share is None:
            a2 = (a2 >= 0.5).astype(np.float32)
        else:
            key = a2[..., 0].astype(np.float64)
            if alpha0 is not None:
                key = key + 1e-3 * area_coverage(alpha0, key.shape)
            key = key + 1e-7 * np.random.default_rng(key.size).random(key.shape)
            key[a2[..., 0] <= 0.0] = -1.0
            a2 = (key >= binary_threshold(key, share)).astype(np.float32)[..., None]
    return np.concatenate([np.clip(rgb2, 0.0, 1.0), a2], axis=2).astype(np.float32)


def level_count(w, h):
    return int(math.log2(max(w, h))) + 1


def mip_chain(l0, l1, mode, wrap):
    """[l0, l1, ...] down to 1 x 1 (l1 may be None: then made from l0)."""
    h, w = l0.shape[:2]
    share = coverage(l0[..., 3]) if mode == "cutout" else None
    levels = [l0]
    for k in range(1, level_count(w, h)):
        if k == 1 and l1 is not None:
            levels.append(l1)
        else:
            levels.append(next_level(levels[-1], mode, wrap, share, l0[..., 3] if share is not None else None))
    return levels


# ------------------------------------------------------------------ DDS files

def write_rgba8_dds(path, levels):
    """A DDS file with a legacy header (32-bit RGBA masks) and every level
    (uint8 H x W x 4 arrays, rows as given)."""
    h, w = levels[0].shape[:2]
    many = len(levels) > 1
    flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | (0x20000 if many else 0)  # CAPS HEIGHT WIDTH PITCH PIXELFORMAT MIPMAPCOUNT
    head = struct.pack("<4sIIIIIII", b"DDS ", 124, flags, h, w, w * 4, 0, len(levels)) + b"\0" * 44
    head += struct.pack("<IIIIIIII", 32, 0x41, 0, 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000)  # RGB | ALPHAPIXELS
    head += struct.pack("<IIIII", 0x1000 | ((0x8 | 0x400000) if many else 0), 0, 0, 0, 0)  # TEXTURE (COMPLEX MIPMAP)
    with open(path, "wb") as f:
        f.write(head)
        for lv in levels:
            f.write(np.ascontiguousarray(lv, dtype=np.uint8).tobytes())


DXGI = {98: "bc7", 99: "bc7", 97: "bc7", 28: "rgba8", 29: "rgba8", 27: "rgba8", 71: "bc1", 77: "bc3"}


def dds_header(path):
    """(encoding, width, height, levels, data offset, file size) of a DDS
    file; raises ValueError for anything but one 2D texture."""
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        head = f.read(148)
    if len(head) < 128 or head[:4] != b"DDS " or struct.unpack_from("<I", head, 4)[0] != 124:
        raise ValueError("not a DDS file")
    height, width, _, depth, levels = struct.unpack_from("<IIIII", head, 12)
    levels = max(levels, 1)
    pf_flags, fourcc = struct.unpack_from("<I4s", head, 80)
    caps2 = struct.unpack_from("<I", head, 112)[0]
    if caps2 & 0x200 or caps2 & 0x200000:
        raise ValueError("a cube map or volume")
    if pf_flags & 4 and fourcc == b"DX10":
        fmt, dim, misc, array = struct.unpack_from("<IIII", head, 128)
        if dim != 3 or array != 1 or misc & 4:
            raise ValueError(f"not one 2D texture (dimension {dim}, array {array}, misc {misc})")
        return DXGI.get(fmt, f"dxgi{fmt}"), width, height, levels, 148, size
    if pf_flags & 4:
        return {b"DXT1": "bc1", b"DXT5": "bc3"}.get(fourcc, fourcc.decode("latin1")), width, height, levels, 128, size
    masks = struct.unpack_from("<IIIII", head, 88)
    if masks == (32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000):
        return "rgba8", width, height, levels, 128, size
    raise ValueError(f"unexpected pixel format {masks}")


def level_sizes(encoding, w, h, levels):
    """[(width, height, bytes)] per level."""
    out = []
    for k in range(levels):
        lw, lh = max(1, w >> k), max(1, h >> k)
        if encoding in ("bc7", "bc3"):
            n = ((lw + 3) // 4) * ((lh + 3) // 4) * 16
        elif encoding == "bc1":
            n = ((lw + 3) // 4) * ((lh + 3) // 4) * 8
        else:
            n = lw * lh * 4
        out.append((lw, lh, n))
    return out


def read_rgba8_levels(path):
    enc, w, h, levels, off, size = dds_header(path)
    if enc != "rgba8":
        raise ValueError(f"{path}: {enc}, not RGBA8")
    data = np.fromfile(path, dtype=np.uint8)
    out = []
    for lw, lh, n in level_sizes("rgba8", w, h, levels):
        out.append(data[off:off + n].reshape(lh, lw, 4))
        off += n
    if off != size:
        raise ValueError(f"{path}: {size - off} bytes after the last level")
    return out


def run(cmd, cwd=None):
    proc = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                          creationflags=(NO_WINDOW | BELOW_NORMAL) if os.name == "nt" else 0)
    if proc.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd)}\n{proc.stdout}\n{proc.stderr}")
    return proc.stdout


# ------------------------------------------------------------------ checks

def compare_level(want, got, mode):
    """(PSNR of the visible colour, mean shift of the visible colour and of
    the plain RGB in 1/255, max error) of a decoded level against the level
    that went in (uint8 arrays)."""
    w = want.astype(np.float32)
    g = got.astype(np.float32)
    if mode in ("opaque", "ignore"):
        dv = (g[..., :3] - w[..., :3])
        weight = np.ones(w.shape[:2] + (1,), np.float32)
        da = g[..., 3] - w[..., 3] if mode == "ignore" else np.zeros(w.shape[:2], np.float32)
    else:
        aw, ag = w[..., 3:4] / 255.0, g[..., 3:4] / 255.0
        dv = g[..., :3] * ag - w[..., :3] * aw
        weight = aw
        da = g[..., 3] - w[..., 3]
    mse = float((np.concatenate([dv.reshape(-1), da.reshape(-1)]) ** 2).mean())
    psnr = 99.0 if mse <= 1e-9 else min(99.0, 10.0 * math.log10(255.0 ** 2 / mse))
    wsum = float(weight.sum()) * 3.0
    shift = float(((g[..., :3] - w[..., :3]) * weight).sum() / wsum) if wsum > 0 else 0.0
    plain = float((g[..., :3] - w[..., :3]).mean())
    return psnr, shift, plain, int(np.abs(g - w).max())


# ------------------------------------------------------------------ main

def readme_text(stats, args, head):
    usa, other, total = stats["usa"], stats["other"], stats["packed"]
    lines = [
        "DW4 Remaster texture pack (4x / 2x)",
        "===================================",
        "",
        "Replacement textures for DW4_PC, the native PC port of Digimon World 4, in",
        "the port's texture pack format 2. {:,} textures: all {:,} that the game's".format(total, usa),
        "USA release loads, plus {:,} that only the other languages load. Each is a".format(other),
        "single DDS file with its whole mip chain: level 0 is 4 times the original",
        "size (Texture quality: Ultra), level 1 twice the original size (High).",
        "",
        "GAME-DERIVED CONTENT - PERSONAL USE ONLY",
        "----------------------------------------",
        "Every picture in this folder was computed from the textures of a copy of",
        "Digimon World 4 (GameCube). The game and its artwork belong to their rights",
        "holders. Use this pack only with your own copy of the game, for your own",
        "non-commercial play. Do not upload, share, sell or redistribute it, in",
        "whole or in part. It is not part of DW4_PC's release packages and is not",
        "covered by DW4_PC's licence.",
        "",
        "Install",
        "-------",
        "1. Keep this folder where you like (the folder that holds dw4pack.tsv).",
        "2. In the game: F1 > Settings > Graphics > Texture pack folder: choose",
        "   this folder, then Reload texture pack.",
        "   (settings.ini: [mods] textures=<this folder>)",
        "3. Texture quality: Ultra (4x) or High (2x). Original draws the game's own",
        "   textures. (settings.ini: [graphics] texture_quality=ultra|high|original)",
        "Textures load on a background thread; until a replacement is on the GPU",
        "the game's own texture is drawn. Texture memory (same menu) limits what",
        "stays loaded across stages.",
        "",
        "How it was made",
        "---------------",
        "Each texture class went through the method that suited it best in a",
        "comparison of upscalers (tools/remaster in the DW4_PC repository):",
        "",
    ]
    for line in stats["method_lines"]:
        lines.append("  " + line)
    lines += [
        "",
        "Bosses and painted portraits are curated: HDcube3 as the base, with",
        "UltraSharpV2's detail blended in through a mask where HDcube3 lost it.",
        "Colours were corrected towards the original where a model shifted them.",
        "Cut-outs (alpha-tested textures) keep binary alpha and their coverage in",
        "every mip level; tiling textures are filtered cyclically across their",
        "border. No sRGB conversion: the values are stored as the game shows them.",
        "",
        "Upscaling models and their licences",
        "-----------------------------------",
    ]
    for key in ("pbrify_v4", "ultrasharp_v2", "hdcube3"):
        name, author, licence, url = MODELS[key]
        licence = licence if isinstance(licence, list) else [licence]
        lines += [f"* {name} by {author} ({stats['by_model'].get(key, 0):,} textures)", f"  Licence: {licence[0]}"]
        lines += [f"  {more}" for more in licence[1:]] + [f"  {url}"]
    lines += [
        "* Lanczos with light sharpening and the text edge reconstruction (font",
        "  glyphs) are plain filters of the DW4_PC tools, no model.",
        "Two of the models carry non-commercial terms, one more reason this pack",
        "is for personal, non-commercial use only. The model authors have no part",
        "in this pack and do not endorse it.",
        "",
        "Files",
        "-----",
        "dw4pack.tsv    the manifest (format 2, docs/TEXTURES.md of DW4_PC): per",
        "               texture hash its file, encoding, scale of level 0 over the",
        "               original (4), level count and size. The columns after",
        "               `file` are information only: `quality` names the level",
        "               each quality draws, then alpha use, wrap, class, method,",
        "               names. Other encodings (ASTC in KTX2 for GLES / Android)",
        "               can be added as more rows of the same hashes later; a",
        "               build that does not read them skips those rows.",
        "tex/<hash>.dds BC7_UNORM ({:,} files) or R8G8B8A8 ({:,}): the font atlas and".format(
            stats["by_encoding"].get("bc7", 0), stats["by_encoding"].get("rgba8", 0)),
        "               the title's PRESS START, whose texels the game reads back,",
        "               and noisy effect, glow and UI pictures that BC7 would",
        "               blur (below 34 dB). Level 0 / 1 of the BC7 files: {:.1f} /".format(
            (stats["bc7_psnr_level0"] or {}).get("median", 0.0)),
        "               {:.1f} dB PSNR (median) against the pictures that went in.".format(
            (stats["bc7_psnr_level1"] or {}).get("median", 0.0)),
        "",
        "Size on disk: {:.0f} MiB. Built {} by tools/remaster/build_pack.py (DW4_PC".format(stats["mib"], stats["date"]),
        "branch feat-remaster, {}).".format(head),
        "",
    ]
    return "\n".join(lines)


METHOD_TEXT = [
    ("environment", "pbrify_v4", "stage materials, rock, wood, foliage: 4x-PBRify_UpscalerV4"),
    ("env_graphic", "ultrasharp_v2", "flat stage graphics (panels, signs, LED grids): 4x-UltraSharpV2"),
    ("env_glow", "lanczos_sharp", "additive stage overlays: Lanczos with light sharpening"),
    ("effect", "lanczos_sharp", "effects: Lanczos with light sharpening"),
    ("character", "hdcube3", "characters (BC1 skins): 4xHDcube3"),
    ("boss", "hdcube3", "bosses: 4xHDcube3, curated"),
    ("weapon", "hdcube3", "weapons: 4xHDcube3"),
    ("item", "hdcube3", "items: 4xHDcube3"),
    ("card", "hdcube3", "cards: 4xHDcube3"),
    ("portrait", "hdcube3", "painted portraits: 4xHDcube3, curated"),
    ("ui", "ultrasharp_v2", "UI pictures and multi-tone digits: 4x-UltraSharpV2"),
    ("loading", "ultrasharp_v2", "loading images: 4x-UltraSharpV2"),
    ("title", "ultrasharp_v2", "title screens: 4x-UltraSharpV2"),
    ("decal", "ultrasharp_v2", "ornaments: 4x-UltraSharpV2"),
    ("font", "pathb", "font: edge reconstruction of the glyphs, 4x-UltraSharpV2 for the button symbols"),
]


def model_of(variant):
    """The models a report variant used ("hdcube3+ultrasharp_v2 (12% masked)" -> both)."""
    v = variant.split(" (")[0]
    out = [m for m in MODELS if m in v]
    if v.startswith("pathb"):
        out.append("ultrasharp_v2")  # the font's button cells
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--final", required=True, help="finalize.py's output folder (master4x, high2x, report.tsv, classes.tsv)")
    ap.add_argument("--export", required=True, help="dw4_texpack export folder (manifest.tsv)")
    ap.add_argument("--out", required=True, help="the pack folder (new, empty or an earlier pack of this tool)")
    ap.add_argument("--report", required=True, help="folder for levels.tsv, coverage.tsv, build.json")
    ap.add_argument("--texconv", required=True)
    ap.add_argument("--name", default="DW4 Remaster (4x / 2x)")
    ap.add_argument("--rgba8", default=DEFAULT_RGBA8, help="hashes kept uncompressed (comma separated)")
    ap.add_argument("--only", default="", help="comma-separated hashes (a trial)")
    ap.add_argument("--batch", type=int, default=24, help="files per texconv call")
    ap.add_argument("--bc", default="", help="texconv -bc flags (e.g. x for BC7's 3-subset modes)")
    ap.add_argument("--resume", action="store_true", help="keep what REPORT/done.tsv lists")
    ap.add_argument("--head", default="", help="the tools' commit, for README.txt")
    args = ap.parse_args()

    t_start = time.perf_counter()
    manifest = rmcommon.read_manifest(args.export)
    report = {r["hash"]: r for r in rmcommon.read_tsv(os.path.join(args.final, "report.tsv"))}
    rgba8 = {h.strip().lower() for h in args.rgba8.split(",") if h.strip()}
    if os.path.isdir(args.out) and os.listdir(args.out) and not os.path.exists(os.path.join(args.out, MARKER)):
        sys.exit(f"{args.out} is neither empty nor an earlier pack of this tool")
    os.makedirs(os.path.join(args.out, "tex"), exist_ok=True)
    os.makedirs(args.report, exist_ok=True)
    open(os.path.join(args.out, MARKER), "w").close()

    only = {h.strip().lower() for h in args.only.split(",") if h.strip()}
    done_path = os.path.join(args.report, "done.tsv")
    done = {}
    if args.resume and os.path.exists(done_path):
        for line in open(done_path, encoding="utf-8"):
            rec = json.loads(line)
            if os.path.exists(os.path.join(args.out, rec["file"])):
                done[rec["hash"]] = rec
    else:
        open(done_path, "w").close()

    # What is packed, what is skipped and why.
    todo, skipped = [], {}
    for h, row in sorted(manifest.items()):
        if only and h not in only:
            continue
        if h in done:
            continue
        rep = report.get(h)
        p4 = os.path.join(args.final, "master4x", h + ".png")
        p2 = os.path.join(args.final, "high2x", h + ".png")
        if rep is None:
            skipped[h] = "not in report.tsv"
        elif "missing" in rep.get("flags", ""):
            skipped[h] = "no upscale (finalize: missing)"
        elif not os.path.exists(p4):
            skipped[h] = "no master4x picture"
        elif not os.path.exists(p2):
            skipped[h] = "no high2x picture"
        else:
            w, h_ = int(row["width"]), int(row["height"])
            todo.append((level_count(4 * w, 4 * h_), h))
    print(f"{len(todo)} textures to build, {len(done)} kept from an earlier run, {len(skipped)} skipped", flush=True)

    # Batches of one level count (texconv's -m applies to every file of a call).
    by_levels = collections.defaultdict(list)
    for n, h in todo:
        by_levels[n].append(h)
    batches = []
    for n in sorted(by_levels):
        hs = by_levels[n]
        for i in range(0, len(hs), args.batch):
            batches.append((n, hs[i:i + args.batch]))

    done_file = open(done_path, "a", encoding="utf-8")
    tmp_root = tempfile.mkdtemp(prefix="tmp_", dir=args.report)  # game-derived: next to the report, not in %TEMP%
    count = len(done)
    target = len(done) + len(todo)
    def encode(chains, n, encodings, bc_flag):
        """Writes, compresses and reads back the chains of one level count:
        {hash: {made, problems, quality, rows, bytes, psnr01}}."""
        tin, tbc, tback = (os.path.join(tmp_root, d) for d in ("in", "bc", "back"))
        for d in (tin, tbc, tback):
            shutil.rmtree(d, ignore_errors=True)
            os.makedirs(d)
        for h, ch in chains.items():
            write_rgba8_dds(os.path.join(tin if encodings[h] == "bc7" else tbc, h + ".dds"), ch["levels"])
        bc = [h for h in chains if encodings[h] == "bc7"]
        for i in range(0, len(bc), args.batch):
            cmd = [args.texconv, "-nologo", "-y", "-f", "BC7_UNORM", "-m", str(n), "-dx10", "-o", tbc]
            if bc_flag:
                cmd += ["-bc", bc_flag]
            run(cmd + [h + ".dds" for h in bc[i:i + args.batch]], cwd=tin)
        names = list(chains)
        for i in range(0, len(names), args.batch):
            # Read back: every level decoded by texconv (it also parses the header).
            run([args.texconv, "-nologo", "-y", "-f", "R8G8B8A8_UNORM", "-m", str(n), "-dx9", "-o", tback]
                + [h + ".dds" for h in names[i:i + args.batch]], cwd=tbc)
        result = {}
        for h, ch in chains.items():
            levels, mode, enc = ch["levels"], ch["mode"], encodings[h]
            made = os.path.join(tbc, h + ".dds")
            problems, quality = [], []
            fmt, dw, dh, dl, off, size = dds_header(made)
            want_w, want_h = levels[0].shape[1], levels[0].shape[0]
            sizes = level_sizes(enc, want_w, want_h, n)
            if fmt != enc or (dw, dh, dl) != (want_w, want_h, n):
                problems.append(f"header {fmt} {dw}x{dh} {dl} levels, wanted {enc} {want_w}x{want_h} {n}")
            if size != off + sum(s[2] for s in sizes):
                problems.append(f"file has {size} bytes, wanted {off + sum(s[2] for s in sizes)}")
            back = read_rgba8_levels(os.path.join(tback, h + ".dds"))
            lv_rows = []
            cov0 = coverage(levels[0][..., 3] / 255.0)
            for k, (want, got) in enumerate(zip(levels, back)):
                if got.shape != want.shape:
                    problems.append(f"level {k} decodes as {got.shape[1]}x{got.shape[0]}")
                    continue
                psnr, shift, plain, maxerr = compare_level(want, got, mode)
                cov = coverage(got[..., 3] / 255.0) if mode == "cutout" else None
                lv_rows.append({"level": k, "width": sizes[k][0], "height": sizes[k][1], "bytes": sizes[k][2],
                                "scale": 4 / 2 ** k, "psnr": round(psnr, 2), "shift": round(shift, 3), "plain_shift": round(plain, 3), "max_error": maxerr,
                                "coverage": None if cov is None else round(cov, 4)})
                if k <= 1 and psnr < BC7_RETRY:
                    quality.append(f"level {k}: PSNR {psnr:.1f} dB")
                # A colour space conversion shifts the mean of every level by
                # 10/255 and more; BC7 errors on a small or sparse level
                # (a few blocks of sparks) are a matter of its PSNR.
                if k <= 1 and abs(plain) > MAX_SHIFT:
                    problems.append(f"level {k}: colour shifted by {plain:+.2f}/255")
                if cov is not None and k <= 6 and sizes[k][0] * sizes[k][1] >= 256 and abs(cov - cov0) > 0.05:
                    problems.append(f"level {k}: coverage {cov:.3f}, level 0 {cov0:.3f}")
            psnr01 = min((r["psnr"] for r in lv_rows if r["level"] <= 1), default=0.0)
            result[h] = {"made": made, "problems": problems, "quality": quality, "rows": lv_rows, "bytes": size,
                         "psnr01": psnr01}
        return result

    def keep(h, ch, enc, res, note):
        rel = "tex/" + h + ".dds"
        shutil.move(res["made"], os.path.join(args.out, rel))
        rep = report[h]
        row = manifest[h]
        wrap = ch["wrap"]
        rec = {"hash": h, "encoding": enc, "scale": 4, "levels": len(ch["levels"]), "width": ch["levels"][0].shape[1],
               "height": ch["levels"][0].shape[0], "file": rel, "quality": "ultra=0,high=1", "alpha": ch["mode"],
               "wrap": ("t" if wrap[0] else "") + ("s" if wrap[1] else "") or "-",
               "class": rep.get("class", "-"), "method": rep.get("variant", "-"), "usa": row.get("usa", "0"),
               "lang": row.get("languages", "-"), "names": row.get("names", "-"), "bytes": res["bytes"],
               "seconds": round(ch["seconds"], 3), "encode_note": note, "level_rows": res["rows"]}
        done[h] = rec
        done_file.write(json.dumps(rec) + "\n")

    try:
        for n, hashes in batches:
            chains = {}
            for h in hashes:
                row = manifest[h]
                t0 = time.perf_counter()
                mode = rmcommon.alpha_mode(row)
                wrap = rmcommon.wrap_axes(row)
                w, hh = int(row["width"]), int(row["height"])
                l0 = rmcommon.load_rgba(os.path.join(args.final, "master4x", h + ".png"))
                l1 = rmcommon.load_rgba(os.path.join(args.final, "high2x", h + ".png"))
                if l0.shape[:2] != (4 * hh, 4 * w) or l1.shape[:2] != (2 * hh, 2 * w):
                    skipped[h] = f"sizes {l0.shape[1]}x{l0.shape[0]} / {l1.shape[1]}x{l1.shape[0]} are not 4x / 2x of {w}x{hh}"
                    continue
                if mode == "opaque":
                    l0[..., 3] = 1.0
                    l1[..., 3] = 1.0
                levels = [rmcommon.to_u8(x) for x in mip_chain(l0, l1, mode, wrap)]
                assert len(levels) == n
                chains[h] = {"levels": levels, "mode": mode, "wrap": wrap, "seconds": time.perf_counter() - t0}
            # BC7 first; where level 0 or 1 loses too much (noisy pixel art),
            # BC7 again with its 3-subset modes, then RGBA8 (not too large).
            enc = {h: ("rgba8" if h in rgba8 else "bc7") for h in chains}
            res = encode(chains, n, enc, args.bc)
            retry = {}
            for h, r in res.items():
                if r["problems"]:
                    skipped[h] = "; ".join(r["problems"])
                    print(f"{h}: SKIPPED {skipped[h]}", flush=True)
                elif r["quality"] and enc[h] == "bc7":
                    retry[h] = r["psnr01"]
                else:
                    keep(h, chains[h], enc[h], r, "-")
            if retry:
                if args.bc == "x":  # the first pass had the 3-subset modes already
                    res2 = {h: res[h] for h in retry}
                else:
                    res2 = encode({h: chains[h] for h in retry}, n, {h: "bc7" for h in retry}, "x")
                fallback = {}
                for h, r in res2.items():
                    best_psnr = max(r["psnr01"], retry[h])
                    if not r["problems"] and r["psnr01"] >= MIN_PSNR:
                        note = (f"BC7, level 0/1 PSNR {r['psnr01']:.1f} dB" if args.bc == "x" else
                                f"BC7 -bc x: level 0/1 PSNR {retry[h]:.1f} -> {r['psnr01']:.1f} dB")
                        keep(h, chains[h], "bc7", r, note)
                    elif chains[h]["levels"][0].size // 4 <= RGBA8_MAX_TEXELS:
                        fallback[h] = best_psnr
                    else:
                        skipped[h] = f"BC7 level 0/1 PSNR {best_psnr:.1f} dB, too large for RGBA8"
                        print(f"{h}: SKIPPED {skipped[h]}", flush=True)
                if fallback:
                    sub = {h: chains[h] for h in fallback}
                    res3 = encode(sub, n, {h: "rgba8" for h in sub}, "")
                    for h, r in res3.items():
                        if r["problems"]:
                            skipped[h] = "; ".join(r["problems"])
                            print(f"{h}: SKIPPED {skipped[h]}", flush=True)
                        else:
                            keep(h, chains[h], "rgba8", r, f"RGBA8: BC7 level 0/1 PSNR {fallback[h]:.1f} dB")
            count = len(done)
            done_file.flush()
            print(f"{count}/{target} ({n} levels, {len(hashes)} files, {len(retry)} retried) "
                  f"{time.perf_counter() - t_start:.0f} s", flush=True)
    finally:
        done_file.close()
        shutil.rmtree(tmp_root, ignore_errors=True)

    # The manifest, in hash order.
    recs = [done[h] for h in sorted(done)]
    cols = ["hash", "encoding", "scale", "levels", "width", "height", "file", "quality", "alpha", "wrap", "class", "method",
            "usa", "names"]
    with open(os.path.join(args.out, "dw4pack.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("#dw4pack\t2\n")
        f.write(f"#name\t{args.name}\n")
        f.write("# Game-derived: personal use only, see README.txt. Made by tools/remaster/build_pack.py.\n")
        f.write("# quality: the level each Texture quality draws (Ultra = level 0 at 4x, High = level 1 at 2x).\n")
        f.write("\t".join(cols) + "\n")
        for r in recs:
            f.write("\t".join(str(r[c]).replace("\t", " ") for c in cols) + "\n")

    with open(os.path.join(args.report, "levels.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("hash\tencoding\tlevel\twidth\theight\tscale\tbytes\tpsnr\tshift\tplain_shift\tmax_error\tcoverage\n")
        for r in recs:
            for lv in r["level_rows"]:
                f.write(f"{r['hash']}\t{r['encoding']}\t{lv['level']}\t{lv['width']}\t{lv['height']}\t{lv['scale']:g}\t{lv['bytes']}\t"
                        f"{lv['psnr']}\t{lv['shift']}\t{lv['plain_shift']}\t{lv['max_error']}\t{'-' if lv['coverage'] is None else lv['coverage']}\n")

    with open(os.path.join(args.report, "coverage.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("hash\tnames\tusa\tlanguages\tclass\tmethod\tstatus\tfile\tnote\n")
        for h, row in sorted(manifest.items()):
            if only and h not in only:
                continue
            rep = report.get(h, {})
            status = "packed" if h in done else "skipped"
            f.write("\t".join([h, row.get("names", "-"), row.get("usa", "0"), row.get("languages", "-"), rep.get("class", "-"),
                               rep.get("variant", "-"), status, done[h]["file"] if h in done else "-",
                               skipped.get(h, "-") if status == "skipped" else done[h].get("encode_note", "-")]) + "\n")

    # Figures for build.json and README.txt.
    total = sum(r["bytes"] for r in recs)
    by_model = collections.Counter()
    for r in recs:
        for m in model_of(r["method"]):
            by_model[m] += 1
    classes = collections.Counter(r["class"] for r in recs)
    method_lines = [f"{text} ({classes.get(cls, 0):,})" for cls, _, text in METHOD_TEXT if classes.get(cls)]
    psnr0 = [r["level_rows"][0]["psnr"] for r in recs if r["encoding"] == "bc7"]
    psnr1 = [r["level_rows"][1]["psnr"] for r in recs if r["encoding"] == "bc7"]
    per_level_bytes = collections.Counter()
    for r in recs:
        for lv in r["level_rows"]:
            per_level_bytes[lv["level"]] += lv["bytes"]
    usa_all = {h for h, row in manifest.items() if row.get("usa") == "1"}
    stats = {
        "packed": len(recs), "skipped": skipped, "usa": sum(1 for r in recs if r["usa"] == "1"),
        "other": sum(1 for r in recs if r["usa"] != "1"), "usa_total": len(usa_all),
        "usa_missing": sorted(usa_all - set(done)) if not only else [],
        "by_encoding": dict(collections.Counter(r["encoding"] for r in recs)),
        "by_class": dict(classes), "by_model": dict(by_model), "method_lines": method_lines,
        "levels": dict(collections.Counter(r["levels"] for r in recs)),
        "bytes": total, "mib": total / 1048576.0,
        "bytes_per_level": {str(k): v for k, v in sorted(per_level_bytes.items())},
        "high_bytes": total - per_level_bytes.get(0, 0),
        "bc7_psnr_level0": {"min": min(psnr0), "median": float(np.median(psnr0))} if psnr0 else None,
        "bc7_psnr_level1": {"min": min(psnr1), "median": float(np.median(psnr1))} if psnr1 else None,
        "encode_notes": {r["hash"]: r.get("encode_note", "-") for r in recs if r.get("encode_note", "-") != "-"},
        "seconds": round(time.perf_counter() - t_start, 1),
        "date": datetime.date.today().isoformat(),
    }
    with open(os.path.join(args.report, "build.json"), "w", encoding="utf-8") as f:
        json.dump({k: v for k, v in stats.items() if k != "method_lines"}, f, indent=1)
    with open(os.path.join(args.out, "README.txt"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(readme_text(stats, args, args.head or "see the manifest"))
    print(f"{len(recs)} textures, {stats['mib']:.1f} MiB, {len(skipped)} skipped; "
          f"USA {stats['usa']} of {len(usa_all)} packed; {stats['seconds']} s")
    for h, why in sorted(skipped.items()):
        print(f"skipped {h}: {why}")
    return 1 if (stats["usa_missing"] and not only) else 0


if __name__ == "__main__":
    sys.exit(main())
