# Canonical HITLANDDELIVER owners and registration

`ModelHitLandInfo` and `ModelHitLandRam` represent actual
`beHitLandDelivInfo` / `beHitLandDelivInfoRam` allocations in the canonical
`ActorSceneResource::models()` graph. They retain the same source document and
physical-reference identity rules as [MODEL_SCENE.md](MODEL_SCENE.md).

The RAM's transform is the exact mutable `ActorSceneNode` allocation also
referenced by MODELCTRL ONE. There is no detached transform or second collision
copy of that node. Its collision-history vectors remain separate fields:
changing the node does not implicitly update previous position or land results.

## Recovered fields and defaults

The original USA GameCube DOL used for this work has SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Metadata registration
`802D5DC8` declares the RAM size `0x68`. Its field metadata is initialized by
`802D5E64`; name and offset tables are `804D1C4C` and `804D1C94`.

| Serialized slot | Native offset | Metadata field | Native owner field |
| --- | --- | --- | --- |
| 11 | `+2C` | `_transform` | `node` |
| 12 | `+30` | `_checkFlag` | `check_flags` |
| 13 | `+34` | `_halfWidth` | `half_width` |
| 14 | `+38` | `_upRange` | `up_range` |
| 15 | `+3C` | `_downRange` | `down_range` |
| 16 | `+40` | `_pos` | `position` |
| 17 | `+4C` | `_oldPos` | `previous_position` |
| 18 | `+58` | `_landPos` | `land_position` |
| 19 | `+64` | `_landPosOK` | `land_position_ok` |

The inherited RAM fields are number at slot 2, active byte at slot 5, four
state words at slots 6–9, and nullable named data at slot 10. Parent and manager
backlinks are runtime registration fields. Serialized non-null backlinks
reject; loading does not pretend that an earlier registration occurred.

Metadata sets half width to 3 and both ranges to 4, from constants
`8041FEF4/F8`. It explicitly initializes all three vectors to zero. The
transform is null, check flags are zero, and land-position validity is false
for a fresh reflected allocation. Inherited active defaults to one through
`802E33B8..C0`; info activity likewise defaults to one through its inherited
`igInfo` metadata at `8002EBB0..B8`. The loader always preserves authored
values instead of substituting these constructor defaults.

The info metadata `802D6180` declares size `0x1C`. It inherits nullable name,
active byte, nullable data list and RAM list. The public owner constructors
provide storage for decoded authored fields; they are not the complete
original reflected info factory. In particular, `802E3E40` enables the RAM-list
reference field's auto-create behavior. Reconstructing that full factory and
its initial backing semantics belongs to original manager preparation. The
loader does not insert a guessed list: it preserves an explicit serialized
null reference as null.

Both list and backing owners use
the existing canonical graph cache, preserving nullable slots, repeated RAM
occurrences, physical reference aliases and shared memory backing. Supported
non-named list shapes retain the existing checked count/capacity profile.
Malformed fields fail loading without replacing previously exposed owners.

## Registration and manager identity

`ActionEventRuntime::register_hit_land_scene_ram(scene, info, ram, number)`
represents one actual per-RAM occurrence after the caller's generic manager
info/context preparation. The caller supplies occurrences in the actual list
order. This helper does not run the enclosing manager scheduler, append the
generic accepted-info list, or execute final context-manager registration.

Generic `802F251C` first checks the info's concrete type, then traverses the
RAM list with its captured count. Its per-RAM prefix at `802F25B4..CC` clears
state words in offset order `+18`, `+24`, `+20`, `+1C`, then writes manager,
parent and ordinal. The concrete HITLANDDELIVER `+7C` hook is `802FD9DC`.
That hook calls `80305308`, which reads current parent and RAM number, reads
the parent's current name, applies null-to-empty fallback, and appends a
`HITLANDDELIVER` recipient row.

These operations preserve the RAM's active byte, node and all collision
fields. **Registration never initializes previous position from the node.**
Repeated registration appends another ordered occurrence of the same canonical
RAM. Each row retains its then-current name/ordinal keys, while activity and
manager resolution use the actual live RAM fields.

The runtime owns a distinct `HitLandManager` allocation corresponding to
vtable `804DE298`, separate from its player MODELCTRL manager. Its actual
messenger reference points to the shared native transport. RAM backlinks and
callbacks retain that manager weakly, and its runtime reference is weak, so
neither RAM nor a suspended message creates an ownership cycle. Registration
retains the source scene, preserving the info, RAM and shared node owners.
Separately loaded scenes cannot claim the same source occurrence across
MODELCTRL and HITLANDDELIVER merely because their paths and reference numbers
match.

Each reached message retains its own `ModelHitLandInvocation`, command and
captured distinct manager. A suspended callback resumes that same owner and
messenger rather than following a later RAM-manager replacement. Missing
manager resolution or a detached runtime remains a service boundary. The
concrete manager's `+90` fallback is the verified base `802F2238`, which
returns zero without consuming fields; unknown non-null commands may decline
through that actual fallback. Null commands retain the reached dictionary
failure implemented by the recipient.

The recipient implements `802FDA0C` in `model_hit_land_handler`; it performs
real nested HITLANDMODEL sends. Registration supplies no fabricated
`CHECKWALL` or `CHECKLAND` response. Missing geometry-query services remain
visible at their reached native delivery boundary.

## Original Agumon and validation

The original `_common/p_agumon.igb` exports HITLANDDELIVER info reference 1712,
name `p_agumon`, active one and null data. RAM list reference 2 uses memory
reference 247 and contains physical RAM references 235, 236, 237, 238 in that
order. Their transform references are 6390, 6457, 6524, 6590: the exact four
MODELCTRL ONE nodes in the same document.

The four authored RAMs have number zero, active one, zero inherited state
words, null data, check flags 7, half width 3 and up/down ranges 4. Their three
collision vectors and land-position-valid byte are zero. Registration assigns
ordinals 0–3 and leaves those authored vectors unchanged. It does not assume
that a neutral first frame has equal node and previous X/Z; the real recipient
decides its query path from the actual fields.

`model_hit_land_scene_tests` covers defaults, decoded payloads, nullable and
repeated list entries, canonical aliases, shared node writes, independent
source loads, retained document ownership, malformed-field rollback, real
registration and dispatch, row snapshots, null-name fallback, live activity,
missing-manager retry and manager lifetime. Its optional extracted-root
argument also loads original Agumon, independently reads the serialized RAM
list, checks all four shared node identities, and verifies registration
preserves authored collision history and node matrices.

These owner and registration tests do not claim complete wall/floor geometry,
the enclosing scheduler, or a playable original combat loop. The visible
walking preview remains a separate prepared application path.
