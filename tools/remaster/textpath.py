"""Text path B: edge reconstruction for glyphs, digits and flat lettering.

Instead of letting a super-resolution model guess letter shapes, the shapes
of the original are kept and only their edges are rebuilt at 4x:

1. the coverage (alpha, or for opaque lettering the tone boundary) is
   interpolated smoothly (bicubic) to 16x,
2. thresholded at 0.5 (the anti-aliased original's edge),
3. traced with OpenCV (findContours) and the contours smoothed along their
   length (a small circular Gaussian, so the staircase of the source grid
   goes and corners stay),
4. filled at 16x and shrunk to 4x by area means (anti-aliased edges).

Colour comes from the original: a two-tone letter (fill and outline) gets a
reconstructed boundary between its tones; within each tone the colour (and
gradient) is the original's, spread within that tone only, so nothing of the
other tone or of the background bleeds in. A one-tone letter (or one whose
tones have no clean boundary) keeps the original's colour, interpolated.

Pilot finding: this suits flat one-colour lettering (font00's glyphs, the
title's white text); lettering with several tones, a shadow and a gradient
(num01's digits) comes out softer than a model's result.
"""
import cv2
import numpy as np

from rmcommon import fill_under, lanczos4

SUPERSAMPLE = 4  # contour fill resolution relative to the 4x output
SMOOTH_PX = 1.0  # contour smoothing sigma in 4x output pixels


def _smooth_contour(pts, sigma):
    n = len(pts)
    if n < 12 or sigma <= 0:
        return pts
    r = int(3 * sigma)
    x = np.arange(-r, r + 1, dtype=np.float64)
    k = np.exp(-0.5 * (x / sigma) ** 2)
    k /= k.sum()
    ext = np.concatenate([pts[-r:], pts, pts[:r]]) if n > r else np.concatenate([pts] * 3)[n - r: 2 * n + r]
    xs = np.convolve(ext[:, 0], k, mode="valid")
    ys = np.convolve(ext[:, 1], k, mode="valid")
    return np.stack([xs, ys], axis=1)[:n]


def recon_mask(field, scale=4):
    """Anti-aliased scale-x mask of `field >= 0.5` from smoothed contours."""
    h, w = field.shape
    b = 2
    f = np.pad(field.astype(np.float32), b, mode="edge")
    s = scale * SUPERSAMPLE
    up = cv2.resize(f, (f.shape[1] * s, f.shape[0] * s), interpolation=cv2.INTER_CUBIC)
    binary = (up >= 0.5).astype(np.uint8)
    contours, hierarchy = cv2.findContours(binary, cv2.RETR_CCOMP, cv2.CHAIN_APPROX_NONE)
    smoothed = []
    for c in contours:
        pts = c[:, 0, :].astype(np.float64)
        pts = _smooth_contour(pts, SMOOTH_PX * SUPERSAMPLE)
        smoothed.append(np.rint(pts).astype(np.int32)[:, None, :])
    mask = np.zeros_like(binary)
    if smoothed:
        cv2.drawContours(mask, smoothed, -1, 255, thickness=cv2.FILLED, hierarchy=hierarchy)
    out = cv2.resize(mask.astype(np.float32) / 255.0, (f.shape[1] * scale, f.shape[0] * scale), interpolation=cv2.INTER_AREA)
    return out[b * scale: (b + h) * scale, b * scale: (b + w) * scale]


def region_colour(rgb, weight, scale=4, sigma=0.6):
    """The colour of the texels in `weight`, spread smoothly outward and
    interpolated to scale x (bicubic)."""
    num = cv2.GaussianBlur(rgb * weight[..., None], (0, 0), sigma)
    den = cv2.GaussianBlur(weight, (0, 0), sigma)
    valid = den > 1e-3
    col = np.where(valid[..., None], num / np.maximum(den, 1e-3)[..., None], 0.0).astype(np.float32)
    if valid.any():
        col = fill_under(col, valid)
    b = 2
    p = np.pad(col, ((b, b), (b, b), (0, 0)), mode="edge")
    up = cv2.resize(p, (p.shape[1] * scale, p.shape[0] * scale), interpolation=cv2.INTER_CUBIC)
    h, w = rgb.shape[:2]
    return np.clip(up[b * scale: (b + h) * scale, b * scale: (b + w) * scale], 0.0, 1.0)


def _tone_field(rgb, c0, c1):
    axis = c1 - c0
    return np.clip(((rgb - c0) @ axis) / (axis @ axis), 0.0, 1.0).astype(np.float32)


def _crossing_step(t, visible):
    """Median jump of `t` between neighbouring visible texels on opposite
    sides of 0.5: large at a real tone edge, small where 0.5 cuts a gradient."""
    steps = []
    for a, b, va, vb in ((t[:, :-1], t[:, 1:], visible[:, :-1], visible[:, 1:]),
                         (t[:-1, :], t[1:, :], visible[:-1, :], visible[1:, :])):
        cross = ((a - 0.5) * (b - 0.5) < 0) & va & vb
        steps.append(np.abs(a - b)[cross])
    s = np.concatenate(steps)
    return float(np.median(s)) if len(s) >= 8 else 0.0


def two_tones(rgb, weight):
    """(dark, light) colours when the visible texels form two tones with
    edges between them (fill and outline); None for one tone, or where the
    split would cut through a gradient. Two splits are tried (k-means on the
    colours, Otsu on the luminance); the one with the sharper boundary wins."""
    visible = weight > 0.25
    pix = rgb[visible].astype(np.float32)
    if len(pix) < 16:
        return None
    candidates = []
    cv2.setRNGSeed(0)
    crit = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 30, 1e-4)
    _, labels, centers = cv2.kmeans(pix, 2, None, crit, 3, cv2.KMEANS_PP_CENTERS)
    candidates.append((centers[0].astype(np.float64), centers[1].astype(np.float64), labels.ravel()))
    lum = pix @ np.array([0.299, 0.587, 0.114], np.float32)
    lum8 = np.clip(np.rint(lum * 255), 0, 255).astype(np.uint8)
    thr, _ = cv2.threshold(lum8.reshape(-1, 1), 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    split = (lum8 > thr).astype(np.int32)
    if 0 < split.sum() < len(split):
        candidates.append((pix[split == 0].mean(axis=0).astype(np.float64), pix[split == 1].mean(axis=0).astype(np.float64), split))
    best = None
    for c0, c1, lab in candidates:
        if np.linalg.norm(c1 - c0) < 0.2:
            continue
        counts = np.bincount(lab, minlength=2) / len(lab)
        if counts.min() < 0.04:
            continue
        if c0.sum() > c1.sum():
            c0, c1 = c1, c0
        step = _crossing_step(_tone_field(rgb, c0, c1), visible)
        if step >= 0.35 and (best is None or step > best[0]):
            best = (step, c0, c1)
    return None if best is None else (best[1], best[2])


def path_b(src, mode, scale=4):
    """Path B for one picture (a whole texture or an atlas cell)."""
    rgb = src[..., :3].astype(np.float32)
    a = src[..., 3].astype(np.float32)
    covered = mode not in ("opaque", "ignore")
    weight = a if covered else np.ones_like(a)
    tones = two_tones(rgb, weight)
    if tones is not None:
        c0, c1 = tones
        t = _tone_field(rgb, c0, c1)
        visible = weight > 0.02
        t = fill_under(t[..., None], visible)[..., 0]
        m = recon_mask(t, scale)[..., None]
        col0 = region_colour(rgb, weight * (t < 0.5), scale)
        col1 = region_colour(rgb, weight * (t >= 0.5), scale)
        rgb4 = m * col1 + (1.0 - m) * col0
    else:
        # One tone (or no clean split): the colour as the original has it,
        # interpolated (Lanczos) after filling under alpha 0; only the
        # coverage edge is rebuilt.
        filled = fill_under(rgb, weight > 0.02) if covered else rgb
        rgb4 = lanczos4(filled, scale)
    alpha4 = recon_mask(a, scale) if covered else np.ones((rgb4.shape[0], rgb4.shape[1]), np.float32)
    return np.concatenate([rgb4, alpha4[..., None]], axis=2).astype(np.float32)


def is_flat_glyph(cell, max_colours=4):
    """A font cell drawn in at most a few flat colours (a letter), not a
    shaded picture (a controller button or an item icon)."""
    a = cell[..., 3]
    vis = cell[a > 0.5][:, :3]
    if len(vis) == 0:
        return True
    q = np.rint(vis * 255).astype(np.int32)
    return len(np.unique(q[:, 0] * 65536 + q[:, 1] * 256 + q[:, 2])) <= max_colours
