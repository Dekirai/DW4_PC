# Original model HIT body cache

Version 0.27 adds the checked local HIT definition and per-target body-box cache
used by ordinary MODELCTRL CHKBULLET. This component does not implement target
scripts, damage, player construction or the outer collision loop. Prepared
registration and target delivery are in [MODEL_COLLISION.md](MODEL_COLLISION.md).
The source HITBOX producer is described in [MODEL_HITBOX.md](MODEL_HITBOX.md).

## Original evidence and graph

Evidence uses the owner's USA GDJEB2 revision-0 DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, and original IGB fields.
Function boundaries use [dw4-gc](https://github.com/ivanno4317/dw4-gc) commit
`794294e17fef0b3ab5eeb15fda9bd57b0dc98a2b`. Native implementation follows the
instructions and serialized metadata; original dumps and data remain local.

| Address | Behavior |
| --- | --- |
| `8030E600..8030E678` | Lazily create and retain work+D4 body cache |
| `802C803C`, `802C8254`, `802C82F0` | Factory, type and field registration for beModelCtrlInfoHitBody |
| `80307EAC..803082A4` | Capture local boxes/distances, timestamp comparison and world-box refresh |
| `800695CC` | First named-list match using exact string bytes |
| `803083CC..803084B0` | MODELCTRL registration followed by authored HIT category flags |
| `80122910` | Exact finite inverted igAABox factory defaults |
| `8012305C` | Six inclusive overlap comparisons used after body-cache lookup |

`decode_model_hit_body(document, model_info_reference, source, occurrence)`
requires an exact exported `beModelCtrlInfo`. It follows its data-list reference
at slot5 and selects the first named `HIT` row, then requires exact
`beModelCtrlInfoDataHit`. A later same-named row never replaces that first match.
Its inherited integer slot3 and byte fields4/5/7 are checked; the AABoxList
reference is slot6. Every ordered exact `igAABox` has vec3 minimum/maximum
fields2/3. Missing HIT, null list, null list entries and malformed references
reject. An explicitly authored count-zero list is valid.

Metadata registration `802C9A0C` identifies slots4/5/7 as
`_isHitPlayerBullet`, `_isHitEnemyBullet` and `_isHitPlayerTalk`. Their getters
retain raw uint8 values. Base registration `803083CC` first registers MODELCTRL,
then tests nonzero bytes in player-bullet, player-talk, enemy-bullet order.
All call `80305308`, using the same info name (null becomes empty) and actual
RAM runtime number. Agumon's bytes 0/1/0 enable only HITENEMYBULLET here;
these player bodies do not supply the HITPLAYERTALK targets required by Set129.
The definition exposes metadata without running registration or the following
controller virtual+9C hook.

The immutable definition retains the explicit source path and loaded occurrence,
MODELCTRL name/info, data/RAM/HIT/box-list references, ordered RAM references,
and every ordered local box reference/value. Finite inverted local bounds remain
valid. Serialized aliases and duplicate box occurrences are retained. The
definition owns its data after the document is destroyed; copies share immutable
values only.

The actual RAM list is read from MODELCTRL slot6 and requires exact
`beBaseInfoRamList`/`beBaseInfoRam` records. `ram_references()` retains serialized
occurrences; `ram_owners()` contains their canonical physical object references.
An owning runtime must share or reject aliased live RAM owners rather than
create independent target state merely because references or ordinals differ.
`ModelHitBodyCache` retains both identities for its explicitly selected instance.
It does not discover a target from its name or validate the external ONE node
chain; the prepared player binding and target owner perform that association.

## Cache lifetime and timestamp behavior

Each cache starts with `_isFirst=true`, `_calcTime=0` and empty parallel lists.
The first `update(matrix, system_time)` captures separate absolute distances
from each local box's original center to minimum and maximum, and creates one
output box per ordered occurrence. Distances are stored separately because
float rounding can make them unequal.

Fresh output minima are `(FLT_MAX, FLT_MAX, FLT_MAX)` and maxima are
`(-FLT_MAX, -FLT_MAX, -FLT_MAX)`, exact component words `7f7fffff` and `ff7fffff`.
These follow AABox field registration `80122910`, which loads the FLT_MAX
literal at `8055DE2C` and negates it for maximum. `_calcTime` uses the registered
eight-byte igLongMetaField zero default. No allocation contents are guessed.

The original compares both stored timestamp words after first-list creation.
A first query at system time 0 therefore returns these default bounds without
refreshing them. Repeated 0 retains the same values. A changed timestamp causes
calculation; returning to 0 after another timestamp also calculates. The timestamp
is the system's uint64 time, not an ACTION node clock or a frame-delta float.
Changing only the matrix at the same timestamp keeps the prior output boxes.

Refresh transforms each original local box using the original eight-corner
AABox routine, computes the transformed center, then subtracts/adds the captured
local distances. Thus transformed center and final extents have different roles:
rotation and scale do not rotate or scale those final stored distances. The
general transform also has its original inverted-X and homogeneous-W branches.
The numeric helpers and their independent instruction comparisons are documented
in [COLLISION_MATH.md](COLLISION_MATH.md).

The API returns a const reference to the cache's value-owned output vector.
Callers must not retain element references across an update that refreshes it.
Cache copies own independent output and distance vectors while sharing the
immutable definition. Source/instance identity remains attached to each copy.

## Checked native boundary and validation

The decoder accepts the verified little-endian profile. It bounds source/name
strings to 4096 bytes, aggregate serialized names to 1 MiB, each data/export list
to 4096 entries, RAMs to 1024, boxes to 4096 and aggregate list links to 32768.
Exact field types, widths, ranges, storage capacity and references are checked;
base-info/RAM boolean bytes must be 0 or 1, while the three HIT category flags
retain all byte values as tested by the original. Source occurrence must not be the size_t invalid
sentinel. These are explicit prepared-input checks, not extra original effects.

Matrices and decoded box endpoints must be finite. Matrix validation also runs
on cache hits. Computation and allocation failures leave the cache unchanged.
The original stores its changed timestamp before traversing output boxes;
the native checked boundary computes temporary results before publishing all
lists/time together. This avoids partial state from rejected nonfinite host
results and does not claim original failure or heap recovery behavior.

The synthetic suite covers source/physical ownership, ordered aliases, first
HIT lookup, malformed graphs, authored empty lists, exact time-zero words,
both timestamp halves, same-time matrix changes, return-to-zero refresh,
noncommuting transforms, separately rounded distances, finite inverted bounds,
independent target/copy lifetimes and atomic checked failures. Optional original
checks use all four Agumon MODELCTRL RAMs and their actual player bindings:
HIT1997 -> AABoxList6395 -> AABox1316, local min(-2.5,0,-2.5), max(2.5,7,2.5).
These cache-sequence checks complement the independent numeric helper oracle;
they are not a claim that the complete original target handler was executed.

```powershell
.\build\Release\dw4_model_hit_body_tests.exe 'E:\path\Digimon World 4 - Extracted'
```

The original target handler still has its own invulnerability/HP-reference,
control and sequence gates, source record query, target Hit script/context,
orientation effects and message result ownership. Source BHit follows recipient
delivery separately. Full player overrides, status callbacks and scheduling
remain required before this cache establishes original combat.
