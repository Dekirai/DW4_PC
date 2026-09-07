# Bounded mesh-layout observations

These notes record read-only investigation of the local USA files on 2026-09-08
using the native `IgbDocument` API. They identify the next implementation target;
they do not claim a mesh decoder, renderer, scene graph or playable map exists.
No original vertex/index buffers are reproduced here.

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
rectangle. The agreement between six vertices and a value of two in geometry
slot 7 supports a triangle-count interpretation, but enum names have not yet been
verified against the engine.

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
The 4-byte stream's meaning has not been established.

## Inferences still requiring verification

- Vertex-array slot 3 appears to be the vertex count.
- Stream elements 0, 1 and 11 appear to represent positions, normals and one UV set.
- Geometry enum 3 is consistent with a triangle list. Enum 4 plus an index list and
  primitive-length record may describe a strip or another grouped primitive;
  its exact meaning and winding are not yet proven.
- The index-array enum value 0 is compatible with the observed 16-bit indices.
  Other widths, restart markers and offsets have not been checked.
- Vertex-array slot 6 changes with the populated streams and may encode a format
  description. Its bit assignments are not established.

Little-endian metadata and these little-endian float/index samples do not justify
assuming every future GPU buffer has the same encoding. Transforms, materials,
texture links, skinned vertices, topology variants and coordinate conventions
remain unverified.

## Next concrete implementation gate

Build a checked unindexed position reader for the verified stream-0 profile,
then add a native wireframe preview of entry 179's geometry reference 3. Require
finite coordinates, exact vertex-count/byte-size agreement and validated nested
memory references. Preserve the original values; do not fabricate replacement
geometry. Only after visual comparison should the indexed example be used to
establish enum 4's topology and then add normals, UVs and material links.

Keep synthetic tests independent of the original bytes and record original-data
validation separately. The local scratch probes are intentionally excluded from
the public source tree.
