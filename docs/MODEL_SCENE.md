# Mutable MODELCTRL scene owners

`ActorSceneResource::models()` owns one lazily created `ModelSceneObjects`
registry for that actual loaded source. The registry decodes mutable model info,
RAM, named data, ONE, HIT, SEQ, sequence data, commands, timer and object-list owners. It uses the resource's
canonical physical references; repeated references and distinct reference-table
aliases resolve to the same allocation. Two separate loads remain separate even
when their path, source occurrence and numeric references are equal.

Captured objects retain an aliasing owner of the original document. They do not
retain the resource facade or registry. Destroying the resource leaves retained
RAM, ONE, work, list, timer and node owners usable. Runtime RAM parent and manager
backlinks are weak and are set only by the registration caller. The original
serialization does not contain those registration links.

## Represented fields

| Original owner | Mutable fields |
| --- | --- |
| `beModelCtrlInfo` | Nullable name and kind, raw active byte, data-list and ordered RAM-list references |
| `beBaseInfoRam` | Signed RAM number, raw active byte, four generic words, named data-list reference, registered parent/manager backlinks |
| `beBaseInfoData` and retained derived records | Nullable name, inherited signed integer; exact source/type identity remains available to the command-specific consumer |
| `beModelCtrlInfoRamData` | Nullable actual node and canonical player-work references |
| `beModelCtrlInfoDataHit` | Raw player-bullet, enemy-bullet and player-talk bytes; nullable `igAABoxList` reference |
| `beBaseInfoRamTimer` | Its actual `MotionTimer` payload, inherited name and integer |
| `beModelCtrlInfoDataSeq` | Actual nullable ordered sequence-list reference |
| `beModelCtrlData` | Nullable name and actual ordered command-list reference |
| Named MODELCTRL commands | Nullable name and raw mutable supported numeric/string/HITBOX parameter fields |
| Object lists | Independent signed count/capacity and a shared backing allocation, including null/repeated object slots |

ONE nodes come from `ActorSceneResource::mutable_node()`. HIT box entries use
the same physical box allocations as `ActorSceneResource::box()` and node bound
fields. The graph does not create another matrix or box for each RAM. The timer
is a named record in the RAM's current data list; it is not a fixed field copied
into player work.

`register_model_ram` publishes only the supplied RAM number and actual
parent/manager backlinks. Registration order, manager lists, activation,
context queries and collision categories belong to its original caller. Merely
loading a four-entry RAM list does not assign numbers zero through three.
The manager backlink has the explicit `ModelSceneManager` type. Its nullable
`ModelSceneContext` owns the current world timestamp, permitting the original
RAM-to-manager-to-context clock read without capturing a stale timestamp.
These owners do not themselves implement the original context attachment or
outer scheduler.

SEQ lookup reads the captured information owner's current data list, selects
the first name match, and then checks the concrete SEQ type. Requested sequence
lookup follows the same order. Its `ModelSceneSequence` owns the actual nullable
command-list reference; commands and list backing are canonical graph owners,
including repeated references and memory aliases. Replacing the parent's SEQ
or sequence list leaves a suspended call's retained selected data alive.
`model_scene_command` forms a detached request from one reached mutable command;
it is not an immutable whole-program cache. LABEL lookup can inspect the raw
owner's type/name without decoding unrelated numeric fields. Null names and raw
nonfinite parameters remain representable; their command-specific consumers
determine which values are safe to consume.

## Ordered lookup and allocation

`find_model_named` reads the signed count once and returns the first object with
an exactly equal nonnull name. It does not read capacity. A nonpositive count
returns no match without reading backing; a reached null entry or null list is
a checked original dereference boundary. Null slots remain representable for
other list consumers and for positions after an already successful match.
Empty names and null names remain different. `find_model_data`, `find_model_one` and `find_model_timer`
perform their requested type checks only after that first name match. A
wrong-type record named `ONE` shadows a later correct ONE; it is a checked
boundary, not a reason to skip forward.

`ensure_model_data` allocates only an empty `beBaseInfoDataList` when the RAM's
slot is null. It never fabricates ONE, node or player work. Fresh lookups read
the live data-list reference. Previously captured lists and ONE objects retain
their existing allocations after a slot replacement. Assigning a fresh work
to ONE likewise leaves captured old-generation views on the old work.

Object-list append follows `80069128`, `80041970`, `80041660` and `8004155C`:
an available capacity slot is written in the existing backing; full capacity
allocates a replacement backing, copies the old capacity and updates only the
growing list's reference. The capacity starts at at least four, doubles below
1024, and subsequently grows by 1024. Other lists sharing the old backing keep
that allocation. Newly represented unused capacity slots are null host slots;
no observation of original allocator padding or uninitialized bytes is claimed.

Timer start at `8031DE44` ensures data, finds the first matching timer, or
allocates/names/appends a fresh timer, then calls the recovered timer restart
arithmetic. Reusing a timer preserves its scale and previous-time field. The
elapsed-frame query at `8031E4C4` also ensures data but requires an existing named
timer; it does not invent a timer or zero result when the original would
dereference a missing record. Work replacement leaves these RAM-owned timers
intact. Replacing the RAM's data-list slot affects subsequent timer lookups.

## Evidence and checked boundaries

The exact named search is `800695CC`, including the reached-null and signed-count
behavior. ONE lookup `8030F8CC` first ensures RAM data and then calls that search.

Source: the owner's USA GameCube extraction, DOL SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original metadata, caller inspection
and the already recovered timer arithmetic establish the represented fields.
Related scalar behavior is described in [MOTION.md](MOTION.md); canonical work
storage in [MODEL_PLAYER_WORK.md](MODEL_PLAYER_WORK.md). No original bytes or
disassembly are included in the repository.

The supplied Agumon info `1386` has RAM owners `1725`, `6417`, `6420`, `6622`,
data lists `6435`, `51`, `52`, `53`, ONE owners `1724`, `121`, `1594`, `4783`, and
nodes `6390`, `6457`, `6524`, `6590`. Authored RAM numbers are all zero before
registration, work references are null, and node X positions remain approximately
`-26.50`, `-13.25`, `0`, `13.25`.

`model_scene_tests` covers physical object/memory aliases, distinct loads,
retention after resource destruction, slot and work replacement, ordered and
wrong-type lookup, actual timer reuse, raw HIT flags, shared boxes, original
list-growth ownership, and malformed counts, capacities, field slots and owning
cycles. Optional local-data coverage loads all four original Agumon RAM/ONE/node
owners and verifies timer survival across work replacement.
It also checks mutable SEQ selection and command fields, independent named-list
count/capacity/backing, and every command occurrence in all 28 original Agumon
sequences. Serialized named-list counts are retained without eagerly validating
unreached backing; concrete lookup/runner consumers enforce their own checks.

Loading is bounded to 65,536 model objects/backing owners, 262,144 represented
backing entries, 16 MiB of copied text and 128 object/memory reference edges.
Malformed loads do not publish partial model caches. Node/box loading uses the
separate existing scene-owner budgets. Serialized nonnull work and serialized
nonnull runtime registration backlinks require their missing original services;
they are not replaced with successful placeholders. Unknown derived named-data
payloads remain source-qualified records for their concrete consumers. This
owner graph alone does not complete GENERATE, collision or the gameplay loop.
