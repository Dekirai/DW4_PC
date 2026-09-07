# Bounded mesh-layout observations

These notes record read-only investigation of the local USA files on 2026-09-08
using the native `IgbDocument` API and the verified original DOL. They distinguish
recovered storage facts from pending scene/animation interpretation. The mesh
consumer implements the bounded position/topology profiles described below;
this does not establish a scene graph or playable map. No original vertex/index
buffers or generated disassembly are reproduced here.

## Observed reference chain

An `igGeometryAttr1_5` object's slot 4 references an `igVertexArray1_1`. The latter's
slot 2 references an 80-byte memory record in the inspected samples. That record
contains 20 little-endian 32-bit serialized reference indices. A value of
`0xffffffff` is absent; populated entries refer to **memory records**, not objects.
The memory records then contain the actual attribute bytes.

Do not pass those nested stream indices to `document.object()`. They must be
validated and resolved with `document.memory()`. The document parser validates
direct typed object/memory fields; interpreting nested words inside an attribute
table belongs to the mesh consumer and needs its own checks.

## Reproducible sample: `area.afs`, entry 3

This document has 217 `igGeometryAttr1_5` instances. The inspected geometries use
at most six vertices and look like planar interface geometry. They are useful
small fixtures for local investigation but do not establish a 3D-map loader.

- Geometry reference 14, slot 4 → vertex array 3772.
- Vertex array 3772, slot 2 → stream-table memory 3773.
- Stream-table element 0 → memory 5760, containing 72 bytes.
- Vertex-array slot 3 is 6; 72 bytes fit six triples of 32-bit floats.
- Geometry slots 6 and 7 are 3 and 2 respectively; slot 5 is null.

A separate vertex array 3740 resolves through table 3741 to memory 5786, again
72 bytes for six vertices. Little-endian float decoding produces a planar
rectangle. Six vertices and a value of two in geometry slot 7 agree with the
triangle-count interpretation. The subsequent DOL and corpus checks below
confirm this interpretation beyond this small sample.

## Larger sample: `ndmw.afs`, entry 179

This document has 430 geometry instances and 430 vertex-array instances. Of the
geometry instances, 329 have a non-null slot 5 reference.

| Populated stream-table element | Bytes per vertex in this document | Arrays observed |
| --- | ---: | ---: |
| 0 | 12 | 430 |
| 1 | 12 | 80 |
| 2 | 4 | 1 |
| 11 | 8 | 381 |

Geometry reference 3 points to vertex array 206. Its slot 3 is 24, its stream table
is memory reference 0, and table element 0 resolves to memory 11135 with 288 bytes.
The 24 little-endian float triples form non-planar geometry. Geometry slot 6 is 3,
slot 7 is 8, and slot 5 is null. The `24 = 8 × 3` relationship is consistent with
unindexed triangles.

An indexed example has this chain:

| Record | Observed fields |
| --- | --- |
| Geometry 219 | Slot 4 → vertex array 21; slot 5 → index array 136; slot 6 = 4; slot 7 = 1; slot 13 → primitive-length array 45 |
| Vertex array 21 | Slot 2 → stream-table memory 22; slot 3 = 4; slot 6 = 65539 |
| Stream-table memory 22 | Element 0 → memory 17817; element 1 → memory 15845; element 11 → memory 34738 |
| Memory 17817 | 48 bytes; four float triples consistent with positions |
| Memory 15845 | 48 bytes; four repeated unit-length float triples perpendicular to the position surface |
| Memory 34738 | 32 bytes; four float pairs in the unit interval |
| `igIndexArray` 136 | Slot 2 → memory 46; slot 3 = 4; slot 4 = 0 |
| Memory 46 | 8 bytes fitting four little-endian 16-bit indices, each within the four-vertex range |
| `igPrimLengthArray1_1` 45 | Slot 2 → memory 2017; slot 3 = 1; slot 4 = 32 |
| Memory 2017 | One 32-bit value equal to four |

The position/normal interpretation has geometric support: the second stream's
unit vector is perpendicular to two independent edges of the surface encoded by
the first stream. The 8-byte-per-vertex stream is consistent with UV coordinates.
The DOL's vertex-component enumeration also identifies components 0, 1 and 2 as
position, normal and color. That is supporting evidence for the first three
streams, not proof of a color byte order or of the legacy UV stream assignment.

## Topology names verified in the original executable

The USA `main.dol` with SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877` contains a big-endian name-pointer/value
table starting at file offset `0x48c090`, mapped to guest address `0x8048f090`.
This directly associates these engine names with their integer values:

| Value | Engine enum name | Name/value pair file offset |
| ---: | --- | --- |
| 0 | `IG_GFX_DRAW_POINTS` | `0x48c090` |
| 1 | `IG_GFX_DRAW_LINES` | `0x48c098` |
| 2 | `IG_GFX_DRAW_LINE_STRIP` | `0x48c0a0` |
| 3 | `IG_GFX_DRAW_TRIANGLES` | `0x48c0a8` |
| 4 | `IG_GFX_DRAW_TRIANGLE_STRIP` | `0x48c0b0` |
| 5 | `IG_GFX_DRAW_TRIANGLE_FAN` | `0x48c0b8` |

A separate name array at guest address `0x80484ef0`, followed by values at
`0x80484f08`, corroborates the same mapping. Index format
`IG_GFX_INDEX_SIZE_16_BIT = 0` is present at file offset `0x48c840`.
These observations use original static data, not a guessed ordering of another
graphics API's enums. No instructions or game functions were copied.

## Complete local corpus checks

A separate native scratch probe checked every raw IGB entry in both archives,
loading and releasing one document at a time. The results count instances,
including repeated content across documents; they are not unique mesh counts.

| Archive | IGB documents | Geometry instances | Enum 3, unindexed | Enum 4, indexed |
| --- | ---: | ---: | ---: | ---: |
| `area.afs` | 1,379 | 152,896 | 144,872 | 8,024 |
| `ndmw.afs` | 894 | 45,172 | 10,704 | 34,468 |
| Total | 2,273 | 198,068 | 155,576 | 42,492 |

There were zero parse failures or inconsistencies in these checked relationships:

- Every vertex-stream table contains exactly 80 bytes; stream 0 contains exactly
  `vertex_count * 12` bytes. Populated attribute sizes divide the vertex count.
- Every enum-3 geometry has no index or primitive-length object, and its vertex
  count is exactly three times geometry slot 7.
- Every enum-4 geometry has an index array and a primitive-length array. The
  index format is 0, its payload is exactly `index_count * 2` bytes, and each
  little-endian index is below the vertex count.
- Each primitive-length array uses slot 4 value 32 and contains exactly
  `length_count * 4` bytes of little-endian lengths. All lengths are at least
  three, their sum equals the index count, and their count equals geometry slot 7.
- Geometry slots 8 and 10, index-array slot 5, and vertex-array slot 4 are zero.
  Vertex-array slot 5 is zero except for 333 arrays with value 8 and format 7.

The largest observed vertex/index counts are 2,514/2,620 in `area.afs` and
29,895/30,691 in `ndmw.afs`. The largest strip lengths are 119 and 666. No restart
marker was observed: the largest actual index in the corpus is 29,894.

For strips, restart winding parity for each length group. Triangle `t` uses
successive indices `t, t+1, t+2`; swap the first two on odd triangles. Omit output
for repeated-index connector degenerates, but retain parity from the original
triangle position, including skipped triangles. Never derive parity from the
emitted triangle count. Distinct-index triangles can still have zero geometric
area; the initial decoder does not silently remove them.

As an independent geometric check, each Agumon sample described below has 2,600
nondegenerate normal-bearing strip triangles whose cross product agrees with the
sum of its stored vertex normals, with none reversed or nearly perpendicular.
There are 844 additional geometric degenerates. Treating the same groups as fans
instead produces 1,566 reversed triangles. This supports the strip winding as
well as the enum-name evidence. Normal agreement is a diagnostic, not a general
validator: the wider corpus has some inconsistent or unsuitable stored normals.

## Legacy vertex format limitations

Observed `igVertexArray1_1` slot-6 values and populated stream elements are:

| Format value | Populated elements | Observed attribute storage |
| --- | --- | --- |
| `0x1` | 0 | Position triples |
| `0x3` | 0, 1 | Position and normal triples |
| `0x7` | 0, 1, 2 | Position, normal and a 4-byte attribute |
| `0x10001` | 0, 11 | Position and float-pair attribute |
| `0x10003` | 0, 1, 11 | Position, normal and float-pair attribute |
| `0x10111` | 0, 11 | Same stream layout, additional skin-related memories |
| `0x10113`, `0x10223` | 0, 1, 11 | Same stream layout, additional format flags |

These correlations support low bits for position/normal/color presence and a
texture-coordinate field at bit 16 in this legacy serialized struct. The extra
fields and their packing are still pending recovery. Do not substitute the
executable's `IG_GFX_VERTEX_FIELD_*` enum masks: for example, its named normal
field is 4 at file offset `0x48c6e8`, whereas the legacy normal-bearing format
above is 3. The executable contains multiple generations of vertex APIs. Stream
table indices, component enum values, and packed format bits are distinct things.

## Actual Agumon resources and scene links

The native resource catalog resolves `_common/m_agumon.igb` to `ndmw.afs` entry
834 and `_common/p_agumon.igb` to entry 850. Both documents contain three indexed
strip geometries with the same vertex/index counts. Their global references
differ. The player document, entry 850, gives useful named targets:

| Appearance | Geometry-attribute reference | Vertex count | Index count | Strip count |
| --- | ---: | ---: | ---: | ---: |
| `HighAppearance` | 5665 | 1,371 | 2,844 | 116 |
| `LowAppearance` | 262 | 570 | 1,000 | 84 |
| `KageAppearance` | 413 | 61 | 154 | 6 |

For the high appearance the validated object chain is:

1. `igSkin` 404, named `HighAppearance`, slot 3 → `igBlendMatrixSelect` 5481.
2. That node's slot 7 → `igNodeList` 5482, containing `igGeometry` 409.
3. Geometry 409 is named `P_agumon_Skin0Shape1`; slot 8 → `igAttrList` 411.
4. That attribute list contains `igGeometryAttr1_5` 5665.

Each appearance has a skin node, a blend-matrix selector, and non-null vertex
slots 7 and 8. In this player document, slot 7 contains one little-endian float
equal to 1 per vertex; slot 8 contains one byte per vertex, ranging up to 23.
Each corresponding blend-matrix selector has an `igIntList` of 24 elements at
slot 10. This is consistent with one full-weight matrix influence per vertex;
the complete palette-to-skeleton mapping has not been established.

The document also contains one `igSkeleton`, 25 `igSkeletonBoneInfo` instances,
12 `igActor` instances, and 2,050 `igEnbayaTransformSource` instances. Those counts
must not be confused with a recovered animation runtime or a proven bone count.

The bounds computed from each raw position array agree with its `igSkin`'s
stored `igAABox`. The high and low appearances have normal streams; the Kage
appearance has none. This establishes a useful complete stored mesh preview,
while the correct bind-pose interpretation, matrix palette, animation update,
world placement, materials and original appearance still need verification.

## Next recovery boundary

Keep the position/topology decoder separate from scene traversal. Version 0.4
adds the verified base-image link and UV stream for the named high Agumon
appearance; see [MATERIAL_RESEARCH.md](MATERIAL_RESEARCH.md). Next recover the
skin palette and verify an original pose and placement before claiming scene
rendering.
Little-endian evidence from this corpus does not establish every future buffer
profile. Other topology/index formats and nonzero offset fields should remain
explicitly unsupported until checked.

Synthetic fixtures remain independent of the original data. The local corpus
decoder integration is reproducible with:

```powershell
.\build\Release\dw4_meshes_tests.exe --archives "E:\Games\Emulator\Dolphin-x64\Games\Digimon World 4 - Extracted\files"
.\build\Release\dw4_meshes_tests.exe "E:\Games\Emulator\Dolphin-x64\Games\Digimon World 4 - Extracted\files\ndmw.afs" 850
```

The independent topology and DOL probes are in ignored `work/` and are not part
of the public source distribution. Public code uses an independently written
checked C++ implementation, with no Alchemy SDK or third-party extractor code.
