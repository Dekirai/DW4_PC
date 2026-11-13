# Native IGB texture decoding

The native viewer reads original `igImage` objects from the user's AFS entries,
resolves their image and palette memory references, and decodes their base image
to straight RGBA8. The verified USA data uses two linear storage layouts:
indexed8 with an RGBA8 palette, and BC1/DXT1. This implementation does not load
the original executable or use Dolphin to produce the decoded pixels.

## Evidence and tested scope

The complete local `GDJEB2`, disc 0, revision 0 corpus was parsed and decoded
on 2026-09-08. Counts include repeated image objects across different IGBs;
they are not a count of unique textures.

| Archive | IGB documents | Indexed8 + RGBA8 | BC1/DXT1 | Images decoded | Failures |
| --- | ---: | ---: | ---: | ---: | ---: |
| `area.afs` | 1,379 | 13,249 | 188 | 13,437 | 0 |
| `ndmw.afs` | 894 | 9,005 | 3,464 | 12,469 | 0 |
| Total | 2,273 | 22,254 | 3,652 | 25,906 | 0 |

Representative images were also inspected visually:

- `ndmw.afs`, entry 79, global object reference 18: `font00.png`, 512 × 512,
  a packed glyph and controller-icon atlas using indexed pixels and a palette.
- `area.afs`, entry 621, global object reference 1: `card03.dds`, 128 × 128,
  an Agumon card using BC1/DXT1.
- `ndmw.afs`, entry 179, global object reference 1270: an unnamed 64 × 64
  BC1/DXT1 texture.

For an independent BC1 comparison, the original `card03.dds` memory payload
was wrapped in a standard DDS header locally and decoded with Pillow's DDS
reader. Its entire 65,536-byte RGBA result matched the native decoder exactly.
Both show blue, inverted creature artwork inside an upright border and name.
That appearance is retained as stored; it is not cosmetically flipped or
recolored to match expectations. How the original game transforms or shades
that asset remains a separate rendering investigation.

The scalar-layout investigation used the primary author's
[IGB exploration notes](https://gist.github.com/mateon1/b123a53d824305767989ebc2ba3e5814)
as leads, then checked values and references against the original local files.
The parser and decoder are independently written C++20. No code from that
unlicensed exploratory script is included.

## Observed image schema

Fields are found by their serialized slot numbers. The loader does not cast
the object to a host struct or assume that slot numbers are byte offsets.
The following table describes the observed `igImage` version 1.0 layout.
Where a semantic name remains uncertain, the slot is described by its observed
value instead of assigning an unsupported meaning.

| Slot | Serialized kind | Indexed8 | BC1/DXT1 | Interpretation used |
| --- | --- | --- | --- | --- |
| 2 | unsigned integer | width | width | Output width |
| 3 | unsigned integer | height | height | Output height |
| 4 | unsigned integer | 1 | 4 | Storage descriptor; **not established as geometric depth** |
| 5 | unsigned integer | 1 | 1 | Additional descriptor, semantic name unverified |
| 6 | unsigned integer | 100 | 100 | Observed order descriptor |
| 7–10 | unsigned integers | 0, 0, 0, 0 | 1, 1, 1, 1 | Channel descriptors |
| 11 | enumeration | 65,536 | 14 | Alchemy image format, not a GX register value |
| 12 | signed integer | byte count | byte count | Declared encoded image size |
| 13 | memory reference | pixel indices | compressed blocks | Source memory |
| 14 | memory reference | not interpreted | not interpreted | Additional image state |
| 15 | boolean | not interpreted | not interpreted | Additional image state |
| 16 | unsigned integer | 0 | 0 | Intensity descriptor |
| 17 | object reference | `igClut` | unused | Palette reference |
| 18 | unsigned integer | 8 | 0 | Index-bit descriptor |
| 19 | signed integer | row stride | not interpreted | Indexed byte pitch |
| 20 | boolean | false | true | Compression marker |
| 21 | unsigned integer | 0 | 0 | Additional bit descriptor |
| 22 | string | optional name | optional name | Display name |

The referenced `igClut` has format enumeration 7 in slot 2, entry count in
slot 3, byte stride 4 in slot 4, a memory reference in slot 5, and declared
byte count in slot 6. Palette entries are consecutive red, green, blue, alpha
bytes. Indexed rows use slot 19 as their byte pitch; padding is not displayed.

BC1 images use consecutive 4 × 4 blocks in row order. Each eight-byte block
has two little-endian RGB565 endpoints and two-bit selectors, starting with
the least-significant bits. The decoder handles both four-color interpolation
and three-color plus transparent-black mode. Partial edge blocks are cropped
to the declared output dimensions. These rules follow Microsoft's
[BC1 description](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression).

The fact that Alchemy format 14 has the same numeric value as GX CMPR does
not establish an identical byte layout. GX CMPR has different block ordering,
byte ordering, and transparent RGB behavior, as shown in Dolphin's
[texture decoder](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/TextureDecoder_Generic.cpp).
Applying that tiled layout to the verified IGB BC1 data would be incorrect.
GX I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8, paletted tiled formats, and CMPR
are not advertised as supported IGB image layouts by this module.

## Safety and rendering limits

`list_igb_textures` resolves descriptors and checks memory ranges without
decoding pixels. Unsupported descriptors remain visible with a diagnostic.
`decode_igb_texture` decodes only the selected object. Its result owns its
RGBA buffer; source references remain local to the owning IGB document.

The decoder rejects zero dimensions, dimensions above 16,384, output above
64 MiB, missing or wrongly typed references, truncated image or palette data,
invalid row pitch, palette indices outside the declared palette, and storage
descriptors that differ from the observed layouts. The source document reader
separately validates serialized field and reference bounds.

Only the base image is decoded. Trailing encoded bytes are not automatically
interpreted as mipmaps. Big-endian IGB image payloads, additional image schemas,
3D textures, and runtime texture modifications have not been verified.

RGBA output preserves straight palette alpha; the viewer must use an
appropriate alpha blend mode. BC1 transparent selectors produce transparent
black. A PPM test preview discards alpha and is only a debugging view. No
color-space conversion, game material or lighting reconstruction is performed.
Visual inspection establishes recognizable layout and content, not pixel-exact
equivalence to the game's final GX rendering. Palette precision, BC1 rounding,
filtering, alpha tests, and texture color-space behavior still need comparison
against a captured original rendering.

## Sampling (filters and wraps)

`engine::texture_sampler` reproduces the GX texture object the original builds
for an `igTextureAttr` (DOL addresses in `src/engine/scene_graph.cpp`):

- Wraps come from `_wrapS` / `_wrapT` through the GX table `0x80490984`
  (0 and 2 clamp, 1 and 3 repeat; GX never mirrors here).
- Palette images (indexed8 and the other C4/C8/C14X2 formats) ignore the
  attribute's `_magFilter` / `_minFilter`: the bind loop `800FCA9C` rebuilds
  their texture object with `GXInitTexObjCI`, whose `GXInitTexObj` defaults
  are LINEAR (LINEAR_MIPMAP_NEAREST with mip levels), after the
  `GXInitTexObjLOD` that carried the attribute filters.
- Other images (BC1) take the attribute filters through `GXInitTexObjLOD`.

In the USA corpus every `igTextureAttr` has repeat wraps, `_mipmapMode` 0 and
one image. All 3,652 BC1 attributes say LINEAR; of the indexed8 ones 21,336
say LINEAR and 918 (95 `ndmw.afs` members: `font00.png`, the 32 × 32 effect
sprites of the stage files such as `y_014eblst01b.png`, `BREAK00.png`,
`y_onpu.png`, `y_fire01a.png`, `y_ice01.png` and many unnamed ones) say
NEAREST, which the original never applies. So every game texture samples
bilinearly. Dolphin comparisons of fire blasts (dng101b2) and disk sparkles
(dng101b1) match the bilinear sprites.

## Texture pack hash

Texture packs (README, *Texture packs*) find a replacement by the 64-bit
FNV-1a hash of the decoded texture: offset basis `0xcbf29ce484222325`, prime
`0x100000001b3`, over the width and the height (each as four little-endian
bytes) and then the RGBA8 rows top to bottom. A BC1 (GX CMPR) texel with
alpha 0 enters the hash as `0, 0, 0, 0`: the decoder keeps GX's average
colour in those texels, the hash keeps the earlier black, so older packs
still match. `TexturePack::image_hash` (`src/game/texture_pack.cpp`) is the
one implementation; the renderer and `dw4_texpack` both call it. Any change
to the decoders or to this rule changes the hashes and invalidates every
pack; `tests/texpack_tests.cpp` pins three values.

## Texture packs: format 2

A texture pack is a folder (F1 > Settings > Graphics > *Texture pack
folder*, settings.ini `[mods] textures`, default `mods/textures`; test runs
`DW4_TEXTURE_PACK`). Two layouts are read:

- **Format 2**: the folder holds `dw4pack.tsv`, a manifest that lists per
  texture hash the files that hold it, their encodings, scales and mip
  levels. Only the files it names are used.
- **Format 1** (loose files, as before): without a manifest, every file
  below the folder whose name ends in `_<16 hex digits>.png` (or `.dds`)
  replaces the texture of that hash, at any size. One file per hash. The
  folder is walked at most 8 folder levels deep and up to 200,000 files and
  folders; a folder with more (a drive's root given by mistake) is not read
  as a pack, and the F1 menu says why.

`src/game/texture_pack.hpp` reads both; `tests/graphics_tests.cpp` checks
the manifest, the DDS headers, the decoders, the file choice and the loader
with valid and malformed synthetic input.

### The manifest (dw4pack.tsv)

UTF-8 text (a BOM is allowed), LF or CRLF line ends, fields separated by
one TAB each, at most 64 MiB and 1,000,000 rows.

1. The first non-empty line is `#dw4pack<TAB>2` (a space instead of the TAB
   is accepted). A higher number is a newer format: the pack is not used and
   the F1 menu says why. Lower numbers and anything else refuse it too.
2. Further lines starting with `#` are directives or comments:
   `#name<TAB>text` names the pack in the F1 menu; other `#` lines are
   comments.
3. The first other line is the column header. The columns are found by
   name, in any order; unknown columns are ignored (a builder may add its
   own, such as `alpha` or `model`). Required: `hash`, `encoding`, `scale`,
   `levels`, `file`. Optional: `width`, `height`. Columns a builder adds
   are information for people and tools only: the loader never reads them.
   The release pack's `quality` column (`ultra=0,high=1`), for one, names
   the level each quality draws, but the loader derives that level from
   `scale` and `levels` alone (*Quality levels and the file choice*).
4. Every further line is one file of one texture. A texture may have up to
   16 rows (encodings, scales).

| Column | Value |
| --- | --- |
| `hash` | The texture's pack hash (*Texture pack hash* above), 16 hex digits, either case. |
| `encoding` | `bc7`, `bc3`, `bc1` or `rgba8` (a DDS file), or `png` (a PNG file of one level). An encoding this build does not read (a later one, such as `astc6x6` for GLES devices) is skipped without error: the F1 menu notes that the pack also holds files for other devices (no warning). |
| `scale` | The first level's size over the original texture's, the same in both directions: 1, 2, 4, 8 or 16. |
| `levels` | The levels the file holds, 1..15; level *k* has the scale `scale / 2^k` (a PNG: 1). |
| `width`, `height` | The first level's size in texels (empty or 0: not given). When given, the file must match. |
| `file` | The file's path relative to the pack folder, `/` as separator. No drive, no root, no `..` component, no control characters or `: * ? " < > \|`. |

A malformed row is skipped with a warning (the F1 menu lists them): a hash
that is not 16 hex digits, a scale outside 1..16 or not a power of two, a
level count outside 1..15, a width or height that is not 1..16384, an unsafe
path, fewer fields than the header, or a second row with the same hash,
encoding and scale.

```
#dw4pack	2
#name	DW4 Remaster
hash	encoding	scale	levels	width	height	file	alpha
05f8453aab0ffa8d	bc7	4	11	1024	512	tex/05f8453aab0ffa8d.dds	opaque
05f8453aab0ffa8d	astc6x6	4	11	1024	512	gles/05f8453aab0ffa8d.ktx2	opaque
ac566f5a15c2a226	rgba8	4	11	1024	512	tex/ac566f5a15c2a226.dds	blend
```

### The files

- **DDS** (`bc7`, `bc3`, `bc1`, `rgba8`): one 2D texture, no cube map,
  array or volume. Either the DX10 header with the DXGI format 98 / 99 / 97
  (BC7), 77 / 78 / 76 (BC3), 71 / 72 / 70 (BC1), 28 / 29 / 27 (R8G8B8A8) or
  87 / 91 / 90 (B8G8R8A8), or the legacy header with the FourCC `DXT1`
  (BC1), `DXT5` (BC3) or 32-bit RGBA / BGRA masks with alpha. sRGB formats
  are read as their UNORM formats: the game samples texel values as stored,
  as GX does, so a pack stores the values it wants drawn (no sRGB
  conversion). Level *k* is `max(1, size >> k)` texels per side; block
  formats store 4x4 blocks with partial blocks rounded up; every level the
  header declares must be in the file. At most 16384 texels per side, 15
  levels and 512 MiB per file.
- **PNG** (`png`): one level; the loader makes the mip chain (2x2 boxes,
  colours weighted by alpha, cut-outs keep a binary alpha).
- Rows run in the order the game's decoder delivers them, as in the
  export's and the dumps' PNGs (some text stands upside down there): do not
  flip them. Alpha is straight (not premultiplied).
- The mip chain is the pack builder's: the loader draws the levels as they
  are. Build them from the 4x picture in the texture's wrap (repeating
  textures wrap, see the export's `tiles` / `uv_edges`), with alpha
  separate and the alpha coverage kept for cut-outs at the material's
  reference (`tools/remaster/make_dds_pack.py` does this with texconv,
  `tools/remaster/build_pack.py` with its own filter, keeping a given 2x
  level as level 1).
- Recommended encodings for the PC: BC7 for colour and alpha; BC1 where a
  texture is opaque and BC1 holds it well (a quarter of the memory of
  RGBA8, half of BC7); RGBA8 for the font atlas (`faccbeda501eefc1`) and
  the title's PRESS START (`ac566f5a15c2a226`), whose pixels the game reads
  back for the controller prompts (a compressed one is decoded on the CPU
  for that).
- For GLES devices (Android, Adreno: no BC7 in GLES), a pack adds rows of
  an ASTC encoding (KTX2 files) beside the BC rows; a loader that reads them
  picks them by the same rules. The PC loader skips them. The renderer is
  ready for GLES 3 in the parts the options touch: the mip LOD bias is the
  fragment shader's `texture()` bias (GLES 3 samplers have no
  `GL_TEXTURE_LOD_BIAS`), anisotropy and the filters are sampler parameters
  GLES 3 has (anisotropy through `GL_EXT_texture_filter_anisotropic`).

### Quality levels and the file choice

*Texture quality* Ultra draws a pack's scale 4, High its scale 2, Original
no pack at all. Per texture the loader takes the file and the first level
whose scale is the quality's; without one the closest scale below it; else
the smallest above it. Among equals it prefers an encoding the GPU reads,
then BC7, BC3, BC1, RGBA8, PNG. The levels above the chosen one are not
read from the disk. So one 4x BC7 file with its whole chain serves Ultra
(level 0) and High (level 1); a pack may also give a separate 2x file (a
2x row of another encoding, or a smaller one for memory). Loose files
(format 1) take their scale from their size over the original's.

A GPU without an encoding (no `GL_ARB_texture_compression_bptc`, no
`GL_EXT_texture_compression_s3tc`) gets the levels decoded to RGBA8 on the
loading thread: BC7 bit exact after the Khronos Data Format Specification
(`tests/graphics_tests.cpp` checks every mode and partition against
DirectXTex's decoder), BC1 / BC3 after Direct3D's rules.

### Streaming

`src/game/texture_stream.hpp` and `GlRenderer::stream_textures`:

- **Read ahead while a stage loads.** Every pass the host hands the
  renderer the documents the engine holds (`Engine::loaded_files` and the
  fonts' document). A loading thread at below-normal priority scans each
  new document once (decodes and hashes its images, as the renderer would)
  and reads the pack files of the hashes it finds, during the loading
  screen. Payloads wait in memory for the upload (at most 256 MiB of read
  ahead).
- **Never a blocked frame.** A texture is drawn with the game's own picture
  until its replacement is on the GPU; a draw that finds none asks for it
  ahead of the read-ahead work. The main thread uploads about 1.5 ms per
  pass (`DW4_TEXTURE_UPLOAD_MS`), into immutable storage in bands of rows
  (256 KiB), so a large texture spreads over several passes; it is drawn
  once complete. A pass's first piece (a texture's storage or a band)
  always goes up; after it, a piece goes only while the measured upload
  rate (a running average per byte) says it ends within the budget, so a
  pass ends near the budget instead of a band past it.
- **A refused upload falls back.** After a texture's storage and after its
  bands the renderer reads the GL error state: a texture the driver refused
  (format, size, memory) is deleted, never asked for again in this
  configuration and the game's own texture is drawn; F1 and
  `DW4_TRACE_TEXTURES` count it as failed with the GL error.
  `DW4_TEXTURE_TEST_REFUSE=<hash>` makes one pack texture's upload raise a
  GL error (test of this path).
- **A quality or pack change applies to the running stage.** Records
  drawn while nothing was hashed (Original, or no pack) are keyed from the
  scan of their document, which the loading thread makes once a pack
  streams; they show the game's own picture until then (a document it does
  not scan is decoded and hashed once on the main thread).
  `DW4_TEXTURE_QUALITY_AT=FRAME:QUALITY[,...]` changes the quality at those
  frames in a test run, as the F1 menu does.
- **A cache by hash.** Identical textures upload once, the pack's by their
  pack hash, the game's own by their pixel hash while a pack streams; they
  stay across stage changes (a stage visited again draws at once).
- **A memory budget.** Past *Texture memory* the least recently drawn
  textures go (never one drawn in the last two frames); a replacement that
  went is read again when it is drawn again.
- **Presentation only.** Nothing of it reaches the simulation, the tick
  state, saves, snapshots or the online wire; texture packs stay out of the
  mods hash, so online partners may use different packs and options.
  `work/dref/checks/det.sh` gives the same fingerprint and audio digests
  without a pack, with a pack at every quality, with every option at its
  maximum and with the block formats decoded on the CPU
  (`DW4_TEXTURE_CPU_DECODE=1`).

`DW4_TRACE_TEXTURES=1` prints a line per stage: the game's and the pack's
textures in GPU memory and the peak, the uploads and their time, the draws
that showed the original while the pack's texture was not there yet,
evictions, failures and the loading thread's scan and read times (2: also
each texture drawn before its pack texture). `DW4_TRACE_LOADTIME` adds a
*loadtime window* line per stage: the longest pass from the stage change to
600 steps after the stage started, the texture work in it, the most texture
work in any pass of the window and the distribution of the passes after the
stage started (median, p95, p99, the longest with its texture work, the
passes over 8.3 and 16.7 ms). The window's single longest pass is mostly the
load's own heaviest step and moves with the machine's load by several ms (it
also does so between two runs without any pack), so it is no measure of the
textures; `tools/remaster/game_compare.py timing` compares the
distribution after the start against an empty pack as its control (below).

## Graphics options

F1 > Settings > Graphics, saved in settings.ini `[graphics]`, applied at
once. Test runs read no settings.ini: they take the defaults and the
environment variables.

| Option | Values (settings.ini) | Test runs | What it does |
| --- | --- | --- | --- |
| Texture quality | `original`, `high`, `ultra` (default) | `DW4_TEXTURE_QUALITY` | No pack, the pack's 2x level, its 4x level (above). |
| Texture filtering | `bilinear` (default), `trilinear`, `anisotropic2` .. `anisotropic16` | `DW4_TEXTURE_FILTER` (also `2x` .. `16x`) | Bilinear samples the game's textures as GX does (full size only; pack textures blend their mipmaps). Trilinear gives the game's textures a mip chain (made on upload: colours by alpha, cut-outs keep a binary alpha, so they keep their coverage at a distance). Anisotropic adds `GL_TEXTURE_MAX_ANISOTROPY` (the levels the GPU offers). Sampler objects: no texture is uploaded again for a change of the anisotropy. |
| Mip LOD bias | `lod_bias`, -2.00 .. 2.00 (0) | `DW4_LOD_BIAS` | Added to the level choice of mipmapped textures (the shader's `texture()` bias; 0 for textures without mip levels). |
| Sharpening | `off` (default), `low`, `high` | `DW4_SHARPEN` | Contrast adaptive sharpening of the game picture (after the published description of AMD FidelityFX CAS): the picture is copied after the scene and drawn again through a 3x3 filter; the menus are not sharpened. |
| Shadow quality | `original` (default), `high`, `ultra` | `DW4_SHADOW_QUALITY` | The projected shadow's map at the game's size (128 x 128), 4x or 8x the texels per side; the light box maps to the same part of it. |
| Texture memory | `texture_memory`, MiB (1024; F1: 512 MiB .. 4 GiB; settings.ini 32 .. 8192, another value shows as itself) | `DW4_TEXTURE_MEMORY` | The cache's budget (above). |
| Texture pack folder | `[mods] textures` | `DW4_TEXTURE_PACK` | Reload reads it again on a thread of its own (the menu says *Reading the texture pack folder* meanwhile); the game keeps running and the stage switches to the new pack once it is read. |

Every option applies to the running stage, the texture quality too (from
Original to High or Ultra the pack's textures come in as their documents
are scanned and their files read, a few frames).

The presets: *Original* sets Texture quality Original, Bilinear, LOD bias 0,
no sharpening and the game's shadow map; *Modernized* Ultra, Anisotropic
16x and a 4x shadow map (sharpening stays as it is: a matter of taste).
A settings.ini written before the Graphics options (no `[graphics]` key)
keeps showing the preset it was set to: one that matched Original gets the
Original graphics, one that matched Modernized the Modernized graphics;
any other keeps the defaults.

## Exporting every texture (dw4_texpack)

`dw4_texpack export --data ROOT --out DIR [--no-uv-masks]` reads the game data
(the extracted folder or the disc image; only read) and writes each unique
texture once, with what the data says about its use. It is a development
tool for texture pack authors and is not part of the release package. Its
output is derived from the game's pictures and stays on the user's machine.
`DIR` must be new, empty or an earlier export (marked by
`.dw4_texpack_export`, whose own files are replaced). The tool runs at
below-normal priority.

| Output | Contents |
| --- | --- |
| `textures/<name>_<w>x<h>_<hash>.png` | The decoded pixels (straight RGBA, the GX average colour in transparent CMPR texels), named exactly as the renderer's dumps; the file works unchanged as a replacement. Rows are stored as the decoder delivers them, so some pictures (text) stand upside down, as in the dumps. |
| `manifest.tsv` | One line per unique hash (columns below). |
| `instances.tsv` | One line per `igImage`: archive, entry, source path, reference, name, hash. |
| `atlas/<hash>.tsv` | Cells in texels (`x0 y0 x1 y1`, end exclusive): `font` glyphs (with the character code), particle sprite sheet `grid` cells, `quad` sprite rectangles and `uvkey` cells of keyed UV animations. |
| `uvmask/<hash>.png` | 8-bit mask of the texels the meshes' UVs cover (one texel of margin), for textures whose UVs stay inside the picture. |
| `summary.txt` | Counts, errors and warnings. |

Manifest columns:

| Column | Meaning |
| --- | --- |
| `hash`, `file`, `names` | Pack hash, PNG name, every image name it carries (one hash can have several, such as `P_dorumon01.dds` and `P_dorumon02.dds`). |
| `width`, `height`, `format` | Size and source format: `C8` (indexed8 with an RGBA8 palette) or `CMPR` (BC1). |
| `palette_entries`, `palette_group`, `palette_variants` | For C8: palette size, an id of the index plane, and how many unique textures share that index plane under other palettes (variants to process alike). |
| `alpha`, `alpha0_texels`, `alpha_partial_texels` | `opaque`, `cutout` (alpha 0 and 255 only) or `blend`, with the texel counts. |
| `alpha_test`, `blend` | The alpha compare (`GREATER 0.1`, the engine default, or the material's) and the blend factors (`SRC_ALPHA/ONE` is additive) of every draw that uses the texture, `off` where disabled. |
| `wrap`, `filter` | The sampler (`engine::texture_sampler`): wraps S/T, filters mag/min. |
| `languages`, `usa` | `all` for language-neutral data, otherwise the language or region packs (`usa`, `eng`, ..., `aus`, `aeu`, `ajp`, `base` for `loading/`); `usa` is 1 when the USA release loads it (neutral, `usa` or `aus`). |
| `stages` | Stages whose files hold it (`common` for `_common/` and `bectext.igb`; loading screens by their stage). |
| `instances`, `documents`, `uses` | `igImage` objects, documents, and draws found in the scene graphs. |
| `pack_names` | Names under which a `beTextureCtrl` pack offers it (`lh_pack`, `ls_pack`, the language packs); scene sets that ask for the name draw it. |
| `uv_range` | Union of the drawn UV boxes (after texture transforms), `u0..u1,v0..v1`. |
| `tiles` | Axes on which a draw's UVs leave the picture by more than a texel (and 1 %) with a repeat wrap: the picture repeats within a face. |
| `uv_edges` | Axes on which drawn UVs reach a border (within a texel) or cross it with a repeat wrap: bilinear filtering reads the opposite edge there, and faces side by side with border-to-border UVs tile the picture. Pad these axes cyclically before upscaling. |
| `uv_motion` | `scroll` (an interpolated UV transform sequence), `keys` (a stepped one), `texgen` (sphere map), `particle` (moving particle UVs). |
| `atlas` | `font:<name>:<glyphs>`, `grid:<x>x<y>` (particle sprite sheets), `quads:<n>` (distinct sprite rectangles, not just the whole picture); cells in `atlas/`. |
| `uv_mask` | `uvmask/<hash>.png`, `full` (repeating or moving UVs reach everything) or `-`. |
| `source` | One document that holds it. |

The scene walk follows the engine's nodes and attributes (`SceneCache`) with
the traversal's rules for the unit 0 texture, its enable state, blending, the
alpha compare and the coordinate source, but visits every child (all switch
and LOD children, actor skins). Scene sets that `beTextureCtrl` binds by name
take every pack texture of that name in their stages (both `lh_pack` and
`ls_pack`, every language), as `TextureCtrlManager::bind_all` would with the
packs a stage loads. Particle UVs come from the `igParticleAttr` records,
glyph cells from `beFontImage`. What only a manager writes at run time
(numbers, menu text quads) is not in the data, so `num01.png` shows only its
authored cell.

Measured on the USA disc (GDJEB2) on 2026-11-12: 2,273 documents, 25,906
images, 1,697 unique hashes (1,538 used by the USA release), 0 errors,
6,674 `beTextureCtrl` bindings all resolved in their own stages, 1,696
textures with draws (the exception, `font00.png`, has its glyph atlas), about
22 s. All 37 hashes the renderer dumped on the scripted route to dng101b1
are among them.

## Upscaled packs (tools/remaster)

`tools/remaster` (Python; see its README) builds 4x replacement PNGs from an
export: per texture it fills colours under alpha 0, pads the axes the
manifest's `tiles` / `uv_edges` name cyclically, cuts atlas cells out one by
one, upscales RGB and alpha separately (a super-resolution model per texture
class, Lanczos, or an edge reconstruction for lettering), thresholds
cut-outs at the material's alpha reference and checks the colours against
the source. Its contact sheets and `game_compare.py` (hidden runs with and
without the pack at the same frames) are the means to judge a pack. Packs
stay presentation only: `work/dref/checks/det.sh` gives the same fingerprint
and audio digests with `DW4_TEXTURE_PACK` set as without.
`make_dds_pack.py` turns such PNGs into a pack of format 2 (BC7 / RGBA8 DDS
files with their mip chains made by texconv, and the manifest), checking
every file's header and its first level decoded back against the PNG.
`build_pack.py` builds the full run's pack: one DDS per texture whose level
0 is the 4x picture (Ultra) and level 1 the 2x picture of the full run
(High), the rest of the chain made by the same footprint-clamped Lanczos3
filter (premultiplied, cyclic on tiling axes, cut-outs binary with the
coverage of level 0, no sRGB conversion), texconv only compressing (BC7;
RGBA8 where BC7 would lose too much), every level read back and compared.

## Reproduce the checks

Build the project, then run synthetic tests with no original data:

```powershell
.\build\Release\dw4_textures_tests.exe
```

The 34 synthetic checks exercise palette indexing, row and palette stride,
straight alpha, BC1 endpoints and selector order, both interpolation modes,
partial blocks, trailing data, truncation and allocation limits. These small
generated inputs are the fixtures used by CI.

Run the separate read-only corpus check with your own extracted files:

```powershell
.\build\Release\dw4_textures_tests.exe --archives 'E:\path\Digimon World 4 - Extracted\files'
```

Decode a sample and optionally write a local PPM preview:

```powershell
.\build\Release\dw4_textures_tests.exe 'E:\path\files\area.afs' 621 .\work\agumon-card.ppm
```

For the viewer, select `area.afs`, entry 621, texture index 0. This list index
is separate from the serialized global reference number shown in diagnostics.
Original archives, extracted image data, and generated previews remain outside
Git. Corpus checks are local integration evidence and are not required by CI.

The pack loader and the Graphics options (CTest `graphics_*`, one test per
part of `dw4_graphics_tests`: `manifest`, `dds`, `codec`, `mips`, `choice`,
`loader`, `stream`, `budget`, `options`) need no game data; with it
(`DW4_TEST_DATA`) `graphics_stream` also scans an original document and
reads a pack texture of it ahead:

```powershell
.\build\Release\dw4_graphics_tests.exe stream 'E:\path\Digimon World 4 - Extracted'
```

The texture pack export test (CTest `texpack`) checks the shared hash on
synthetic C8 and CMPR images, the exported names as replacements and the
output folder rules. Given the game data (CMake `DW4_TEST_DATA`, or as its
argument) it also compares the export's hashes of original documents with
the renderer's path (the engine's texture bindings, decoded and hashed) and
with hashes the renderer dumped:

```powershell
.\build\Release\dw4_texpack_tests.exe 'E:\path\Digimon World 4 - Extracted'
```
