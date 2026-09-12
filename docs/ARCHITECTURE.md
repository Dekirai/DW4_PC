# Native port architecture

## Implemented boundary

Version 0.43 adds the actual movement-collision consumer and a separately owned
HITLANDDELIVER manager. Its decoded info/RAM records share canonical scene
nodes with MODELCTRL ONEs; registration preserves authored previous-position
fields instead of initializing them from an already moved player. Captured
manager references and per-delivery receiver invocations survive nested native
query suspension without creating a manager/work ownership cycle.

The outer wall/floor consumer retains its original parent/work, resolves only
reached physical response fields and performs real SQ_FALL entry. Landing
updates the freshly selected vertical motion owner with the original mode-5
stores. The five HITLANDDELIVER commands construct real HITLANDMODEL requests,
consume original reply gates and update the shared node/position fields.
Complete HITLANDMODEL graph traversal and query results remain required;
the preview's host collision solver supplies no substitute results. The
remaining WHit/FHit script/context and ride operations are explicit native
service boundaries. See [MODEL_MOVEMENT_COLLISION.md](MODEL_MOVEMENT_COLLISION.md),
[MODEL_HIT_LAND_SCENE.md](MODEL_HIT_LAND_SCENE.md) and [HITLAND_DELIVER.md](HITLAND_DELIVER.md).

Version 0.42 connects `ModelFrameInvocation` to actual player input, raw
post-input control, status update, motion, common/player wait, invulnerability
and delayed subcommand advancement. Each callback captures its own original
owners; a replacement ONE/work does not retarget the outer frame. Direct wait
completion resumes the real command loop without an extra ordinary-entry
reset or player callback. Required position-lock, ride and remaining collision
operations suspend at their reached phases. See [MODEL_FRAME.md](MODEL_FRAME.md).

The manager owns distinct nullable context, system and pad objects. Named
timer helpers use current RAM.manager/context; player input retains the
invoking controller and reads its current system only when reached; delayed
subcommands read current RAM.manager/system separately per entry. These
consumers do not implement device polling, the fifth pad's producer, manager
scheduling or original time advancement. Effect providers cannot report
completion without performing their required operation.

An actual frame has an exclusive continuation lease within its runtime.
Host suspension retains its work, helper state and exact subcommand wrapper.
Recursive/competing frames or abandonment fault unfinished execution instead
of replaying prefixes. Unrelated runtime guest execution rejects while the
owning frame is paused; its own nested calls and later service configuration
remain available. The runtime lease has no frame or work back-reference.

Version 0.41 connects the actual MODELCTRL lifecycle through ordinary sequence
execution, player `+B8` and the outer GENERATE box tail. `ModelSceneObjects`
owns canonical mutable SEQ, sequence-data, command and list allocations.
First-name lookup precedes concrete type checks; nullable slots, repeated
references, independent counts/capacities and shared physical backing survive
loading. Captured selected data remains alive when the parent's current SEQ or
another reference changes.

`ActionEventRuntime` retains an execution bundle for each captured player-work
generation. One canonical executor owns that work's command state; individual
ordinary calls retain separate invocation frames. Name and selected-data
publication precede later label reads, subcommand cleanup retains its captured
wrapper, and the loop captures selected data and a local cursor. Host service
suspension preserves those captures and completed effects. Direct JUMP completes
its nested entry and player callback before returning without overwriting the
nested cursor. End, LABEL and WCHECK returns still reach player `+B8`.

The player callback freshly captures its own parent/ONE/work, performs the
SQ_WAIT effect send and panel handling, and then resolves work again for status
reconciliation. Sleep/stun timer removal, status-icon cleanup and direct stun
entry retain their own phases and required providers. The GENERATE caller
continues to its final box write only after this callback actually returns.
Original four-slot acceptance and the explicitly prepared startup services are
recorded in [VALIDATION.md](VALIDATION.md).

`ModelSceneManager` owns a nullable current `ModelSceneContext`; RAM manager
backlinks stay weak. Actual named timer helpers resolve the RAM's current
manager and that manager's current timestamp at each original helper site.
This supplies context time without a host clock or a timer copied into work.
Attaching the context is explicit preparation; the original attachment and
outer scheduler are still missing.

`action_dispatch` connects defined ACTION programs and the existing actor-aware
runtime to real messenger rows. It retains source/service owners and pending
delivery, keeps native query priority, and resumes incomplete commands or
broadcasts. Its existing ACTION execution profile samples current context when
accepting a call and retains that timestamp across host retries. It does not
yet reproduce every original ACTION helper's separate clock read or replace
the prepared ACTION program/work representation with a mutable loaded graph.

Checked boundaries remain explicit. One work allocation cannot currently acquire
independent executors through two different RAM owners. Verified direct JUMP
permits nested ordinary frames; ACTION- or Lua-triggered reentry into an active
sequence is rejected. Synchronous read-only ACTION queries are supported, while
recursive ACTION mutation is rejected. Unsupported controller effects, missing
services and unsafe consumed fields cannot become successful no-ops.

These core paths do not replace the preview's provisional input, movement or
camera. Original outer scheduling, remaining collision-query/ride helpers,
complete startup, HUD rendering, combat, transitions, audio, saves and local
multiplayer still require integration. See [MODEL_SCENE.md](MODEL_SCENE.md),
[MODEL_SEQUENCE.md](MODEL_SEQUENCE.md), [MODEL_DISPATCH.md](MODEL_DISPATCH.md),
[MODEL_TIMER.md](MODEL_TIMER.md) and [ACTION_DISPATCH.md](ACTION_DISPATCH.md).

## Earlier implementation milestones

The versioned descriptions below record each earlier milestone and its limits
at that time. Requirements subsequently connected through version 0.43 are described
above; historical mentions of missing ordinary entry, player callbacks or
execution bundles are not the current implementation boundary.

Version 0.40 adds `ModelRamTimerAccess`, retaining one actual RAM and a required
context-clock reader outside player work. Commands, movement and individual
turning axes perform their own original named helper calls. Changed-time frame
queries preserve the captured outer timer while a fresh inner lookup reads its
own clock. Missing timer creation and completed movement prefixes survive later
checked failures. Work-only sequence/control snapshots have no timer authority;
full copies of an actual timer binding are rejected. A recovered positive
invulnerability countdown uses the same endpoint, including the original
unordered comparison branches. At that milestone the actual manager/context
resolver, per-work execution bundle and original scheduling were separate work.
See [MODEL_TIMER.md](MODEL_TIMER.md) and [MODEL_CONTROL.md](MODEL_CONTROL.md).

Version 0.39 adds one canonical mutable model graph per loaded scene resource.
`ActionEventRuntime` registers actual RAM owners and routes their messages through
retained `ModelBaseInvocation` captures. Player `+98` performs fresh work
installation, point query, placement, GETPLAYER and the existing startup. The
outer branch reloads its original ONE's work and performs HP fallback, box and
heading preparation, and optional AI allocation before a fresh direct controller
entry. Full ordinary entry and player `+B8` were still missing at that milestone;
the existing sequence adapters provided the explicit prepared profile.
See [MODEL_SCENE.md](MODEL_SCENE.md), [MODEL_DISPATCH.md](MODEL_DISPATCH.md),
and [PLAYER_CONSTRUCTION.md](PLAYER_CONSTRUCTION.md).

Version 0.38 introduces one noncopyable `ModelPlayerWork` allocation containing
all 68 reflected field locations. Motion/control, sequence/wait, script and
status facades retain aliasing pointers into it. The work object never stores
those retaining facades. Actual node matrices and RAM-owned KEIKATIMER slots
remain separate. The normal built-in factory field profile allocates six
distinct referenced guest objects and applies the recovered defaults. The
existing checked MODELCTRL runtime directly consumes supplied canonical work;
its legacy prepared overload uses an explicit compatible preparation profile.
No second gameplay state is maintained. The complete GENERATE continuation
and native original frame scheduling still remain. See
[MODEL_PLAYER_WORK.md](MODEL_PLAYER_WORK.md).

Version 0.37 replaces independent movement arrays, optional PDMOVE values,
inline subcommand entries and zipped label state with actual referenced list
allocations consumed by the existing algorithms. Movement entries are separate
owners allocated on the first MOVE. PDMOVE separates logical count from backing
storage; original sequence entry clears only count. A subcommand facade retains
the actual wrapper, list, entries and pending pass. Three sequence-label lists
start null and are allocated by SETLABEL; inspection returns detached values.
Sequence selection now stages a selection plan and model values explicitly,
then publishes through existing owners. Mutable RAM/ONE registration, work
replacement and GENERATE remain required.
These list bindings are production prerequisites, not a second disconnected
player-state implementation. See [MODEL_CONTROL.md](MODEL_CONTROL.md),
[MOTION.md](MOTION.md), [MODEL_SUBCOMMANDS.md](MODEL_SUBCOMMANDS.md) and
[MODEL_SEQUENCE.md](MODEL_SEQUENCE.md).

Version 0.36 adds a registered `player_point` provider and retained
`model_placement` invocation. The provider preserves actual group/child owners,
saved-group state and ordered RAM registration. Placement captures its source
matrix and concrete setter separately, preserving the original vector/matrix
precedence and nine-entry Euler update. `ModelMotionPose` retains a separate
node matrix allocation: construction can supply that shared owner, copies
detach candidates, and assignment publishes through the destination's existing
owner. Script work retains the matrix directly without retaining the controller.
This separates node lifetime from per-work turn/control state; complete work
replacement and retained old-generation callbacks remain required. The prepared
spawn integration does not complete GENERATE or change the walking preview's
provisional controller. See [PLAYER_SPAWN.md](PLAYER_SPAWN.md).

Version 0.35 adds `model_player_startup`, `number_control`, `meter_control` and
`model_status`. The startup continuation owns the existing prefix, five ordered
UI sends and one captured map-name owner. Actual NUMBERCTRL RAM/digit/UV owners
and METERCTRL ONE/node/time-list owners receive these messages. MODELCTRL GETHP
appends the shared HP/MHP wrappers. An explicitly attached status work owner
shares the same prepared ONE; a separate virtual+C4 invocation belongs to each
recipient callback and retains nested GETPLAYER delivery. The model keeps a weak
status link to preserve explicit prepared-work lifetime. Unknown
status commands decline before allocation; known unsupported effects retain a
service boundary. This completes the represented post-GETPLAYER caller, not
the preceding constructor or original gameplay scheduler. HUD drawing remains
required. See [MODEL_PLAYER_STARTUP.md](MODEL_PLAYER_STARTUP.md),
[NUMBER_CONTROL.md](NUMBER_CONTROL.md), [METER_CONTROL.md](METER_CONTROL.md)
and [MODEL_STATUS.md](MODEL_STATUS.md).

Version 0.34 adds retained `scene_traversal` common callbacks and a shared
prepared renderer pass. Ordered normal attribute scopes, geometry trigger and
invisibility gates, common transforms and actual selector children preserve
loaded owners through missing services. Each draw retains its own model-view;
one projection and depth/MSAA target serve the complete pass. `weapon_preview`
uses the same registered ACTION runtime for animation, skeleton queries and
body/weapon rendering, under an explicit reset graphics inspection baseline.
The walking preview connects that prepared owner to host movement and the
original map. This does not establish the original locomotion state machine,
game camera, complete materials, combat or full player construction. See
[SCENE_TRAVERSAL.md](SCENE_TRAVERSAL.md),
[PREPARED_RENDERING.md](PREPARED_RENDERING.md) and [WEAPON_PREVIEW.md](WEAPON_PREVIEW.md).

Version 0.33 adds `actor_scene`, `weapon_resource`, `weapon_runtime` and
`bone_select`. Loaded source occurrences own canonical node, appearance-list
and skeleton objects. ACTION returns its actual ordered appearance list and
first database skeleton through typed messenger objects. Prepared PLAYER name
queries and MODELCTRL SETLABEL use retained live RAM fields; labels preserve
null independently from an empty string. The weapon manager shares four tracking
slots across all registered weapon infos, retains nested delivery prefixes and
removes old selectors by identity. The common selector callback retains its
computed model-view attribute through child-service suspension. Four prepared
original starts now deliver common05 and replace it with original two-hand
weapons across twelve appearances. Generic child rendering, the compiled selector
path, later player UI/constructor effects and the original frame scheduler remain
required. See [WEAPON_RUNTIME.md](WEAPON_RUNTIME.md),
[MODEL_PLAYER_QUERIES.md](MODEL_PLAYER_QUERIES.md), [ACTOR_SCENE.md](ACTOR_SCENE.md)
and [BONE_SELECT.md](BONE_SELECT.md).

Version 0.32 adds original deferred packet ownership and explicit manager-update
continuations to `messenger` and `action_event_runtime`. The queue retains live
head ordering, integer deadlines, pause shifts and the original initial pool
budget across service suspension. GAMERAM owns RECALCPLAYER through the existing
stat runtime. `model_player_start` connects a supplied post-GETPLAYER caller state
to HP binding, exact modifier resets, GAMESTART, BASE_ISUSE, a delayed PLAYERARMS
request and RECALCPLAYER. It stops before later UI and constructor services.
Full player creation and original outer scheduling remain required. Prepared
weapon attachment is connected in version 0.33. See [DEFERRED_MESSENGER.md](DEFERRED_MESSENGER.md) and
[MODEL_PLAYER_START.md](MODEL_PLAYER_START.md).

Version 0.31 adds `game_database`, `game_items`, `game_stats`, `game_name` and
`game_start`. GAMERAM retains the selected GAMESTART route and executes its
original species/base/equipment/stat/common/name sequence. Shared shinka and
pcTech caches belong to the manager; each weapon operation uses a fresh retained
database message. Separate helper entries capture their then-current player rows
while common and name publications preserve their original earlier owners.
The shared game-data/messenger object variant carries actual weapon and Unicode
objects without converting opaque name payloads. Four original Agumon data
initializations complete against explicitly registered original databases and a
prepared font. Full MODELCTRL construction, other item providers, skill growth,
startup and gameplay scheduling remain required. See [GAME_START.md](GAME_START.md).

Version 0.30 adds immutable `database` graphs and `database_runtime` ownership
for exact exported beDBManagerInfo/RAM occurrences. All six typed column groups
retain original widths, order and provenance. Shared DATABASE results have
their own messenger object type. Atomic provider/registration batches preserve
physical RAM aliases and executing callback owners. GAMERAM now retains the
P2ESKILLUP source row, wrapped ceiling, first matching technique record and
actual pcTech query before the no-growth comparison. A nested row reset does
not retarget the captured row. The native ItemWaza type represents prepared
techniques; full selected-player population, growth and stat effects remain
required. See [DATABASE.md](DATABASE.md),
[DATABASE_RUNTIME.md](DATABASE_RUNTIME.md) and [GAME_RAM.md](GAME_RAM.md).

Version 0.29 adds `game_data`, `game_ram`, `model_game_data` and
`model_script_messenger`. Generated common/player lists retain their original
named wrapper and row identities. The GAMERAM endpoint uses a separate native
controller identity domain and the existing shared messenger; GETPLAYER returns
the actual retained row. Prepared MODELCTRL work binds its HP/MHP objects and
applies the verified zero-HP refill prefix. The two combat-script wrappers retain
their real sends and current-context arguments. P2PKOUKAMAGIC implements the
unequal-RAM exit and default no-effect branch; named effects and P2ESKILLUP
remain explicit services. Complete player selection, startup and gameplay
scheduling are still required. See [GAME_DATA.md](GAME_DATA.md),
[GAME_RAM.md](GAME_RAM.md) and [MODEL_SCRIPT_MESSENGER.md](MODEL_SCRIPT_MESSENGER.md).

Version 0.28 adds `script_registry`, `model_script_context` and
`model_script_runtime`. Explicit ordered LuaInfo occurrences own their bytes;
prepared ONE work shares HP objects and a live model matrix. Target-Hit dispatch
executes the selected chunks in one retained Lua VM, preserving nested context
overwrites, incomplete native calls and the original conditional HP merge.
`action_event_runtime` exposes qualified script work and reads live HP presence
and the signed GETRECNO value. Complete GAMERAM/player preparation, damage,
status and filesystem services remain required. See
[MODEL_SCRIPT_RUNTIME.md](MODEL_SCRIPT_RUNTIME.md).

Version 0.27 adds immutable `model_hit_body` definitions and independent
timestamp-keyed caches for actual selected RAMs. `model_collision` owns base
CHKBULLET gates, first body overlap, captured arguments and nested
GETRECNO/target-script/heading phases. `action_event_runtime` attaches this
prepared work to bound MODELCTRL endpoints and registers authored collision
categories. Five stable messenger buffers preserve explicit push/pop behavior
across service suspension. Full controller preparation and damage remain
required. See [MODEL_COLLISION.md](MODEL_COLLISION.md),
[MODEL_HIT_BODY.md](MODEL_HIT_BODY.md) and [MESSENGER.md](MESSENGER.md).

Version 0.26 adds exact HITBOX command decoding and a third typed entry in the
ordered model subcommand queue. `collision_math` reconstructs the query AABox
from the source ONE matrix with recovered float and homogeneous-W arithmetic.
`action_event_runtime` retains the box through debug, CHKBULLET delivery and
source BHit Lua service phases. Typed messenger objects distinguish these boxes
from controller RAM identities. MUTEKI and LUACALL own the represented collision
flags, invulnerability value and callback prefixes; ordinary sequence entry
resets them after queue cleanup. Registered target categories are a prepared
prerequisite, and missing managers/scripts remain explicit service boundaries.
This does not implement target collision responses or original combat. See
[MODEL_HITBOX.md](MODEL_HITBOX.md), [COLLISION_MATH.md](COLLISION_MATH.md) and
[ACTION_EVENT_RUNTIME.md](ACTION_EVENT_RUNTIME.md).

Version 0.25 adds `model_wait` for common model waits and the original player
attack fallback. `model_sequence` owns WCHECK assignment and entry-time clearing;
`action_queries` exposes a live, checked ISCOMEND messenger endpoint.
`action_event_runtime` retains registered ACTION owners, pending queries and
prepared command dispatches, and reads the bound RAM's separate `OriginalPads`
slot. Retrying missing services preserves the original invocation time, shared
random state, command cursor and completed delivery prefix. Full outer sequence
entry remains a required service. See [MODEL_WAIT.md](MODEL_WAIT.md).

Version 0.24 adds immutable `player_runtime_binding` ownership across actual
MODELCTRL/ACTION RAMs, ONE nodes and selected actor skins. `messenger` implements
the original ordered registration/cache/shared-buffer delivery kernel;
`action_event_tables` decodes loaded Se/ActionStarter graphs. `action_event_runtime`
owns prepared model sequences, consumers and resumable event delivery, connecting
ACTION broadcasts to `model_subcommands` queues. External and local-jump sequence
entry publishes its name/cursor and control reset before cleanup, then applies
owned fall/motion effects after cleanup and before executing new commands.
Real audio/effect managers, full player preparation and the original outer game
loop remain required. This explicit prepared context does not advance the boot
controller stage or replace the walking preview. See
[ACTION_EVENT_RUNTIME.md](ACTION_EVENT_RUNTIME.md), [MESSENGER.md](MESSENGER.md)
and [MODEL_SUBCOMMANDS.md](MODEL_SUBCOMMANDS.md).

`dw4_core` is a C++20 library with explicit byte-order decoding and no console or
graphics dependencies. `assets` reads boot, DOL, AFS and BNR structures. `igb`
reads version 6 metadata; `igb_document` loads owned object records, memory
payloads and validated references. `resources` resolves logical names through
the original AFS setup. `packages` follows exported file-list descriptors into
ordered catalog-addressed members and their explicit info-list exports, retaining
preliminary `/s` loading-stage metadata. `scripts` follows exported embedded Lua
records and builds an ordered name-resolution plan with separate native
prerequisite barriers. `lua4` parses and executes a bounded Lua 4 chunk profile
with persistent globals, typed values and caller-supplied strict host bindings.
`startup` executes the func/boot phases, supplies explicit inspection state,
builds the verified common-file queue and resolves its names. Its move-only
`BootRuntime` retains the actual successful VM and host state. `startup_loading`
owns initial/common documents, qualified export identities and the original
immediate/held/deferred routing lists. `textures` decodes verified indexed8/RGBA8 and BC1
base images. `meshes` decodes positions, triangle indices and explicitly requested
UV streams. `materials` resolves a bounded local/inherited single-stage texture
binding profile with reference provenance and unsupported-state diagnostics.
`scenes` traverses verified static export roots into placed geometry instances,
with explicit source paths, switch selection and bounded transform composition.
`scene_controls` validates exported switch-controller targets and evaluates
explicit `ALLON`/`ALLOF` child selection without modifying source bytes.
`player_placement` reads explicitly exported entry-point groups and preserves
their full affine matrices. `player_visual` follows an exported player model's
selected instance, `ONE` record and explicit LOD branch into a supported actor,
appearance and skin. It validates the authored outer transform but does not
compose that editor placement: the original generation path replaces it.
`skinning` resolves a bounded geometry/skin/database/skeleton profile, decodes
single-influence weights and inverse-bind frames, and evaluates explicit manual
joint deltas or complete bone-local transforms on the CPU. `enbaya` decodes
bounded packed streams into immutable samples; `animations` validates the
database/binding/track graph, translation mask and original one-clip pose profile.
`actor_skin` binds a source-qualified prepared actor's explicit main geometry,
captures existing root/joint palette caches by value, and deforms vertices with
the original one-influence CPU arithmetic. The prepared renderer retains raw
coordinates and applies the captured model-view and separate projection. The
mesh workbench owns an explicit prepared clip session with original node/source
clocks and host camera/fitting placement; it does not dispatch ACTION events.
See [ACTOR_SKIN.md](ACTOR_SKIN.md) for graph ownership and arithmetic provenance.
`camera` supplies
the host orbit/projection math. `settings` loads and validates host preferences. `dw4_inspect` and
`dw4_catalog` are read-only CLIs; `dw4_pc` is the native SDL3/OpenGL/ImGui viewer
and workbench.

The scene viewer holds the map and player as separate owned IGB documents.
Numeric image references never identify assets across those documents. One
explicit player visual is appended after the static scene batches. The CPU
samples a selected clip in skin-local space, applies the entry-point matrix once,
and replaces only that batch's position range, preserving UVs, materials, indices
and shared scene normalization. Focus uses world-space visual bounds with the
same normalization. Changing a map root or member removes the old visual; a
failed render-mode replacement clears the GPU scene and can be rebuilt in Solid
mode. Clip replacement validates and uploads before publishing new clip state.

The inspection path is a visual integration boundary, without player input, collision, shadow
composition, automatic LOD, startup execution or controller lifetime management.
The startup research distinguishes `func.lua`, `boot.lua`, `start.lua` and
`first.lua` dispatch order and embedded script lookup behavior. A separate
startup executor now runs the first two phases with a fresh persistent Lua state.
It does not alter the scene renderer or execute the later phases. See
[PLAYER_VIEWER.md](PLAYER_VIEWER.md) and [STARTUP_RESEARCH.md](STARTUP_RESEARCH.md).

AFS payloads are read on demand with limits. Guest addresses remain integers
inside diagnostics; they are never cast into host pointers. A validated DOL is
not executable native code. There is currently no PowerPC execution path,
recompiled PowerPC code, gameplay scene activation, or emulated GPU.

Boot execution reads initial package members in preliminary-then-ordinary order,
retaining their order within each stage and immediately exposing their Lua info
records. Every matching func chunk runs before the boot phase begins. Unknown
host effects, malformed data and exhausted budgets stop execution. Native CFL
methods clear or append to a host queue, suppressing exact duplicate names while
retaining the first call's provenance. Resolution lowercases ASCII A–Z in each
request and searches exact catalog keys using one stable index; it never uses
host filesystem case rules or changes the catalog's global lookup behavior.
Failed re-resolution clears old locations and success flags before validation.

The fixed boot profile reads at most 32 initial members, 64 MiB per member and
256 MiB combined. It executes at most 64 chunks with 32,768 instructions each;
one VM retains bounded globals, closures, strings and tables across chunks.
The host caps total callbacks, path scan work and queue metadata. Lua file/OS/
network APIs are absent. There is no coroutine resume past an unknown method.
Version 0.11 consumes the common queue and retains the decoded files in an owned
`StartupSession`. Repeated load/export occurrences keep separate identities;
only exact Lua infos are immediate, while camera/ROOTATTR scenes are appended
after ordinary deferred infos and other scenes stay in a separate holding list.
These are loader routes, not completed manager callbacks. Successful byte
loading and complete routing have separate flags, neither of which bypasses the
original loading-screen condition. Runtime manager callbacks and layer/text
initialization remain pending; start/first are never silently skipped or marked
complete. The CLI and Startup page use the session API, while the original
`inspect_package_boot` API remains available for queue-only inspection. See
[STARTUP_LOADING.md](STARTUP_LOADING.md) for ownership and aggregate limits.

Version 0.13 separates preliminary `/s` loading from that aggregate inspection
path. The original registers preliminary infos and waits for sound-bank transfer
before ordinary files and func/boot. A `StartupPreliminaryStage` therefore owns
only the descriptor and preliminary documents, without a Lua VM. `controllers`
implements recovered StaticInfo/SwitchCtrl callback effects in owned native
manager, RAM and messenger state. Its ordered-prefix mode preserves earlier
effects and stops at the first unsupported handler. A fresh prepared callback
context is explicit; complete original manager construction and audio services
remain pending. See [CONTROLLERS.md](CONTROLLERS.md).

The portable `pad` module implements independent original input state for four
physical slots. Platform sampling and game player ownership are separate from
button edges, directional masks and update-count repeats. These original input
rules are not yet wired into a multiplayer gameplay state machine.

Version 0.14 adds `motion` and `motion_timer`: original six-mode scalar state,
exact MOVE/MOVEEX decoding, four owned channels, cached elapsed timing and local
model-basis translation. Authored zero-frame commands retain their original
infinity/NaN arithmetic; the native matrix boundary rejects non-finite results
explicitly. These modules consume caller-owned state and timestamps without
creating an OS clock or assuming a simulation rate. They are not wired into the
walking preview or a complete original actor/sequence/turning/collision loop.
See [MOTION.md](MOTION.md) for source addresses and validation.

Version 0.15 adds `model_motion`: caller-prepared desired/rate/applied angles,
an owned model matrix and the motion-written `_isHitCheck` flag. The combined motion
API ticks the rotation channel, updates Y angle/rate, applies recovered Euler
turning, then translates along the resulting basis using the same cached timer.
Repeated float angle wrapping is batched exactly within eligible exponent bins.
These are native pose effects before collision, not a full actor loop.
See [MODEL_MOTION.md](MODEL_MOTION.md).

Version 0.16 adds `model_control`: one explicitly prepared model owns motion
channels, an optional elapsed timer, CTRL mode/phase, PDMOVE parameters and the
fall-check flag. MOVE/MOVEEX creates the missing timer and releases position lock
with original heading synchronization before configuring channels. CTRL/PDMOVE
has separate effects. The post-input control API preserves existing requests
and exposes the original WAIT/WALK dispatch decision without executing an
animation/action sequence. Explicit helpers recover player rates `(0,7,0)` and
matrix-derived Y angles. Full work construction, named RAM ownership, original
input/sequence callbacks, attachments and collision remain required; none of
these APIs replaces the preview controller. See [MODEL_CONTROL.md](MODEL_CONTROL.md).

Version 0.17 adds `model_sequence`. An immutable program owns source identity and
ordered decoded SEQ/command records; each runtime owns its label bindings,
cached offsets, current command position and model control state. Selection
implements the owned control/motion portion of ordinary sequence entry. The
runner consumes supported commands, stops at weapon LABEL boundaries and retains
new state across ordinary local sequence jumps. Missing ACTION execution and
unsupported controller/command effects stop at the exact unconsumed command;
completed prefix effects survive. A supplied ACTION dispatcher must perform the
external effect, not merely acknowledge a name. Subcommand cleanup, collision
fields, original wait/virtual callbacks and complete player construction remain
outside this prepared-work API. The application does not use it as a replacement
for those effects or for the preview controller. See [MODEL_SEQUENCE.md](MODEL_SEQUENCE.md).

Version 0.22 adds `animation_hierarchy` and shared combiner bone/joint caches.
An explicit ACTION actor traversal consumes current view attributes and parent
time, applies node timing once, writes root0 and evaluates the no-modifier
hierarchy when invalid. Successful local sampling invalidates it even through
direct pose APIs. Cached bone queries use the first actor and ASCII-folded first
name match, without forcing evaluation. Renderer context stacks and full node/
appearance traversal remain separate. See [ANIMATION_HIERARCHY.md](ANIMATION_HIERARCHY.md).

Version 0.21 adds `action_actors` and the actor-aware ACTION factory. A qualified
prepared RAM owns every ordered actor's combiner link and identity bone/joint
allocation. NDSP releases actor buffers, action work, timers and attributes;
reactivation preserves node clocks and retained combiner matrices. The command-only
factory still rejects cleanup without actor ownership. Original traversal and
cached bone queries remain separate from the existing skin-pose inspection API.
See [ACTION_ACTORS.md](ACTION_ACTORS.md).

Version 0.20 adds `actions`: immutable qualified ACTION2 programs and per-RAM
command, timer, node, appearance and combiner state. Exact model sequence ACTION
recipients execute the recovered five command types with retained wait/resume
positions and original update order. Required RNG/broadcast services and explicit
suspension preserve successful prefixes. Complete actor traversal, messenger
consumers, scene attribute attachment and outer player callbacks remain separate;
the preview still uses its provisional controller. See [ACTIONS.md](ACTIONS.md).

Version 0.19 adds `animation_combiner`. An immutable document-qualified clip
library is shared by owned source runtimes; copies share decoded data but retain
independent clocks and play modes. Each combiner owns its states, full bone
bindings and last evaluated float matrices. Prepared serialized combiners can
seed those caches explicitly. Priority-ordered retirement and fixed snapshots
support interrupted transitions. MOTIONSET helpers require an already resolved
clip plus explicit system/node timestamps. Updates publish combiner, source and
work changes only on success. Version 0.20 supplies the prepared ACTION dispatch
path above; actor construction and preview integration remain pending.
See [ANIMATION_COMBINER.md](ANIMATION_COMBINER.md).

Version 0.18 adds `animation_timing`: owned animation-state clocks, explicit
MOTIONSET numeric blend preparation, shared source wrapping caches and recovered
speed-origin adjustment. `animations` can sample decoded clips and full skin poses
from these integer local times, then mix already-bound layers using original
quaternion and fused translation arithmetic. Source ownership and mode caching
are explicit. Versions 0.19/0.20 add bounded state/snapshot and prepared ACTION2
execution; full actor and preview integration remain pending. See
[ANIMATION_TIMING.md](ANIMATION_TIMING.md).

Version 0.12 adds a separate native walking preview. `walking_collision` reads
the explicitly selected original floor and wall models; `walking` builds bounded
query hierarchies and advances a grounded controller over continuously supported
triangle paths, with a swept body circle against static wall/door triangles.
The application supplies keyboard/gamepad input in camera space,
advances provisional host movement at 120 Hz, selects original idle/run clips
and updates the dynamic player batch. Presentation has its own frame limit.
These constants are not attributed to original game timing. The preview does
not execute the unfinished startup stages, construct original actor controllers,
or implement dynamic character collision, combat or saves. See
[PLAY_PREVIEW.md](PLAY_PREVIEW.md) and [WALKING_COLLISION_RESEARCH.md](WALKING_COLLISION_RESEARCH.md).

The original DOL BSS envelope overlaps some initialized data sections. A future
loader must zero the envelope before loading initialized sections or clear only
uncovered ranges; blindly zeroing it after section loading destroys valid data.

The application uses graphics dependencies pinned to exact upstream commits and
archive hashes. Core-only builds do not fetch dependencies. Parser tests use
small generated fixtures instead of original game bytes. `verify_game.py`
additionally checks the original DOL checksum and compares every extracted
filesystem file against its ISO extent.

## Engine reconstruction order

1. **Implemented:** resolve the AFS setup in `ndmw.igb`, including archive/entry
   identities and logical paths. See [RESOURCES.md](RESOURCES.md).
2. **Implemented:** decode IGB object instances, memory references and optional
   directory/pool names. External-object loading is still pending. On-disk schemas
   stay separate from host objects; inherited slots are format metadata, not a
   ready-to-use host ABI. See [IGB_OBJECTS.md](IGB_OBJECTS.md).
3. **In progress:** texture base images and object-space meshes are decoded and
   displayed natively. Triangle lists and grouped 16-bit indexed strips use
   recovered engine enum values and checked memory streams. Original UVs and
   a verified base-texture binding profile are displayed in Textured mode.
   Inverse-bind hierarchy inspection and manual single-influence skinning are
   available for a verified profile. Original Enbaya clips are decoded and
   evaluated with checked full-bone transforms. Static export traversal and
   original instance transforms are available for a verified profile. Full material
   state, additional actor/combiner profiles and comparison with the original renderer remain.
   See [SKINNING.md](SKINNING.md), [TEXTURES.md](TEXTURES.md),
   [MATERIALS.md](MATERIALS.md), [MESHES.md](MESHES.md) and the evidence in
   [MESH_RESEARCH.md](MESH_RESEARCH.md).
4. Connect the implemented actor construction and sequence/ACTION state to
   original startup, gameplay scene activation and per-frame input/collision
   order. Replace OS/DVD/PAD/GX boundaries with native services using explicit
   interfaces.
5. Recover game loop, combat, inventory, scene transitions and persistence in
   narrow, behavior-tested increments. Retain a trace from each adapted function
   to reference addresses and evidence.

The OpenGL 3.3 inspection renderer consumes platform-independent `MeshData`,
normalizes a GPU copy for stable viewing and renders to a depth-tested offscreen
target with optional MSAA resolve. The original coordinate values remain in the
core data. ImGui displays the resulting texture; it does not generate the 3D
geometry. See [RENDERER.md](RENDERER.md). Choosing a final game rendering backend
is still open. Avoid making new gameplay objects depend on ImGui or SDL.

Selecting geometry resolves material provenance without decoding pixels. Entering
Textured mode explicitly reads the UV stream and bound image and uploads them to
the GPU. Unsupported bindings do not prevent solid geometry inspection. Binding
resolution uses object references and a unique checked ancestor path; asset names
are diagnostic labels, never a texture-selection fallback. This deliberately
limited resolver is not a complete scene-state traversal engine. For static scene
instances, a separate path-aware resolver validates the exact supplied ancestry
and independently inherits unit-zero binding and enable state. Shared graph nodes
therefore retain the material of each placement without guessing a global parent.

The scene decoder follows an explicitly selected `igSceneInfo` root and records
unsupported behavior instead of flattening dynamic or unknown nodes. Stored
switch bits remain authoritative by default. A separate, labeled inspection
option selects one child of a disabled root switch without changing nested
selection or source data. The application refuses incomplete traversals and uses
a separate batch renderer with common bounds, camera, depth and MSAA resolve.
See [SCENES.md](SCENES.md) and [SCENE_RENDERER.md](SCENE_RENDERER.md).

An alternative explicit scene option evaluates an exported `beSwitchCtrlInfo`
command whose target must be exactly the selected root. It is exclusive with
manual root-child inspection. The controller decoder validates export identity,
mask layout and child types, with an aggregate child-validation budget. It does
not construct runtime controller registrations or run the Lua scheduler. Package
inspection similarly preserves loading-stage metadata without executing the
original staged initialization. See [PACKAGES.md](PACKAGES.md),
[SCENE_CONTROLS.md](SCENE_CONTROLS.md) and [SCENE_ACTIVATION.md](SCENE_ACTIVATION.md).

Skin inspection requires an explicit unique skin/database association and one
referenced skeleton. Bone-list ordinals, joint indices and per-vertex palette
indices remain separate. Affine inversion reconstructs bind frames from the
original inverse-joint matrices; stored translation arrays are not substituted
for that reference space. Manual local joint deltas propagate through the
recovered joint hierarchy. The renderer updates positions while preserving
the source mesh's normalization, indices, UVs and GPU texture. A separate animation
path follows explicit database bindings, decodes a selected clip and uses the
original translation mask to select sampled or skeleton-constant translations.
Quaternion conversion preserves the original signs and norm. Helpers participate
in the full bone hierarchy. Only the verified single-clip, no-modifier skeleton
profile is accepted. Original timing and ordered layer arithmetic are available
in the core, alongside the prepared full-binding combiner and snapshot path.
The verified actor-aware ACTION and preview paths consume these primitives.
Additional original combiner/material profiles and complete frame integration
remain pending.
See [ANIMATIONS.md](ANIMATIONS.md), [ANIMATION_POSE.md](ANIMATION_POSE.md) and
[ANIMATION_COMBINER.md](ANIMATION_COMBINER.md).

## Timing and PC settings

Host presentation frequency and game simulation frequency must be separate.
Measure original DW4 timing before choosing a fixed simulation step; do not
assume 30 or 60 Hz. Animation, physics, input sampling and timers must preserve
their original semantics at 144 Hz presentation. Interpolation and widescreen
camera/UI behavior require game-specific work. A borderless window uses the
desktop display size; selecting 3840×2160 does not change the desktop mode or
prove a 4K internal renderer exists.

SDL-mapped gamepads can be inspected now. Keyboard/mouse gameplay, Xbox,
PlayStation and GameCube gameplay mappings remain separate tasks. Native
GameCube adapters may require dedicated adapter handling and calibration.

## Modding and online co-op dependencies

Stable asset identity and a validated loader come before texture overrides or
mod manifests. Version the future mod API and distinguish declarative content
from executable code. Original versus modernized gameplay should share a tested
original baseline before opting into changes.

Four-player online co-op requires proven local multiplayer, identified player
and world state, authoritative simulation ownership, reproducible RNG handling,
save ownership, reconnect/error behavior and latency testing. The network model
(lockstep, rollback or host-authoritative snapshots) remains undecided until
simulation behavior is understood. No networking implementation is claimed by
this milestone.
