#!/usr/bin/env python3
"""Turns a folder of upscaled replacement PNGs (make_pack.py's output, or any
loose files named ..._<w>x<h>_<hash>.png after the original's size) into a
texture pack of format 2 (docs/TEXTURES.md): DDS files with their whole mip
chain, made with texconv, and the manifest dw4pack.tsv.

    E:/DW4Remaster/venv/Scripts/python.exe -I tools/remaster/make_dds_pack.py \
        --pngs E:/DW4Remaster/pilot/pack --export E:/DW4Remaster/export \
        --out E:/DW4Remaster/pilot/pack_dds --texconv "C:/Program Files/.../texconv.exe"

Per texture: BC7_UNORM (no sRGB conversion: the game samples the stored
values as GX does), R8G8B8A8_UNORM for the hashes given with --rgba8 (by
default the font atlas and the title's PRESS START, whose pixels the game
reads back), the mips made by texconv with the wrap the export's `tiles` /
`uv_edges` name, alpha separate, and the alpha coverage kept for cut-outs
(at the material's reference). Every file is checked after it is written:
its header against the manifest row, its level 0 decoded back with texconv
against the PNG (a colour space conversion would show as a shift).

--out must be new, empty or an earlier pack of this tool (marked by
.dw4_dds_pack); its files are replaced. Runs at below-normal priority.
Game-derived files stay where the command line puts them (never in the
repository, never in a release).
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rmcommon  # noqa: E402

rmcommon.lower_priority()

import numpy as np  # noqa: E402

MARKER = ".dw4_dds_pack"
NAME = re.compile(r"_(\d+)x(\d+)_([0-9a-fA-F]{16})\.png$")
DEFAULT_RGBA8 = "faccbeda501eefc1,ac566f5a15c2a226"  # font00.png, mojigc.png
NO_WINDOW = 0x08000000
BELOW_NORMAL = 0x00004000


def dds_info(path):
    """(format name, width, height, levels, data offset) of a DDS file."""
    with open(path, "rb") as f:
        head = f.read(148)
    if head[:4] != b"DDS " or struct.unpack_from("<I", head, 4)[0] != 124:
        raise ValueError(f"{path}: not a DDS file")
    height, width = struct.unpack_from("<II", head, 12)
    levels = max(struct.unpack_from("<I", head, 28)[0], 1)
    flags, fourcc = struct.unpack_from("<I4s", head, 80)
    if flags & 4 and fourcc == b"DX10":
        fmt = struct.unpack_from("<I", head, 128)[0]
        name = {98: "bc7", 99: "bc7", 28: "rgba8", 29: "rgba8", 71: "bc1", 77: "bc3"}.get(fmt, f"dxgi{fmt}")
        return name, width, height, levels, 148
    if flags & 4:
        return {b"DXT1": "bc1", b"DXT5": "bc3"}.get(fourcc, fourcc.decode("latin1")), width, height, levels, 128
    masks = struct.unpack_from("<IIII", head, 92)
    if masks == (0xFF, 0xFF00, 0xFF0000, 0xFF000000):
        return "rgba8", width, height, levels, 128
    raise ValueError(f"{path}: unexpected pixel format")


def run(cmd):
    proc = subprocess.run(cmd, capture_output=True, text=True, creationflags=(NO_WINDOW | BELOW_NORMAL) if os.name == "nt" else 0)
    if proc.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd)}\n{proc.stdout}\n{proc.stderr}")
    return proc.stdout


def decode_back(texconv, dds, tmp):
    """Level 0 of a DDS file as RGBA8 (texconv's decoder)."""
    out = os.path.join(tmp, "check")
    os.makedirs(out, exist_ok=True)
    run([texconv, "-nologo", "-y", "-m", "1", "-f", "R8G8B8A8_UNORM", "-ft", "dds", "-dx9", "-o", out, dds])
    path = os.path.join(out, os.path.basename(dds))
    _, w, h, _, off = dds_info(path)
    data = np.fromfile(path, dtype=np.uint8, offset=off, count=w * h * 4)
    os.remove(path)
    return data.reshape(h, w, 4)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pngs", required=True, help="folder of ..._<w>x<h>_<hash>.png replacements")
    ap.add_argument("--export", required=True, help="dw4_texpack export folder (manifest.tsv)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--texconv", required=True)
    ap.add_argument("--name", default="DW4 Remaster")
    ap.add_argument("--rgba8", default=DEFAULT_RGBA8, help="hashes kept uncompressed (comma separated)")
    ap.add_argument("--nogpu", action="store_true", help="texconv's CPU BC7 encoder")
    args = ap.parse_args()

    manifest = rmcommon.read_manifest(args.export)
    rgba8 = {h.strip().lower() for h in args.rgba8.split(",") if h.strip()}
    if os.path.isdir(args.out) and os.listdir(args.out) and not os.path.exists(os.path.join(args.out, MARKER)):
        sys.exit(f"{args.out} is neither empty nor an earlier pack of this tool")
    os.makedirs(os.path.join(args.out, "tex"), exist_ok=True)
    open(os.path.join(args.out, MARKER), "w").close()

    rows, problems = [], []
    pngs = []
    for base, _, files in os.walk(args.pngs):
        pngs += [os.path.join(base, f) for f in files if NAME.search(f)]
    with tempfile.TemporaryDirectory(prefix="dw4_dds_pack_") as tmp:
        for png in sorted(pngs):
            m = NAME.search(os.path.basename(png))
            ow, oh, hash_ = int(m.group(1)), int(m.group(2)), m.group(3).lower()
            img = rmcommon.load_rgba(png)
            h, w = img.shape[:2]
            if w % ow or h % oh or w // ow != h // oh or (w // ow) & (w // ow - 1):
                problems.append(f"{os.path.basename(png)}: {w}x{h} is no power-of-two scale of {ow}x{oh}")
                continue
            scale = w // ow
            row = manifest.get(hash_)
            mode = rmcommon.alpha_mode(row) if row else ("cutout" if np.all((img[..., 3] == 0) | (img[..., 3] == 1)) else "blend")
            wrap_y, wrap_x = rmcommon.wrap_axes(row) if row else (False, False)
            encoding = "rgba8" if hash_ in rgba8 else "bc7"
            cmd = [args.texconv, "-nologo", "-y", "-m", "0", "-f", "R8G8B8A8_UNORM" if encoding == "rgba8" else "BC7_UNORM",
                   "-sepalpha", "-o", tmp]
            if wrap_x or wrap_y:
                cmd.append("-wrap")
            if mode == "cutout" and row:
                thr, _ = rmcommon.cutout_threshold(row)
                cmd += ["--keep-coverage", f"{thr:.3f}"]
            if encoding == "bc7":
                cmd.append("-dx10")
                if args.nogpu:
                    cmd.append("-nogpu")
            else:
                cmd.append("-dx9")
            # (texconv names its output after the input; a copy under the hash avoids clashes.)
            src = os.path.join(tmp, hash_ + ".png")
            shutil.copyfile(png, src)
            run(cmd + [src])
            made = os.path.join(tmp, hash_ + ".dds")
            fmt, dw, dh, levels, _ = dds_info(made)
            if fmt != encoding or (dw, dh) != (w, h):
                problems.append(f"{hash_}: texconv wrote {fmt} {dw}x{dh}, wanted {encoding} {w}x{h}")
                continue
            # Level 0 decoded back against the PNG: no colour space shift.
            back = decode_back(args.texconv, made, tmp).astype(np.float32) / 255.0
            a = img[..., 3:4]
            shift = float(((back[..., :3] - img[..., :3]) * a).sum() / max(float(a.sum()) * 3, 1.0))
            err = float(np.abs(back - img).mean())
            if abs(shift) > 0.02:
                problems.append(f"{hash_}: level 0 shifted by {shift:+.3f} (a colour space conversion?)")
                continue
            dest = os.path.join(args.out, "tex", hash_ + ".dds")
            shutil.move(made, dest)
            os.remove(src)
            rows.append((hash_, encoding, scale, levels, w, h, "tex/" + hash_ + ".dds", mode, f"{err:.4f}",
                         (row or {}).get("names", "-")))
            print(f"{hash_} {encoding} {w}x{h} x{scale} {levels} levels {mode} mean error {err:.4f} shift {shift:+.4f}", flush=True)

    with open(os.path.join(args.out, "dw4pack.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("#dw4pack\t2\n")
        f.write(f"#name\t{args.name}\n")
        f.write("# made by tools/remaster/make_dds_pack.py from " + os.path.abspath(args.pngs).replace("\\", "/") + "\n")
        f.write("hash\tencoding\tscale\tlevels\twidth\theight\tfile\talpha\tbc_error\tnames\n")
        for r in rows:
            f.write("\t".join(str(v) for v in r) + "\n")
    total = sum(os.path.getsize(os.path.join(args.out, r[6])) for r in rows)
    print(f"{len(rows)} textures, {total / 1048576:.1f} MiB, {len(problems)} problems")
    for p in problems:
        print("problem:", p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
