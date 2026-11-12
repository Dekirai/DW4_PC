# Texture remaster pipeline (pilot)

Scripts that turn the textures exported by `dw4_texpack export`
(docs/TEXTURES.md) into a 4x texture pack, compare super-resolution models
per texture class, and check the result in hidden game runs. They are
development tools: they are not part of the release package, and nothing
they produce from the game's pictures (exports, upscales, contact sheets,
packs, screenshots) belongs in the repository or in a public package. Keep
all of it in a folder outside the repository (the pilot used
`E:\DW4Remaster\`).

## Requirements

* Python 3.13 with numpy, OpenCV (`cv2`) and Pillow for everything.
* For the models: a virtual environment with torch + torchvision (CUDA),
  spandrel, safetensors and einops, e.g.
  `py -3.13 -m venv --system-site-packages <dir>\venv`, then
  `pip install --no-cache-dir torch==2.14.1 torchvision==0.29.1 --index-url https://download.pytorch.org/whl/cu132`
  and `pip install --no-cache-dir spandrel==0.4.2 safetensors==0.8.0 einops==0.8.2`.
* Model files in one folder (never in the repository). The pilot used:

  | Variant | File | Architecture | Licence |
  | --- | --- | --- | --- |
  | `pbrify_v4` | [4x-PBRify_UpscalerV4.safetensors](https://github.com/Kim2091/Kim2091-Models/releases/tag/4x-PBRify_UpscalerV4) | DAT2, bf16 | CC0 |
  | `pbrify_v3` | [4x-PBRify_RPLKSRd_V3.pth](https://github.com/Kim2091/Kim2091-Models/releases/tag/4x-PBRify_RPLKSRd_V3) | RealPLKSR, fp16 | CC0 |
  | `ultrasharp_v2` | [4x-UltraSharpV2.safetensors](https://huggingface.co/Kim2091/UltraSharpV2) | DAT2, bf16 | CC-BY-NC-SA-4.0 |
  | `hdcube3` | [4xHDcube3_500k.pth](https://github.com/Venomalia/HDcube/releases/tag/3.0) | ESRGAN, fp16 | treated as non-commercial |

  `.pth` files are read only with `torch.load(weights_only=True)` (allowing
  just `typing.OrderedDict`, an alias of `collections.OrderedDict`), and
  `.safetensors` with safetensors. Record each file's SHA-256 next to it.

Run the scripts with `python -I` (isolated mode; they add their own folder
to the import path). They lower their own priority to below normal.

## Steps

1. `dw4_texpack export --data <game data> --out <dir>\export`
2. `pilot_select.py --export <dir>\export --out <dir>\pilot\selection.tsv`:
   the pilot's 31 textures (all classes, first from what the hidden test
   routes show) and one palette variant pair, with the variants each class
   is compared with.
3. `upscale.py --variant V --export ... --selection ... --models <models> --out <dir>\pilot\up`
   for each variant: `lanczos`, `lanczos_sharp` (CPU), `pbrify_v4`,
   `pbrify_v3`, `ultrasharp_v2`, `hdcube3` (GPU, one model at a time, in
   tiles of `--tile` texels) and `pathb` (text path B, with
   `ultrasharp_v2` for the atlas cells that are pictures). Per texture:
   * colours under alpha 0 are filled from the neighbours (push-pull), for
     characters also outside the UV islands of the export's `uvmask`;
   * axes in the manifest's `tiles` or `uv_edges` are padded cyclically,
     others by edge extension; the padding is cut off after 4x;
   * atlas cells (font glyphs, UI pictures) are cut out one by one with a
     transparent border and pasted back;
   * RGB and alpha are upscaled separately;
   * cut-outs are thresholded at the material's alpha reference (at least
     0.5 where a draw also blends); soft alpha stays soft: Lanczos in gentle
     ramps (a model sharpens a vignette into steps, which the loading cards'
     enlarged additive copies turn into rings), the model's alpha only at
     hard edges, everything within the 3 x 3 range of the source; additive
     effects upscale colour times alpha and take a Lanczos alpha (no
     invented alpha);
   * colour fidelity: the result shrunk back by 4 x 4 area means against
     the source (CIEDE2000 mean / 95th percentile, PSNR) in `metrics.tsv`.
   A model in fp16/bf16 is compared with fp32 on its first pictures and
   falls back to fp32 when it is not finite or below 35 dB.
4. `contact_sheets.py --export ... --selection ... --up ... --out <dir>\pilot\sheets`:
   one sheet per texture (original nearest 4x, Lanczos, the class's
   variants, two zoomed crops; for tiling textures the corner where four
   copies meet), `overview.png` and `palette_check.png`.
5. `make_pack.py --selection ... --up ... --out <dir>\pilot\pack --choose class=variant,... [--override slot=variant]`.
6. Point the game at the pack. Hidden test runs read no settings.ini; they
   take `DW4_TEXTURE_PACK=<folder>`. In play: F1 > Settings > Texture packs
   (settings.ini `[mods] textures=<folder>`), then Reload.
7. `game_compare.py run` (hidden runs with screenshots, with and without
   the pack), `game_compare.py compare` (side-by-side pictures and crops,
   fingerprint check), `game_compare.py timing` (longest pass of the
   dng101b1 load without a pack, with an empty pack and with the pack).
8. Determinism: `DW4_TEXTURE_PACK=<pack> bash work/dref/checks/det.sh EXE NAME`
   must print the same fingerprint and audio digests as without the pack.

## Text path B

`textpath.py` rebuilds lettering instead of letting a model guess it: the
coverage (alpha, or the boundary between two tones) is interpolated to 16x,
thresholded at 0.5, traced with `cv2.findContours`, the contours smoothed
along their length and filled, then shrunk to 4x (anti-aliased). Colours and
gradients are the original's, spread within each tone only. Font cells
drawn in at most four flat colours take this path; shaded cells (controller
buttons, icons) go through the model one cell at a time.
