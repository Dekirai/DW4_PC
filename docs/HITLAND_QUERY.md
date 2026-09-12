# Actual HITLAND query storage

`HitLandQuery` represents `beHitLandModelOrder`, whose original vtable begins
at `804DE34C`. Its virtual `+5C` entry is `8030111C` and `+60` is `80300E90`.
The implementation uses actual reusable `igLineSegment` and
`beHitLandResultData` owners, independent signed query/list counts, and shared
physical backing. It does not flatten a query into detached triangle hits.

Evidence is the supplied USA DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Local disassembly and original
instruction drivers remain in ignored `work/` files. No game data is included
in the native module or its generated test fixtures.

## Query and pool ownership

| Query field | Original offset | Native member |
| --- | --- | --- |
| Maximum / minimum vectors | `+08` / `+14` | `maximum` / `minimum` |
| Current info name / child index | `+20` / `+24` | `current_name` / `current_index` |
| Kind and query flags | `+28` | `flags` |
| Active segment count / retained list | `+2C` / `+30` | `segment_count` / `segments` |
| Active candidate count / retained list | `+34` / `+38` | `candidate_count` / `candidates` |

Reset `803011FC` writes only flags, segment count and candidate count to zero,
in that order. Names, indices, bounds, list counts, backing and existing objects
survive. The next append reuses the physical object at the active query index.
It creates and appends exactly one object only when that query index is at
least the list's signed count. It does not fill a malformed gap in advance.

The concrete factories are `8011F908` for a `0x24`-byte `igLineSegment` and
`802D5A08` for a `0x58`-byte `beHitLandResultData`. Their native equivalents
create those represented objects with the recovered field defaults. Query and
candidate names initially contain the nonnull empty string established by
metadata `802D5760` and `802D5C2C`; explicit null names remain distinguishable.
This represents the concrete allocations and fields, not the original engine's
allocator registries, reference-count bit encoding or out-of-memory machinery.

## Segment construction

The setter `801251D0` copies the first endpoint to origin at `+08`, computes
the endpoint displacement into `+14`, and stores its length at `+20`.
Scalar loads and stores are interleaved, preserving aliases into an already
retained segment rather than snapshotting both endpoints first.

Length uses three separately rounded squares, the X-plus-Y sum, then Z-plus-
that-sum. The original positive path uses `frsqrte` and three double Newton
steps before rounding the square root to float. Direction is normalized only
when length is strictly greater than binary32 `0x350637BD` (approximately
`0.0000005`). Zero, underflowed and shorter segments retain their displacement.
There is no invented rejection of a zero-length segment.

Arithmetic follows the startup NI=1, nearest-even profile: loads preserve tiny
values and signed zeros; tiny arithmetic results flush to signed zero. Only
reached arithmetic operands/results require finiteness. A numeric boundary
preserves earlier origin/direction writes and any pool allocation, while the
active query count advances only after the setter returns.

## Candidate publication

`HitLandVisitorPayload` represents the visitor's actual `0x48`-byte argument:

| Payload offset | Native member |
| --- | --- |
| `+00` | `kind` |
| `+04` | `scratch`, the actual traversal scratch owner |
| `+08` | `node_path`, the actual visited path owner |
| `+0C` | `matrix`, an alias of the reached matrix-stack element |
| `+10` | `segment`, the actual polymorphic segment owner |
| `+14` | `distance` |
| `+18` / `+24` / `+30` | `position` / `local_position` / `normal` |
| `+3C` / `+40` / `+44` | `attribute_index` / `group_index` / `triangle_index` |

The last field is the primitive stream element offset, not an ordinal triangle
number. The visitor publishes the group counter and that stream offset at
`801F6CB4..801F6CC0`.

Append first ensures a reusable candidate exists. It then transforms the
incoming normal **in place** with the incoming matrix using `8012D434`:
rounded Y products, fused X terms, then fused Z terms. This is a rotation-only
copy; it does not add translation, normalize the normal, or use inverse transpose.
Unused matrix components may be nonfinite without causing an eager fault.

After that transformation, append resolves the current physical candidate and
copies the complete payload to its `+10..+54` fields. The pointer fields retain
their actual shared owners, including the physical matrix alias. It freshly
resolves the candidate for the current query name copy to `+08`, and resolves
again for the current child-index copy to `+0C`. Only then does it increment the
active candidate count. A null matrix therefore faults after a needed pool
allocation; an invalid name faults after the normal and candidate payload have
already changed. C strings are bounded to 65,535 bytes without embedded NULs.

## Physical list operations and limits

Query pools and traversal scratch use `ModelSceneList` owners with separate
signed count, capacity and shared physical storage. The HITLAND-specific helpers
do not call that class's eager whole-list validation. A physical read ignores
both count and capacity, checks only its reached physical slot, and preserves
null and duplicate entries.

Append follows `80069128`: when count is below capacity, it publishes count plus
one before the reached pointer store. Growth follows `80041970` / `80041660` /
`8004155C`: minimum capacity four, doubling below 1,024 and adding 1,024 above
it, replacement allocation, old-capacity copy, capacity publication, count
publication, then pointer store. Retained aliases of the old backing remain
on that old allocation. Growth does not shrink the old copy to logical count.
The host profile bounds a backing to 262,144 pointer slots.

Fresh spare pointer words have no invented value. They contain an internal
unknown-slot marker, and a reached read faults until an actual store establishes
the slot. A raw store over a still-owned spare pointer is also an explicit
boundary: the original overwrites without releasing it, and ordinary
`shared_ptr` replacement must not introduce an earlier destruction.

Clear represents the `800691E8` resize-to-zero operation. It captures the old
count, releases reached owners, clears the captured prefix, and publishes zero
count last, retaining capacity and backing aliases. Concrete final
`HitLandSegment` owners have no destructor callbacks; other owners must remain
externally retained when released. A reached foreign final-release destructor
is an explicit boundary because shared-pointer slots cannot expose the original
temporarily dangling pointer bytes between its release and zero passes. This is
not a general destructor-reentry emulation. A negative old count follows the
original count-zero publication before rejecting its negative memset address.

## Verification and scope

The focused suite covers reset/reuse identity, all retained fields and pointer
aliases, the segment threshold, endpoint aliases, normal transformation,
allocation and mutation prefixes, late name failure, physical spare words,
negative counts, malformed old-capacity copies and the explicit storage limits.

An independent local driver executed `801251D0` and `8012D434` directly from the
original DOL for 10,826 cases: 7,307 segment and 3,519 normal transformations.
Its 359,388 instructions produced zero differences, including partial scalar
writes in 4,850 checked numeric-boundary cases. All 2,840 reached positive
Newton square-root results agreed with the native rounded square root. The
driver uses independent integer rounding and an ignored research-only seed
model; it is not part of the distributed runtime. This evidence covers the
stated numeric profile, not arbitrary Gekko exception flags or nonfinite math.

The module implements query storage and publication. Producing visitor
intersections, selecting map geometry, reducing wall/floor candidates and
sending complete `CHECKWALL` / `CHECKLAND` responses require their corresponding
native consumers. Pool operations do not manufacture collision results.
