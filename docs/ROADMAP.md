# Roadmap and acceptance criteria

The selected implementation is a complete native source port: reconstruct the
original game and engine behavior in authored C++20 using the existing
`dw4_core` and native application. The owner set DolRecomp/ModernGekko aside on
September 18, 2026. Its packages, source patches, imported data and saves remain
preserved, but its tests do not establish features in the native engine.
A successful build, decoded record or settings control alone does not establish
playable behavior. The full original game, PC features, modding, online co-op and
simple ROM-driven release experience remain required.

| Gate | Deliverable | Acceptance | Current evidence |
| --- | --- | --- | --- |
| 1 | Complete original native game and source | Recovered game and engine source drives original startup, menus, gameplay, combat, progression, transitions, audio and saves; independent local 1–4-player controls; behavior compared against the original | A prepared neutral original player frame completes native CHECKLAND/CHECKWALL through HITFLOOR/HITWALL, with all five stage collision infos, the recovered global clock and verified transform updates. Complete dynamic collision, CHECKWALLFULL, startup and outer scheduling remain unfinished. The walking preview has provisional movement and camera. |
| 2 | Performance and PC graphics | At least 60 FPS on documented hardware; options up to 300 FPS without faster simulation/audio; validated frame times, 16:9 camera/culling/HUD, 4K, display modes, VSync and MSAA | Original assets render through the SDL/OpenGL preview and prepared draws. Complete original materials, camera, rendering and game timing remain pending; preview frame rates cannot qualify full-game performance. |
| 3 | ImGui settings and QoL | Working persistent graphics/input controls, freely editable keyboard bindings, optional cheats, save/load states, supported game languages and original/modernized options; applied settings and unsupported combinations visible | The native workbench has ImGui and provisional walking input. The integrated F1 gameplay menu, persistent freely editable bindings, original game input, cheats, states and supported-language selection require native implementation and verification. See [LOCALIZATION.md](LOCALIZATION.md). |
| 4 | Content modding | HD texture replacements, versioned loader/manifests, custom maps, Digimon and weapons, reproducible randomizers, optional rebalancing and bug fixes | Original asset identities and selected decoders are available. Full native content contracts, loading and gameplay integration remain pending. |
| 5 | Online four-player co-op | Multi-machine sessions with consistent state, input ownership, latency/disconnect handling, matching mods and clear save ownership | Pending. Four separate core player/pad owners are not connected local multiplayer. Verify local play before network integration. |
| 6 | Simple EXE release | User selects their ROM; supported revision is validated and required assets prepared automatically, without a compiler or manual extraction; repeat launches reuse validated local data; portable saves beside one EXE entry point | The active native application reads supplied extracted data. Automatic ROM preparation, native gameplay save handling and clean-machine release acceptance remain pending. Historical recompilation import/profile tests are reference evidence only. |

Gate order sets the immediate queue, not an excuse to design out later features.
The final native application must provide one `DW4_PC.exe` entry point with
user-provided assets and portable save data beside it. The in-game ImGui menu
must open with **F1**, receive the current File/View commands and apply supported
settings while playing. These remain native integration requirements. Do not
count the set-aside package's menu, import or saved profiles as their completion.
Preserve every existing save and use explicit migration where formats differ.

Keep rendering enhancements separate from simulation speed; a speed multiplier,
duplicated presents or an FPS slider alone cannot satisfy the 300 FPS target.
Higher internal render resolution and higher-resolution texture content also
have separate acceptance checks. Report hardware, scene, settings and measured
frame times with each performance claim; 300 FPS is a target, not a guarantee
for every device or scene.

## Next acceptance gate

The [porting strategy](PORTING_STRATEGY.md#ordered-continuation-plan) turns this
gate into ordered native implementation checkpoints. Resume the existing
collision and player-frame integration. Do not return to recompilation
optimization or packaging as a substitute for recovering original source.
Aurora remains an optional platform candidate, not an implemented migration.

Version 0.45 connects actual HITLANDMODEL static geometry selection, typed
visitor dispatch, mapped primitive enumeration and candidate publication.
The original player frame still reaches the required HITLANDMODEL recipient
through its outer wall/floor consumer and HITLANDDELIVER. The native receiver
now handles CHECKLAND and CHECKWALL; a prepared neutral original frame completes
with every stage collision info included, using the recovered long timer and
verified null-input transform updates. Nonnull transform inputs, other update
bindings, missing geometry bounds and CHECKWALLFULL remain collision
requirements. The September 19 updates also implement
canonical AABox transforms and the original matrix inverse. See
[HITLAND_WALL.md](HITLAND_WALL.md), [MATRIX_INVERSE.md](MATRIX_INVERSE.md) and
[HITLAND_FLOOR.md](HITLAND_FLOOR.md) for measured scope.
Runtime class registration in tests is
explicit preparation; original manager startup remains required.
Four original Agumon GENERATE calls and the prepared neutral frame form the local
original-data acceptance scope. Results and explicitly prepared prerequisites
are tracked in [VALIDATION.md](VALIDATION.md).

The prepared directional frame now resumes through SQ_WALK/RUNND1 using the
native viewer owner, inverse and original finite heading calculation. Another
48 per-RAM updates qualify held movement and release for all four prepared
slots, actual system-time consumption, SQ_WAIT/WAITD1 return and independent
node/subcommand changes. The viewer's proven initial matrix defaults are zero;
these tests do not select or publish an active original camera.
The application time prefix, system time leaf and concrete serial scheduler
batch are recovered. The common manager traversal now invokes the actual
player frame through its authored info/RAM lists; seven prepared serial
batches complete 28 player calls, including suspension inside the second RAM.
The camera's specialized view inverse is available separately. Original
registration/order, dispatcher, other manager bodies and complete camera
publication still need connection. See [MODEL_MANAGER_FRAME.md](MODEL_MANAGER_FRAME.md),
[CAMERA_MATH.md](CAMERA_MATH.md), [VIEWER.md](VIEWER.md), [SERIAL_SCHEDULER.md](SERIAL_SCHEDULER.md),
[PLAYER_INPUT.md](PLAYER_INPUT.md) and [APPLICATION_FRAME.md](APPLICATION_FRAME.md).

The scheduled system now supplies its original reset/time/stage/storage/tail
orchestration. The same seven four-player batches enter through the application
and actual system callback; time is published inside that callback, and pending
player work does not replay it. Concrete loader, context registration, viewer
error-scene operations and real host storage/reset policy remain required.
The real initialization requests `ndmw.igb`; null factory stage fields in
prepared tests do not qualify original boot. Continue through that requested
stage and its actual operations. See [SYSTEM_FRAME.md](SYSTEM_FRAME.md),
[SYSTEM_STAGE.md](SYSTEM_STAGE.md) and [SYSTEM_STORAGE.md](SYSTEM_STORAGE.md).

Directory lookup, claim acquisition/release, request control flow and context
info dispatch now have native implementations connected through the stage
adapter. The prepared cached-directory test reaches real registration and
zero-claim cache removal, with a retained pending second handler. This is not a
new-file boot acceptance. The `800528DC` caller now connects actual stream/header,
the first three tables and `8004FC94` runtime type/slot reconciliation. With
twenty recovered object layouts, forty-seven field-class identities and prepared
list endpoints, original `ndmw.igb` resolves twenty types and sixty fields.
Directory-path preparation and dependency-table processing now connect native
ArkCore loader access, string-table interning and synchronous dependency requests.
With explicitly prepared loader/string-pool allocation state, the supplied prefix
enters the native `igIGBFile` factory `8002EBEC` and reaches its required class
registration `8002F178`. The actual runner and prerequisite once guard now
compose ThreadManager registration `800238A8`, handle-based pool selection
and metadata factory `80029E64`. With the existing enabled runtime preserved,
the next reached boundary is `igMetaObject` registration `8002A0A8`.
Generic type registration, callback-list operations, parent-field inheritance
and metadata-name assignment are reconstructed. Raw child/field list append
and growth now preserve original allocator queries, zeroing and count stores;
field descriptor registration, registry publication and general layout remain explicit.
Concrete int/bool field defaults now write through the native initializer.
Typed metadata and internal-field-list constructor chains preserve physical
headers, exact defaults and separate backing allocation; allocator and
reflective registration endpoints remain explicit. The supplied-data prefix
does not substitute a completed cold bootstrap for its enabled runtime.
The actual ArkCore constructor/bootstrap caller now enters metadata registration
while disabled. A separate composed test constructs and publishes canonical
busy metadata, then enters actual `igObject` parent registration, root-parent
callback construction, native name interning and layout probe. The two specialized
field defaults `80066B7C` now execute their real constructor, name assignment,
default-buffer preparation, finalization and append. Actual type publication
`800660AC` and field layout `80065A58` now complete the base registration; actual
child-list construction and inherited field append enter the `igMetaObject`
constructor probe `80029FFC`. Its actual reset `80065218` now completes, without
creating a field list, and the probe publishes type word `804761D8`. The same
cold chain now completes defaults `8002A164` and native field batch `80065924`
using the actual seven descriptor profiles reached across base and local fields.
All 25 local descriptors join the two inherited fields; native value setters,
schema configuration, layout and publication finish `igMetaObject` with flags
`7` as the second published type. Two lazy metadata/list globals are constructed
through the original allocation and constructor callers. The actual deferred
callbacks `8002DD18`, `8002E054`, `80066AE0` are appended in that order.
The first now executes `8002DD18 -> 8002DD40 -> 8002E07C`, inherits the two
base fields into non-reference-counted list metadata `805619D4`, and runs
actual probe `8002E020 -> 800536F8`. Defaults `8002E108` construct two local
descriptors through memory-reference factory `8002AC34` and signed-integer
factory `8002E1AC`, bringing the chain to 29 constructed descriptors. Getter
`80028BFC` then enters actual object-field registration `800289F8`, recursively
registering the base and reference field classes. Their original defaults
`8002A764`, `800273EC`, and `80028AB4` construct 23 more descriptors through
native batch/schema and value operations. The ten reached descriptor profiles
have native constructors, including signed-short and signed-byte fields.
The three field classes complete inheritance, schema, layout and publication:
the chain contains 52 constructed descriptors, 50 default buffers and five
published types. Abstract prototype callback `80027454` completes its actual
null reflected-factory branch. Object callback `80028B64` now completes the real
reflected factory and `800667F4` with a canonical field binding, actual reached
field passes, string defaults and object-field tail `8006A438`. The prototype
is published at `805616EC`, appended to its native registry and finalized with
ordinal and type ordinal zero. Callback cleanup returns to the list chain;
parent metadata `805619D4` finishes with flags `7` as the sixth published type.
Its native name/index lookup uses the actual published `805617C4` descriptor
and enters the inherited memory-reference clone virtual `+54`, argument 1
(`80476088` vtable). The actual cold chain
`8002ACD8 -> 8006331C -> 8006CC88 -> 800639E4` now completes, including
separate default-buffer allocation/copy. Original clone finalization and
replacement finish child metadata `805619CC` with flags `7` as the seventh
published type; parent `805619D4` also retains flags `7`. The composition now
contains 53 schema descriptors plus one reflected prototype, 54 default buffers
and one prototype entry. The actual bootstrap caller enables reflection and
snapshots type/prototype counts 7 and 1 into core `+04/+08` before reaching
`igMetaEnum` registration body `8002AAFC`, canonical cell `80561808`.
That body now enters its actual prerequisite guard and ThreadManager registrar.
The enabled reflected metadata factory now executes its full field passes,
including positive object-member construction of an internal field list and
the list's actual memory-member allocation/zero fill. The returned metadata
feeds original registration and inherited field append. ThreadManager defaults
`80023940` now create both reflected object fields and two lazy metadata/list
pairs, publish the schema/redirect and append bootstrap callback `80023A50`.
ThreadManager finishes as type eight with flags `7`; there are 55 schema
descriptors and 56 default buffers. Deferred thread-list registration `80023F28`
now enters actual non-reference-counted object-list and DataList registration.
The signed-integer field class and its reflected prototype complete before
DataList's third field enters memory-field registration `8002AEAC`. Its native
probe and eight-field defaults now execute the reached Boolean registration,
scalar probe and reflected prototype. Seven local descriptors are constructed
before the final factory enters string-field registration `80025404`.
Its native probe and defaults construct a Boolean local before the inherited
signed-byte clone. Native signed-byte registration and its reflected prototype
now complete. Enabled clone `800680B4` performs actual reflected allocation and
`80068060 -> 80067B60` with the original argument one. The first seven copied
members now feed actual memory-buffer copy `80062824` and its allocation/ownership
helpers. Both inherited string replacements, the complete string schema and its
reflected prototype finish in this same cold invocation. The eighth memory field
and inherited shallow-copy replacement now lead through enum factory `80031378`.
Actual enum/raw-reference registration, probes, defaults and prototypes finish,
then the memory reset descriptor, memory schema and memory prototype complete.
Unsigned-byte registration, its exact probe and reflected prototype now complete.
The same retained getter resumes the DataList schema. Native reflected child-list
initialization and the memory-field clone complete the object-list and thread-list
registrations. Deferred `igThread` wrapper `80023C40` now executes its actual
`igNamedObject` parent and `_name` defaults. Unsigned-integer registration
`80022CD8`, its probe and reflected prototype now complete, allowing Thread's
ten-field defaults/schema and semaphore registration `80026098` to finish.
ThreadManager startup `80023A30 -> 800755F8`, its manager factory and concrete
manager metadata redirect now execute in the retained cold test. Concrete
registration completes as the twenty-fourth type. Distinct runtime owners now
enter actual manager allocation and inherited thread-list construction. Native
memory defaults complete the list and its ownership/publication in the manager.
Recover the reached semaphore redirect `80026160 -> 8003B238`, its concrete
registration/defaults and initializer, then the concrete main thread and
reached platform operations. See [IGB_RUNTIME_OBJECTS.md](IGB_RUNTIME_OBJECTS.md).
Metadata registration alone does not establish those objects or platform behavior.
See [IGB_THREAD_STARTUP.md](IGB_THREAD_STARTUP.md).
See [IGB_NAMED_TYPES.md](IGB_NAMED_TYPES.md) and
[IGB_UNSIGNED_INT_FIELD.md](IGB_UNSIGNED_INT_FIELD.md). Enum metadata remains null while these
prerequisites are pending. Further member/reference branches
remain explicit, together with pool/platform, allocator, callback deallocation
and BSS prerequisites. Initial core-list constructors and reserve run in this
same chain; the separate byte-only backing profile remains covered. Continue
through `igMetaEnum`, later registrations and the outer startup before claiming
complete native initialization. See [IGB_DATA_LIST_REGISTRATION.md](IGB_DATA_LIST_REGISTRATION.md),
[IGB_DATA_LIST_DEFAULTS.md](IGB_DATA_LIST_DEFAULTS.md),
[IGB_INT_FIELD_REGISTRATION.md](IGB_INT_FIELD_REGISTRATION.md),
[IGB_MEMORY_FIELD_REGISTRATION.md](IGB_MEMORY_FIELD_REGISTRATION.md),
[IGB_MEMORY_FIELD_DEFAULTS.md](IGB_MEMORY_FIELD_DEFAULTS.md),
[IGB_STRING_FIELD_REGISTRATION.md](IGB_STRING_FIELD_REGISTRATION.md),
[IGB_STRING_FIELD_DEFAULTS.md](IGB_STRING_FIELD_DEFAULTS.md),
[IGB_THREAD_DEFAULTS.md](IGB_THREAD_DEFAULTS.md),
[IGB_THREAD_DEPENDENCIES.md](IGB_THREAD_DEPENDENCIES.md),
[IGB_META_ENUM.md](IGB_META_ENUM.md),
[IGB_METADATA_INIT.md](IGB_METADATA_INIT.md),
[IGB_OBJECT_FIELD_CONSTRUCT.md](IGB_OBJECT_FIELD_CONSTRUCT.md),
[IGB_MEMORY_FIELD_CONSTRUCT.md](IGB_MEMORY_FIELD_CONSTRUCT.md),
[IGB_FIELD_TYPE_REGISTRATION.md](IGB_FIELD_TYPE_REGISTRATION.md),
[IGB_FIELD_TYPE_DEFAULTS.md](IGB_FIELD_TYPE_DEFAULTS.md),
[IGB_FIELD_PROTOTYPE.md](IGB_FIELD_PROTOTYPE.md),
[IGB_FIELD_INIT.md](IGB_FIELD_INIT.md),
[IGB_FIELD_STRING_INIT.md](IGB_FIELD_STRING_INIT.md),
[IGB_FIELD_CLONE.md](IGB_FIELD_CLONE.md),
[IGB_FIELD_COPY.md](IGB_FIELD_COPY.md),
[IGB_LIST_REGISTRATION.md](IGB_LIST_REGISTRATION.md),
[IGB_LIST_DEFAULTS.md](IGB_LIST_DEFAULTS.md),
[IGB_METADATA_PROBE.md](IGB_METADATA_PROBE.md),
[IGB_METADATA_DEFAULTS.md](IGB_METADATA_DEFAULTS.md),
[IGB_FIELD_SCHEMA.md](IGB_FIELD_SCHEMA.md),
[IGB_FIELD_VALUES.md](IGB_FIELD_VALUES.md),
[IGB_TYPE_PUBLICATION.md](IGB_TYPE_PUBLICATION.md),
[IGB_METADATA_LAYOUT.md](IGB_METADATA_LAYOUT.md),
[IGB_OBJECT_DEFAULTS.md](IGB_OBJECT_DEFAULTS.md),
[IGB_META_FIELD_FACTORY.md](IGB_META_FIELD_FACTORY.md) and
[IGB_FIELD_OPERATIONS.md](IGB_FIELD_OPERATIONS.md).
Complete the outer ArkCore creation/publication wrapper
and remaining pool/platform services before joining cold startup to the decoder.
See [IGB_ARK_CORE_BOOTSTRAP.md](IGB_ARK_CORE_BOOTSTRAP.md),
[IGB_OBJECT_REGISTRATION.md](IGB_OBJECT_REGISTRATION.md),
[IGB_TYPE_LIST_FACTORY.md](IGB_TYPE_LIST_FACTORY.md),
[IGB_POINTER_LIST.md](IGB_POINTER_LIST.md),
[IGB_METADATA_REGISTRATION.md](IGB_METADATA_REGISTRATION.md) and
[IGB_METADATA_OPERATIONS.md](IGB_METADATA_OPERATIONS.md).
Reflected allocation, header/field-pass orchestration
and the concrete file post-default pool assignments are reconstructed, with
prepared metadata and allocator/field endpoints in focused tests. One serialized
field class remains unmatched; loading is unfinished. Continue through actual
prerequisite classes, file and parent class registration, their reached field defaults and pool
allocation, complete runtime factories, string-pool
initialization/arena allocation/release, object population and post-load callbacks, plus decoder
cleanup and loader queue/lifecycle work. Reuse the existing checked asset
and document readers where qualified; do not replace required initialization
with a successful parse. See [LOADER_REQUEST.md](LOADER_REQUEST.md),
[LOADER_DIRECTORY.md](LOADER_DIRECTORY.md), [IGB_DECODE.md](IGB_DECODE.md) and
[CONTEXT_INFO.md](CONTEXT_INFO.md).
The new construction boundaries and evidence are documented in
[IGB_FILE_FACTORY.md](IGB_FILE_FACTORY.md),
[IGB_OBJECT_INIT.md](IGB_OBJECT_INIT.md) and [IGB_FILE_POOLS.md](IGB_FILE_POOLS.md).
Continue the documented registration and reference/list construction work in
[IGB_TYPE_REGISTRATION.md](IGB_TYPE_REGISTRATION.md),
[IGB_REGISTRATION_CALLBACKS.md](IGB_REGISTRATION_CALLBACKS.md),
[IGB_THREAD_REGISTRATION.md](IGB_THREAD_REGISTRATION.md),
[IGB_BOOTSTRAP_OBJECT.md](IGB_BOOTSTRAP_OBJECT.md),
[IGB_FIELD_REGISTRY_FACTORY.md](IGB_FIELD_REGISTRY_FACTORY.md),
[IGB_METADATA_FACTORY.md](IGB_METADATA_FACTORY.md) and
[IGB_FIELD_DEFAULTS.md](IGB_FIELD_DEFAULTS.md).

After completing the query responses, connect that core lifecycle to original
startup and per-frame execution order: assigned device input, remaining
position-lock/ride operations, camera and rendering. Complete original manager
attachment, audio/effect services, HUD traversal, enemies and verified combat
remain part of that work. The visible walking demo is unchanged and still uses
its provisional controller and follow camera. Completing GENERATE does not
complete a gameplay frame or establish local multiplayer.

The integration also needs broader original evidence for shared-work execution
across distinct RAMs and ACTION/Lua-triggered nested sequence entry; those paths
currently stop at checked execution boundaries. The retained ACTION call
timestamp remains an explicit execution profile, separate from MODELCTRL's
per-helper live context reads. See
[ARCHITECTURE.md](ARCHITECTURE.md), [MODEL_DISPATCH.md](MODEL_DISPATCH.md) and
[ACTION_DISPATCH.md](ACTION_DISPATCH.md).

Track original behavior in [ORIGINAL_BASELINE.md](ORIGINAL_BASELINE.md).
Build and test locally, with occasional preview snapshots for the owner to
launch manually. Do not open interactive previews automatically.

## Historical recompilation evidence

The earlier DolRecomp/ModernGekko package reached the original main menu and
implemented its own F1 menu, ROM preparation and portable profiles. Its opening
movie performance, audible output, gameplay and wider release acceptance
remained incomplete. Preserve those artifacts and reports in
[RECOMPILATION.md](RECOMPILATION.md), [RECOMP_AUDIO.md](RECOMP_AUDIO.md),
[RECOMP_INGAME_MENU.md](RECOMP_INGAME_MENU.md), [ROM_IMPORT.md](ROM_IMPORT.md)
and [PC_APPLICATION.md](PC_APPLICATION.md). They document the set-aside runtime;
they are not acceptance evidence for the authored native game. None of its
unfinished optimization, embedded-module or packaging tasks is the active queue.

## Earlier milestone acceptance notes

The following versioned notes describe the implemented scope and outstanding
work at each earlier milestone. Their historical next steps are superseded by
the current gate above where those paths have since been connected.

Version 0.40 connects actual named RAM timers to movement, turning, MOVE/MUTEKI
and positive invulnerability countdown. Sequence/control staging publishes work
without reading or writing timers. Original four-slot Agumon WAIT/WALK branches
exercise that binding with explicit ACTION request boundaries. At that milestone
the next requirements were complete direct sequence entry, player `+B8`, a
retained execution bundle per work owner and original manager/context scheduling.
This timer integration does not change the visible walking demo or complete
the original gameplay loop. See [MODEL_TIMER.md](MODEL_TIMER.md).

Version 0.39 connects actual loaded RAM/ONE owners to native player construction
and the retained outer GENERATE branch. Four original Agumon owners can reach
the post-startup ordinary sequence boundary through original point and game-data
providers. Full direct sequence entry, player `+B8` and a retained execution
bundle per actual work owner were the next lifecycle requirements at that time.
Named RAM timers supplied a separate prerequisite for those consumers.

Version 0.38 supplies one canonical 68-field player-work allocation and routes
the existing checked MODELCTRL, motion/control, sequence/wait, script and status
consumers through it. Fresh defaults and retained old-generation views are
covered separately from prepared value publication. The next player-lifecycle
work continues with the full retained GENERATE continuation.

Version 0.37 establishes actual list storage for existing movement, PDMOVE,
subcommand and label consumers. Retained views share original allocations;
validation candidates detach and publication preserves existing owners. The
next player-lifecycle work requires actual RAM/ONE routing and the full retained
GENERATE continuation.

Version 0.36 connects original PLAYERPOINT queries to the recovered model
placement helper in prepared integration. Node matrices now outlive individual
work owners; fresh work allocation, the full GENERATE continuation and original
frame scheduling still remain. See [PLAYER_SPAWN.md](PLAYER_SPAWN.md).

Version 0.35 connects the original post-GETPLAYER caller to actual number,
meter and status recipients across four Agumon slots. Digit UV mutations,
shared HP/MHP bindings, meter sample times and map-dependent SETMETAL/main-lobby
effects retain original ownership and command order. Full earlier player
construction, HUD traversal, original input/sequence/camera scheduling and
combat remain required. See [MODEL_PLAYER_STARTUP.md](MODEL_PLAYER_STARTUP.md).

Version 0.34 connects common weapon child traversal and shared prepared GPU
rendering to the original ACTION owner. A refreshed walking preview displays
the original common05 weapon while host movement selects its stand/run actions.
Original geometry gates and normal attribute scope restoration are compared
against bounded original instructions. Complete original materials, compiled
traversal, player construction, game camera and locomotion/combat state machines
remain required. See [WEAPON_PREVIEW.md](WEAPON_PREVIEW.md).

Version 0.33 connects original weapon registration, GENERATE/REMOVE, live PLAYER
name queries, ACTION appearance/skeleton results and nullable MODELCTRL labels.
Four prepared original Agumon starts deliver their real deferred common05
requests, replace weapons across all twelve appearances and exercise the common
bone-selector calculation. Generic child rendering, the compiled selector path,
subsequent NUMBERCTRL/METERCTRL calls, full player construction and original
outer scheduling remain required. See [WEAPON_RUNTIME.md](WEAPON_RUNTIME.md),
[ACTOR_SCENE.md](ACTOR_SCENE.md) and [BONE_SELECT.md](BONE_SELECT.md).

Version 0.32 connects the prepared post-GETPLAYER MODELCTRL prefix through exact
modifier-array resets, GAMESTART, BASE_ISUSE, deferred PLAYERARMS GENERATE and
RECALCPLAYER. The messenger owns the original deferred queue and explicit manager
update continuation. Its prepared weapon-delivery boundary is extended in 0.33;
the later player UI and construction effects remain required before gameplay.
See [MODEL_PLAYER_START.md](MODEL_PLAYER_START.md) and
[DEFERRED_MESSENGER.md](DEFERRED_MESSENGER.md).

Version 0.31 connects original GAMESTART selection, base-array copying, initial
weapons, full represented status/technique calculation and both name routes.
All four Agumon data rows complete this command with explicitly prepared original
database and font services. Existing skill selection can consume their populated
WAZALIST and reach the remaining growth boundary. Full MODELCTRL player/weapon
construction, other dynamic item providers, skill growth, named damage/status
effects, startup and gameplay scheduling remain the next work. See
[GAME_START.md](GAME_START.md) and [GAME_RAM.md](GAME_RAM.md).

Version 0.28 connects prepared target-Hit work to ordered script registration,
shared HP/context publication and resumable original Lua chunks. Selected
Agumon early-return paths can execute; ordinary damage paths require their
actual native services. Complete player/GAMERAM construction, damage/status
effects and original gameplay scheduling remain the next integration work.
See [MODEL_SCRIPT_RUNTIME.md](MODEL_SCRIPT_RUNTIME.md).

Version 0.27 routes HITBOX queries through prepared target body caches, ordered
overlap gates and retained record/hit-script/heading phases. Four Agumon RAMs
retain independent bodies and authored collision categories. Damage services,
player creation and the outer game loop remain required. The
first Set129 still needs its separate HITPLAYERTALK recipients. See
[MODEL_COLLISION.md](MODEL_COLLISION.md).

Version 0.26 runs original HITBOX commands through timed work, exact query-box
math and resumable CHKBULLET/BHit service dispatch. Original SQ_ATCK1 preparation
can now reach TALKWF and WCHECK without skipping the first hitbox. Complete
target collision managers, script context, player construction and outer
entry/status effects are still required; this does not establish attack damage.
See [MODEL_HITBOX.md](MODEL_HITBOX.md).

Version 0.25 resolves original common/player waits against registered live
ACTION work and separate original pad slots. WCHECK advances and retains its
cursor; unavailable services suspend without replaying completed commands or
nested dispatches. Complete original player creation, sequence-entry/status
callbacks and device assignment are still required. This prepared integration
does not establish combat or connected multiplayer. See [MODEL_WAIT.md](MODEL_WAIT.md).

Version 0.24 binds prepared player MODELCTRL/ACTION graphs and routes original
ACTION broadcasts through loaded Se/ActionStarter tables into owned subcommand
queues. Native messenger delivery and sequence cleanup preserve completed work
when an external service is missing. Original audio/effect managers, complete
player construction, input/collision callbacks and outer frame scheduling remain
required before these prepared owners become a gameplay session. See
[ACTION_EVENT_RUNTIME.md](ACTION_EVENT_RUNTIME.md).

Version 0.23 connects an explicitly selected actor's main skin to the cached
joint palette, recovered CPU vertex deformation and a native prepared draw.
The captured root/model-view is applied once before a separate projection.
This visible integration uses a host inspection camera; original camera,
complete effective appearance/material traversal, action events and player
scheduling remain required. See [ACTOR_SKIN.md](ACTOR_SKIN.md).

Version 0.22 adds explicit ordinary actor traversal, original float hierarchy
calculation and cached bone queries. Four original Agumon instances pass 720
traversals across their three appearances; independent hierarchy instruction
checks match 12,976 float words. Shared arrays persist through NDSP and are
invalidated by new local sampling. Complete renderer context/appearance
traversal and outer player scheduling remain required before gameplay integration.
See [ANIMATION_HIERARCHY.md](ANIMATION_HIERARCHY.md).

Version 0.21 adds prepared ACTION actor ownership and NDSP cleanup. All ordered
actor buffers belong to their selected RAM; activation, deactivation and
reactivation preserve the original work lifetime and retained node clocks.
Actor traversal, cached bone queries, messenger consumers and full player
construction remain required. See [ACTION_ACTORS.md](ACTION_ACTORS.md).

Version 0.20 adds prepared ACTION2 execution: exact action dispatch, retained
cursors, motion/joint/fade/wait/flash effects, original node timing and freeze.
All 182 ordinary Agumon actions complete their command/effect phases in local
core integration. Four actual weapon sequences additionally dispatch actions
into 240 poses; 20,000 numeric/state instruction comparisons pass. Complete
actor construction, broadcast consumers, scene attribute attachment and outer
player callbacks remain required before connecting original gameplay.
The preview and connected local multiplayer remain separate unfinished work.
See [ACTIONS.md](ACTIONS.md).

Version 0.19 adds prepared combiner decoding, owned states, priority-ordered
retirement, cached-pose snapshots and explicit MOTIONSET/motion-update effects.
Four original Agumon combiners pass 240 combined poses with interrupted weapon
idle/run transitions. Independent instruction checks cover 10,000 state updates
and 30,556 matrix/quaternion conversions. Version 0.20 adds the prepared ACTION2
path above; full actor/work construction and event integration remain. This
core increment does not change the walking preview or provide connected local
multiplayer. See [ANIMATION_COMBINER.md](ANIMATION_COMBINER.md).

Version 0.18 adds original animation clocks, MOTIONSET numeric blend fields,
source play modes and ordered pose-layer mixing. Instruction comparisons pass
30,000 timing operations and 10,000 layer tuples; all 82 Agumon clips pass timed
pose integration. Version 0.19 extends those primitives into the bounded
combiner path described above. The walking preview retains its provisional
behavior. See [ANIMATION_TIMING.md](ANIMATION_TIMING.md).

Version 0.17 retains model sequence programs, weapon label selection and command
positions in native runtime state. It runs the supported control/motion commands,
preserves label barriers and local jumps, and exposes required ACTION dispatch.
All 9,060 SEQ programs found in the original archive corpus decode; their
447,274 command occurrences include unsupported runtime effects. Decoding those
records is not execution of the whole corpus. Next implement ACTION manager
behavior, full sequence-entry/outer callbacks and original wait/resume rules,
then connect original player state, collision and camera. See [MODEL_SEQUENCE.md](MODEL_SEQUENCE.md).

Version 0.16 adds original CTRL/PDMOVE effects, movement timer/position-lock
preparation, player turn rates and post-input WAIT/WALK requests. All 668 original
CTRL/PDMOVE records decode; 10,000 bounded instruction comparisons pass. Explicit
command effects also pass at all eight stored entry matrices. Full player work
construction, named RAM ownership, input and sequence/action execution remain
the next boundary before collision and camera integration. These core changes
do not replace the provisional walking controller. See [MODEL_CONTROL.md](MODEL_CONTROL.md).

Version 0.15 connects the original motion channels to recovered turning and an
owned model matrix in their original order. Four original entry matrices and
Agumon commands pass data integration checks; original player preparation,
input/sequence/action dispatch, camera and collision response remain the next
boundary. These checks do not establish connected local multiplayer or original
gameplay. See [MODEL_MOTION.md](MODEL_MOTION.md).

Version 0.14 implements original scalar movement channels, MOVE/MOVEEX decoding,
local model translation and the cached elapsed timer. All 3,796 discovered
original commands decode, including 131 zero-frame records. The next movement
boundary is retained player/sequence state, original turning and collision
integration with reference comparisons. This core does not replace the preview's
provisional controller. See [MOTION.md](MOTION.md).

Version 0.13 separates preliminary loading from aggregate boot inspection and
implements StaticInfo/SwitchCtrl registration effects plus four independent
original pad states. The original preliminary sound gate still blocks complete
startup. These foundations do not satisfy the original multiplayer or game
baseline gates. See [CONTROLLERS.md](CONTROLLERS.md).

Version 0.12 adds a controllable original-map walking preview in response to the
owner's request for an initial interactive state. Its bounded host controller
follows decoded original floor collision triangles and uses original idle/run
clips. The camera and movement constants are provisional. Pause and restart are
native preview state; this does not complete original startup or stage 2's
enemy/combat criteria. Static original wall geometry now constrains a provisional
body circle; door state remains fixed. See [PLAY_PREVIEW.md](PLAY_PREVIEW.md).
Next, recover original movement and collision response, then one enemy and
verified combat.

The Agumon viewer decodes original compressed animation tracks and evaluates
complete bone transforms, including helpers. Synthetic fixtures and original DOL
arithmetic establish the current sampling, quaternion and hierarchy profile.
The scene viewer now composes verified static transforms and preserves ordered
draw instances. Named packages preserve original member and export order. The
`BG02` map's disabled switch can now be enabled through its original exported
controller's `ALLON` command. That command is present in `first.lua`, and its
dispatcher and mask effect have been traced. The host currently replays it only
on explicit request. A selected original player model/instance/appearance can
now share this scene's depth target, with the original full entry-point matrix
replacing its authored outer model transform. Single-clip playback updates only
the visual's vertex range. Original startup dispatch order and player-matrix
assignment are documented. The first two startup scripts now execute natively;
later controller state remains unimplemented. Continue with startup dependencies
and player lifecycle, then recover movement, collision and the camera. Compare matching camera/animation
states against original frames before claiming visual parity. Explicit visual
placement and clip playback do not establish gameplay spawning or simulation.

Version 0.10 executes `func.lua` and `boot.lua` in a bounded native Lua 4 VM.
The host implements proven common-file queue methods and explicit inspection
state queries, retaining USA constant getters and original name/dedup rules.
Three profiles match official Lua 4.0.1 execution call-for-call and queue-for-queue.
Version 0.11 consumes that queue, retains the live Lua state and decoded files,
and reconstructs the original immediate/held/deferred info-routing order.
Continue by implementing the queued managers' real initialization callbacks,
the original loading-screen state and layer/text prerequisites, then the native
calls reached by `start.lua`. A retained info record is not an initialized
controller; generic registration invokes additional per-info and per-RAM work.
Do not skip an unsupported host operation or mark a later startup phase complete
because a script buffer exists. The separate walking preview explicitly loads
one verified asset profile; it does not mark those startup stages complete.

## Requested PC options

| Option | Current scope | Full-port target |
| --- | --- | --- |
| 3840×2160 | Host window preference | Verified 4K scene render target and readable UI |
| Borderless | Host fullscreen desktop window | Game renderer resizes correctly across monitors |
| 16:9 | Stored preference only | Camera, culling, HUD and menus adapted |
| 60–300 FPS | Provisional walking-preview frame pacing only; original game scheduling remains pending | At least 60 FPS on documented hardware and presentation options up to 300 FPS, decoupled from measured original simulation |
| VSync Off | Host OpenGL swap-interval request | Validated frame pacing in gameplay |
| MSAA 8x | Host context and mesh/static-scene viewport requests; actual samples reported and offscreen resolve tested | Gameplay render passes and material coverage validation |
| Keyboard / Mouse | Tool UI and provisional keyboard walking controls | Original gameplay, freely editable saved bindings and appropriate mouse camera support |
| Xbox / PlayStation | SDL device inspection and mapped provisional walking input | Tested original gameplay mappings, independent slots, hotplug and prompts |
| GameCube | SDL-mapped devices when available | Original gameplay mapping, dead zones and adapter support |
| Original / Modernized | Stored preference only | Original behavior baseline plus explicit optional changes |

## Requested extensions

- **HD texture packs:** replacement lookup after reliable texture decoding.
- **Mod loader:** versioned manifests, load order, stable IDs and validation.
- **Custom maps:** scene, collision, navigation and spawn schemas plus import tools.
- **Custom Digimon:** actor definitions, meshes, rigging, animations and abilities.
- **Custom weapons:** weapon data, models, animation hooks and damage behavior.
- **Randomizer:** identified data tables and reproducible seeds, with save metadata.
- **Difficulty rebalancing:** optional, versioned changes after original combat tests.
- **Bug fixes:** reproductions and regression checks against recovered behavior.
- **Online four-player co-op:** follows the simulation/local multiplayer gates above.

Keep each increment buildable and publish source progress to this repository.
Do not distribute the user's ISO, DOL, extracted assets or generated asset packs.
