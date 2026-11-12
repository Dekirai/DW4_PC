#!/usr/bin/env python3
"""Contact sheets of the pilot: per texture original (nearest 4x) | Lanczos
4x | the class's model variants, with two zoomed crops (the most detailed
region; for tiling textures the corner where four copies meet) and the
colour fidelity numbers of upscale.py; plus an overview of all textures and
a palette variant check.

    python -I tools/remaster/contact_sheets.py --export E:/DW4Remaster/export \
        --selection E:/DW4Remaster/pilot/selection.tsv --up E:/DW4Remaster/pilot/up \
        --out E:/DW4Remaster/pilot/sheets

Sheets are PNG, at most --max-width pixels wide (default 4096). Pictures of
2D classes (UI, font, title, loading, cards) are shown upright (the export
keeps the decoder's row order, in which they stand upside down).
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

from rmcommon import alpha_mode, load_rgba, nearest, wrap_axes  # noqa: E402

LABELS = {
    "original": "Original (nearest 4x)",
    "lanczos": "Lanczos 4x (only scaled)",
    "lanczos_sharp": "Lanczos 4x + light sharpening",
    "pbrify_v4": "4x-PBRify_UpscalerV4 (DAT2, bf16)",
    "pbrify_v3": "4x-PBRify_RPLKSRd_V3 (RealPLKSR, fp16)",
    "ultrasharp_v2": "4x-UltraSharpV2 (DAT2, bf16)",
    "hdcube3": "4xHDcube3 (ESRGAN, fp16)",
    "pathb": "Path B: edge reconstruction",
}
UPRIGHT = {"ui", "font", "title", "loading", "card"}
GUTTER = 8
BG = (24, 24, 32)


def font(size):
    for name in ("arial.ttf", "DejaVuSans.ttf"):
        for base in (os.path.join(os.environ.get("WINDIR", "C:/Windows"), "Fonts"), "/usr/share/fonts/truetype/dejavu"):
            p = os.path.join(base, name)
            if os.path.exists(p):
                return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def composite(img, mode, checker=16):
    """RGB for display: additive over black, alpha over a grey checker."""
    rgb, a = img[..., :3], img[..., 3:4]
    if mode == "additive":
        return np.clip(rgb * a, 0, 1)
    if mode in ("opaque", "ignore"):
        return rgb
    h, w = img.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    chk = np.where(((yy // checker) + (xx // checker)) % 2 == 0, 0.32, 0.44).astype(np.float32)[..., None]
    return rgb * a + chk * (1 - a)


def fit(img, max_w, max_h):
    """Scales a picture to fit: area means when shrinking, nearest when growing."""
    h, w = img.shape[:2]
    s = min(max_w / w, max_h / h)
    nw, nh = max(1, int(round(w * s))), max(1, int(round(h * s)))
    interp = cv2.INTER_AREA if s < 1 else cv2.INTER_NEAREST
    return cv2.resize(img, (nw, nh), interpolation=interp)


def to_pil(img):
    return Image.fromarray(np.clip(np.rint(img * 255), 0, 255).astype(np.uint8))


def detail_centres(src, cw, ch, count=2):
    """Centres (source texels) of the most detailed windows of cw x ch."""
    h, w = src.shape[:2]
    lum = (src[..., :3] @ np.array([0.299, 0.587, 0.114], np.float32)) * src[..., 3]
    energy = np.abs(cv2.Laplacian(lum, cv2.CV_32F)) + np.abs(cv2.Laplacian(src[..., 3], cv2.CV_32F))
    win = cv2.boxFilter(energy, -1, (cw, ch), normalize=True, borderType=cv2.BORDER_CONSTANT)
    centres = []
    for _ in range(count):
        y, x = np.unravel_index(int(np.argmax(win)), win.shape)
        x = int(np.clip(x, cw // 2, w - (cw - cw // 2)))
        y = int(np.clip(y, ch // 2, h - (ch - ch // 2)))
        centres.append((x, y))
        win[max(0, y - ch):y + ch, max(0, x - cw):x + cw] = -1
    return centres


def crop4(img4, cx, cy, cw, ch, tiled=False):
    """A cw x ch source-texel window of a 4x picture (around cx, cy); with
    `tiled`, of the picture repeated 2 x 2 around the corner of the copies."""
    if tiled:
        img4 = np.tile(img4, (2, 2, 1))
        h4, w4 = img4.shape[0] // 2, img4.shape[1] // 2
        x0, y0 = w4 - cw * 2, h4 - ch * 2
    else:
        x0, y0 = (cx - cw // 2) * 4, (cy - ch // 2) * 4
    return img4[y0:y0 + ch * 4, x0:x0 + cw * 4]


def load_metrics(up_dir, variant):
    path = os.path.join(up_dir, variant, "metrics.tsv")
    if not os.path.exists(path):
        return {}
    return {r["hash"]: r for r in rmcommon.read_tsv(path)}


def metric_text(m):
    if not m:
        return "-"
    return f"dE2000 {m['de_mean']} mean / {m['de_p95']} p95   PSNR {m['psnr']} dB   alpha {m['psnr_alpha']} dB"


def sheet_for(sel, row, up_dir, export_dir, metrics, max_width):
    variants = sel["variants"].split(",")
    columns = ["original"] + variants
    mode = alpha_mode(row)
    src = load_rgba(os.path.join(export_dir, "textures", row["file"]))
    pics = {"original": nearest(src, 4)}
    for v in variants:
        p = os.path.join(up_dir, v, row["file"])
        pics[v] = load_rgba(p) if os.path.exists(p) else None
    upright = sel["class"] in UPRIGHT
    n = len(columns)
    cw_px = min(720, (max_width - GUTTER * (n + 1)) // n)
    h, w = src.shape[:2]
    wy, wx = wrap_axes(row)
    seam = sel["class"] in ("environment", "palette") and (wy or wx)
    # Crop windows in source texels, both shown at the same zoom.
    # (Small pictures: half of them, so the crops show more than the full view.)
    cw = min(40, w // 2 if w <= 64 else 40, w)
    ch = min(40, h // 2 if h <= 64 else 40, h)
    cw, ch = max(cw, min(w, 8)), max(ch, min(h, 8))
    centres = detail_centres(src, cw, ch, 1 if seam else 2)
    crops = [("detail", centres[0], False)]
    crops.append(("tiled 2x2 (seams)", None, True) if seam else ("detail 2", centres[1], False))
    zoom_h = int(cw_px * ch / cw)

    full_h = max(1, min(cw_px, int(cw_px * h / w)))
    title_h, label_h, text_h = 64, 26, 50
    height = title_h + label_h + full_h + GUTTER + 2 * (zoom_h + 22 + GUTTER) + text_h + GUTTER
    width = GUTTER + n * (cw_px + GUTTER)
    sheet = Image.new("RGB", (width, height), BG)
    d = ImageDraw.Draw(sheet)
    f_title, f_label, f_small = font(24), font(17), font(15)
    d.text((GUTTER, 6), f"{sel['slot']}  {row['names']}  {w}x{h} {row['format']}  hash {row['hash']}", fill=(255, 230, 120), font=f_title)
    wrap_txt = ("t" if wy else "") + ("s" if wx else "") or "-"
    d.text((GUTTER, 36), f"class {sel['class']}, alpha {mode}, wrap-padded axes {wrap_txt}, "
           f"blend {row['blend']}, alpha test {row['alpha_test']}{', shown upright' if upright else ''}  -  {sel['note']}",
           fill=(200, 200, 200), font=f_small)
    for i, col in enumerate(columns):
        x = GUTTER + i * (cw_px + GUTTER)
        y = title_h
        d.text((x, y), LABELS.get(col, col), fill=(255, 255, 255), font=f_label)
        y += label_h
        pic = pics[col]
        if pic is None:
            d.text((x + 10, y + 10), "missing", fill=(255, 80, 80), font=f_label)
            continue
        disp = composite(pic, mode)
        if upright:
            disp = disp[::-1]
        full = fit(disp, cw_px, full_h)
        sheet.paste(to_pil(full), (x, y))
        y += full_h + GUTTER
        for label, centre, tiled in crops:
            part = pic
            if upright:
                part = part[::-1]
            if tiled:
                c = crop4(part, 0, 0, cw, ch, tiled=True)
            else:
                cx, cy = centre
                if upright:
                    cy = h - cy
                c = crop4(part, cx, cy, cw, ch)
            cd = composite(c, mode, checker=8)
            zoomed = cv2.resize(cd, (cw_px, zoom_h), interpolation=cv2.INTER_NEAREST)
            d.text((x, y), f"{label}: {cw}x{ch} texels, 4x pixels at {cw_px / (cw * 4):.1f}x", fill=(170, 170, 170), font=f_small)
            sheet.paste(to_pil(zoomed), (x, y + 20))
            y += zoom_h + 22 + GUTTER
        m = metrics.get(col, {}).get(row["hash"]) if col != "original" else None
        if col == "original":
            d.text((x, y), "source", fill=(200, 200, 200), font=f_small)
        else:
            d.text((x, y), metric_text(m), fill=(200, 255, 200), font=f_small)
            if m:
                d.text((x, y + 20), f"{m['seconds']} s; {m['notes']}"[:110], fill=(160, 160, 160), font=f_small)
    return sheet


def overview(selection, manifest, up_dir, export_dir, metrics, out_path):
    thumb, label_w, row_h = 150, 240, 150 + 34
    max_cols = max(len(s["variants"].split(",")) + 1 for s in selection)
    half = (len(selection) + 1) // 2
    block_w = label_w + max_cols * (thumb + 6)
    img = Image.new("RGB", (2 * block_w + 3 * GUTTER, 30 + half * row_h + GUTTER), BG)
    d = ImageDraw.Draw(img)
    f_s, f_t = font(13), font(20)
    d.text((GUTTER, 4), "DW4 remaster pilot: original (nearest 4x) | Lanczos 4x | class models (dE2000 mean of the result shrunk back)",
           fill=(255, 230, 120), font=f_t)
    for i, sel in enumerate(selection):
        row = manifest[sel["hash"]]
        mode = alpha_mode(row)
        bx = GUTTER + (i // half) * (block_w + GUTTER)
        by = 30 + (i % half) * row_h
        d.text((bx, by + 4), f"{sel['slot']} {row['names'][:28]}", fill=(255, 255, 255), font=f_s)
        d.text((bx, by + 22), f"{row['width']}x{row['height']} {sel['class']}", fill=(180, 180, 180), font=f_s)
        d.text((bx, by + 40), sel["note"][:34], fill=(150, 150, 150), font=f_s)
        d.text((bx, by + 58), sel["note"][34:68], fill=(150, 150, 150), font=f_s)
        src = load_rgba(os.path.join(export_dir, "textures", row["file"]))
        cols = ["original"] + sel["variants"].split(",")
        for j, col in enumerate(cols):
            if col == "original":
                pic = nearest(src, 4)
            else:
                p = os.path.join(up_dir, col, row["file"])
                if not os.path.exists(p):
                    continue
                pic = load_rgba(p)
            disp = composite(pic, mode, checker=8)
            if sel["class"] in UPRIGHT:
                disp = disp[::-1]
            t = fit(disp, thumb, thumb)
            x = bx + label_w + j * (thumb + 6)
            img.paste(to_pil(t), (x, by))
            m = metrics.get(col, {}).get(row["hash"]) if col != "original" else None
            tag = "orig" if col == "original" else f"{col[:13]} {m['de_mean'] if m else '-'}"
            d.text((x, by + thumb + 2), tag, fill=(200, 255, 200), font=f_s)
    img.save(out_path)


def palette_check(pair, manifest, up_dir, export_dir, variant, out_path):
    """Two palette variants of one index plane side by side, with how much
    their upscales differ against how much their sources differ."""
    rows = [manifest[s["hash"]] for s in pair]
    srcs = [load_rgba(os.path.join(export_dir, "textures", r["file"])) for r in rows]
    outs = [load_rgba(os.path.join(up_dir, variant, r["file"])) for r in rows]
    mode = alpha_mode(rows[0])
    src_diff = np.abs(srcs[0] - srcs[1])
    out_diff = np.abs(outs[0] - outs[1])
    # Structure: the upscales' differences should be what the palettes make
    # of the same shapes: the alpha channels (shared index plane) agree.
    alpha_same = float(np.abs(outs[0][..., 3] - outs[1][..., 3]).max())
    tiles = [nearest(srcs[0], 4), nearest(srcs[1], 4), outs[0], outs[1]]
    disp = [fit(composite(t, mode), 512, 512) for t in tiles]
    diff_disp = fit(np.clip(out_diff[..., :3] * 8, 0, 1), 512, 512)
    img = Image.new("RGB", (5 * (512 + GUTTER) + GUTTER, 512 + 110), BG)
    d = ImageDraw.Draw(img)
    f = font(17)
    names = [f"{pair[0]['slot']} source", f"{pair[1]['slot']} source", f"{pair[0]['slot']} {variant}", f"{pair[1]['slot']} {variant}",
             "|upscale 1 - upscale 2| x 8"]
    for i, t in enumerate(disp + [diff_disp]):
        x = GUTTER + i * (512 + GUTTER)
        d.text((x, 6), names[i], fill=(255, 255, 255), font=f)
        img.paste(to_pil(t), (x, 30))
    d.text((GUTTER, 512 + 40), f"{rows[0]['names']}: sources differ by mean {src_diff[..., :3].mean() * 255:.2f} / max {src_diff[..., :3].max() * 255:.0f} "
           f"(RGB), {src_diff[..., 3].max() * 255:.0f} (alpha); upscales by mean {out_diff[..., :3].mean() * 255:.2f} / max "
           f"{out_diff[..., :3].max() * 255:.0f} (RGB), {alpha_same * 255:.0f} (alpha)", fill=(200, 255, 200), font=f)
    img.save(out_path)
    return {"src_mean": src_diff[..., :3].mean() * 255, "out_mean": out_diff[..., :3].mean() * 255, "alpha_max": alpha_same * 255}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--export", required=True)
    ap.add_argument("--selection", required=True)
    ap.add_argument("--up", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--max-width", type=int, default=4096)
    ap.add_argument("--palette-variant", default="pbrify_v4", help="model shown in the palette variant check")
    args = ap.parse_args()
    manifest = rmcommon.read_manifest(args.export)
    selection = rmcommon.read_tsv(args.selection)
    variants = sorted({v for s in selection for v in s["variants"].split(",")})
    metrics = {v: load_metrics(args.up, v) for v in variants}
    os.makedirs(args.out, exist_ok=True)
    for sel in selection:
        row = manifest[sel["hash"]]
        sheet = sheet_for(sel, row, args.up, args.export, metrics, args.max_width)
        name = f"{sel['slot']}_{os.path.splitext(row['file'])[0]}.png"
        sheet.save(os.path.join(args.out, name))
        print(f"{name}: {sheet.width}x{sheet.height}")
    overview(selection, manifest, args.up, args.export, metrics, os.path.join(args.out, "overview.png"))
    pair = [s for s in selection if s["class"] == "palette"]
    if len(pair) == 2:
        r = palette_check(pair, manifest, args.up, args.export, args.palette_variant, os.path.join(args.out, "palette_check.png"))
        print(f"palette check: {r}")


if __name__ == "__main__":
    main()
