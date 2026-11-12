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


def wrap_axes(row):
    """(wrap_y, wrap_x): axes padded cyclically before upscaling.

    `tiles` (UVs repeat the picture within a face) and `uv_edges` (UVs reach
    a border with a repeat wrap, so bilinear filtering reads the opposite edge
    and border-to-border faces tile it) both ask for it (docs/TEXTURES.md).
    """
    axes = set()
    for col in ("tiles", "uv_edges"):
        v = row.get(col, "no")
        if v not in ("no", "-"):
            axes |= set(v)
    return ("t" in axes, "s" in axes)


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


def pad(img, py, px, wrap_y, wrap_x):
    width = [(py, py), (0, 0)] + [(0, 0)] * (img.ndim - 2)
    img = np.pad(img, width, mode="wrap" if wrap_y else "edge")
    width = [(0, 0), (px, px)] + [(0, 0)] * (img.ndim - 2)
    return np.pad(img, width, mode="wrap" if wrap_x else "edge")


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
