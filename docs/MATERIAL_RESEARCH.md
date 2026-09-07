# Stored-material evidence for Agumon

This read-only investigation uses the local USA data and the native IGB document,
mesh and texture readers. It establishes an explicit stored texture binding for
the original Agumon mesh. It does not reproduce the game's lighting, animation,
world state, GX texture environment or final rendered colors.

The original DOL used for enum and field-name evidence has SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Guest addresses below are evidence
locations, not host pointers. No executable instructions, original texture
buffers, UV arrays or extracted assets are included in this repository.

## Verified high-appearance binding

The resource catalog resolves `_common/p_agumon.igb` to `ndmw.afs`, entry 850.
Its high appearance is geometry-attribute reference 5665. Reference numbers are
local to this IGB document; matching a reference from another entry is invalid.

| Link | Observed relationship |
| --- | --- |
| `igSkin` 404 | Name `HighAppearance`; slot 3 references blend node 5481 |
| `igBlendMatrixSelect` 5481 | Slot 7 references `igNodeList` 5482, containing geometry node 409 |
| `igGeometry` 409 | Name `P_agumon_Skin0Shape1`; slot 8 references `igAttrList` 411 |
| `igAttrList` 411 | Contains only geometry attribute 5665 |
| `igBlendMatrixSelect` 5481 | Slot 8 references inherited `igAttrList` 408 |
| `igAttrList` 408 | Contains texture-bind attribute 414 and texture-state attribute 397 |
| `igTextureBindAttr` 414 | Slot 4 references texture attribute 405; slot 5 selects unit 0 |
| `igTextureStateAttr` 397 | Slot 4 is true; slot 5 selects unit 0 |
| `igTextureAttr` 405 | Slot 12 references image 5609 |
| `igImage` 5609 | Name `P_agumon01.dds`; 256 by 256 pixels; supported BC1 storage |

The image is selected through these object references. Its filename is diagnostic
text, not a lookup heuristic. Geometry node 409 has no binding of its own; the
nearest complete stored binding is on its parent blend node.

The same document's low appearance, geometry attribute 262, resolves through
node 5495, scope 5484, attribute list 5490, bind 5509 and texture 5491 to image
5597. Its Kage appearance, geometry attribute 413, has a different state profile
and is not automatically assigned either body texture.

## Scope and inherited state

Attribute list 408 contains exactly these five objects, in stored order:

| Reference | Type | Observed state |
| ---: | --- | --- |
| 401 | `igVertexBlendStateAttr` | Enabled |
| 396 | `igShadeModelAttr` | Enum 1 |
| 397 | `igTextureStateAttr` | Enabled, unit 0 |
| 414 | `igTextureBindAttr` | Texture 405, unit 0 |
| 395 | `igMaterialAttr` | Material values described below |

Neither this list nor the child geometry list contains a texture-coordinate
source, texture matrix, explicit texture-function, alpha-test or blending
attribute. That is a fact about this local skin path; it does not establish
the renderer's global defaults or a particular actor instance's inherited world
state. The skin is also referenced by several `igAppearance`/`igActor` instances.
Those instance graphs remain outside the stored base-image preview.

`igAttrSet` slot 8 is `_attributes`; slot 9 is `_trigger`. Their original field
name table is at guest address `0x80560880`, with runtime offsets at
`0x80560890`. The geometry's trigger is true and the blend node's is false.
The execution semantics of `_trigger` have not been recovered; it must not be
relabeled as an inheritance or override flag.

A bounded resolver can therefore require one geometry owner and an unambiguous
group-parent path to the nearest complete local unit-0 binding. Split bindings,
disabled texturing, multiple matching owners/parents, duplicate binds, cycles
and additional texture-coordinate state require explicit diagnostics. A complete
local binding establishes a stored image association, not the final material of
every actor instance. `igTexGen*` classes also affect texture-coordinate state
and must not slip through a guard that only recognizes names starting with
`igTexture`.

## Texture fields and sampler enums

The original `igTextureAttr` field-name array starts at guest `0x804785e4`
(DOL file offset `0x4755e4`), and its runtime-offset array starts at `0x8047865c`.
The ordering agrees with the serialized field slots and the omitted runtime
texture identifier.

| Slot | Original field name | Value in texture 405 |
| ---: | --- | --- |
| 4 | `_bColor` | 0; full semantics not assigned here |
| 5 | `_magFilter` | 1 |
| 6 | `_minFilter` | 1 |
| 7 | `_wrapS` | 1 |
| 8 | `_wrapT` | 1 |
| 9 | `_texId` | Runtime field omitted from this serialized object |
| 10 | `_mipmapMode` | 0 |
| 11 | `_source` | 0 |
| 12 | `_image` | Image reference 5609 |
| 13 | `_paging` | False |
| 14 | `_tu` | Null; additional referenced state not interpreted |
| 15 | `_imageCount` | 1 |
| 16 | `_imageMipMaps` | Reference 406, an empty `igImageMipMapList` |

Original enum name/value pairs identify:

| Enum | Value | DOL file offset of name/value pair |
| --- | ---: | --- |
| `IG_GFX_TEXTURE_FILTER_NEAREST` | 0 | `0x48c2f8` |
| `IG_GFX_TEXTURE_FILTER_LINEAR` | 1 | `0x48c300` |
| `IG_GFX_TEXTURE_WRAP_CLAMP` | 0 | `0x48c328` |
| `IG_GFX_TEXTURE_WRAP_REPEAT` | 1 | `0x48c330` |
| `IG_GFX_TEXTURE_SOURCE_IMAGE` | 0 | `0x48c348` |

Thus linear magnification/minification and repeat wrapping on both axes are
directly supported for this sample. Other enum names alone do not establish
all backend details: for example, the exact meaning of the engine's clamp mode
must be checked before selecting a particular OpenGL clamp variant.

Material attribute 395 has shininess 0; diffuse and ambient values are white
with alpha 1; emission and specular RGB are zero with alpha 1; flags are zero.
The field-name array at guest `0x804791b8` identifies slots 4 through 9 in that
order. These values do not authorize inventing a lighting model for the preview.

## UV storage and image validation

Geometry 5665 references vertex array 5621. Its stream-table memory is reference
5500; element 11 references memory 6434. That memory has 10,968 bytes, exactly
1,371 pairs of little-endian 32-bit floats. Every component is finite. Only one
float-pair attribute stream is populated in this vertex array.

Observed U values range from approximately 0.00390625 to 0.998081, and V values
from -0.000822544 to 0.992188. The small negative V values are valid input to the
observed repeat sampler. Do not clamp stored coordinates into the unit interval,
flip V, or alter the atlas merely to match a visual expectation. The absence of
a local coordinate-source or matrix attribute supports this simple stored-UV
profile; recovery of general texture-unit defaults remains separate work.

The bound image was decoded by `decode_igb_texture` and inspected visually. It is
a recognizable yellow/orange Agumon atlas containing green-eye, claw and mouth
details. Its entire 262,144-byte RGBA output matches an independent Pillow DDS
decode of the original BC1 payload wrapped in a scratch DDS header. Every one
of its 65,536 texels has alpha 255. These observations concern this bound image;
they do not generalize from the previously investigated card texture.

An atlas contains islands with different orientations, so looking at its bitmap
alone cannot establish the game's UV origin. The initial textured preview uses
unchanged UV values and unchanged decoded pixel rows. A recognizable alignment
of eye, mouth and claw islands on the mesh is a useful native visual check.
Independent inspection of the native 1920 by 1080 screenshot on 2026-09-08
confirmed a recognizable yellow/orange Agumon in the stored pose, with the
visible green eye, mouth/teeth and white claws aligned to their mesh features.
The viewer identified geometry 5665 and bound image 5609. No color adjustment
or UV flip was applied. This validates that sample's visible atlas placement;
pixel-exact agreement with an original game rendering is still pending.

Local image previews, the independent decoder comparison and DOL inspection
probes remain in ignored `work/`. The checked C++ readers are independently
written and do not incorporate an Alchemy SDK or external extractor source.

## Complete local material and UV census

An isolated native scratch executable loaded each of the 2,273 original AFS IGB
documents once, called `resolve_igb_material` for every geometry attribute, and
called `decode_igb_texcoords` independently for the same geometry. Each supported
bound image was decoded once per document/reference pair. Repeated content in
different documents is counted again; these are instance counts, not unique
assets. There were no document failures.

| Archive | Geometry instances | Supported material bindings | Valid UV streams | Material and UV both valid | Distinct bound images within documents |
| --- | ---: | ---: | ---: | ---: | ---: |
| `area.afs` | 152,896 | 2,687 | 8,024 | 2,687 | 694 |
| `ndmw.afs` | 45,172 | 521 | 38,964 | 521 | 480 |
| Total | 198,068 | 3,208 | 46,988 | 3,208 | 1,174 |

Every one of those 1,174 image decodes succeeded. Material resolution stayed
deliberately narrow; the unsupported results were:

| Diagnostic category | `area.afs` | `ndmw.afs` |
| --- | ---: | ---: |
| Incomplete or ambiguous texture binding/state within one scope | 145,001 | 16,360 |
| Multiple possible scene parents | 3,221 | 19,432 |
| Additional `igTextureMatrixStateAttr` | 1,987 | 4,156 |
| Additional `igTextureCoordSourceAttr` | 0 | 111 |
| No complete binding on the unique ancestor path | 0 | 4,592 |

Those diagnostics do not say the original assets are broken. They identify
material contexts outside this implementation's accepted local profile.

The only UV diagnostic was absent or empty stream 11: 144,872 instances in
`area.afs` and 6,208 in `ndmw.afs`. No populated UV stream failed its exact byte
count or finite-component checks. Across 5,132,732 decoded pairs, 48,360 pairs
were outside the unit square, affecting 3,825 geometries. Observed ranges were
U `[0,12]`, V `[0,1]` in `area.afs`, and approximately U `[-1.99994,14.0188]`,
V `[-3.3125,9.84375]` in `ndmw.afs`. These finite repeat coordinates are retained;
they are not classified as malformed ranges.

The two archive passes ran concurrently and completed in approximately 150 and
173 seconds on the local host. The census executable compiled current material
and mesh sources into ignored scratch objects while linking the existing native
document/asset readers; it did not modify the application's build directory.
After the final `igTexGen*` and attribute-member guards were added, a separate
complete document scan found zero `igTexGen*` objects and zero non-`igAttr`
members in `igAttrList` objects, so those guards do not change these counts.

The exact local probe commands were:

```powershell
.\work\material_census.exe "E:\Games\Emulator\Dolphin-x64\Games\Digimon World 4 - Extracted\files\area.afs"
.\work\material_census.exe "E:\Games\Emulator\Dolphin-x64\Games\Digimon World 4 - Extracted\files\ndmw.afs"
.\work\material_census.exe "E:\Games\Emulator\Dolphin-x64\Games\Digimon World 4 - Extracted\files\ndmw.afs" 850
```

These probes and their original-data reports are local research artifacts under
ignored `work/`, not distributed test fixtures or a prerequisite for CI.
Portable synthetic tests exercise the public reader contracts without game data.
