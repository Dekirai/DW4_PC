#!/usr/bin/env python3
"""Upscales the textures of a selection 4x with one variant: a
super-resolution model, Lanczos, or text path B.

    E:/DW4Remaster/venv/Scripts/python.exe -I tools/remaster/upscale.py --variant ultrasharp_v2 \
        --export E:/DW4Remaster/export --selection E:/DW4Remaster/pilot/selection.tsv \
        --models E:/DW4Remaster/models --out E:/DW4Remaster/pilot/up [--tile 256]

The selection is pilot_select.py's TSV or classify.py's classes.tsv (the
full run: every texture whose `variants` names VARIANT). Writes
OUT/VARIANT/<export file name>.png (the renderer's pack naming) and
OUT/VARIANT/metrics.tsv, a line as each texture finishes; --resume skips
the textures an interrupted run finished. Per texture:

* colours under alpha 0 are filled from their neighbours (push-pull), and
  for 3D skins (characters, bosses, weapons, items) the texels outside
  their UV islands (the export's uvmask, when the UVs do not scroll) too;
* axes padded cyclically only where classify.py's `wrap` says the texture
  tiles there (rmcommon.tiling_axes: `tiles`, or a 3D texture whose picture
  continues across a UV edge), others by edge extension (Lanczos) or by
  mirroring (a model's input: it draws a band of repeated rows as streaks),
  so no opposite edge bleeds into the outer rows; the padding is cropped
  after 4x;
* preprocessing (`pre`): `smooth` takes the steps out of coarse soft alpha
  ramps (blurred, each texel kept within half a step of its level) and
  drops the sharpening; `dither` also blurs the colour's dither lightly;
  `denoise` smooths a decal's dither (edge-preserving) before the model;
  `mask_nearest` takes a cut-out's mask by nearest neighbour (rows of
  square windows one texel wide);
* atlas cells (font glyphs, UI pictures, sprite frames) are cut out one by
  one with a border of their own edge texels and pasted back, so nothing
  bleeds between cells and pieces that fill their cell keep solid edges;
* RGB and alpha are upscaled separately (alpha as a grey picture);
* alpha afterwards: cut-outs take the Lanczos-upscaled source mask
  thresholded at the material's alpha reference (the model's alpha changed
  their shapes: round holes for square windows, lost thin texels), soft alpha stays soft (Lanczos in gentle ramps, the model's
  only at hard edges) and within the 3 x 3 range of the source,
  additive effects (SRC_ALPHA/ONE) upscale their visible colour (colour times
  alpha over black) and take a Lanczos alpha, so no alpha is invented;
* colour fidelity: the result shrunk back 4x by area means against the
  source (CIEDE2000 mean / 95th percentile, PSNR).

Models load only through safetensors or torch.load(weights_only=True).
Runs at below-normal priority, one model at a time, in tiles.
"""
import argparse
import json
import os
import sys
import time
import typing

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rmcommon  # noqa: E402

rmcommon.lower_priority()

import cv2  # noqa: E402
import numpy as np  # noqa: E402

from rmcommon import (alpha_mode, cutout_threshold, fill_under, fidelity, lanczos4, load_rgba, nearest,  # noqa: E402
                      pad, pad_amount, save_rgba, unsharp, wrap_axes)
import textpath  # noqa: E402

SCALE = 4
CELL_BORDER = 4
ALPHA_RAMP = 0.35  # soft alpha: 3 x 3 alpha range up to this keeps Lanczos alpha ...
ALPHA_EDGE = 0.25  # ... and from ALPHA_RAMP + ALPHA_EDGE on the model's alpha

VARIANTS = {
    "lanczos": {"kind": "lanczos"},
    "lanczos_sharp": {"kind": "lanczos", "sharpen": True},
    "pbrify_v4": {"kind": "model", "file": "4x-PBRify_UpscalerV4.safetensors", "dtype": "bf16"},
    "pbrify_v3": {"kind": "model", "file": "4x-PBRify_RPLKSRd_V3.pth", "dtype": "fp16"},
    "ultrasharp_v2": {"kind": "model", "file": "4x-UltraSharpV2.safetensors", "dtype": "bf16"},
    "hdcube3": {"kind": "model", "file": "4xHDcube3_500k.pth", "dtype": "fp16"},
    # Path B for lettering; atlas cells that are pictures (button icons) and
    # everything outside the cells come from this model.
    "pathb": {"kind": "pathb", "model": "ultrasharp_v2"},
}


# ------------------------------------------------------------------ models

class ModelRunner:
    """A spandrel model on the GPU as a function HxWx3 -> 4H x 4W x 3."""

    def __init__(self, path, dtype, tile, log):
        import torch
        import safetensors.torch
        import spandrel
        self.torch = torch
        torch.backends.cuda.matmul.allow_tf32 = True
        torch.backends.cudnn.allow_tf32 = True
        torch.backends.cudnn.benchmark = False
        if path.lower().endswith(".safetensors"):
            sd = safetensors.torch.load_file(path, device="cpu")
        else:
            # Older checkpoints pickle their container as typing.OrderedDict
            # (an alias of collections.OrderedDict): allow just that type.
            with torch.serialization.safe_globals([typing.OrderedDict]):
                sd = torch.load(path, map_location="cpu", weights_only=True)
        sd = spandrel.canonicalize_state_dict(sd)
        self.desc = spandrel.ModelLoader(device="cpu").load_from_state_dict(sd)
        del sd
        if self.desc.scale != SCALE:
            raise ValueError(f"{path}: scale {self.desc.scale}, need {SCALE}")
        self.desc.to(torch.device("cuda"))
        self.desc.eval()
        self.arch = self.desc.architecture.name
        self.tile = tile
        self.requested = dtype
        self.dtype = {"bf16": torch.bfloat16, "fp16": torch.float16, "fp32": torch.float32}[dtype]
        self.desc.model.to(self.dtype)
        self.log = log
        self.checked = 0
        self.check_results = []

    def set_dtype(self, dtype):
        self.dtype = dtype
        self.desc.model.to(dtype)

    def _infer(self, img):
        torch = self.torch
        t = torch.from_numpy(np.ascontiguousarray(img.transpose(2, 0, 1)))[None].to("cuda", self.dtype)
        out = self.desc(t)
        o = out[0].float().clamp(0, 1).cpu().numpy().transpose(1, 2, 0)
        del t, out
        return o

    def _tiled(self, img):
        h, w = img.shape[:2]
        tile, ov = self.tile, 16
        if h <= tile and w <= tile:
            return self._infer(img)

        def starts(n):
            if n <= tile:
                return [0]
            s = list(range(0, n - tile, tile - 2 * ov))
            return s + [n - tile]

        out = np.zeros((h * SCALE, w * SCALE, 3), np.float32)
        wsum = np.zeros((h * SCALE, w * SCALE, 1), np.float32)
        for y0 in starts(h):
            for x0 in starts(w):
                y1, x1 = min(y0 + tile, h), min(x0 + tile, w)
                o = self._infer(img[y0:y1, x0:x1])

                def ramp(n, first, last):
                    r = np.ones(n * SCALE, np.float32)
                    k = ov * SCALE
                    i = np.arange(n * SCALE, dtype=np.float32)
                    if not first:
                        r = np.minimum(r, (i + 0.5) / k)
                    if not last:
                        r = np.minimum(r, (n * SCALE - i - 0.5) / k)
                    return np.clip(r, 1e-3, 1.0)

                wy = ramp(y1 - y0, y0 == 0, y1 == h)
                wx = ramp(x1 - x0, x0 == 0, x1 == w)
                wt = (wy[:, None] * wx[None, :])[..., None]
                out[y0 * SCALE:y1 * SCALE, x0 * SCALE:x1 * SCALE] += o * wt
                wsum[y0 * SCALE:y1 * SCALE, x0 * SCALE:x1 * SCALE] += wt
        return out / np.maximum(wsum, 1e-6)

    def __call__(self, img):
        torch = self.torch
        with torch.inference_mode():
            out = self._tiled(img.astype(np.float32))
            # Reduced precision checked against fp32 on the first pictures; a
            # model that is not finite or far off falls back to fp32 (TF32).
            if self.dtype != torch.float32 and self.checked < 3 and min(img.shape[:2]) >= 16:
                self.checked += 1
                keep = self.dtype
                self.set_dtype(torch.float32)
                ref = self._tiled(img.astype(np.float32))
                self.set_dtype(keep)
                finite = bool(np.isfinite(out).all())
                mse = float(((out - ref) ** 2).mean()) if finite else float("inf")
                result = {"shape": list(img.shape[:2]), "finite": finite, "psnr_vs_fp32": round(rmcommon.psnr(mse), 2),
                          "max_abs": round(float(np.abs(out - ref).max()), 4) if finite else None}
                self.check_results.append(result)
                self.log(f"  precision check {self.requested} vs fp32: {result}")
                if not finite or result["psnr_vs_fp32"] < 35.0:
                    self.log(f"  {self.requested} rejected for {self.arch}: using fp32 (TF32) from here on")
                    self.set_dtype(torch.float32)
                    self.requested = "fp32 (fallback)"
                    out = ref
            if not np.isfinite(out).all():
                raise RuntimeError("model output not finite")
        return out

    def close(self):
        del self.desc
        self.torch.cuda.empty_cache()


def lanczos_sr(img):
    return lanczos4(img, SCALE)


# --------------------------------------------------------------- pipeline

def run_picture(src, row, mode, sr, alpha_sr, wrap, valid_extra=None, sharpen=False, mask_nearest=False):
    """One picture (a texture or a cell) through the whole 4x pipeline.
    `mask_nearest`: a cut-out's mask by nearest neighbour (square windows
    one texel wide stay square)."""
    h, w = src.shape[:2]
    rgb, a = src[..., :3], src[..., 3]
    if mode == "additive":
        cin = rgb * a[..., None]  # what the player sees over black
    else:
        valid = (a > 0) if mode in ("blend", "cutout") else None
        if valid_extra is not None:
            valid = valid_extra if valid is None else (valid & valid_extra)
        cin = fill_under(rgb, valid) if valid is not None and not valid.all() else rgb
    wy, wx = wrap
    py, px = pad_amount(h, wy), pad_amount(w, wx)

    def crop(x):
        return x[py * SCALE:(py + h) * SCALE, px * SCALE:(px + w) * SCALE]

    # Models get mirrored padding on axes that do not wrap: repeated border
    # rows become streaks the model draws into the outer rows. Lanczos keeps
    # the edge extension (a clamp).
    edge = "edge" if sr is lanczos_sr else "symmetric"
    cout = crop(sr(pad(cin, py, px, wy, wx, edge)))
    if sharpen:
        cout = unsharp(cout)
    if mode == "opaque":
        aout = np.ones((h * SCALE, w * SCALE), np.float32)
    else:
        apad = pad(a, py, px, wy, wx)
        if mode == "cutout" and mask_nearest:
            aout = crop(nearest(apad, SCALE))
        elif mode in ("additive", "ignore", "cutout"):
            aout = crop(lanczos4(apad, SCALE))
        else:
            amod = pad(a, py, px, wy, wx, edge)
            aout = crop(alpha_sr(np.repeat(amod[..., None], 3, axis=2))).mean(axis=2)
            if mode == "blend":
                # Soft alpha ramps (vignettes, glows: the loading cards draw
                # enlarged additive copies of a picture whose soft border
                # makes the glow) stay as smooth as Lanczos makes them; the
                # model's sharper alpha is kept only at hard edges (lettering,
                # outlines: a jump of more than ~0.35 within 3 x 3 texels).
                k = np.ones((3, 3), np.uint8)
                jump = cv2.dilate(apad, k) - cv2.erode(apad, k)
                edge = np.clip((jump - ALPHA_RAMP) / ALPHA_EDGE, 0.0, 1.0)
                edge = crop(cv2.resize(edge, (edge.shape[1] * SCALE, edge.shape[0] * SCALE), interpolation=cv2.INTER_LINEAR))
                aout = edge * aout + (1.0 - edge) * crop(lanczos4(apad, SCALE))
        if mode in ("blend", "additive", "cutout"):
            k = np.ones((3, 3), np.uint8)
            lo = crop(nearest(cv2.erode(apad, k), SCALE))
            hi = crop(nearest(cv2.dilate(apad, k), SCALE))
            aout = np.clip(aout, lo, hi)
        if mode == "cutout":
            thr, inclusive = cutout_threshold(row)
            aout = ((aout >= thr) if inclusive else (aout > thr)).astype(np.float32)
    if mode == "additive":
        safe = np.maximum(aout, 1.0 / 255.0)[..., None]
        rgb_out = np.where(aout[..., None] > 1.0 / 255.0, np.clip(cout / safe, 0.0, 1.0), cout)
    else:
        rgb_out = cout
    return np.concatenate([rgb_out, aout[..., None]], axis=2).astype(np.float32)


merge_cells = rmcommon.merge_cells


def cut_cell(src, rect, mode):
    """A cell with a border of its own edge texels (colour and alpha): nothing
    of the neighbouring cells bleeds in, and a cell whose picture reaches its
    border (window frame pieces drawn side by side) keeps a solid edge
    instead of fading into an invented transparent margin."""
    x0, y0, x1, y1 = rect
    cell = src[y0:y1, x0:x1]
    b = CELL_BORDER
    return pad(cell, b, b, False, False)


def paste_cell(out, rect, cell_out):
    x0, y0, x1, y1 = rect
    b = CELL_BORDER * SCALE
    out[y0 * SCALE:y1 * SCALE, x0 * SCALE:x1 * SCALE] = cell_out[b:b + (y1 - y0) * SCALE, b:b + (x1 - x0) * SCALE]


def dequantize_alpha(a, wrap, sigma=1.5):
    """Coarse soft alpha (a staircase of a few levels) made a smooth ramp:
    blurred, then each texel kept within half a step of its own level (the
    quantisation bin it came from), so the picture at its own size stays as
    it was while the 4x ramp has no hard steps."""
    q = np.round(a * 255.0)
    levels = np.unique(q)
    if len(levels) < 2:
        return a, 0.0
    idx = np.searchsorted(levels, q)
    mids = (levels[1:] + levels[:-1]) / 2.0
    lo = np.where(idx > 0, mids[np.maximum(idx - 1, 0)], q) / 255.0
    hi = np.where(idx < len(levels) - 1, mids[np.minimum(idx, len(mids) - 1)], q) / 255.0
    p = int(np.ceil(3 * sigma)) + 1
    wy, wx = wrap
    ab = cv2.GaussianBlur(pad(a, p, p, wy, wx), (0, 0), sigma)[p:-p, p:-p]
    return np.clip(ab, lo, hi).astype(np.float32), sigma


def smooth_steps(src, mode, wrap, dither):
    """`smooth`: the soft alpha dequantised (dequantize_alpha); `dither`:
    also the colour's dither blurred lightly (sigma 0.7 texels, on
    premultiplied colour where alpha is seen) before the upscale."""
    out = src.copy()
    sig_a = 0.0
    if mode not in ("opaque", "ignore"):
        out[..., 3], sig_a = dequantize_alpha(src[..., 3], wrap)
    sig_c = 0.0
    if dither:
        sig_c = 0.7
        p = 4
        wy, wx = wrap
        big = pad(src, p, p, wy, wx)
        if mode in ("opaque", "ignore"):
            out[..., :3] = cv2.GaussianBlur(big[..., :3], (0, 0), sig_c)[p:-p, p:-p]
        else:
            pm = cv2.GaussianBlur(big[..., :3] * big[..., 3:4], (0, 0), sig_c)[p:-p, p:-p]
            ab = cv2.GaussianBlur(big[..., 3], (0, 0), sig_c)[p:-p, p:-p][..., None]
            out[..., :3] = np.where(ab > 1.0 / 255.0, pm / np.maximum(ab, 1.0 / 255.0), src[..., :3])
    return np.clip(out, 0.0, 1.0).astype(np.float32), sig_a, sig_c


def denoise_dither(src):
    """A decal's fine dither smoothed edge-preservingly before the model."""
    out = src.copy()
    out[..., :3] = cv2.bilateralFilter(np.ascontiguousarray(src[..., :3]), 5, 0.06, 1.0)
    return out


def process(src, row, sel, variant, sr, alpha_sr, export_dir, notes):
    """The 4x picture and the texels that count for the fidelity check (a
    character's UV islands, else None)."""
    mode = alpha_mode(row)
    spec = VARIANTS[variant]
    cls = sel["class"]
    wrap = wrap_axes(row, sel)
    valid_extra = rmcommon.uv_island_mask(export_dir, row, cls)
    if valid_extra is not None:
        notes.append(f"uv islands {valid_extra.mean():.0%} of texels")
    sharpen = spec.get("sharpen", False)
    pre = sel.get("pre", "-")
    if pre in ("smooth", "dither"):
        src, sig_a, sig_c = smooth_steps(src, mode, wrap, pre == "dither")
        sharpen = False
        notes.append(f"{pre}: alpha dequantised (sigma {sig_a:g}), colour blur {sig_c:g}, no sharpening")
    elif pre == "denoise":
        src = denoise_dither(src)
        notes.append("dither denoised")
    mask_nearest = pre == "mask_nearest" and mode == "cutout"
    if mask_nearest:
        notes.append("cut-out mask nearest")
    h, w = src.shape[:2]
    rects = merge_cells(rmcommon.read_cells(export_dir, row), w, h) if sel["cells"] == "1" else []
    if spec["kind"] == "pathb" and not rects:
        notes.append("path B (whole picture)")
        return textpath.path_b(src, mode, SCALE), valid_extra
    out = run_picture(src, row, mode, sr, alpha_sr, wrap, valid_extra, sharpen, mask_nearest)
    if rects:
        glyphs = icons = 0
        for rect in rects:
            cell = cut_cell(src, rect, mode)
            if spec["kind"] == "pathb" and textpath.is_flat_glyph(cell):
                cell_out = textpath.path_b(cell, mode, SCALE)
                glyphs += 1
            else:
                cell_out = run_picture(cell, row, mode, sr, alpha_sr, (False, False), None, sharpen)
                icons += 1
            paste_cell(out, rect, cell_out)
        notes.append(f"{len(rects)} cells" + (f" ({glyphs} path B, {icons} model)" if spec["kind"] == "pathb" else ""))
    if mode == "cutout":
        thr, inclusive = cutout_threshold(row)
        notes.append(f"cut-out alpha {'>=' if inclusive else '>'} {thr:g}")
    return out, valid_extra


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--variant", required=True, choices=sorted(VARIANTS))
    ap.add_argument("--export", required=True)
    ap.add_argument("--selection", required=True)
    ap.add_argument("--models", required=True, help="folder with the model files (MANIFEST.txt)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--tile", type=int, default=256, help="model tile size in source texels (default 256)")
    ap.add_argument("--only", default="", help="comma-separated slots or hashes to process (default: all)")
    ap.add_argument("--resume", action="store_true",
                    help="skip textures whose picture and metrics line exist already (an interrupted run)")
    args = ap.parse_args()

    manifest = rmcommon.read_manifest(args.export)
    selection = rmcommon.read_tsv(args.selection)
    only = set(s for s in args.only.split(",") if s)
    todo = [s for s in selection if args.variant in s["variants"].split(",")
            and (not only or s["slot"] in only or s["hash"] in only)]
    out_dir = os.path.join(args.out, args.variant)
    os.makedirs(out_dir, exist_ok=True)
    log_path = os.path.join(out_dir, "run.log")
    log_file = open(log_path, "a", encoding="utf-8")

    def log(msg):
        print(msg, flush=True)
        log_file.write(msg + "\n")
        log_file.flush()

    cols = ["slot", "hash", "class", "mode", "wrap", "size", "de_mean", "de_p95", "psnr", "psnr_alpha", "seconds", "notes"]
    metrics_path = os.path.join(out_dir, "metrics.tsv")
    done = {}
    if os.path.exists(metrics_path):
        done = {r["hash"]: r for r in rmcommon.read_tsv(metrics_path)}
    redo = {s["hash"] for s in todo}
    if args.resume:
        redo = {h for h in redo if h not in done or not os.path.exists(os.path.join(out_dir, manifest[h]["file"]))}
        todo = [s for s in todo if s["hash"] in redo]
    # Metrics lines are written as each texture finishes; lines of textures
    # computed again are replaced, the others kept.
    with open(metrics_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\t".join(cols) + "\n")
        for h, r in done.items():
            if h not in redo:
                f.write("\t".join(str(r.get(c, "-")) for c in cols) + "\n")
    metrics_file = open(metrics_path, "a", encoding="utf-8", newline="\n")

    spec = VARIANTS[args.variant]
    log(f"== {args.variant} ({time.strftime('%Y-%m-%d %H:%M:%S')}): {len(todo)} textures")
    runner = None
    t_load = time.perf_counter()
    if spec["kind"] == "lanczos":
        sr = alpha_sr = lanczos_sr
    else:
        model_variant = args.variant if spec["kind"] == "model" else spec["model"]
        mspec = VARIANTS[model_variant]
        runner = ModelRunner(os.path.join(args.models, mspec["file"]), mspec["dtype"], args.tile, log)
        sr = alpha_sr = runner
        log(f"  model {mspec['file']} ({runner.arch}, {mspec['dtype']}) loaded in {time.perf_counter() - t_load:.2f} s")

    metrics = []
    t_all = time.perf_counter()
    for sel in todo:
        row = manifest[sel["hash"]]
        src = load_rgba(os.path.join(args.export, "textures", row["file"]))
        notes = []
        t0 = time.perf_counter()
        out, mask = process(src, row, sel, args.variant, sr, alpha_sr, args.export, notes)
        dt = time.perf_counter() - t0
        mode = alpha_mode(row)
        fid = fidelity(src, out, mode, mask)
        save_rgba(os.path.join(out_dir, row["file"]), out)
        wy, wx = wrap_axes(row, sel)
        m = {"slot": sel["slot"], "hash": sel["hash"], "class": sel["class"], "mode": mode,
             "wrap": ("t" if wy else "") + ("s" if wx else "") or "-", "size": f"{src.shape[1]}x{src.shape[0]}",
             **fid, "seconds": round(dt, 3), "notes": "; ".join(notes) or "-"}
        metrics.append(m)
        metrics_file.write("\t".join(str(m[c]) for c in cols) + "\n")
        metrics_file.flush()
        log(f"  {sel['slot']} {row['file']}: {mode}, wrap {m['wrap']}, dE {fid['de_mean']}/{fid['de_p95']}, "
            f"PSNR {fid['psnr']} dB, alpha {fid['psnr_alpha']} dB, {dt:.2f} s; {m['notes']}")
    metrics_file.close()
    total = time.perf_counter() - t_all
    info = {"variant": args.variant, "textures": len(todo), "seconds": round(total, 2),
            "texture_seconds": round(sum(m["seconds"] for m in metrics), 2)}
    if runner is not None:
        import torch
        info.update({"arch": runner.arch, "precision": runner.requested, "precision_checks": runner.check_results,
                     "max_vram_mib": round(torch.cuda.max_memory_allocated() / 2 ** 20)})
        runner.close()
    if todo:  # a resumed run with nothing left keeps the last run's record
        with open(os.path.join(out_dir, "run.json"), "w", encoding="utf-8") as f:
            json.dump(info, f, indent=1)
    log(f"== {args.variant}: {len(todo)} textures in {total:.1f} s {json.dumps({k: v for k, v in info.items() if k != 'precision_checks'})}")


if __name__ == "__main__":
    main()
