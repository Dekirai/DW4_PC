# Loaded actor scene owners

`ActorSceneResource` retains one immutable IGB document and its explicit source
occurrence. Canonical object references share node, skeleton, appearance and
appearance-list owners. Separately constructed resources remain different loads
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

Node records retain names, raw flags and their ordered child DAG, with original
geometry/material fields available through the owned document. This reader
does not execute inherited node flags, switches, transforms or drawing callbacks.
Loaded nonempty serialized bone-selector lists remain an explicit preparation
boundary; the verified original Agumon appearances start empty and receive newly
allocated weapon selectors. Skeleton lookup uses the original first-match ASCII
uppercase comparison, including helper bones with no skinning joint index.

Parsing checks exact types, widths, list capacities/storage, reference aliases,
source qualification and graph cycles. Each source permits at most 65,536
published wrapper owners, 262,144 copied relationships/hierarchy entries and
16 MiB of copied text. Node paths have at most 128 edges, including paths through
cached subtrees. Invalid data fails through `ActorSceneError`; original unchecked
invalid-memory behavior is not reproduced.

The source layout and query/attachment semantics were checked against the locally
supplied USA GameCube executable, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Relevant entries are ACTION
GETAPPEARANCELIST `802F1870`, appearance append/remove `801D38B4`/`801D38D8`, and
skeleton lookup `8020323C` with string comparison `80077768`. No original bytes
are included. See [WEAPON_RESOURCE.md](WEAPON_RESOURCE.md),
[WEAPON_RUNTIME.md](WEAPON_RUNTIME.md) and [BONE_SELECT.md](BONE_SELECT.md).

The scene suite passes 43 synthetic checks and 63 with the supplied original
extraction. The ACTION query suite passes 75 synthetic and 91 original-inclusive
checks. Regressions include shared-list mutations, independent loads, nullable
names, malformed fields/storage, cached-depth and aggregate-storage limits,
foreign delayed preparation, retained query ownership and all four original
Agumon appearance lists.
