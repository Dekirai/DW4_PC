#!/usr/bin/env python3
"""Before/after contact sheets for a correction round: per texture one row
with the original (nearest 4x), the earlier 4x result, the new one, and
crops (source texels at 8x) of the earlier and the new result where they
differ most and at the border row where the change is largest (edge bleed).

    python -I tools/remaster/compare_sheets.py --export E:/DW4Remaster/export \
        --classes E:/DW4Remaster/out/classes.tsv --before E:/DW4Remaster/texfix/master4x_prev \
        --after E:/DW4Remaster/out/master4x --list E:/DW4Remaster/texfix/redo.tsv \
        --out E:/DW4Remaster/out/sheets/texfix [--rows 8] [--first HASH,...]

`--list` is a TSV with a `hash` column (and optionally `why`); rows are
sorted by the largest change (the review's hashes given with --first come
first). Writes OUT/texfix_NNN.png and OUT/index.tsv (sheet, hash, change).
Alpha is shown over a checkerboard.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rmcommon  # noqa: E402

rmcommon.lower_priority()

import cv2  # noqa: E402
import numpy as np  # noqa: E402
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

PIC = 256  # full pictures fitted into PIC x PIC
CROP = 16  # crop edge in source texels, shown at PIC (16x)
GUTTER = 8
TEXT_W = 300


def font(size):
    for name in ("consola.ttf", "DejaVuSansMono.ttf", "arial.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def composite(img, mode, checker=16):
    rgb = img[..., :3]
    if mode in ("opaque", "ignore"):
        return rgb
    h, w = img.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    board = np.where(((yy // checker) + (xx // checker)) % 2 == 0, 0.30, 0.55).astype(np.float32)[..., None]
    if mode == "additive":
        return np.clip(board * 0.4 + rgb * img[..., 3:4], 0, 1)
    a = img[..., 3:4]
    return rgb * a + board * (1 - a)


def fit(img, size):
    h, w = img.shape[:2]
    s = size / max(h, w)
    interp = cv2.INTER_NEAREST if s >= 1 else cv2.INTER_AREA
    out = cv2.resize(img, (max(1, int(round(w * s))), max(1, int(round(h * s)))), interpolation=interp)
    canvas = np.full((size, size, 3), 0.12, np.float32)
    canvas[:out.shape[0], :out.shape[1]] = out
    return canvas


def to_pil(img):
    return Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8))


def change_map(a, b, mode):
    """Per source texel: mean absolute difference of the visible pictures."""
    va = composite(a, mode, 8)
    vb = composite(b, mode, 8)
    d = np.abs(va - vb).mean(axis=2)
    if mode not in ("opaque", "ignore"):
        d = np.maximum(d, np.abs(a[..., 3] - b[..., 3]))
    h, w = d.shape
    return cv2.resize(d, (w // 4, h // 4), interpolation=cv2.INTER_AREA)


def crop(img4, cx, cy, cw, ch):
    x0 = int(np.clip(cx - cw // 2, 0, img4.shape[1] // 4 - cw)) * 4
    y0 = int(np.clip(cy - ch // 2, 0, img4.shape[0] // 4 - ch)) * 4
    return img4[y0:y0 + ch * 4, x0:x0 + cw * 4]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--export", required=True)
    ap.add_argument("--classes", required=True)
    ap.add_argument("--before", required=True, help="folder of the earlier <hash>.png 4x results")
    ap.add_argument("--after", required=True, help="folder of the new <hash>.png 4x results")
    ap.add_argument("--list", required=True, help="TSV with a hash column (why optional)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--rows", type=int, default=8)
    ap.add_argument("--first", default="", help="comma-separated hashes shown first (in this order)")
    args = ap.parse_args()

    manifest = rmcommon.read_manifest(args.export)
    classes = {r["hash"]: r for r in rmcommon.read_tsv(args.classes)}
    items = rmcommon.read_tsv(args.list)
    first = [h for h in args.first.split(",") if h]
    os.makedirs(args.out, exist_ok=True)
    f_s = font(15)

    rows = []
    for it in items:
        h = it["hash"]
        pb, pa = os.path.join(args.before, h + ".png"), os.path.join(args.after, h + ".png")
        if not (os.path.exists(pb) and os.path.exists(pa)):
            continue
        row = manifest[h]
        b, a = rmcommon.load_rgba(pb), rmcommon.load_rgba(pa)
        mode = rmcommon.alpha_mode(row)
        if b.shape != a.shape:
            continue
        cm = change_map(b, a, mode)
        rows.append((h, it.get("why", "-"), float(cm.max()), float(cm.mean()), cm))
    order = {h: i for i, h in enumerate(first)}
    rows.sort(key=lambda r: (order.get(r[0], len(order)), -r[2]))

    index = open(os.path.join(args.out, "index.tsv"), "w", encoding="utf-8", newline="\n")
    index.write("sheet\thash\tnames\tclass\twhy\tmax_change\tmean_change\n")
    width = TEXT_W + 5 * (PIC + GUTTER) + GUTTER
    for s in range(0, len(rows), args.rows):
        part = rows[s:s + args.rows]
        sheet = Image.new("RGB", (width, len(part) * (PIC + 30) + GUTTER), (24, 24, 24))
        d = ImageDraw.Draw(sheet)
        name = f"texfix_{s // args.rows + 1:03d}.png"
        for i, (h, why, mx, mean, cm) in enumerate(part):
            row = manifest[h]
            c = classes.get(h, {})
            mode = rmcommon.alpha_mode(row)
            src = rmcommon.load_rgba(os.path.join(args.export, "textures", row["file"]))
            b = rmcommon.load_rgba(os.path.join(args.before, h + ".png"))
            a = rmcommon.load_rgba(os.path.join(args.after, h + ".png"))
            y = GUTTER + i * (PIC + 30)
            lines = [h, row["names"][:34], f"{row['width']}x{row['height']} {mode}",
                     f"{c.get('class', '-')} / {c.get('model', '-')}", f"wrap {c.get('wrap', '-')} pre {c.get('pre', '-')}",
                     f"why {why}"[:36], f"change max {mx:.3f} mean {mean:.4f}"]
            for k, t in enumerate(lines):
                d.text((GUTTER, y + 20 + k * 19), t, fill=(255, 230, 120) if k == 0 else (210, 210, 210), font=f_s)
            hh, ww = cm.shape
            cy, cx = np.unravel_index(int(np.argmax(cm)), cm.shape)
            cw, ch = min(CROP, ww), min(CROP, hh)
            pics = [("original", rmcommon.nearest(src, 4)), ("before", b), ("after", a),
                    ("before (most changed)", crop(b, cx, cy, cw, ch)), ("after (most changed)", crop(a, cx, cy, cw, ch))]
            x = TEXT_W
            for label, p in pics:
                sheet.paste(to_pil(fit(composite(p, mode, 16 if p.shape[0] > 128 else 4), PIC)), (x, y + 22))
                d.text((x, y + 2), label, fill=(255, 255, 255), font=f_s)
                x += PIC + GUTTER
            index.write(f"{name}\t{h}\t{row['names']}\t{c.get('class', '-')}\t{why}\t{mx:.4f}\t{mean:.5f}\n")
        sheet.save(os.path.join(args.out, name))
    index.close()
    print(f"{len(rows)} textures on {(len(rows) + args.rows - 1) // args.rows} sheets -> {args.out}")


if __name__ == "__main__":
    main()
