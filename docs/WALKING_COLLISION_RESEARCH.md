# Original floor geometry for the walking prototype

The native walking prototype reads actual collision triangles from the original
map package. Its ground movement solver is new host code: it does not claim
the original player physics, complete collision manager or collision response.
The original floor geometry and its selection can nevertheless be identified
independently of the visible map meshes.

## Evidence and source boundary

Evidence comes from the locally supplied USA `GDJEB2` revision-0 extraction,
`main.dol` SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, and the original
`ndmw.afs` entry 179. Function boundaries use the reference decompilation's
`config/GDJEB2/symbols.txt`; the runtime code is an independent native reader.
Game bytes, decoded triangle dumps and generated disassembly remain outside
tracked source in ignored `work/` files.

`dng101b1.igb` selects entry 179 as its ordinary map member. The visual `BG02`
export uses scene info 3372 and root 2877. Its collision surfaces are separate
exported `beHitLandModelInfo` objects, not extra rendered batches:

| Info reference | Stored kind | Shape names | Use established here |
| --- | --- | --- | --- |
| 1768 | 1 | `hitPShape101`–`106`, `hitRShape101`–`106` | Floor triangles used by the original land query |
| 192 | 2 | `doorwall01`–`03` | Door-wall geometry in its stored closed state |
| 1953 | 2 | `wallPShape101`–`106`, `wallRShape101`–`106` | Static wall triangles |
| 42 | 4 | `limit_Circle` | A separate collision category; not selected as floor |

The floor decoder requires an explicit exported reference and exact kind 1. It does
not select a model by a name substring or mistake visible ceiling polygons for
walkable floor. Kinds are a bit field in the original query; this initial
floor decoder intentionally accepts only the verified exact value 1. The
separate wall decoder requires exact kind 2.

## Original field and query behavior

The `beHitLandModelInfo` registration at `0x802D48FC` references the native
field table at `0x804D1A70`. Its own fields establish the following mapping:

| Serialized slot | Native offset | Original name | Entry-179 info 1768 |
| --- | --- | --- | --- |
| 7 | `0x20` | `_switchgraph` | Outer `igSwitch` 1766 |
| 8 | `0x24` | `_checkSwitch` | Inner `igSwitch` 1770 |
| 9 | `0x1C` | `_nameList` | `igNamedObjectList` 1752 |
| 10 | `0x28` | `_kind` | 1 |

The command names and enum values at `0x804D1ABC` include `CHECKLAND` with
value 2. Dispatch at `0x802FE4EC`–`0x802FE57C` sends that command to
`0x80300444`. The land query sets its kind mask to 1 at `0x8030063C` and
`0x8030068C`, then calls the common geometry query at `0x803009D0`.

That common query filters each info's native `+0x28` kind with the query mask
at `0x80300C3C`–`0x80300C44`. It then reads the info's `+0x24` check switch
directly at `0x80300C70`. Geometry children and stored mask bits are selected
from that switch, including the loop at `0x80300DC4` onward. This path does
not consult the outer `_switchgraph` mask.

The `CHECKWALL` handler at `0x802FE7F0` also invokes this common geometry query
(for example at `0x802FEE00`). Its query mask excludes the floor bit at
`0x802FEB38`–`0x802FEB44`. The native reader keeps floor and wall inputs separate
and does not reproduce the original wall handler's response calculations.

This distinction matters in the original sample: the outer switch's one stored
bit is **off**, while all 12 inner child bits are **on**. Floor queries still
use the selected inner geometry. The native decoder preserves both source masks
and follows the same inner selection; it does not simulate an activation command
or alter the source document.

The original land query has additional query extents, multiple candidates,
result selection and messenger outputs. The prototype's bounded ground solver
is separate host behavior. Identifying its input triangles does not establish
equivalence to that full query or the original movement physics.

## Verified static graph and triangles

Info 1768's outer switch 1766 has one child, the same inner switch 1770 named
by `_checkSwitch`. The inner switch has 12 direct `igGeometry` children through
node list 1724. There are no transforms on these paths. Each geometry has one
`igGeometryAttr1_5`, an empty child list, flags `0x10` and enabled attributes.
Each geometry uses the already decoded unindexed triangle-list profile.

| Shape | Geometry | Geometry attribute | Vertices | Triangles |
| --- | --- | --- | --- | --- |
| `hitPShape101` | 1823 | 1819 | 30 | 10 |
| `hitPShape102` | 1811 | 1822 | 12 | 4 |
| `hitPShape103` | 1799 | 1813 | 30 | 10 |
| `hitPShape104` | 1743 | 1821 | 24 | 8 |
| `hitPShape105` | 1745 | 1728 | 6 | 2 |
| `hitPShape106` | 1806 | 1802 | 18 | 6 |
| `hitRShape101` | 1810 | 1750 | 30 | 10 |
| `hitRShape102` | 1808 | 1749 | 30 | 10 |
| `hitRShape103` | 1815 | 1747 | 42 | 14 |
| `hitRShape104` | 1800 | 1817 | 18 | 6 |
| `hitRShape105` | 1748 | 1741 | 24 | 8 |
| `hitRShape106` | 1742 | 1820 | 42 | 14 |

The 12 shapes contain **306 vertices and 102 triangles**. Their combined bounds
are `(-350, 14, -450)` through `(350, 40, 460)`. Original stored positions are
already in the same world coordinates as the player entry points; no visual-map
normalization, unit conversion or guessed matrix is applied.

An independent barycentric calculation on the decoded original triangles gives:

- Entry position `(-300, 40, 431)` lies on geometry 1810, triangle 6, at height 40.
- Entry position `(345, 16, -440)` lies on geometry 1806, triangle 5, at height 16.
  This is a sloped triangle, so flattening the map to height 40 is incorrect.
- `(-300, 400)` and `(-300, 370)` remain on the entry corridor at height 40.
- `(-260, 340)` and `(-240, 340)` have no supporting triangle. A ground-only
  prototype must reject these positions rather than use the map's bounding box
  as a walkable rectangle.

## Static walls and closed doors

The two category-2 exports have the same direct, untransformed graph profile.
Their inner switch masks select all stored children:

| Info | Outer switch | Check switch | Shapes | Vertices | Triangles |
| --- | --- | --- | --- | --- | --- |
| 192 | 130 | 132 | 3 | 72 | 24 |
| 1953 | 89 | 1985 | 12 | 612 | 204 |

Together they provide 15 meshes, 684 vertices and 228 wall triangles. All 228
triangles are vertical in world space. All 102 floor triangles have an upward
winding. Wall heights generally span 40 through 140; the sloped second entry
corridor lowers the wall bottoms to height 14. Wall heights and floor heights
remain original values, without normalizing them into camera coordinates.

For example, `doorwall01` has bounds `(-197, 40, -25)` through
`(-163, 140, -15)`. Its stored mask is enabled, so the prototype retains it as a
closed barrier. Opening doors, named `ONHIT`/`OFFHIT` changes and progression
triggers require separate runtime work. The prototype can explore the starting
area without pretending these barriers have already been unlocked.

## Native API and validation

`decode_walking_collision(document, info_reference)` returns owned `MeshData`
instances with original world positions. It requires the exact static graph
above, verifies both wrapper and check-switch relationships, and retains the
ordered inner mask selection. It rejects transformed, dynamic, wall and unknown
profiles explicitly. `decode_walking_walls(document, info_reference)` uses the
same checked static graph for exact category 2. It returns an empty vector when
the inner switch disables every wall; an empty floor selection rejects because
it cannot support a walking session. No native controller, save state or actor
is constructed.

Limits are 4,096 export slots, 256 floor shapes and 1,048,576 combined vertices
and triangles. List capacities and mask storage are checked before traversal;
repeated geometry occurrences count toward the combined budgets. The mesh
reader additionally checks topology, stream byte ranges and finite coordinates.

The asset-free `walking_collision_tests` suite covers valid source selection,
unchanged coordinates/source bytes, inner mask selection, duplicate occurrences,
unexported and wrong-kind infos, malformed references and field types, unknown
flags, cycles/transforms, invalid masks, truncated/nonfinite geometry and combined
resource limits. An optional local-data argument also verifies the original
12-shape/306-vertex/102-triangle floor and both wall exports, including the
15-shape/684-vertex/228-triangle combined wall total. Original-data checks are separate from
asset-free CI; the test fixture contains only generated synthetic geometry.

The original wall response, dynamic doors, object collision, original query tolerances,
falling, jumping and the original player's state machine remain future work.
