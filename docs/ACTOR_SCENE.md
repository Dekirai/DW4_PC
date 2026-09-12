# Loaded actor scene owners

`ActorSceneResource` retains one immutable IGB document and its explicit source
occurrence. Canonical object references share node, child-list, box, skeleton,
appearance and appearance-list owners. `node()` and `mutable_node()` return the
same physical node; `document_owner()` aliases its retained source document.
Separately constructed resources remain different loads
even when their source labels and numeric references match. Captured objects
retain their source document after the resource facade is released.

`action(info, ram)` requires an exact exported beAction2Info and an actual RAM
member. It preserves the RAM's appearance-list order independently from actor
order and obtains the first skeleton from that info's animation database.
`ActorSceneAction` can only be constructed by the resource reader. The prepared
ACTION query binds this graph to the existing player/runtime identity and returns
the same typed appearance-list/skeleton objects through GETAPPEARANCELIST.
Inactive ACTION returns handled without an object payload. An active missing
scene owner is a resumable service boundary. A published owner cannot be replaced.

Each appearance retains its actual bone-selector-list owner. Aliased lists share
mutations; repeated appearance occurrences stay repeated. Appending the same
selector to multiple appearances preserves its identity. Removal deletes all
pointer-equal entries while preserving other entries. This is the ownership
needed by the original weapon manager's four shared tracking slots.

Node records retain names, raw flags and their actual mutable matrix, bound and
child-list fields. The verified transform field is decoded from slot 8 without
changing float bits. Each physical node owns its matrix separately; retained
placement and control consumers can share that exact allocation. `placement_node()`
supplies the recovered `beModelCtrlNode2` setter; other concrete setters remain
an explicit service boundary.

The bound slot distinguishes an unavailable schema field from an authored null
reference. Non-null `igAABox` references use one canonical endpoint allocation,
also returned by `box()` for other consumers such as MODELCTRL HIT data.
Replacing one node's bound reference preserves every other retained box owner.
The four original Agumon roots have null bounds; their twelve LOD actors share
the authored box at physical reference 5554.

An actual nullable child-list slot retains one `ActorSceneNodeList`. Each list
keeps its own signed logical count and capacity. Distinct lists can share one
canonical physical `ActorSceneNodeStorage`; all nullable capacity slots survive
decoding, including entries beyond count. `values()` checks the logical span.
The outer GENERATE helper instead uses `first_backing_node()` because original
`8030DA7C` through `8030DA94` reads storage slot zero without testing count.
A logical clear therefore preserves a still-addressable first node.

Checked replacement and append write through existing backing when capacity
suffices. Growth replaces only that list's backing, copies the old capacity,
and preserves other lists and callbacks retaining the old allocation. Capacity
starts at four, doubles below 1024, then grows in blocks of 1024, following
`80069128` through `80041970`/`80041660`/`8004155C`. Weak incoming-storage,
aliased-list and attached-parent links check cycles through every strong backing
slot and every affected ancestor path, including another source. These weak
links never keep a scene node alive. The checked host boundary rejects strong
ownership cycles even when their entries lie outside the logical count.

Original geometry/material fields remain available through the owned document.
This reader does not execute inherited node flags, switches or drawing callbacks.
Loaded nonempty serialized bone-selector lists remain an explicit preparation
boundary; the verified original Agumon appearances start empty and receive newly
allocated weapon selectors. Skeleton lookup uses the original first-match ASCII
uppercase comparison, including helper bones with no skinning joint index.

Parsing checks exact types, widths, list capacities/storage, reference aliases,
source qualification and graph cycles. Each actor cache permits at most 65,536
published wrapper owners, including boxes, child lists and physical backing,
262,144 copied capacity/hierarchy entries and
16 MiB of copied text. Node paths have at most 128 edges, including paths through
cached subtrees. Runtime list mutation permits at most 65,536 entries, reachable
nodes and attached parents, and 262,144 reachable edges/retained parent links.
Invalid data fails through `ActorSceneError`; original unchecked
invalid-memory behavior is not reproduced.

The source layout and query/attachment semantics were checked against the locally
supplied USA GameCube executable, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Relevant entries are ACTION
GETAPPEARANCELIST `802F1870`, appearance append/remove `801D38B4`/`801D38D8`, and
skeleton lookup `8020323C` with string comparison `80077768`. No original bytes
are included. See [WEAPON_RESOURCE.md](WEAPON_RESOURCE.md),
[WEAPON_RUNTIME.md](WEAPON_RUNTIME.md) and [BONE_SELECT.md](BONE_SELECT.md).

The scene suite passes 94 synthetic checks and 198 with the supplied original
extraction. The ACTION query suite passes 75 synthetic and 91 original-inclusive
checks. Regressions include shared-list mutations, independent loads, nullable
names, malformed fields/storage, cached-depth and aggregate-storage limits,
foreign delayed preparation, retained query ownership and all four original
Agumon appearance lists. Mutable-field regressions also cover all four original
node matrices bit for bit, twelve shared actor bounds, independent source loads,
direct box-query reuse, null versus unavailable fields, exact placement storage,
captured child storage, aliased memory with different logical counts, growth,
unconditional first-slot reads, rejected strong cycles and ancestor depth overflow.
