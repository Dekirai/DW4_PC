# Canonical HITLANDMODEL geometry owners

`ModelHitLandGeometryInfo` represents the actual `beHitLandModelInfo` category
used by the original common collision traversal. It is distinct from
`ModelHitLandInfo`, which represents the HITLANDDELIVER recipient. Both belong
to the single `ActorSceneResource::models()` registry; separately loaded resources
remain separate source occurrences even when paths and reference numbers match.

## Recovered source fields

Evidence is the locally supplied USA GameCube DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Metadata registration `802D48FC`
declares `beHitLandModelInfo` size `0x2C`; `802D4998` installs its four fields
from the tables at `804D1A8C`, `804D1A9C` and `804D1AAC`.

| Serialized slot | Native offset | Original field | Owner field |
| --- | --- | --- | --- |
| 7 | `+20` | `_switchgraph` | `switch_graph` |
| 8 | `+24` | `_checkSwitch` | `check_switch` |
| 9 | `+1C` | `_nameList` | `names` |
| 10 | `+28` | `_kind` | `kind` |

Inherited name, active byte, data list and RAM list retain their authored
values. The kind remains a signed 32-bit field with all bits intact; it is
not narrowed to the preview decoder's exact floor/wall categories. Static
categories author `igNamedObjectList` names, while the original dynamic `g_box`
category authors an empty `igStringObjList`. Null list fields, repeated entries
and source-qualified physical aliases remain distinct from missing fields.
The public constructor provides decoded-field storage, not a complete reflected
factory or original manager registration.

## Shared scene nodes and reached reads

Both switch references retain the same canonical `ActorSceneNode` allocations
returned by `node()` and `mutable_node()`. Their child lists, physical backing,
bounds and matrices are the existing actual scene owners. The loader does not
copy collision geometry into `MeshData`, publish a dynamic matrix update, change
switch selection or inspect geometry attributes and triangles.

An `igSwitch` now retains its actual slot-8 mask owner. `ActorSceneBitMask`
represents signed list count/capacity, unsigned bit count and nullable physical
word backing. Different mask objects may share the same canonical backing,
including through separately numbered serialized memory references. A write
through that backing is visible to every alias. Replacing one switch's mask
reference preserves the captured old owner and other switch fields.

Original `80300CA0` and `80300DC4` read the selected word directly. Accordingly,
`backing_word(index)` checks only the reached physical addressability; it does
not validate count, capacity or declared bit count. A malformed logical shape
or unselected later word is not consumed early. `ActorSceneNodeList::backing_node`
likewise reads one physical slot without requiring it to lie in the logical
prefix. The dynamic branch can therefore read nested slot zero after logical
clear, as the original does. These accessors preserve nullable addressed entries;
the caller determines whether the original next dereference requires a node.

Dynamic collision children are actual `beCopyTransform` nodes. Metadata
registration `802E0684` declares size `0x70`; `802E0724` and table
`804D2934..804D293C` identify slot 12, `_srcTransform`, at native `+6C`.
`source_transform_owner()` retains that exact node. Its matrix remains separate
from the destination matrix until the traversal executes `80300CD4..80300CE0`.
Neither loading nor replacing the source reference performs this copy.

A node distinguishes an unavailable mask/source field from an authored null
reference. Checked setters preserve nulls and reject strong ownership cycles
through both child capacity slots and source-transform links. Weak incoming
source links allow existing ancestor/depth checks to include affected paths
without retaining either node after the last real owner is released. Structural
source decoding, physical allocation budgets and cycle checks remain asset-load
boundaries; the helpers do not reproduce unsafe guest pointer behavior.

## Original-data and synthetic validation

The focused suite passes 37 synthetic and 56 original-inclusive checks. Its
shared synthetic fixture covers signed malformed counts, independent list and
backing owners, null/repeated children and names, physical reference aliases,
mask replacement, source matrix identity, source/child cycles and lifetime after
releasing the resource facade. It also checks explicit serialized null fields
and rejects a partial physical mask word.

The original-data profile reads `ndmw.afs` entry 179:

| Geometry info | Kind | Inner switch | Children | Established owner relationship |
| --- | --- | --- | --- | --- |
| 1768 | 1 | 1770 | 12 | Floor nodes; outer switch 1766 remains disabled |
| 192 | 2 | 132 | 3 | Door-wall nodes; outer switch 130 remains disabled |
| 1953 | 2 | 1985 | 12 | Wall nodes; outer switch 89 remains disabled |
| 24723 | 1026 | 24718 | 56 | Dynamic copies; first node 21503 retains source node 24992 |

The static inner masks select the actual geometry while their outer masks remain
off. No outer-wrapper mask is substituted for the inner query selection. The
dynamic source and nested bound remain canonical, mutable owners. These checks
establish data ownership and reached-field access, not triangle intersection,
CHECKWALL/CHECKLAND responses, reflected fresh allocation, or a complete game
frame. See [ACTOR_SCENE.md](ACTOR_SCENE.md),
[MODEL_HIT_LAND_SCENE.md](MODEL_HIT_LAND_SCENE.md) and
[WALKING_COLLISION_RESEARCH.md](WALKING_COLLISION_RESEARCH.md).
