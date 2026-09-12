# Canonical geometry and mapped array owners

`ActorSceneResource` now connects an actual `igAttrSet` node's nullable attribute
list and raw enable byte to its `ModelSceneObjects` registry. `igGeometryAttr1_5`,
`igVertexArray1_1`, `igIndexArray` and `igPrimLengthArray1_1` have canonical mutable
owners in that same registry. Repeated list slots, object-reference aliases and
physical memory aliases retain their actual identities. These owners are not
the detached `MeshData` used by the walking preview.

The attribute list retains signed count, capacity and its complete nullable
pointer allocation independently. The loader does not require a valid active
prefix for `igAttrList`; the real visitor checks reached slots. Array counts,
format bits, primitive mode, vertex base and serialized state fields are raw
values. Loading does not expand strips, check triangle indices, reject blended
formats or test coordinate finiteness. Those are reached consumer operations.

Byte allocations are shared across compatible vertex, index, primitive-length
and blend-data references. Vertex arrays can independently share one physical
stream-pointer table. Replacing the table or one stream pointer preserves old
mapped pointers and other retained allocations. Incompatible reuse between
scalar bytes, object-pointer lists and memory-pointer tables is a checked load
boundary. Resource limits bound actual allocations, not authored logical counts.
Source reference and document metadata remain available after the resource
facade is released; the graph does not retain that facade.

## Recovered concrete mapping

The USA DOL provides the following concrete GameCube operations. Generated
fixtures and local original data are tested without redistributing game bytes.

| Original function | Represented operation |
| --- | --- |
| `800E326C` | Return the current inline vertex-format owner; original serialized slot 6 is a four-byte `igStructMetaField` |
| `801008C0` | If mapping length is zero, first publish `count - first` with 32-bit wrapping; position selector zero then reads stream slot zero, adds `first * 12`, publishes stride 12 and trailing state zero |
| `80100C1C` | Vertex unmap sets concrete array field `+30` to 1; it does not read mapping or mode |
| `800DFFE8` | Index map returns current backing plus wrapped `first * 2`; count, format, mode and length are not checked |
| `800E0008` | Concrete index unmap is an actual empty function |
| `800EADB4` | Primitive length reads a reached physical 32-bit element; count and element-width fields are not consulted |
| `800C692C` | Return the current nullable primitive-length owner |

`ActorGeometryPointer` retains the physical byte owner and a guest-width byte
offset. Reads apply wrapped 32-bit arithmetic and check only the reached bytes.
The original little-endian serialized values are kept in canonical host byte
storage and decoded explicitly. Raw float reads preserve nonfinite payloads for
the consuming arithmetic routine's checked boundary. Null base plus a nonzero
offset remains an invalid nonzero address until dereferenced; it is not silently
converted into an absent index stream.

Mapping callbacks preserve the original virtual-call seams and permit live
array replacement during a map or unmap. They are synchronous operations. The
concrete position mapper reaches the original GPU synchronization function
`800FC1FC` when array flags bit 2 is set. It requires its explicit service after
publishing the map fields. Other vertex semantics also require their reached
native mapping service. No successful synchronization or unsupported semantic
mapping is fabricated.

## Runtime type and remaining boundaries

Node runtime dispatch index is a distinct nullable field. A fresh decoded node
has no registered native table index; typed visitor registration supplies it.
It is never initialized from the serialized metadata type number. Node and
geometry `is_a` use the retained document's actual parent-type relationships.

The alternate `igGeometryAttr2` family is distinguished from the primary
geometry. Its `igVertexArray2` semantic graph still needs native preparation.
The primitive enumerator reaches an explicit alternate preparation service:
a true result represents its semantic-6/5 early rejection; a false result reaches
the original null primary-owner virtual call at `801F69FC`. This service does
not establish a decoded alternate vertex graph or a complete alternate mapper.

These are decoded authored owners and the represented mapping operations, not
a complete reflected fresh-object factory or graphics driver. The actual
primitive enumerator and attribute producer are documented separately in
[HITLAND_PRIMITIVES.md](HITLAND_PRIMITIVES.md) and
[HITLAND_GEOMETRY.md](HITLAND_GEOMETRY.md).

## Local validation

`actor_geometry_tests` checks object and memory aliases, independent list and
array counts, live replacement, retained pointer reads, uint32 wrapping, null
and malformed backings, map-prefix preservation before service failures, exact
unmap effects, nullable attributes, runtime type registration and graph lifetime.
Optional original-data checks load all 27 static collision attributes and vertex
arrays in `ndmw.afs` entry 179: 12 floor and 15 wall nodes. They read actual
positions and perform native map/unmap operations. A separate indexed stage
mesh checks its real index and primitive-length allocations. This coverage does
not assert complete CHECKWALL/CHECKLAND responses or the original game loop.
