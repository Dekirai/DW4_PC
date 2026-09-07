# Native stored-geometry decoding

The `meshes` module reads original `igGeometryAttr1_5` objects and reconstructs
their stored positions and triangle indices. The native viewer can display
the resulting geometry in its local coordinate space. This includes the
Agumon geometry in `_common/p_agumon.igb`; it does not implement the character's
rig, animation, scene transforms, or gameplay. A separate, bounded material
resolver can associate a stored base image for the textured preview; see
[MATERIALS.md](MATERIALS.md).

The implementation is independent C++20 with no graphics-library dependency.
Positions are copied from the original little-endian float stream without
changing their values. The renderer may center and scale a display copy for
inspection; those viewing operations are separate from asset decoding.

## Verified corpus

On 2026-09-08, the native decoder processed every `igGeometryAttr1_5` object
found in the original `GDJEB2`, disc 0, revision 0 archives. Counts include duplicated geometry in
different IGB files and are not a count of unique models.

| Archive | IGB files | Unindexed triangle lists | Indexed triangle strips | Geometry decoded | Output triangles | Failures/unsupported |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `area.afs` | 1,379 | 144,872 | 8,024 | 152,896 | 872,195 | 0 |
| `ndmw.afs` | 894 | 10,704 | 34,468 | 45,172 | 2,921,234 | 0 |
| Total | 2,273 | 155,576 | 42,492 | 198,068 | 3,793,429 | 0 |

The decoder checked every position for finiteness and every index against
its vertex count. Triangle totals exclude strip triangles whose indices
repeat. Distinct indices with identical or collinear positions can still
produce geometrically degenerate triangles and are retained.

## Reference chain and storage

Geometry slot 4 references an `igVertexArray1_1`. The vertex array's slot 2
references an 80-byte memory record containing twenty little-endian 32-bit
global reference numbers. `0xffffffff` denotes an absent stream. Every
populated element must select a memory record, including elements the current
decoder does not interpret. Stream element 0 contains positions.

Vertex-array slot 3 gives the vertex count. The position memory must contain
exactly `vertex_count * 12` bytes, interpreted as successive little-endian
IEEE 754 float triples. The module does not use the vertex-format bitmask to
guess offsets or reinterpret other streams. Vertex-array slot 4 is required
to be zero, as observed throughout this corpus. Slot 5 is not blanket-rejected:
some valid original arrays have the value 8.

Geometry slots 8 and 10 are also required to be zero. Their broader semantics
remain unverified. The nested stream references are checked by the mesh reader;
they are ordinary memory bytes, so validating the document's direct typed
references alone would not validate them.

## Optional UV coordinates

`decode_igb_mesh(document, geometry_reference)` produces positions, indices,
and bounds, leaving `MeshData::texcoords` empty. Call
`decode_igb_texcoords(document, geometry_reference)` separately to read stream
element 11 as successive little-endian IEEE 754 float pairs. Its memory must
contain exactly `vertex_count * 8` bytes, and both components must be finite.
The existing vertex limit bounds this allocation to 8 MiB.

The helper preserves the original U and V values, including negative values
and coordinates above one. It does not clamp, flip, rescale, or derive UVs from
positions. A caller attaches the returned vector only after choosing a verified
material. Missing, incorrectly sized, or non-finite optional UVs produce a
separate diagnostic while position-only decoding stays available. Invalid
nested memory references still invalidate the shared vertex descriptor.

Across the same 2,273 original IGB files, stream 11 provided valid UVs for
46,988 geometries: 8,024 in `area.afs` and 38,964 in `ndmw.afs`. The other
151,080 geometries had an absent stream; no present stream failed the byte
count or finite-value checks. Coordinates outside `[0, 1]` occur in 3,825
geometries, making preservation of sampler wrap behavior necessary.

## Topology evidence

The original USA DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, contains the engine's enumeration
name/value tables. Read-only inspection establishes:

| Fact | Original DOL file offset |
| --- | --- |
| `TRIANGLES` has value 3 | Name pointer `0x48c0a8`, value `0x48c0ac` |
| `TRIANGLE_STRIP` has value 4 | Name pointer `0x48c0b0`, value `0x48c0b4` |
| `INDEX_SIZE_16_BIT` has value 0 | Index-size enumeration table at `0x48c840` |

These are format facts recovered from the user's local binary. No table bytes,
original vertex buffers, or generated disassembly are included in the source.
Further investigation details are recorded in [MESH_RESEARCH.md](MESH_RESEARCH.md).

For topology 3, geometry slot 5 and slot 13 must be null. Slot 7 is the
triangle count, and the vertex count must equal three times that count.
Output indices are the consecutive vertex numbers in their original order.

For topology 4, geometry slot 5 references an `igIndexArray`. Its slot 2
references the index memory, slot 3 is the index count, slot 4 must be format
0, and slot 5 must be zero. The memory must contain exactly two bytes per
index, decoded as little-endian unsigned 16-bit values.

Geometry slot 13 references an `igPrimLengthArray1_1`. Its slot 2 references
the lengths memory, slot 3 is the group count, and slot 4 must be 32. The
memory must contain exactly four bytes per group. Geometry slot 7 equals
the group count, each length is at least three, and their sum must consume
the index buffer exactly. There are no restart markers in the observed corpus;
the decoder uses only these explicit group boundaries.

Each group expands into successive triples. The first two indices are swapped
on odd-numbered triangles, and parity resets at each group boundary. Repeated
indices are omitted without resetting parity. Independent geometric checking
of each of the two investigated Agumon resources found that all 2,600
nondegenerate strip triangles agreed with the stored normal direction; a triangle-fan
interpretation produced many opposite-facing triangles. That comparison
supports the recovered enum mapping and winding, rather than assigning a
topology only because a small sample looked plausible.

## Reproducible original samples

`ndmw.afs`, entry 179, geometry reference 3 is a small unindexed example with
24 vertices and eight triangles. Its stored bounds are `(203, 40, -385)` to
`(237, 140, -375)`. It is useful for inspecting a simple non-planar surface.
The same IGB contains 430 supported geometries after indexed strips are enabled.

`ndmw.afs`, entry 850 resolves through the resource catalog to
`_common/p_agumon.igb`. Its mesh-list order is:

| List index | Global geometry reference | Vertices | Output triangles | Observed scene association |
| --- | ---: | ---: | ---: | --- |
| 0 | 262 | 570 | 634 | `LowAppearance` |
| 1 | 413 | 61 | 102 | `KageAppearance` / shadow |
| 2 | 5665 | 1,371 | 1,966 | `HighAppearance` |

The high-detail geometry's stored bounds are approximately
`(-6.62556, -0.0533592, -2.43946)` to `(6.62556, 8.68473, 3.29281)`.
These names describe associations found during graph research. The core
mesh listing labels objects as `Geometry <reference>`; it does not yet
traverse the scene graph to choose or combine appearances automatically.

This resource includes skinning-related memory and objects. The preview is
therefore described as **stored geometry**, not as a verified bind pose,
world-space model, animated character, or final in-game appearance.

## Safety and remaining boundaries

The decoder limits a mesh to 1,048,576 vertices and 1,048,576 output triangles.
The source strip-index limit is 4,194,304; expansion is checked before allocating
the output index buffer. Bounds are computed from the finite original position
values. Source sizes and count arithmetic use explicit widths and checked
comparisons before reading or allocating.

Unsupported topology, missing or wrongly typed references, nonzero unverified
offset/state fields, unexpected table or stream sizes, inconsistent counts,
out-of-range indices, invalid group lengths, non-finite coordinates, and
allocation-limit violations produce a diagnostic. Listing validates descriptors
and ranges; selecting a mesh performs the coordinate decoding and finite check.

The optional stream-11 UV helper and the separate material resolver support
a narrow stored base-image preview. Normals, additional UV sets, colors,
complete material execution, scene transforms, skinning, animations, and
collision remain separate work. No handedness conversion,
automatic mirroring, cosmetic pose adjustment, or coordinate reinterpretation
is applied. A recognizable native geometry preview is evidence of the asset
layout; comparison with an original rendered scene is still required before
claiming the game renderer or a playable model has been reconstructed.

## Run the tests and viewer

Run the 128 synthetic checks without original game files:

```powershell
.\build\Release\dw4_meshes_tests.exe
```

The generated fixtures cover valid IGB reference chains, float decoding and
bounds, strip winding and group boundaries, connector degenerates, invalid
references and types, malformed sizes and counts, non-finite positions, and
vertex/index/expanded-triangle allocation limits. UV regressions cover exact
float pairs, preservation of values outside `[0, 1]`, absent/wrong-size streams,
non-finite components, and independence from plain geometry decoding. Checks remain active in
Release builds.

Run the separate read-only corpus check:

```powershell
.\build\Release\dw4_meshes_tests.exe --archives 'E:\path\Digimon World 4 - Extracted\files'
```

Inspect the Agumon sample's decoded counts and bounds:

```powershell
.\build\Release\dw4_meshes_tests.exe 'E:\path\files\ndmw.afs' 850
```

In the native viewer, load `ndmw.afs`, entry 850, and select mesh index 2.
The application displays the archive/entry identity and global geometry
reference separately. Builds and local preview screenshots remain ignored
by Git, and corpus checks are optional local integration evidence rather
than an asset dependency of CI.
