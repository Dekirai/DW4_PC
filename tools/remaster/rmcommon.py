"""Shared helpers of the texture remaster pipeline (tools/remaster).

Pictures are float32 arrays in [0, 1], H x W x C, rows in the order the
export stores them (the decoder's row order, so some text stands upside
down). Nothing here writes outside the folders the command lines name; the
game-derived pictures stay on the user's machine (see README.md).
"""
import csv
import ctypes
import math
import os

import cv2
import numpy as np


def lower_priority():
    """Below-normal CPU priority for this process (Windows), nice 10 elsewhere."""
    if os.name == "nt":
        kernel32 = ctypes.windll.kernel32
        kernel32.SetPriorityClass(kernel32.GetCurrentProcess(), 0x00004000)
    else:
        try:
            os.nice(10)
        except OSError:
            pass


# ---------------------------------------------------------------- export data

def read_tsv(path):
    with open(path, encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


def read_manifest(export_dir):
    """manifest.tsv of `dw4_texpack export` as {hash: row}."""
    return {r["hash"]: r for r in read_tsv(os.path.join(export_dir, "manifest.tsv"))}


def read_cells(export_dir, row):
    """The atlas cells of a texture (atlas/<hash>.tsv) as (source, label, x0, y0, x1, y1)."""
    path = os.path.join(export_dir, "atlas", row["hash"] + ".tsv")
    if not os.path.exists(path):
        return []
    return [(r["source"], r["label"], int(r["x0"]), int(r["y0"]), int(r["x1"]), int(r["y1"])) for r in read_tsv(path)]


def load_rgba(path):
    data = np.fromfile(path, dtype=np.uint8)
    img = cv2.imdecode(data, cv2.IMREAD_UNCHANGED)
    if img is None:
        raise ValueError(f"cannot read {path}")
    if img.ndim == 2:
        img = cv2.cvtColor(img, cv2.COLOR_GRAY2BGRA)
    elif img.shape[2] == 3:
        img = cv2.cvtColor(img, cv2.COLOR_BGR2BGRA)
    img = cv2.cvtColor(img, cv2.COLOR_BGRA2RGBA)
    return img.astype(np.float32) / 255.0


def to_u8(img):
    return np.clip(np.rint(img * 255.0), 0, 255).astype(np.uint8)


def save_rgba(path, img):
    """Writes RGBA (or RGB) float pictures as 8-bit PNG."""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    u8 = to_u8(img)
    if u8.shape[2] == 4:
        bgr = cv2.cvtColor(u8, cv2.COLOR_RGBA2BGRA)
    else:
        bgr = cv2.cvtColor(u8, cv2.COLOR_RGB2BGR)
    ok, buf = cv2.imencode(".png", bgr)
    if not ok:
        raise ValueError(f"cannot encode {path}")
    buf.tofile(path)


# ------------------------------------------------------- material information

def _list(value):
    return [v.strip() for v in value.split(",") if v.strip() and v.strip() != "-"]


def alpha_mode(row):
    """How the texture's alpha is used, from the manifest:

    opaque    alpha 255 everywhere
    ignore    no draw blends or alpha-tests it (alpha is never seen)
    additive  every draw blends SRC_ALPHA/ONE (black carries the transparency)
    cutout    alpha is 0 or 255 only (alpha-tested)
    blend     soft alpha
    """
    if row["alpha"] == "opaque":
        return "opaque"
    blends = _list(row["blend"])
    tests = _list(row["alpha_test"])
    used = not blends and not tests  # no draws found (font00): treat alpha as used
    used = used or any(b != "off" for b in blends) or any(t != "off" for t in tests)
    if not used:
        return "ignore"
    if blends and all(b == "SRC_ALPHA/ONE" for b in blends):
        return "additive"
    if row["alpha"] == "cutout":
        return "cutout"
    return "blend"


def cutout_threshold(row):
    """(threshold, inclusive) for a cutout's upscaled alpha.

    The material's alpha compare decides the drawn edge: the highest
    reference of the draws (GREATER 0.1 is the engine default). Where a draw
    also blends with SRC_ALPHA/ONE_MINUS_SRC_ALPHA the original's soft edge
    sits at about half coverage, so the threshold is at least 0.5 there.
    """
    refs = []
    inclusive = False
    for t in _list(row["alpha_test"]):
        parts = t.split()
        if len(parts) == 2 and parts[0] != "off":
            try:
                ref = float(parts[1])
            except ValueError:
                continue
            refs.append(ref)
            if parts[0] == "GEQUAL" and ref >= max(refs):
                inclusive = True
    thr = max(refs) if refs else 0.5
    if any(b not in ("off", "SRC_ALPHA/ONE") for b in _list(row["blend"])):
        if thr < 0.5:
            thr, inclusive = 0.5, True
    return thr, inclusive


# Drawn as flat quads (2D): no face samples across their borders.
FLAT_CLASSES = {"ui", "loading", "title", "font", "card", "portrait", "decal"}
CONTINUITY_FACTOR, CONTINUITY_SLACK = 1.5, 0.02


def border_continuity(src, mode, axis):
    """(jump across the wrap border, mean jump between neighbouring rows
    (axis 0) or columns (axis 1)) of the visible picture: colour times alpha
    and alpha where alpha is seen, else colour."""
    if mode in ("blend", "cutout", "additive"):
        vis = np.concatenate([src[..., :3] * src[..., 3:4], src[..., 3:4]], axis=2)
    else:
        vis = src[..., :3]
    if vis.shape[axis] < 2:
        return 0.0, 0.0
    first = np.take(vis, 0, axis=axis)
    last = np.take(vis, -1, axis=axis)
    jump = float(np.abs(first - last).mean())
    inner = float(np.abs(np.diff(vis, axis=axis)).mean())
    return jump, inner


def tiling_axes(row, src, cls):
    """(wrap_y, wrap_x) and a note: the axes padded cyclically before
    upscaling (and filtered cyclically for the smaller levels).

    `tiles` (the UVs repeat the picture within a face, or scroll across it)
    always wraps. `uv_edges` alone (the UVs reach near a border under a
    repeat wrap mode) wraps only for 3D classes whose picture continues
    across that border (the jump between its last and first row/column at
    most CONTINUITY_FACTOR times the picture's mean neighbour jump plus
    CONTINUITY_SLACK): border-to-border faces that tile. Everything else is
    padded by edge extension: a light beam whose first row is black, a skin
    whose UV islands touch the border, a HUD piece, must not get its
    opposite edge blended into its outer rows."""
    mode = alpha_mode(row)
    out, notes = [], []
    for axis, name in ((0, "t"), (1, "s")):
        tiles = row.get("tiles", "no")
        edges = row.get("uv_edges", "no")
        if tiles not in ("no", "-") and name in tiles:
            out.append(True)
            notes.append(f"{name}: tiles")
            continue
        if edges in ("no", "-") or name not in edges:
            out.append(False)
            continue
        if cls in FLAT_CLASSES:
            out.append(False)
            notes.append(f"{name}: uv edge, flat class -> edge")
            continue
        jump, inner = border_continuity(src, mode, axis)
        cont = jump <= CONTINUITY_FACTOR * inner + CONTINUITY_SLACK
        out.append(cont)
        notes.append(f"{name}: uv edge, border jump {jump:.3f} vs {inner:.3f} -> {'wrap' if cont else 'edge'}")
    return (out[0], out[1]), "; ".join(notes) or "-"


def wrap_axes(row, sel=None):
    """(wrap_y, wrap_x): axes padded cyclically before upscaling.

    The decision is classify.py's `wrap` column (tiling_axes: `t`, `s`,
    `ts` or `-`), carried by classes.tsv and finalize.py's report.tsv; pass
    that row as `sel`. Without it only the export's `tiles` column counts
    (uv_edges alone does not make a texture tile, docs/TEXTURES.md).
    """
    if sel is not None and sel.get("wrap") not in (None, ""):
        v = sel["wrap"]
        return ("t" in v, "s" in v)
    v = row.get("tiles", "no")
    v = "" if v in ("no", "-") else v
    return ("t" in v, "s" in v)


def merge_cells(cells, w, h):
    """Atlas cells as rectangles: clipped, the whole picture dropped, cells
    overlapping by more than 2 texels both ways merged into one."""
    rects = []
    for _, _, x0, y0, x1, y1 in cells:
        x0, y0, x1, y1 = max(0, x0), max(0, y0), min(w, x1), min(h, y1)
        if x1 - x0 < 2 or y1 - y0 < 2 or (x1 - x0) * (y1 - y0) >= 0.9 * w * h:
            continue
        rects.append([x0, y0, x1, y1])
    changed = True
    while changed:
        changed = False
        for i in range(len(rects)):
            for j in range(i + 1, len(rects)):
                a, b = rects[i], rects[j]
                if min(a[2], b[2]) - max(a[0], b[0]) > 2 and min(a[3], b[3]) - max(a[1], b[1]) > 2:
                    rects[i] = [min(a[0], b[0]), min(a[1], b[1]), max(a[2], b[2]), max(a[3], b[3])]
                    del rects[j]
                    changed = True
                    break
            if changed:
                break
    return sorted({tuple(r) for r in rects}, key=lambda r: (r[1], r[0]))


def edge_stats(src, mode):
    """(hard, flat): shares of neighbouring texel pairs whose visible
    luminance (colour times alpha for blended and additive textures) jumps
    by more than 35 % of the picture's range, and by less than 2 %. Glows
    and soft gradients have almost no hard jumps; pixel art and flat
    graphics (screens, panels, signs, lettering) have many hard jumps
    between flat areas; natural materials (rock, ground, bark) have few of
    either."""
    a = src[..., 3:4]
    vis = src[..., :3] if mode in ("opaque", "ignore") else src[..., :3] * a
    y = vis @ np.array([0.299, 0.587, 0.114], np.float32)
    rng = max(float(np.percentile(y, 99) - np.percentile(y, 1)), 0.05)
    jumps = np.concatenate([np.abs(np.diff(y, axis=1)).ravel(), np.abs(np.diff(y, axis=0)).ravel()]) / rng
    return float((jumps > 0.35).mean()), float((jumps < 0.02).mean())


def hard_edge_fraction(src, mode):
    return edge_stats(src, mode)[0]


# ------------------------------------------------------------- preprocessing

def fill_under(rgb, valid):
    """Push-pull fill: texels where `valid` is false get the colour of the
    nearest valid texels (smoothly extrapolated over a mean pyramid), so no
    unrelated colour under alpha 0 bleeds into upscaled edges."""
    valid = valid.astype(np.float32)
    if valid.min() >= 1.0 or valid.max() <= 0.0:
        return rgb.copy()
    levels = []
    p = rgb * valid[..., None]
    w = valid.copy()
    levels.append((p, w))
    while min(p.shape[0], p.shape[1]) > 1:
        h2, w2 = max(1, (p.shape[0] + 1) // 2), max(1, (p.shape[1] + 1) // 2)
        p = cv2.resize(p, (w2, h2), interpolation=cv2.INTER_AREA)
        w = cv2.resize(w, (w2, h2), interpolation=cv2.INTER_AREA)
        if p.ndim == 2:
            p = p[..., None]
        levels.append((p, w))
    p, w = levels[-1]
    col = p / np.maximum(w, 1e-8)[..., None]
    if w.max() <= 0:
        col[:] = 0.5
    for p, w in reversed(levels[:-1]):
        up = cv2.resize(col, (p.shape[1], p.shape[0]), interpolation=cv2.INTER_LINEAR)
        if up.ndim == 2:
            up = up[..., None]
        cur = p / np.maximum(w, 1e-8)[..., None]
        conf = np.clip(w * 2.0, 0.0, 1.0)[..., None]
        col = cur * conf + up * (1.0 - conf)
    out = rgb.copy()
    out[valid < 1.0] = col[valid < 1.0]
    return out


def pad_amount(size, wrap):
    """Source texels added on each side of an axis."""
    if not wrap:
        return 16
    return 32 if size >= 32 else max(size, 16)  # small tiling textures: 3 x 3 (or more) copies


def pad(img, py, px, wrap_y, wrap_x, edge="edge"):
    """Padding per axis: cyclic where the axis wraps, else `edge` (numpy's
    "edge": the border texel repeated; "symmetric": mirrored, for the
    models, which turn a band of repeated rows into streaks)."""
    width = [(py, py), (0, 0)] + [(0, 0)] * (img.ndim - 2)
    img = np.pad(img, width, mode="wrap" if wrap_y else edge)
    width = [(0, 0), (px, px)] + [(0, 0)] * (img.ndim - 2)
    return np.pad(img, width, mode="wrap" if wrap_x else edge)


def lanczos4(img, scale=4):
    h, w = img.shape[:2]
    out = cv2.resize(img, (w * scale, h * scale), interpolation=cv2.INTER_LANCZOS4)
    if out.ndim == 2 and img.ndim == 3:
        out = out[..., None]
    return np.clip(out, 0.0, 1.0)


def nearest(img, scale=4):
    return np.repeat(np.repeat(img, scale, axis=0), scale, axis=1)


def unsharp(img, sigma=1.2, amount=0.6):
    """Light sharpening for the effects' Lanczos variant (sigma in output pixels)."""
    blur = cv2.GaussianBlur(img, (0, 0), sigma)
    if blur.ndim == 2 and img.ndim == 3:
        blur = blur[..., None]
    return np.clip(img + amount * (img - blur), 0.0, 1.0)


# ------------------------------------------------------------------- metrics

def srgb_to_lab(rgb):
    rgb = np.clip(rgb.astype(np.float64), 0.0, 1.0)
    lin = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
    m = np.array([[0.4124564, 0.3575761, 0.1804375],
                  [0.2126729, 0.7151522, 0.0721750],
                  [0.0193339, 0.1191920, 0.9503041]])
    xyz = lin @ m.T
    xyz /= np.array([0.95047, 1.0, 1.08883])
    e = 216.0 / 24389.0
    k = 24389.0 / 27.0
    f = np.where(xyz > e, np.cbrt(xyz), (k * xyz + 16.0) / 116.0)
    lab = np.empty_like(xyz)
    lab[..., 0] = 116.0 * f[..., 1] - 16.0
    lab[..., 1] = 500.0 * (f[..., 0] - f[..., 1])
    lab[..., 2] = 200.0 * (f[..., 1] - f[..., 2])
    return lab


def delta_e2000(lab1, lab2):
    """CIEDE2000 (Sharma, Wu, Dalal 2005)."""
    L1, a1, b1 = lab1[..., 0], lab1[..., 1], lab1[..., 2]
    L2, a2, b2 = lab2[..., 0], lab2[..., 1], lab2[..., 2]
    C1 = np.hypot(a1, b1)
    C2 = np.hypot(a2, b2)
    Cb = (C1 + C2) / 2.0
    G = 0.5 * (1.0 - np.sqrt(Cb ** 7 / (Cb ** 7 + 25.0 ** 7)))
    a1p = (1.0 + G) * a1
    a2p = (1.0 + G) * a2
    C1p = np.hypot(a1p, b1)
    C2p = np.hypot(a2p, b2)
    h1p = np.degrees(np.arctan2(b1, a1p)) % 360.0
    h2p = np.degrees(np.arctan2(b2, a2p)) % 360.0
    dLp = L2 - L1
    dCp = C2p - C1p
    dhp = h2p - h1p
    dhp = np.where(dhp > 180.0, dhp - 360.0, dhp)
    dhp = np.where(dhp < -180.0, dhp + 360.0, dhp)
    dhp = np.where(C1p * C2p == 0, 0.0, dhp)
    dHp = 2.0 * np.sqrt(C1p * C2p) * np.sin(np.radians(dhp) / 2.0)
    Lbp = (L1 + L2) / 2.0
    Cbp = (C1p + C2p) / 2.0
    hsum = h1p + h2p
    hbp = np.where(np.abs(h1p - h2p) > 180.0, (hsum + 360.0) / 2.0, hsum / 2.0)
    hbp = np.where(C1p * C2p == 0, hsum, hbp)
    T = (1.0 - 0.17 * np.cos(np.radians(hbp - 30.0)) + 0.24 * np.cos(np.radians(2.0 * hbp))
         + 0.32 * np.cos(np.radians(3.0 * hbp + 6.0)) - 0.20 * np.cos(np.radians(4.0 * hbp - 63.0)))
    dtheta = 30.0 * np.exp(-(((hbp - 275.0) / 25.0) ** 2))
    Rc = 2.0 * np.sqrt(Cbp ** 7 / (Cbp ** 7 + 25.0 ** 7))
    Sl = 1.0 + (0.015 * (Lbp - 50.0) ** 2) / np.sqrt(20.0 + (Lbp - 50.0) ** 2)
    Sc = 1.0 + 0.045 * Cbp
    Sh = 1.0 + 0.015 * Cbp * T
    Rt = -np.sin(np.radians(2.0 * dtheta)) * Rc
    return np.sqrt((dLp / Sl) ** 2 + (dCp / Sc) ** 2 + (dHp / Sh) ** 2 + Rt * (dCp / Sc) * (dHp / Sh))


def box_down(img, f=4):
    h, w = img.shape[0] // f, img.shape[1] // f
    return img[: h * f, : w * f].reshape(h, f, w, f, *img.shape[2:]).mean(axis=(1, 3))


def _weighted_percentile(values, weights, q):
    order = np.argsort(values)
    v, w = values[order], weights[order]
    cw = np.cumsum(w)
    if cw[-1] <= 0:
        return float("nan")
    return float(v[min(int(np.searchsorted(cw, q * cw[-1])), len(v) - 1)])


def psnr(mse):
    return float("inf") if mse <= 1e-12 else 10.0 * math.log10(1.0 / mse)


def fidelity(src, out, mode, mask=None):
    """Colour fidelity of a 4x result: shrunk back by 4 x 4 area means and
    compared with the source where the source is visible (alpha-weighted;
    `mask` limits it further, e.g. to a character's UV islands). Returns
    CIEDE2000 mean and 95th percentile, RGB PSNR and alpha PSNR."""
    sa = src[..., 3]
    oa = out[..., 3]
    if mode == "additive":
        # What the player sees: colour times alpha over black.
        s = src[..., :3] * sa[..., None]
        d = box_down(out[..., :3] * oa[..., None])
        w = np.ones_like(sa)
    elif mode in ("opaque", "ignore"):
        s = src[..., :3]
        d = box_down(out[..., :3])
        w = np.ones_like(sa)
    else:
        s = src[..., :3]
        pa = box_down(out[..., :3] * oa[..., None])
        da = box_down(oa)
        d = np.where(da[..., None] > 1e-4, pa / np.maximum(da, 1e-4)[..., None], box_down(out[..., :3]))
        w = sa
    if mask is not None:
        w = w * mask
    de = delta_e2000(srgb_to_lab(s), srgb_to_lab(d))
    wf = w.reshape(-1).astype(np.float64)
    dev = de.reshape(-1)
    if wf.sum() <= 0:
        wf = np.ones_like(wf)
    de_mean = float((dev * wf).sum() / wf.sum())
    de_p95 = _weighted_percentile(dev, wf, 0.95)
    mse = float((((s - d) ** 2).mean(axis=2).reshape(-1) * wf).sum() / wf.sum())
    amse = float(((sa - box_down(oa)) ** 2).mean())
    return {"de_mean": round(de_mean, 3), "de_p95": round(de_p95, 3), "psnr": round(psnr(mse), 2),
            "psnr_alpha": round(psnr(amse), 2)}


# ------------------------------------------------------------ finishing steps

UV_ISLAND_CLASSES = {"character", "boss", "weapon", "item"}


def uv_island_mask(export_dir, row, cls):
    """The texels the meshes' UVs cover (the export's uvmask) for 3D skins
    whose UVs do not scroll, or None: upscale.py fills everything else from
    the islands, and the fidelity check counts only the islands."""
    if cls not in UV_ISLAND_CLASSES or row.get("uv_mask", "-") in ("-", "full") or row.get("uv_motion", "-") != "-":
        return None
    mask = load_rgba(os.path.join(export_dir, row["uv_mask"]))[..., 0] > 0
    h, w = int(row["height"]), int(row["width"])
    if mask.shape != (h, w) or not 0.05 <= mask.mean() <= 0.98:
        return None
    return mask


def visible_pair(src, out, mode):
    """(source colour, result shrunk back 4x by area means, weights) as the
    player sees them: additive textures as colour times alpha over black,
    blended ones alpha-weighted (the colour where the result is visible)."""
    sa = src[..., 3]
    oa = out[..., 3]
    if mode == "additive":
        return src[..., :3] * sa[..., None], box_down(out[..., :3] * oa[..., None]), np.ones_like(sa)
    if mode in ("opaque", "ignore"):
        return src[..., :3], box_down(out[..., :3]), np.ones_like(sa)
    pa = box_down(out[..., :3] * oa[..., None])
    da = box_down(oa)
    d = np.where(da[..., None] > 1e-4, pa / np.maximum(da, 1e-4)[..., None], box_down(out[..., :3]))
    return src[..., :3], d, sa


def local_de(src, out, mode):
    """CIEDE2000 per source texel between the source and the result shrunk back."""
    s, d, _ = visible_pair(src, out, mode)
    return delta_e2000(srgb_to_lab(s), srgb_to_lab(d)).astype(np.float32)


def _blur_weighted(x, w, sigma, wrap):
    """Gaussian blur of x weighted by w (normalised convolution), with the
    texture's wrap on each axis; texels without weight nearby get the push-pull fill."""
    wy, wx = wrap
    r = int(np.ceil(3 * sigma)) + 1
    xp = pad(x * w[..., None], r, r, wy, wx)
    wp = pad(w, r, r, wy, wx)
    num = cv2.GaussianBlur(xp, (0, 0), sigma)
    den = cv2.GaussianBlur(wp, (0, 0), sigma)
    num, den = num[r:-r, r:-r], den[r:-r, r:-r]
    ok = den > 1e-3
    out = np.where(ok[..., None], num / np.maximum(den, 1e-3)[..., None], 0.0).astype(np.float32)
    if not ok.all() and ok.any():
        out = fill_under(out, ok)
    return out


def colour_fix(src, out, mode, wrap, mask=None, sigma=2.0, strength=1.0):
    """Gentle colour correction of a 4x result: the difference between the
    source and the result shrunk back (per texel, as the player sees it),
    low-passed with a Gaussian of `sigma` source texels and added back at 4x.
    The model's detail inside each texel stays; only slow colour and
    brightness shifts (a model's brightening, saturation) go."""
    s, d, w = visible_pair(src, out, mode)
    if mask is not None:
        w = w * mask
    resid = _blur_weighted((s - d).astype(np.float32), w.astype(np.float32), sigma, wrap)
    h, wd = resid.shape[:2]
    wy, wx = wrap
    p = 2
    rp = pad(resid, p, p, wy, wx)
    up = cv2.resize(rp, (rp.shape[1] * 4, rp.shape[0] * 4), interpolation=cv2.INTER_LINEAR)
    up = up[p * 4:(p + h) * 4, p * 4:(p + wd) * 4] * strength
    res = out.copy()
    if mode == "additive":
        a = out[..., 3:4]
        vis = np.clip(out[..., :3] * a + up, 0.0, 1.0)
        res[..., :3] = np.where(a > 1.0 / 255.0, np.clip(vis / np.maximum(a, 1.0 / 255.0), 0.0, 1.0), out[..., :3])
    else:
        res[..., :3] = np.clip(out[..., :3] + up, 0.0, 1.0)
    return res


def keep_alpha_coverage(src_a, a4, wrap, tol=0.08, span=0.12):
    """Soft alpha of a 4x result checked against the source's coverage:
    where the result shrunk back by 4 x 4 area means misses the source alpha
    by more than `tol` (a model that turned a half-transparent margin, a
    soft dark outline or a shadow, into solid alpha), the Lanczos alpha
    takes over, blended in smoothly over `span`; edges the model drew where
    the source has them stay sharp."""
    h, w = src_a.shape
    wy, wx = wrap
    py, px = pad_amount(h, wy), pad_amount(w, wx)
    lz = lanczos4(pad(src_a[..., None], py, px, wy, wx), 4)[..., 0][py * 4:(py + h) * 4, px * 4:(px + w) * 4]
    err = np.abs(box_down(a4) - src_a)
    wgt = np.clip((err - tol) / span, 0.0, 1.0).astype(np.float32)
    wgt = cv2.dilate(wgt, np.ones((3, 3), np.uint8))
    p = 2
    wp = cv2.GaussianBlur(pad(wgt, p, p, wy, wx), (0, 0), 0.7)
    w4 = cv2.resize(wp, ((w + 2 * p) * 4, (h + 2 * p) * 4), interpolation=cv2.INTER_LINEAR)[p * 4:-p * 4, p * 4:-p * 4]
    w4 = np.clip(w4, 0.0, 1.0)
    return (a4 * (1.0 - w4) + lz * w4).astype(np.float32), float(wgt.mean())


def _lanczos3_half_weights():
    d = np.arange(-5, 7, dtype=np.float64)  # input texel 2j + d contributes to output texel j
    t = (d - 0.5) / 2.0
    w = np.sinc(t) * np.sinc(t / 3.0)
    return d.astype(int), (w / w.sum()).astype(np.float32)


def _down2_axis(x, axis, wrap):
    offs, wts = _lanczos3_half_weights()
    n = x.shape[axis]
    p = 6
    width = [(0, 0)] * x.ndim
    width[axis] = (p, p)
    xp = np.pad(x, width, mode="wrap" if wrap else "edge")
    m = n // 2
    out = np.zeros(x.shape[:axis] + (m,) + x.shape[axis + 1:], np.float32)
    lo = np.full_like(out, np.inf)
    hi = np.full_like(out, -np.inf)
    for o, wgt in zip(offs, wts):
        idx = np.arange(m) * 2 + o + p
        part = np.take(xp, idx, axis=axis)
        out += wgt * part
        if -1 <= o <= 2:  # the 4-texel footprint bounds the ringing
            lo = np.minimum(lo, part)
            hi = np.maximum(hi, part)
    return np.clip(out, lo, hi)


def down2(x, wrap):
    """Half size with a Lanczos3 filter (wrap-aware per axis), each output
    clamped to the range of its 4 x 4 input footprint (no ringing haloes)."""
    wy, wx = wrap
    return _down2_axis(_down2_axis(x.astype(np.float32), 0, wy), 1, wx)


def downscale2x(img, mode, wrap):
    """The 2x level from a 4x result: premultiplied colour (what blending
    and bilinear filtering mix), alpha alike; cut-outs stay binary with
    their coverage (threshold 0.5 of the filtered mask); colour under alpha
    0 keeps the straight filtered colour (the fill), so filtering at the
    edges reads no black."""
    rgb, a = img[..., :3], img[..., 3]
    if mode in ("opaque", "ignore"):
        rgb2 = down2(rgb, wrap)
        a2 = np.ones(rgb2.shape[:2], np.float32) if mode == "opaque" else np.clip(down2(a[..., None], wrap)[..., 0], 0, 1)
        return np.concatenate([np.clip(rgb2, 0, 1), a2[..., None]], axis=2)
    a2 = np.clip(down2(a[..., None], wrap)[..., 0], 0.0, 1.0)
    pm = down2(rgb * a[..., None], wrap)
    straight = down2(rgb, wrap)
    rgb2 = np.where(a2[..., None] > 1.0 / 255.0, pm / np.maximum(a2, 1.0 / 255.0)[..., None], straight)
    if mode == "cutout":
        a2 = (a2 >= 0.5).astype(np.float32)
    return np.concatenate([np.clip(rgb2, 0, 1), a2[..., None]], axis=2).astype(np.float32)


def border_jump(img, mode, axis, mask=None):
    """Mean jump of the visible colour across the wrap border on an axis
    (0: between the last and the first row, t; 1: columns, s). A 4x result
    that wraps like its source jumps there at most about as much as the
    source does between its last and first texel; more is a seam. `mask`
    (source texels, e.g. the UV islands) limits it to border positions
    drawn on both sides."""
    vis = img[..., :3] * img[..., 3:4] if mode in ("additive", "blend", "cutout") else img[..., :3]
    d = np.abs(vis[0] - vis[-1]).mean(axis=-1) if axis == 0 else np.abs(vis[:, 0] - vis[:, -1]).mean(axis=-1)
    if mask is None:
        return float(d.mean())
    m = (mask[0] & mask[-1]) if axis == 0 else (mask[:, 0] & mask[:, -1])
    m = np.repeat(m, d.shape[0] // m.shape[0]).astype(np.float32)
    return float((d * m).sum() / m.sum()) if m.sum() > 0 else 0.0