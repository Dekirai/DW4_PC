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
     border of their own edge texels and pasted back (the pilot used a
     transparent border, which fades pieces that fill their cell);
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

## Full run

The full run uses the settings the pilot ended with: the variant follows
content and source format, not the name alone.

1. `classify.py --export <dir>\export --out <dir>\out\classes.tsv`: a class
   and variant for every texture (rules in its docstring), the language
   (`usa` 1 for what the USA release loads; the others are computed too and
   marked by `lang`), whether atlas cells are cut out, and whether the
   texture is curated. `class_overrides.tsv` holds the visual checks (enemy
   skins, painted pictures and ornaments among the unnamed stage textures,
   and the few textures whose first result looked wrong). C8 stage textures
   drawn additively count as glows; stage textures that are flat graphics
   (many hard jumps between flat areas: panels, signs, LED grids; not
   cut-outs) go to the 2D model, because PBRify smears pixel art.

   | Class | Variant |
   | --- | --- |
   | environment (materials, foliage) | `pbrify_v4` |
   | env_graphic (flat stage graphics) | `ultrasharp_v2` |
   | env_glow (additive stage overlays), effect | `lanczos_sharp` |
   | character, weapon, item, card (BC1 imagery) | `hdcube3` |
   | boss, portrait (painted) | `hdcube3`, curated against `ultrasharp_v2` |
   | ui (C8 2D, multi-tone digits), loading, title, decal | `ultrasharp_v2` |
   | font (font00) | `pathb` (glyphs), `ultrasharp_v2` (button cells) |

2. `upscale.py --variant V --selection <dir>\out\classes.tsv --out <dir>\out\full --resume ...`
   for `lanczos_sharp`, `pathb`, `ultrasharp_v2`, `hdcube3`, `pbrify_v4`
   (one model at a time; each takes the textures whose `variants` name it).
   Cells are cut out with a border of their own edge texels (pieces that
   fill their cell keep solid edges), and 3D skins (characters, bosses,
   weapons, items) are filled outside their UV islands.
3. `finalize.py --export ... --classes ... --up <dir>\out\full --out <dir>\out`:
   `master4x\<hash>.png` (4x RGBA), `high2x\<hash>.png` (2x: Lanczos3 on
   premultiplied colour, wrap-aware, clamped to each 4 x 4 footprint so no
   ringing halo appears; cut-outs stay binary with their coverage),
   `report.tsv` (class, variant, CIEDE2000 before and after correction,
   alpha PSNR, seam check, flags, seconds) and `report.json` (counts,
   palette group check). Soft alpha whose 4x result misses the source's
   coverage by more than 0.08 (a model that made a half-transparent outline
   or shadow solid) takes the Lanczos alpha there. Curation blends the alternative in through a
   feathered mask where the base lost detail the alternative kept (per
   texel CIEDE2000 against the source). Colour correction where the mean
   CIEDE2000 exceeds 3 (not for cards, whose redraw look is intended): the
   low-passed difference to the source (sigma 2 texels, 1 when needed) is
   added back, for all palette variants of a group alike, and kept only
   when it lowers the mean noticeably.
4. `contact_sheets.py --report <dir>\out\report.tsv --classes ... --final <dir>\out --up <dir>\out\full --out <dir>\out\sheets`:
   six textures per sheet (original and final, crops of 32 x 32 texels at
   8x: original, Lanczos, final, and the corner where four copies meet for
   tiling environment textures): 60 random ones, every flagged one, and the
   curated ones with both variants.

## Text path B

`textpath.py` rebuilds lettering instead of letting a model guess it: the
coverage (alpha, or the boundary between two tones) is interpolated to 16x,
thresholded at 0.5, traced with `cv2.findContours`, the contours smoothed
along their length and filled, then shrunk to 4x (anti-aliased). Colours and
gradients are the original's, spread within each tone only. Font cells
drawn in at most four flat colours take this path; shaded cells (controller
buttons, icons) go through the model one cell at a time.
