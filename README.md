# Digimon World 4 PC

An early native PC port project for **Digimon World 4 (USA, GameCube)**.

The active path is a **native source port**: recover the original game and
engine as authored C++20 source, using the existing `dw4_core` and native
application. The owner selected this direction on September 18, 2026 and set
the DolRecomp/ModernGekko implementation aside. Builds and tests run locally;
GitHub Actions is disabled.

The native core already reconstructs selected asset, script, animation, actor,
player and collision operations. The walking preview displays original assets,
but its movement and camera remain provisional. **It is not yet a complete or
1:1 playable reconstruction of Digimon World 4.**

The immediate work is connecting the original startup loader to runtime object
construction, alongside the remaining player-frame and collision requirements. See
[PORTING_STRATEGY.md](docs/PORTING_STRATEGY.md) for the continuation plan and
[ROADMAP.md](docs/ROADMAP.md) for the acceptance gates. Build the active project
with `scripts/build.ps1 -Jobs 4`; `Play-DW4-PC.cmd` and `Start-DW4-PC.cmd` open
its walking preview and workbench respectively. Builds do not launch the game.

The complete target includes original gameplay, audio, progression, saves and
local one-to-four-player play; keyboard play and freely saved bindings; graphics
settings, 16:9, 4K and presentation up to 300 FPS without faster simulation;
an F1 ImGui menu, supported languages, HD packs, content mods and online co-op.
The release should accept the user's ROM, prepare its assets locally, use one
EXE entry point and preserve portable saves. These are requirements, not
implemented native-core claims. The [language audit](docs/LOCALIZATION.md) also
keeps supported translations separate from pack filenames.

## Set-aside recompilation experiment

The prior DolRecomp/ModernGekko work and every existing package, imported asset
and save are retained. Its CPU translation still uses Dolphin-derived services;
its boot, performance, audio, import and F1-menu results apply to that separate
implementation. They do not establish native-core gameplay or UI support.
See [RECOMPILATION.md](docs/RECOMPILATION.md),
[PC_APPLICATION.md](docs/PC_APPLICATION.md) and their linked reports for the
historical evidence. Its scripts remain available for preservation and explicit
reference work; recompilation optimization and packaging are not the active queue.
Existing package saves remain beside their own EXE and are not migrated by the
native workbench.

## Active native core and walking preview

Version 0.45 connects original static geometry to the native collision candidate
pipeline. Canonical vertex/index/group owners feed the original primitive scan,
typed node dispatch and actual query result storage. Point, segment, matrix and
triangle operations retain their original arithmetic and publication order.
The September 19 updates add the original native matrix inverse, canonical
AABox transforms, CHECKLAND floor response and CHECKWALL's three-pass wall
correction. Native geometry responses reach the existing HITFLOOR/HITWALL
consumers through registered receivers. The recovered global clock and actual
null-input transform updates now let all four prepared original player slots
finish one neutral frame each, with all five stage collision infos registered.
Supplying the explicitly prepared native viewer owner now resumes a directional
frame through original SQ_WALK/RUNND1, motion and collision. Another 48 prepared
per-RAM frames cover six held and six released samples for each of four slots,
including return to SQ_WAIT/WAITD1 and independent node/subcommand state.
The recovered serial scheduler now calls the original manager info/RAM
traversal and its actual player update binding. A prepared original-data test
follows all four authored RAM occurrences through seven serial batches,
including suspension inside the second player without replaying the first.
Those batches now run through the native application and scheduled system:
reset handling, time publication, stage control flow and storage-error
orchestration precede the player updates. Original loading and scene operations
remain explicit reached services; prepared clock/reset/storage inputs do not
establish complete startup. See [SYSTEM_FRAME.md](docs/SYSTEM_FRAME.md),
[SYSTEM_STAGE.md](docs/SYSTEM_STAGE.md) and
[SYSTEM_STORAGE.md](docs/SYSTEM_STORAGE.md).
The stage adapter now enters actual directory lookup/request, loading-claim
release and application info-registration bodies. A prepared cached-directory
integration covers request, suspended registration and cache removal. New-file
requests retain the original synchronous path search or asynchronous queue
call. The decoder now supplies original load orchestration, concrete byte-stream
opening/header processing and three early runtime table helpers. Supplied
`ndmw.igb` now passes runtime metatype/slot reconciliation using twenty recovered
object layouts, forty-seven registered field-class identities and explicit
prepared list endpoints. Sixty inherited field mappings lead into native
directory-path and dependency-table processing. The supplied-data prefix reads
the dependency block, resolves the canonical ArkCore loader, interns its base
path and enters the original synchronous dependency request. With explicitly
prepared loader/string-pool allocation state, it enters the native `igIGBFile`
factory and its registration wrapper. With explicit cold prerequisite flags,
the callback runner, once guard and native ThreadManager registration select the
metadata pool by its actual handle. The enabled runtime enters metadata factory
`80029E64` and stops at required `igMetaObject` registration `8002A0A8`; one
unsupported serialized field class remains null. The reflected allocation caller, object
header initialization, three field-pass callers and concrete file post-default
pool assignments are now reconstructed. Their focused tests use explicit
metadata, allocator and field bodies; actual class registration and concrete
field defaults remain required before this original dependency can be created.
The generic type registrar now preserves original parent/default/registry/layout
ordering and deferred callback lists. Concrete integer and boolean field
handlers perform the original default-byte copies inside the object initializer;
remaining recursive reference/member operations and automatically constructed
directory lists remain required.
Separate metadata and internal-field-list constructor chains now preserve actual
header/default writes, pool selection and backing allocation boundaries. Their
allocator and reflective registration endpoints remain explicit; this does not
complete cold startup or switch the supplied-data test to a different runtime mode.
The actual cold ArkCore constructor and bootstrap caller now preserve the original
disabled initialization phase. A separate composed test enters real metadata
registration and direct constructors, publishes the canonical busy metadata,
and now executes actual `igObject` parent registration, name interning, layout
probe and specialized field defaults `80066B7C`. The `_refCount` and
`_memPoolIndex` descriptors use the real field constructor, mutable name setter,
unsigned-integer default-buffer preparation, finalization and raw registry append.
The cold chain now executes type publication `800660AC` and field layout
`80065A58`, completes `igObject`, and inherits its two fields into `igMetaObject`.
The latter's local constructor probe `80029FFC` now executes the actual reset
`80065218`, publishes type word `804761D8` and enters defaults `8002A164`.
The same cold chain now constructs all 25 local descriptors through native
batch `80065924`, retaining the two inherited fields. That schema's seven
descriptor profiles have actual constructors; native value setters,
schema configuration, layout and type publication complete `igMetaObject`
with flags `7` as the second published type. Its two lazy metadata/list globals
are also constructed through the original callers. Deferred callbacks are
appended in order: `8002DD18`, `8002E054`, `80066AE0`. The first callback now
executes `8002DD18 -> 8002DD40 -> 8002E07C`, inherits two base fields into
the non-reference-counted list metadata, and runs actual probe
`8002E020 -> 800536F8`. Its defaults `8002E108` construct the memory-reference
and signed-integer descriptors through `8002AC34` and `8002E1AC`, bringing
the chain to 29 constructed descriptors. Getter `80028BFC` now enters actual
object-field registration `800289F8`, including its base and reference parents.
Their defaults `8002A764`, `800273EC`, and `80028AB4` add 18, 2, and 3
descriptors. All ten reached descriptor profiles have native constructors and
value operations. The three field classes finish registration, bringing the
chain to 52 constructed descriptors, 50 default buffers, and five published
types. Abstract reference-prototype callback `80027454` executes the real
reflected factory's null branch; concrete object-prototype callback `80028B64`
now initializes its `0x44`-byte allocation through actual `800667F4`, the reached
field passes, string defaults, and object-field tail `8006A438`. Native prototype
publication, registry append and finalization produce canonical `805616EC`
with ordinal and type ordinal zero. The actual list name/index search through
canonical descriptor `805617C4` now enters the memory-reference clone
`8002ACD8 -> 8006331C -> 8006CC88 -> 800639E4`. Its native copy, independent
default buffer, finalization and replacement complete child list registration.
Both list metadata owners `805619D4` and `805619CC` finish with flags `7`.
The chain contains 53 schema descriptors plus the reflected prototype, 54
default buffers, seven published types and one prototype entry. The original
bootstrap caller now writes enabled byte 1 and snapshots type/prototype counts
7 and 1 at core `+04/+08`. It next reaches `igMetaEnum` registration body
`8002AAFC`, canonical metadata `80561808`. That body now enters its actual
prerequisite guard and ThreadManager registration. Enabled metadata construction
uses the native reflected factory and all three field passes. Its `_metaFields`
member constructs the real internal list, including a 64-byte backing allocation
with four-byte alignment, reference-element ancestry zero fill and balanced object assignment.
The resulting metadata continues into registration and inherited field append.
ThreadManager defaults `80023940` now construct two actual reflected object
descriptors and lazily allocate distinct metadata/internal-list pairs for their
thread-list and semaphore types. Native schema publication and bootstrap
callback append complete ThreadManager as the eighth registered type, with 55
schema descriptors and 56 default buffers. Deferred thread-list registration
`80023F28` now enters its actual non-reference-counted object-list and DataList
parents, including prerequisite guards, canonical metadata and layout probes.
The reached signed-integer field class has native registration, reflected
initialization and prototype publication. DataList's first two descriptors
are constructed through that registered class; schema configuration and
finalization wait for its third factory. That factory now enters native
memory-field registration `8002AEAC`, its exact probe and eight-field defaults.
The reached Boolean class completes registration and reflected prototype
publication; seven memory-class descriptors are constructed before the final
factory enters string-field registration `80025404`. Its native probe and
defaults construct the local Boolean descriptor, then enter the inherited
signed-byte clone. Signed-byte registration and its reflected prototype complete
before the clone enters actual reflected copying `800680B4`. The native caller
now performs reflected allocation and the original member pass, including raw
scalar, string, parent-object and native memory-buffer copies (`80062824`).
Both inherited string replacements, the string schema and its reflected prototype
now complete in the same cold chain. Memory defaults construct their eighth field
and perform their inherited shallow-copy replacement. Reset descriptor factory
`80031378` now registers `igEnumMetaField` and its `igRawRefMetaField` dependency,
executes their actual probes, schema and reflected prototypes, and returns to the
memory defaults caller. The four-byte reset value and enum-getter pointer are
published through their original operations. Memory registration and its reflected
prototype complete. Unsigned-byte registration `80023234`, its exact base-reset
probe and reflected prototype now complete as well. The retained DataList
getter shares that prototype cell and finishes its schema. Enabled metadata
child lists use the actual reflected initializer and memory-member allocation;
the object-list specialization runs the original reflected memory-field clone.
DataList, the non-reference-counted object list and the thread list finish
registration. Deferred `igThread` registration now runs its named-object parent,
including the original probe and `_name` schema. Unsigned-integer registration
`80022CD8`, its actual probe and reflected prototype now resume `_stackSize`
construction. The ten-field Thread batch finishes its setters and schema, and
both Thread and Semaphore metadata complete. ThreadManager activation
`80023A30 -> 800755F8` now enters the original pool lookup, manager factory
and concrete metadata redirect. Actual `igGamecubeThreadManager` registration,
probe and defaults bring the cold composition to twenty-four published types
and ten prototypes. Distinct runtime owners now let the real reflected
initializer allocate the manager and construct its nested thread list. The
list's scalar and memory defaults complete, and balanced object assignment
publishes it in the manager. Concrete semaphore redirect `80026160` now enters
its real registration and creates three local descriptors. Its fourth factory,
`8002450C` for `igStructMetaField`, is the next reached dependency; the manager
singleton and main thread remain uninitialized. See
[IGB_PLATFORM_SEMAPHORE.md](docs/IGB_PLATFORM_SEMAPHORE.md) and
[IGB_RUNTIME_OBJECTS.md](docs/IGB_RUNTIME_OBJECTS.md).
See [IGB_THREAD_STARTUP.md](docs/IGB_THREAD_STARTUP.md).
Physical schema stores update descriptor
`+1C/+08/+18` in original order as well as the canonical field members.
Enum metadata remains null while the prerequisite chain is incomplete.
This is still incomplete startup. Further reference operations, method fields,
remaining registrations and pool/platform allocator
internals remain required. See [IGB_REFLECTED_CLONE.md](docs/IGB_REFLECTED_CLONE.md),
[IGB_FIELD_MEMBER_COPY.md](docs/IGB_FIELD_MEMBER_COPY.md),
[IGB_MEMORY_MEMBER_COPY.md](docs/IGB_MEMORY_MEMBER_COPY.md),
[IGB_BUFFER_COPY.md](docs/IGB_BUFFER_COPY.md),
[IGB_ENUM_FIELD_DEFAULTS.md](docs/IGB_ENUM_FIELD_DEFAULTS.md),
[IGB_UNSIGNED_BYTE_FIELD.md](docs/IGB_UNSIGNED_BYTE_FIELD.md),
[IGB_NAMED_TYPES.md](docs/IGB_NAMED_TYPES.md),
[IGB_UNSIGNED_INT_FIELD.md](docs/IGB_UNSIGNED_INT_FIELD.md),
[IGB_DATA_LIST_REGISTRATION.md](docs/IGB_DATA_LIST_REGISTRATION.md),
[IGB_DATA_LIST_DEFAULTS.md](docs/IGB_DATA_LIST_DEFAULTS.md),
[IGB_INT_FIELD_REGISTRATION.md](docs/IGB_INT_FIELD_REGISTRATION.md),
[IGB_MEMORY_FIELD_REGISTRATION.md](docs/IGB_MEMORY_FIELD_REGISTRATION.md),
[IGB_MEMORY_FIELD_DEFAULTS.md](docs/IGB_MEMORY_FIELD_DEFAULTS.md),
[IGB_STRING_FIELD_REGISTRATION.md](docs/IGB_STRING_FIELD_REGISTRATION.md),
[IGB_STRING_FIELD_DEFAULTS.md](docs/IGB_STRING_FIELD_DEFAULTS.md),
[IGB_THREAD_DEFAULTS.md](docs/IGB_THREAD_DEFAULTS.md),
[IGB_THREAD_DEPENDENCIES.md](docs/IGB_THREAD_DEPENDENCIES.md),
[IGB_META_ENUM.md](docs/IGB_META_ENUM.md),
[IGB_METADATA_INIT.md](docs/IGB_METADATA_INIT.md),
[IGB_OBJECT_FIELD_CONSTRUCT.md](docs/IGB_OBJECT_FIELD_CONSTRUCT.md),
[IGB_MEMORY_FIELD_CONSTRUCT.md](docs/IGB_MEMORY_FIELD_CONSTRUCT.md),
[IGB_FIELD_TYPE_REGISTRATION.md](docs/IGB_FIELD_TYPE_REGISTRATION.md),
[IGB_FIELD_TYPE_DEFAULTS.md](docs/IGB_FIELD_TYPE_DEFAULTS.md),
[IGB_FIELD_PROTOTYPE.md](docs/IGB_FIELD_PROTOTYPE.md),
[IGB_FIELD_INIT.md](docs/IGB_FIELD_INIT.md),
[IGB_FIELD_STRING_INIT.md](docs/IGB_FIELD_STRING_INIT.md),
[IGB_FIELD_CLONE.md](docs/IGB_FIELD_CLONE.md),
[IGB_FIELD_COPY.md](docs/IGB_FIELD_COPY.md),
[IGB_LIST_REGISTRATION.md](docs/IGB_LIST_REGISTRATION.md),
[IGB_LIST_DEFAULTS.md](docs/IGB_LIST_DEFAULTS.md),
[IGB_METADATA_PROBE.md](docs/IGB_METADATA_PROBE.md),
[IGB_METADATA_DEFAULTS.md](docs/IGB_METADATA_DEFAULTS.md),
[IGB_FIELD_SCHEMA.md](docs/IGB_FIELD_SCHEMA.md),
[IGB_FIELD_VALUES.md](docs/IGB_FIELD_VALUES.md),
[IGB_TYPE_PUBLICATION.md](docs/IGB_TYPE_PUBLICATION.md),
[IGB_METADATA_LAYOUT.md](docs/IGB_METADATA_LAYOUT.md),
[IGB_OBJECT_DEFAULTS.md](docs/IGB_OBJECT_DEFAULTS.md),
[IGB_META_FIELD_FACTORY.md](docs/IGB_META_FIELD_FACTORY.md) and
[IGB_FIELD_OPERATIONS.md](docs/IGB_FIELD_OPERATIONS.md). Parent-field
inheritance and metadata-name assignment are also reconstructed. Native typed
list factories and raw append/reserve preserve allocator queries, physical
zeroing, pointer publication and count order. The same cold integration now uses
actual initial list construction/reserve and the actual inherited child-list
constructor. Physical allocators remain explicit endpoints.
Allocator services and remaining field/class registrations are still explicit;
this is not a complete native game boot. See
[IGB_OBJECT_REGISTRATION.md](docs/IGB_OBJECT_REGISTRATION.md),
[IGB_TYPE_LIST_FACTORY.md](docs/IGB_TYPE_LIST_FACTORY.md),
[IGB_POINTER_LIST.md](docs/IGB_POINTER_LIST.md),
[IGB_ARK_CORE_BOOTSTRAP.md](docs/IGB_ARK_CORE_BOOTSTRAP.md),
[IGB_METADATA_REGISTRATION.md](docs/IGB_METADATA_REGISTRATION.md) and
[IGB_METADATA_OPERATIONS.md](docs/IGB_METADATA_OPERATIONS.md).
The separate native pool-name table and
original 46-name lookup are also implemented. Loading is still incomplete.
Full runtime initialization, concrete factories,
object population, post-load work and cleanup remain required. See
[IGB_TYPE_MAPPING.md](docs/IGB_TYPE_MAPPING.md),
[IGB_RUNTIME_BOOTSTRAP.md](docs/IGB_RUNTIME_BOOTSTRAP.md),
[IGB_LOADING_PATHS.md](docs/IGB_LOADING_PATHS.md),
[IGB_DEPENDENCIES.md](docs/IGB_DEPENDENCIES.md),
[LOADER_GLOBALS.md](docs/LOADER_GLOBALS.md),
[LOADER_DEPENDENCY.md](docs/LOADER_DEPENDENCY.md),
[IGB_STRING_POOL.md](docs/IGB_STRING_POOL.md),
[IGB_FILE_FACTORY.md](docs/IGB_FILE_FACTORY.md),
[IGB_OBJECT_INIT.md](docs/IGB_OBJECT_INIT.md),
[IGB_FILE_POOLS.md](docs/IGB_FILE_POOLS.md),
[IGB_TYPE_REGISTRATION.md](docs/IGB_TYPE_REGISTRATION.md),
[IGB_REGISTRATION_CALLBACKS.md](docs/IGB_REGISTRATION_CALLBACKS.md),
[IGB_THREAD_REGISTRATION.md](docs/IGB_THREAD_REGISTRATION.md),
[IGB_BOOTSTRAP_OBJECT.md](docs/IGB_BOOTSTRAP_OBJECT.md),
[IGB_FIELD_REGISTRY_FACTORY.md](docs/IGB_FIELD_REGISTRY_FACTORY.md),
[IGB_METADATA_FACTORY.md](docs/IGB_METADATA_FACTORY.md),
[IGB_FIELD_DEFAULTS.md](docs/IGB_FIELD_DEFAULTS.md),
[NATIVE_STAGE_SERVICES.md](docs/NATIVE_STAGE_SERVICES.md) and
[IGB_DECODE.md](docs/IGB_DECODE.md).
The specialized camera view-inverse helper is also recovered; complete camera
mode evaluation and publication remain required. Original registration, other
manager bodies and the dispatcher still need integration. See
[MODEL_MANAGER_FRAME.md](docs/MODEL_MANAGER_FRAME.md),
[CAMERA_MATH.md](docs/CAMERA_MATH.md), [VIEWER.md](docs/VIEWER.md),
[SERIAL_SCHEDULER.md](docs/SERIAL_SCHEDULER.md),
[APPLICATION_FRAME.md](docs/APPLICATION_FRAME.md) and
[PLAYER_INPUT_MATH.md](docs/PLAYER_INPUT_MATH.md).
Nonnull transform inputs,
missing bounds, CHECKWALLFULL and startup remain required. See
[GAME_CLOCK.md](docs/GAME_CLOCK.md), [ACTOR_TRANSFORM.md](docs/ACTOR_TRANSFORM.md),
[HITLAND_WALL.md](docs/HITLAND_WALL.md),
[HITLAND_FLOOR.md](docs/HITLAND_FLOOR.md), [MATRIX_INVERSE.md](docs/MATRIX_INVERSE.md),
[HITLAND_GEOMETRY.md](docs/HITLAND_GEOMETRY.md),
[HITLAND_PRIMITIVES.md](docs/HITLAND_PRIMITIVES.md) and
[HITLAND_NODE_DISPATCH.md](docs/HITLAND_NODE_DISPATCH.md).

This is a core execution milestone. Original four-slot GENERATE acceptance
and its prepared service prerequisites are tracked in
[VALIDATION.md](docs/VALIDATION.md). The original frame scheduler, device-to-player
assignment, gameplay camera, complete collision queries, combat and complete
game remain unfinished. The visible
walking preview is unchanged. The full original game, including local
one-to-four-player play, remains the target. See
[ORIGINAL_BASELINE.md](docs/ORIGINAL_BASELINE.md).

**The native walking preview lets you control Agumon in an original
map with keyboard or an SDL-mapped gamepad.** It uses original map, model,
textures, floor/wall collision geometry, ACTION animations and weapon attachment.
Movement and the
follow camera are provisional native implementations. The complete original
game, combat, progression, audio and saves remain unimplemented.

The existing walking-preview application uses SDL3, OpenGL and Dear ImGui. It
reads your original local files directly. In this separate application, the
PowerPC `main.dol` is inspected as data and never executed. A bounded native Lua
4 interpreter executes selected embedded
boot bytecode with explicitly implemented host methods. This repository does
not contain game files.

## Native-core and walking-preview capabilities

- Produce original static triangle candidates through canonical
  `beHitLandModelInfo` selection, registered node handlers and live geometry
  attribute/vertex/index owners. Results retain the actual query segment,
  matrix-stack element, node path and original group/stream cursor. Dynamic
  source matrices still copy before broad-phase rejection. Native CHECKLAND and
  CHECKWALL responses are implemented; complete dynamic updates, CHECKWALLFULL
  and original manager startup remain required. See
  [HITLAND_WALL.md](docs/HITLAND_WALL.md),
  [HITLAND_GEOMETRY.md](docs/HITLAND_GEOMETRY.md) and
  [HITLAND_FLOOR.md](docs/HITLAND_FLOOR.md).

- Route original player wall/floor checks through canonical HITLANDDELIVER
  info/RAM records with a distinct manager and shared model nodes. The five
  recipient commands preserve their original query/response and matrix-update
  order. Prepared native integration delivers real floor/wall geometry responses
  through HITFLOOR/HITWALL. A prepared original player frame now completes native
  collision with all stage collision infos, the recovered global clock and
  verified transform updates. Complete startup and scheduled gameplay remain
  required. Direction normalization and XZ
  broad-phase arithmetic have separate instruction comparisons. See
  [MODEL_MOVEMENT_COLLISION.md](docs/MODEL_MOVEMENT_COLLISION.md),
  [HITLAND_DELIVER.md](docs/HITLAND_DELIVER.md) and
  [HITLAND_MATH.md](docs/HITLAND_MATH.md).

- Execute one actual player-manager frame with independently captured input,
  status, motion and wait invocations. Direct wait completion resumes the live
  command loop; delayed subcommands read the current system delta per entry.
  Required camera, sound/effect, position-lock, ride and collision-query operations
  retain explicit boundaries. This is core integration, not a complete game
  loop or connected multiplayer. See [MODEL_FRAME.md](docs/MODEL_FRAME.md),
  [MODEL_MOTION_ACTUAL.md](docs/MODEL_MOTION_ACTUAL.md),
  [MODEL_WAIT_ACTUAL.md](docs/MODEL_WAIT_ACTUAL.md) and
  [MODEL_SUBCOMMAND_UPDATE.md](docs/MODEL_SUBCOMMAND_UPDATE.md).

- Execute ordinary MODELCTRL entries against actual mutable sequence and command
  owners, preserving first-name lookup, list aliases, selected data and the
  captured work generation. Cleanup, direct JUMP, command-loop return and player
  `+B8` run in their original order. Required external services retain explicit
  boundaries. See [MODEL_SEQUENCE.md](docs/MODEL_SEQUENCE.md) and
  [MODEL_DISPATCH.md](docs/MODEL_DISPATCH.md).

- Dispatch defined ACTION programs through the native messenger, retaining
  animation/service owners, waits and incomplete broadcasts. Existing ACTION
  queries keep their own behavior. MODELCTRL timers resolve current manager
  context at each helper; the existing ACTION runtime retains the timestamp of
  each accepted call across host retries. See [MODEL_TIMER.md](docs/MODEL_TIMER.md) and
  [ACTION_DISPATCH.md](docs/ACTION_DISPATCH.md).

- Query original PLAYERPOINT groups through the native messenger and apply the
  original vector/matrix placement rules to the actual model node. Shared node
  matrices survive retained work owners, while candidate motion updates remain
  independent until publication. These helpers are connected to fresh player-work
  allocation and the retained GENERATE caller. See [PLAYER_POINT.md](docs/PLAYER_POINT.md),
  [MODEL_PLACEMENT.md](docs/MODEL_PLACEMENT.md) and [PLAYER_SPAWN.md](docs/PLAYER_SPAWN.md).

- Complete the prepared post-GETPLAYER startup through all five original
  number/meter messages and the map-dependent metal/main-lobby tail. NUMBERCTRL
  mutates actual digit UV owners, METERCTRL preserves shared HP/MHP bindings and
  node animation times, and PLAYER SETMETAL retains the real status lists and
  nested GAMERAM row. Independent original-instruction comparisons cover the
  caller and recipients. The walking preview remains the existing original-asset
  walking profile; these changes do not yet draw its HUD or create a complete
  gameplay session. See [MODEL_PLAYER_STARTUP.md](docs/MODEL_PLAYER_STARTUP.md),
  [NUMBER_CONTROL.md](docs/NUMBER_CONTROL.md), [METER_CONTROL.md](docs/METER_CONTROL.md)
  and [MODEL_STATUS.md](docs/MODEL_STATUS.md).

- Traverse common weapon children through their actual ordered attribute scopes,
  shared geometry owners and stored transforms. Render body and weapons with
  separately captured model-views and one shared depth/MSAA target. Original
  ACTION and PLAYERARMS messages run on the same prepared actor that is drawn.
  This uses an explicit reset graphics baseline and the host inspection camera;
  complete original materials, compiled traversal and the game frame remain
  pending. See [SCENE_TRAVERSAL.md](docs/SCENE_TRAVERSAL.md),
  [PREPARED_RENDERING.md](docs/PREPARED_RENDERING.md) and
  [WEAPON_PREVIEW.md](docs/WEAPON_PREVIEW.md).

- Decode original weapon node/bone/sequence graphs, preserve shared loaded scene
  owners, and execute prepared PLAYERARMS GENERATE/REMOVE through real PLAYER,
  ACTION and MODELCTRL endpoints. Four original Agumon slots pass deferred
  generation and two-hand weapon changes across all twelve appearance branches.
  The common bone-selector calculation is independently compared against original
  instructions. Original frame scheduling and gameplay remain
  required. See [WEAPON_RUNTIME.md](docs/WEAPON_RUNTIME.md),
  [ACTOR_SCENE.md](docs/ACTOR_SCENE.md) and [BONE_SELECT.md](docs/BONE_SELECT.md).

- Retain original deferred message packets, signed deadlines, pause shifts and
  callback order through missing-service interruptions. The prepared player
  caller resets its modifier prefixes, starts unused data, queues PLAYERARMS
  and recalculates stats. Its original UI and map tail now continues through
  the recipient owners described above and the actual player constructor.
  The original outer loop remains required.
  See [DEFERRED_MESSENGER.md](docs/DEFERRED_MESSENGER.md) and
  [MODEL_PLAYER_START.md](docs/MODEL_PLAYER_START.md).

- Initialize selected player data through the original ordered GAMESTART path,
  retaining database calls and shared objects across interruptions. Four Agumon
  player slots complete local integration with the supplied original databases
  and an explicitly prepared font. Complete startup and gameplay
  scheduling remain required. See [GAME_START.md](docs/GAME_START.md),
  [GAME_ITEMS.md](docs/GAME_ITEMS.md), [GAME_STATS.md](docs/GAME_STATS.md) and
  [GAME_NAME.md](docs/GAME_NAME.md).

- Decode the original six database column groups and retain their typed owners
  through DATABASE registration and GETDATABASE. GAMERAM resolves weapon or
  technique skills before the original no-growth gate, including a retained
  pcTech query. Actual skill growth and damage effects remain required.
  See [DATABASE.md](docs/DATABASE.md),
  [DATABASE_RUNTIME.md](docs/DATABASE_RUNTIME.md) and [GAME_RAM.md](docs/GAME_RAM.md).

- Construct the original common-data list and four separate player-data rows,
  preserve shared objects across resets, and route GETPLAYER/GAMEDATAINIT through
  the native GAMERAM. Selected original Hit scripts reach its verified magic
  dispatch gates; skill growth and actual damage/status effects remain required.
  See [GAME_DATA.md](docs/GAME_DATA.md), [GAME_RAM.md](docs/GAME_RAM.md) and
  [MODEL_SCRIPT_MESSENGER.md](docs/MODEL_SCRIPT_MESSENGER.md).

- Execute selected target-Hit chunks in one shared Lua VM, retain incomplete
  native calls and nested scripts, and apply the original conditional HP merge.
  Explicit script registration and model work remain prerequisites; damage
  services and original per-frame collision/script integration are still required. See
  [MODEL_SCRIPT_RUNTIME.md](docs/MODEL_SCRIPT_RUNTIME.md).

- Route original HITBOX queries to source-qualified target bodies, preserve
  timestamp-based box updates and resume nested record/hit-script operations.
  Original damage services and per-frame collision integration remain required.
  See [MODEL_COLLISION.md](docs/MODEL_COLLISION.md) and
  [MODEL_HIT_BODY.md](docs/MODEL_HIT_BODY.md).

- Run original HITBOX Set/AllClear commands in the same ordered queue as sounds
  and effects, and send their checked boxes through the original messenger.
  MUTEKI fields and LUACALL callback assignment retain their original sequence
  behavior. Complete collision managers and damage/status script services remain
  required before combat works. See [MODEL_HITBOX.md](docs/MODEL_HITBOX.md) and
  [COLLISION_MATH.md](docs/COLLISION_MATH.md).

- Decode original WCHECK occurrences, retain each player's attack wait/latch,
  and resume the live sequence cursor through qualified ACTION queries.
  The actual player-frame input/wait caller and direct ordinary entry are
  connected in the core; device assignment and connected multiplayer remain
  pending. See [MODEL_WAIT.md](docs/MODEL_WAIT.md) and
  [MODEL_WAIT_ACTUAL.md](docs/MODEL_WAIT_ACTUAL.md).

- Bind an original MODELCTRL/ACTION instance through its actual node and actor
  graph, route loaded Se/ActionStarter consumers across source files, and retain
  delayed sound/effect work through service suspension and sequence cleanup.
  Original audio/effect generation managers and the full game frame remain pending.
  See [ACTION_EVENT_RUNTIME.md](docs/ACTION_EVENT_RUNTIME.md).

- Play the original `dng101b1` / `BG02` map with camera-relative movement,
  original floor following, automatic idle/run animation, pause and restart.
  See [PLAY_PREVIEW.md](docs/PLAY_PREVIEW.md) for controls and current limits.
- Validate GameCube headers and DOL sections; identify the USA release.
- Index both AFS archives without loading the entire large files into memory.
- Browse archive entries and read bounded byte previews.
- Read Alchemy IGB v6 metadata, serialized objects, memory buffers and references.
- Resolve logical game paths from `ndmw.igb` to checked AFS archive entries.
- Read named package descriptors, preserve their ordered members and `/s`
  loading-stage metadata, and inspect explicitly selected member exports.
- Read exported embedded Lua records and resolve scheduled script names with the
  original first-match-per-info rule, preserving multiple registered matches.
- Execute original `func.lua` and `boot.lua` with persistent Lua globals and
  explicit player/difficulty/camera inspection inputs. Build the original
  common-resource queue, preserve exact duplicate semantics and resolve requests
  using the original ASCII lowercase lookup.
- Keep the successful Lua state and all initial/common IGB documents alive in
  an owned inspection session, with source-qualified export routing. Its
  aggregate routes span stages and are not the original later registration queue.
- Load only preliminary `/s` members before ordinary package files and boot.
  Initialize supported StaticInfo/SwitchCtrl callbacks into owned RAM, manager
  and messenger state; stop the ordered callback prefix at an unknown handler.
  See [CONTROLLERS.md](docs/CONTROLLERS.md).
- Process four independent original pad states in the portable core, with
  recovered button edges, stick directions and repeat timing. Device assignment
  and an original multiplayer gameplay session remain pending.
- Advance original six-mode motion objects with preserved speed, expiry,
  zero-frame arithmetic and a shared original elapsed timer. Decode MOVE/MOVEEX
  records and apply local model-basis translation. See [MOTION.md](docs/MOTION.md).
- Apply recovered Euler turning before local movement, preserving arc/timer
  rules, matrix arithmetic and independently owned model state. Original
  per-frame input, movement and collision integration remain pending; the preview
  still uses its provisional controller. See [MODEL_MOTION.md](docs/MODEL_MOTION.md).
- Decode CTRL/PDMOVE commands, prepare movement timers and heading, and apply
  the original post-input WAIT/WALK request rules. Player turn rates are recovered;
  the recovered player-frame caller consumes these operations in the core.
  Original device polling and outer frame scheduling remain pending. These core
  effects do not change the preview. See [MODEL_CONTROL.md](docs/MODEL_CONTROL.md)
  and [MODEL_FRAME.md](docs/MODEL_FRAME.md).
- Run supported original sequence commands with retained cursors and label caches.
  Preserve weapon-section boundaries, nested local jumps and sequence-stop motion.
  Actual entries send ACTION messages to retained action runtimes and complete
  the player callback after a real loop return. Original frame scheduling remains
  pending. See [MODEL_SEQUENCE.md](docs/MODEL_SEQUENCE.md).
- Decode original indexed8/RGBA8 and BC1 textures into native RGBA pixels.
- Browse textures with zoom, pan, filtering and an alpha checkerboard.
- Decode original triangle lists and indexed triangle strips from IGB geometry.
- Inspect original models in 3D with orbit, pan, zoom, solid and wireframe modes.
- Resolve explicit local or inherited texture bindings and show supported meshes
  with their original UVs, base image and verified linear/repeat sampler.
- Read verified single-influence skin weights, joint palettes and inverse-bind
  matrices; inspect the joint hierarchy and manually pose supported meshes.
- Decode bounded Enbaya animation streams and inspect supported original clips
  with time seeking and playback, including helper-bone transforms.
- Sample decoded clips through recovered integer animation clocks and shared
  source caches; prepare original blend weights and mix ordered bone layers.
  See [ANIMATION_TIMING.md](docs/ANIMATION_TIMING.md).
- Read prepared original animation combiners, retain independent state clocks,
  retire completed transitions and resume interrupted blends from the last cached
  pose. Apply explicit MOTIONSET and motion-update effects to a resolved clip.
  Actor-aware execution and the prepared preview use these owners; additional
  original combiner and material profiles remain pending.
  See [ANIMATION_COMBINER.md](docs/ANIMATION_COMBINER.md).
- Execute prepared ACTION2 programs with original motion, joint, fade, wait and
  flash commands, same-action suppression, named timers and freeze behavior.
  Four original Agumon sequence instances drive their own combined poses in core
  integration tests. Prepared actor groups now allocate all ordered actor buffers
  and release them through the original NDSP path, preserving node clocks for
  reactivation. Ordinary actor traversal and cached bone queries now use the
  original shared hierarchy. Loaded event consumers and ordinary appearance
  traversal are connected for verified profiles; complete original rendering
  and frame scheduling remain pending. See [ACTIONS.md](docs/ACTIONS.md) and
  [ACTION_ACTORS.md](docs/ACTION_ACTORS.md).
- Evaluate the original no-modifier bone/joint hierarchy with ordered float
  matrix operations, helper bones, retained cache validity and node-adjusted
  traversal time. See [ANIMATION_HIERARCHY.md](docs/ANIMATION_HIERARCHY.md).
- Render an explicitly selected actor's main skin using its original shared
  palette caches and recovered single-influence CPU deformation. The captured
  root is applied once before projection. This prepared clip view retains a
  host inspection camera. See [ACTOR_SKIN.md](docs/ACTOR_SKIN.md).
- Traverse verified static scene exports with original transforms, ordered
  switch selection and separate instances for shared graph paths.
- Inspect placed map geometry under one camera and depth target, with explicit
  root-child inspection for stored disabled exports and per-instance textures.
- Apply the verified `ALLON` or `ALLOF` command of an exported switch controller
  to its selected scene root, with command provenance and unchanged source bytes.
- Follow an explicitly selected player model instance and appearance branch into
  its original skin, then replace the outer model transform with a checked map
  entry-point matrix. Animate that visual alongside the map in one depth target.
- Focus the inspection camera on the placed visual, select its original clips,
  seek, play/pause and update only its GPU vertex range during playback.
- Render a bounded offscreen mesh viewport with depth testing and optional MSAA.
- Decode and display the original 96×32 banner from `opening.bnr` natively.
- Configure workbench window resolution, display mode, VSync and frame limit.
- Request OpenGL MSAA and report the actual sample count provided by the driver.
- Inspect SDL-detected gamepads and live input.
- Save settings locally and validate malformed configuration values.

Graphics settings currently affect the **walking preview, development window and asset viewers**. Saved gameplay,
aspect ratio and controller preferences are preparation for future work;
they do not implement game features. A 144 FPS workbench does not establish
that DW4 gameplay runs correctly at 144 FPS.

## Build on Windows

Requirements: Visual Studio with **Desktop development with C++**, Windows SDK
and CMake/Ninja. Visual Studio Insiders installations are supported. The first
application build downloads two open-source dependencies pinned by commit and SHA-256.

```powershell
git clone https://github.com/Dekirai/DW4_PC.git
cd DW4_PC
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Package
```

The script builds and tests the project. The application package is written to
`dist/DW4_PC/`. Run `Start-DW4-PC.cmd` to launch it using the local configuration
in `local/settings.ini`. On first launch, enter your extracted game directory
under **Original data** and select **Remember directory**. Alternatively:

```powershell
.\scripts\run.ps1 -Data 'E:\path\Digimon World 4 - Extracted'
```

Select **Play walking preview** on the Overview page. Move with **WASD**, arrow
keys or a gamepad's left stick; right-drag to orbit, use the wheel to zoom,
**Escape / Start** to pause and **R** to restart. To launch directly:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --play
```

You can also build manually in an x64 developer shell:

```powershell
cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release -DDW4_BUILD_APP=ON
cmake --build build/Release --parallel 4
ctest --test-dir build/Release --output-on-failure
cmake --install build/Release --prefix dist/DW4_PC
```

Build only the portable core, inspection tools and tests, without application
dependency downloads:

```sh
cmake -S . -B build/core -DDW4_BUILD_APP=OFF
cmake --build build/core --config Debug --parallel 4
ctest --test-dir build/core -C Debug --output-on-failure
python -m unittest discover -s tests -p "test_*.py" -v
```

## Run with your own game data

The current native application expects this extracted GameCube data layout:

```text
Digimon World 4 - Extracted/
  sys/boot.bin
  sys/main.dol
  sys/fst.bin
  files/area.afs
  files/ndmw.afs
  files/ndmw.igb
  files/opening.bnr
```

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted'
.\dist\DW4_PC\dw4_inspect.exe 'E:\path\Digimon World 4 - Extracted' --json
.\dist\DW4_PC\dw4_inspect.exe 'E:\path\Digimon World 4 - Extracted\files\area.afs' --list
.\dist\DW4_PC\dw4_catalog.exe 'E:\path\Digimon World 4 - Extracted' --find agumon
```

Open **Textures** to choose a logical resource or a raw archive entry. These
examples display the original font atlas and an Agumon card, respectively:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --resource bectext.igb
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --archive area.afs --entry 621
```

Open **Meshes** for native 3D inspection. This example loads Agumon's high-detail
geometry from the original archive with its bound original texture; the second
command shows its wireframe:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --resource _common/p_agumon.igb --mesh 2 --mesh-mode wireframe
```

Drag to orbit, right-drag to pan, and use the wheel to zoom. **Fit** resets the
inspection camera. **Textured** applies the image reached through a verified
single-stage binding. The viewer reports unsupported or ambiguous bindings;
it never chooses an image by filename or list order. **Solid** is the default
plain inspection shader. The default view shows stored positions. Enable **Inspect
rig**, then use the left **Rig** tab for a supported skeleton, an X-ray joint
overlay and manual joint rotation. The reference is reconstructed from the original inverse-bind matrices;
manual rotation is an inspection control, not a decoded game animation.
This is not a playable character or loaded level.

The following example rotates the `LArm01` joint in Agumon's left arm while keeping the
original image, weights and camera normalization:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --rig --bone 8 --rotate-z 45
```

This sample contains 25 bones and 20 joint matrices; five helper bones have no
inverse-bind frame and cannot be manually posed by this profile. The animation
path uses the full hierarchy, including those helpers. In the **Rig** tab, choose
**Animation** to select an original clip, play/pause or seek. For a reproducible
paused inspection:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --animation runD --animation-time 0.2
```

All 82 Agumon clips decode under the current profile. To render through the
prepared original actor's shared hierarchy and palette caches, select its
ACTION2 info, RAM slot and ordered actor explicitly:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' `
  --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured `
  --actor-info 546 --actor-ram 0 --actor-index 1 `
  --animation waitS --animation-time 0.4 --animation-play
```

This selects Agumon's high-detail main skin. The geometry must belong to the
selected actor; mismatched selections reject. Clip playback uses original
node/source clocks, cached float hierarchy and CPU skin arithmetic, with an
explicit host fitting transform and inspection camera. Action dispatch/events,
automatic LOD and original-frame comparison remain separate work. See
[ACTOR_SKIN.md](docs/ACTOR_SKIN.md).

Open **Scenes** for static map inspection. The following example selects the
`BG02` export in `ndmw.afs[179]` and explicitly inspects child zero of its disabled
root switch:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --archive ndmw.afs --entry 179 --scene-root 6 --scene-inspect-child 0 --scene-mode textured
```

This subtree contains 235 geometry instances placed through 219 transforms.
The amber inspection label identifies the root override; nested switches retain
their stored masks. Omitting `--scene-inspect-child` preserves the empty stored
selection. This is a view of the original map assets, not a loaded gameplay
level. The page exposes source paths, matrices and unsupported-state diagnostics.
See [SCENE_VIEWER.md](docs/SCENE_VIEWER.md) and [SCENES.md](docs/SCENES.md).

The same map can now be reached through its original named package and enabled
through the controller targeted by the original `first.lua` command:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' `
  --package dng101b1.igb --package-member 1 --scene-root 1 `
  --scene-switch-control 3471 --scene-switch-command all-on --scene-mode textured
```

Package scene indices follow the explicit info-list order, so this is index 1
instead of the raw-entry index 6. The **State** tab provides **ALLON**, **ALLOF**
and **Stored selection**. This executes the verified native child-selection
effect for one controller; this scene view does not run original scripts or game startup.
Use `dw4_catalog EXTRACTED_ROOT --package dng101b1.igb --member 1` to inspect
member/export provenance without opening the graphics app. See
[PACKAGE_VIEWER.md](docs/PACKAGE_VIEWER.md) and [SCENE_CONTROLS.md](docs/SCENE_CONTROLS.md).

To add an animated Agumon visual at the first explicitly selected entry point:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' `
  --package dng101b1.igb --package-member 1 --scene-root 1 `
  --scene-switch-control 3471 --scene-switch-command all-on --scene-mode textured `
  --scene-player-resource _common/p_agumon.igb --scene-player-model 1386 `
  --scene-player-slot 0 --scene-player-branch 1 `
  --scene-player-point 115 --scene-player-group 0 `
  --scene-player-animation waitS --scene-player-play
```

The **Player** tab exposes the model, placement and clip selectors. **Focus
player** recenters the camera; **Fit** shows the map. These are explicit
inspection choices, including the entry group and appearance branch. The
application does not execute the original player-generation command or choose a
gameplay spawn automatically. One visual is supported at a time; slot 0–3 selects
the original model instance and matching point, without creating a multiplayer
session. See [PLAYER_VIEWER.md](docs/PLAYER_VIEWER.md).

Open **Startup** to execute a package's first two scheduled scripts. This runs
the original Lua bytecode through native C++ code, with a strict set of resource
queue and state-query bindings. The default inspection inputs have no joined
players, difficulty 0 and camera mode 0; they are not imported save data.

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' `
  --boot-package dng101b1.igb

.\dist\DW4_PC\dw4_boot.exe 'E:\path\Digimon World 4 - Extracted' `
  --package dng101b1.igb --boot-player-0 p_agumon `
  --boot-weapon-0-0 w_sword01 --boot-weapon-0-1 w_gun01
```

The first example produces 94 distinct resource requests. The second explicitly
configures one joined player with two weapon names and produces seven. Original
USA country, area and pack-only methods keep their verified constant return
value 1. The queue's exact request spelling is retained, while catalog lookup
lowercases ASCII A–Z as the original resolver does. Every ordered host call and
queue entry for three test profiles matches an independent official Lua 4.0.1
reference run.

The inspection session also loads and retains every requested IGB document. The one-player
example owns nine files containing 454 exports; the original routing rules
produce one Lua info, 17 held scenes and 436 aggregate deferred infos. The Startup page
exposes Resources, Loaded files and Info routing views. The actual Lua state
and its function definitions remain alive alongside the loaded documents.

This inspection bypasses preliminary initialization and the sound-transfer gate,
which the original performs before func/boot. Its aggregate queue must not be
replayed as the original later registration queue. The original loading-screen
state, remaining runtime managers and layer/text initialization are still pending.
`start.lua` and `first.lua` do not run; this page
does not automatically create a player or turn the scene inspector into gameplay.
Unknown native effects stop explicitly. See [STARTUP_VIEWER.md](docs/STARTUP_VIEWER.md)
and [STARTUP_LOADING.md](docs/STARTUP_LOADING.md).

To exercise only the recovered preliminary loading and callback boundary:

```powershell
.\dist\DW4_PC\dw4_boot.exe 'E:\path\Digimon World 4 - Extracted' --package dng101b1.igb --preliminary
```

This command supplies a fresh prepared system/messenger callback context and
reads only `/s` members. On the original package it initializes StaticInfo and
stops at the next Sound callback, preserving the completed native effects.
Exit code 3 identifies the remaining native barrier; no ordinary files, func/boot
or common resources are executed. Full original manager construction and audio
services are separate prerequisites.

All 2,274 recognized original IGB documents passed native object loading. All
25,906 `igImage` objects found in the two archives decoded without errors.
Those totals include repeated asset instances and verify loading, not each
image's appearance against the original renderer. The viewer currently decodes
base images; mipmaps and full game material reconstruction remain separate work.

With Python 3.10+, you can also compare the extracted files against the
unmodified ISO. This read-only tool outputs a JSON verification report:

```powershell
python tools/verify_game.py 'E:\path\Digimon World 4 - Extracted' --iso 'E:\path\Digimon World 4 (USA).iso'
```

Reference build: `GDJEB2`, DOL SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.
The verified disc header has revision byte **0**, while the reference README
calls its supported version "Rev 1". The DOL hash matches exactly. The workbench
performs structural checks; the Python verifier additionally checks this hash.

## Next milestones

Original textures, logical asset lookup and object-space mesh viewing are
available, including verified base-texture bindings, original clip decoding and
static map inspection through named packages and original switch commands, plus
explicitly placed animated player visuals. Original `func.lua` and `boot.lua`
execute natively in the inspection path. Preliminary loading and the first
controller callbacks now have separate native implementations. Actual player
construction, ordinary sequence execution and its player callback are connected
in the core. The immediate next steps are the original dynamic collision bounds
and inverse, complete wall/floor response selection and delivery into the player
frame. Continue with original manager preparation, audio and layer/text services,
`start.lua`, device assignment, frame scheduling and camera/render integration.
Original-reference comparisons and independent controller slots are required
throughout. Reconstructing
the missing gameplay and engine logic is the largest remaining part of the project.

[Roadmap and acceptance criteria](docs/ROADMAP.md) ·
[Architecture](docs/ARCHITECTURE.md) ·
[Resource catalog](docs/RESOURCES.md) ·
[Package loading](docs/PACKAGES.md) ·
[Native texture formats](docs/TEXTURES.md) ·
[Mesh formats](docs/MESHES.md) ·
[Material bindings](docs/MATERIALS.md) ·
[Skinning](docs/SKINNING.md) ·
[Animation loading](docs/ANIMATIONS.md) ·
[Original animation timing](docs/ANIMATION_TIMING.md) ·
[Static scenes](docs/SCENES.md) ·
[Original scene commands](docs/SCENE_CONTROLS.md) ·
[Player visuals](docs/PLAYER_VISUAL.md) ·
[Player placement](docs/PLAYER_PLACEMENT_RESEARCH.md) ·
[Original startup](docs/STARTUP_RESEARCH.md) ·
[Original movement core](docs/MOTION.md) ·
[Embedded scripts](docs/SCRIPTS.md) ·
[Native Lua 4](docs/LUA4.md) ·
[Native renderer](docs/RENDERER.md) ·
[Upstream source audit](docs/UPSTREAM_AUDIT.md) ·
[Milestone validation](docs/VALIDATION.md)

The reference [ivanno4317/dw4-gc](https://github.com/ivanno4317/dw4-gc) is an
ongoing GameCube decompilation. It provides useful symbols and reconstructed
libraries, but does not currently contain complete gameplay source.
Dependencies, provenance and implementation boundaries are recorded in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
