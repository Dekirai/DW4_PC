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
