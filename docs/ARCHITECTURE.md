# Native port architecture

## Active source-port path

The owner selected a complete native source reconstruction on September 18,
2026. The active implementation is the authored C++20 `dw4_core` with its native
SDL/OpenGL application. Recover the original game and engine rather than making
generated CPU code or an embedded console runtime the final deliverable.
[PORTING_STRATEGY.md](PORTING_STRATEGY.md) defines the continuation order;
[ROADMAP.md](ROADMAP.md) defines acceptance for the full game and PC features.

Original assets and reference instructions supply evidence. Host data structures,
services and control flow are implemented explicitly, keeping guest addresses
and serialized layouts distinct from native pointers. The core remains
independent of SDL and ImGui. DTK, objdiff and Ghidra may assist focused recovery;
they do not replace native ownership, byte-order or behavior validation.

Resume at the existing player-frame/collision boundary below. Static triangle
candidates, CHECKLAND floor and CHECKWALL wall responses are implemented;
dynamic operations and CHECKWALLFULL remain required. Startup, the actual outer
frame scheduler, assigned input, camera, audio, combat and persistence must be connected
without substituting the walking preview's provisional controller or camera.

`ApplicationFrameState` shares its context world-time field with model managers.
Its separately owned timer advances the recovered application time prefix before
the dispatcher and scheduler boundaries. `publish_system_frame_time` implements
the reached system timing leaf on that same context, without claiming complete
system startup or scheduling. Player input uses the native matrix inverse and
recovered finite MSL heading math. Its viewer binding selects and retains the
actual parameters' inline matrix; unrepresented setup, represented null owners
and service overrides stay distinct. Runtime matrix defaults are zero, not the
temporary layout probe's identity values. The separate original camera mode
and view-model publication remain required.
The serial scheduler reconstructs the captured-count copy/update/clear sequence
with its original live pointer reloads and retained pending manager callback.
The common manager virtual `+74` now connects that batch to ordered info/RAM
traversal and the player's actual `+84` frame. Each occurrence selects a fresh
operation; a pending operation retains its selected RAM and runtime. System
and manager gate bytes retain their original zero defaults, so installing the
binding does not invent startup activation or registration. Prepared original
integration crosses both loops with all four authored player RAMs. See
[MODEL_MANAGER_FRAME.md](MODEL_MANAGER_FRAME.md), [VIEWER.md](VIEWER.md) and
[SERIAL_SCHEDULER.md](SERIAL_SCHEDULER.md).
The specialized camera inverse preserves original aliasing, arithmetic and
partial publication into the separate view-model matrix. It does not replace
the required camera mode, graph sampling, Euler rebuild or viewer publication.
See [CAMERA_MATH.md](CAMERA_MATH.md).
`SystemFrameState` now shares its original base timing/gates with those model
managers and embeds recovered stage/storage fields. The actual system update
runs reset sampling and possible reset return, timing, stage processing,
storage-error processing and the first-time tail in original order. Stage
request/completion remain separate updates; info registration reads live list
counts/backing and context. Storage phases use actual native messenger sends
and deferred pause state, with retained explicit scene/manager/loader operations
where concrete implementations remain missing. Seven original-data batches
now enter through the application and scheduled system, replacing their manual
system timing calls. Prepared samples, registry setup and dispatcher endpoint
remain explicit. See [SYSTEM_FRAME.md](SYSTEM_FRAME.md),
[SYSTEM_STAGE.md](SYSTEM_STAGE.md) and [SYSTEM_STORAGE.md](SYSTEM_STORAGE.md).
The native stage factory connects directory request, claim release and context
info registration to their recovered implementations. The application's ordered
manager registry remains distinct from the scheduler's active registry, while
both may hold the same native manager identities. Directory loading claims are
separate signed words from object references. Cache lookup/reuse and removal,
mutex callbacks and new-file request publication preserve their actual order.
The IGB decoder now enters actual stream/header and early runtime table bodies
through the original load orchestration. Its canonical state belongs to the
loader file, with a weak stream back-reference. Live status words, version gates,
timing and success/failure tails retain their original publication order.
Canonical runtime metadata now serves both field callbacks and type lookup.
The type mapper reconciles known versions and inherited signed slot maps, with
the dynamic caller retaining explicit constructor/field/registry boundaries.
A bounded native registration profile supplies twenty object layouts and
forty-seven field-class identities recovered from original registration/default
routines. Supplied `ndmw.igb` with explicit list allocation endpoints resolves
twenty types and sixty field mappings. Native directory-path preparation
`800505E0` and dependency-table processing `800521EC` now read the dependency
block before entering native getter `8007042C`, string interning `80054094`
and synchronous dependency request `8006FA6C`. The getter reads the canonical
ArkCore owner `80562100` field `+40`, separately from direct loader global
`80562104`. Native interning preserves ordered table search/insertion, live mutex
selection and full-word string references. With explicitly prepared loader and
string-pool allocation state, the original prefix enters file factory
`8002EBEC`, its `8002F178` registration runner and prerequisite once guard. With
explicit cold prerequisite flags, native ThreadManager registration and actual
handle-based pool access now enter metadata factory `80029E64`. The canonical
runtime remains enabled and reaches missing `igMetaObject` registration
`8002A0A8`; it has not loaded all 223 dependencies. The reflected factory now owns original redirect/pool/allocation
selection and calls the reconstructed object initializer. Explicit physical
storage and typed bindings preserve header writes, fresh metadata/virtual reads,
three captured field ranges and instance-count publication. Concrete file
virtuals connect the actual three post-default pool assignments and selected
manager pool lookup. Registration, field defaults and allocation services remain
explicit; a prepared empty field list does not establish original construction.
See [IGB_FILE_FACTORY.md](IGB_FILE_FACTORY.md),
[IGB_OBJECT_INIT.md](IGB_OBJECT_INIT.md) and [IGB_FILE_POOLS.md](IGB_FILE_POOLS.md).
The generic `80066204` registrar now shares the file's canonical metadata cell,
preserves original busy/parent/configuration/finalization flags and connects the
actual deferred callback-list constructor and append. Native parent metadata
linkage preserves child publication, live layout reads and inherited field
retains; native raw list append/growth now preserves physical pointer and count
publication with explicit allocator endpoints. Native name assignment shares
the canonical string pool and preserves interning, release and publication order.
Field registration, registry publication and layout remain reached operations.
Concrete int/bool field virtuals copy physical default bytes, including overlap,
inside the actual initializer; serialized names do not select these handlers.
Native metadata and internal-field-list factories also distinguish enabled
reflective construction from disabled bootstrap constructors. Explicit typed
storage preserves physical header/default writes, live string ownership and
separate list backing allocation. Allocator, type registration and reflected
construction endpoints remain required; these bodies do not establish complete
cold startup. The current-pool wrapper remains fixed at handle two while
registration uses the same native manager access with its captured handle.
ArkCore construction `8003D234` and bootstrap caller `8003D2F8` now preserve
physical stores and the actual disabled-to-enabled transition. The canonical
`igMetaObject` wrapper/getters compose with the registrar and direct constructors;
a cold integration now constructs distinct `igMetaObject` and `igObject`
metadata, executes actual root-parent registration, naming and layout probe,
then constructs both specialized descriptors through native defaults `80066B7C`.
Their actual field factory, name setter, unsigned-integer default-buffer virtual,
finalization and raw append now feed actual type publication `800660AC` and
field layout `80065A58`. Pointer-identity lookup, retained insertion and a fresh
second registry lookup preserve the exact published type index. Layout assigns
alignment, prefix and field ownership, then sets flag4 before dispatching the
captured-count registration callbacks; it does not rewrite native size. The
base registration completes and actual child-list construction/inherited field
append feed metadata constructor probe `80029FFC`. The current composition
also completes that schema, three field-class registrations and the reflected
object-field prototype through actual `800667F4` and all three field passes.
Files and fields retain separate typed results and canonical physical stores.
Prototype publication and parent-list registration now feed the actual
memory-reference clone `8002ACD8 -> 8006331C -> 8006CC88 -> 800639E4`.
The copied descriptor retains the canonical configured source, receives its
own default buffer, then runs original finalization and list replacement.
Both list metadata owners finish with flags `7`. The chain contains 53 schema
descriptors plus one reflected prototype, 54 default buffers, seven published
types and one prototype-registry entry. The original bootstrap caller enables
reflection and snapshots counts 7 and 1 at core `+04/+08`; it then reaches
the `igMetaEnum` registration body `8002AAFC` for metadata `80561808`.
That body now enters its prerequisite ThreadManager registration. The enabled
metadata factory composes actual reflected initialization with distinct metadata
and field-list owners. Positive object-member construction calls the nested
reflected factory; its memory member derives size/alignment, tests element
ancestry and zero-fills the actual allocated backing before pointer publication.
ThreadManager's two local object fields now use that same reflected factory,
with exact typed descriptor results. Defaults compose allocation-only dependency
getters, schema publication and the actual ArkCore bootstrap callback append.
They construct separate metadata/internal-list pairs without marking either
dependency class ready. ThreadManager publishes as the eighth type before the
runner enters `80023F28`. Its returned dependency/startup callbacks remain
separate from the `80023A50` callback queued on ArkCore `+28`.
The thread-list, non-reference-counted object-list and DataList registrars share
those canonical cells and execute their actual prerequisite guards. Their
layout probes preserve distinct core-capture positions and physical vtable
stores. DataList defaults use native batch/schema callers; object specialization
retains the selected inherited field through clone, prototype lookup and
replacement. Integer-field registration shares the existing descriptor metadata
and value-prototype cells, including the reflected initializer and scalar
virtuals. Memory-field registration now uses the canonical descriptor metadata,
its exact reset/probe/cleanup and the eight-field defaults caller. The latter
preserves inherited method lookup/replacement and physical configuration order.
Reached Boolean registration and its reflected prototype complete before the
composed chain enters string-field registration `80025404` at the eighth
memory-class descriptor. The actual string probe and defaults construct their
one Boolean local, then select the inherited signed-byte clone. Native
signed-byte registration and reflected prototype publication now precede the
native enabled copy `800680B4`. It captures source metadata and pool, composes
actual reflected allocation, then executes the selected object-copy pass. The
mode-one pass copies the first seven class members through raw, string and
object operations before native memory-buffer copy `80062824`. Scalar clone wrappers preserve their captured
argument and original enabled/cold branch; cold copy constructors reuse `800639E4`.
Memory copying composes actual element release, conditional free, three independent
owner queries, two size queries and directional byte copying. Object-element and
nested-memory loops retain their original ownership and publication order; concrete
pool, object-clone and destructor endpoints remain explicit. Canonical member stores
also update overlapping typed views without importing stale physical neighbors.
The same cold chain now completes both inherited string replacements, its schema
and reflected prototype, then the eighth memory field and inherited shallow-copy
replacement. Reset descriptor factory `80031378` now registers the enum-field
class and its raw-reference dependency. Actual probes, one-field defaults and
reflected prototype lifecycles share their canonical globals. Reflected member
initialization zeroes the enum getter word, while the raw enum reset leaves it
untouched. The memory defaults caller assigns its four-byte reset policy and
getter function, completes its schema and returns through memory prototype
publication. Unsigned-byte registration `80023234` now executes its actual
prerequisite, direct-base probe and reflected prototype lifecycle. Its shared
prototype cell resumes the DataList schema. Enabled child-list factories
compose the existing reflected initializer with typed metadata-pointer backing
and native zero fill. The original reflected memory clone then completes the
object-list specialization. DataList, object-list and thread-list metadata
finish before the deferred `igThread` wrapper `80023C40`. That wrapper now runs
its actual named-object parent and schema. Unsigned-integer registration
`80022CD8`, probe and reflected prototype share the legacy factory/scalar cells
and resume Thread's ten-field batch. Its defaults and schema complete before
Semaphore registration. Native ThreadManager startup `80023A30 -> 800755F8`
then enters handle-four pool selection and the actual manager factory.
Its redirect runs concrete `igGamecubeThreadManager` registration, probe and
defaults, producing twenty-four types and ten prototypes. The caller retains
physical storage/offset identities and composes the existing reflected factory.
Distinct runtime identities now participate in its construction lease, field
passes, reference ownership and strict result adapters. Actual manager allocation
and nested thread-list initialization complete through the existing native
callers and the recovered memory default/retain bodies. The list is assigned
to manager `+08`; the manager remains unfinished during concrete semaphore
registration. Its distinct layout probe restores the named-object vtable after
capturing the derived type word. Three local descriptors are constructed before
struct-field factory `8002450C` runs its registration, probe and local field batch.
Unsigned-short registration/prototype and inherited `_size` clone `80021EB4`
now complete struct defaults and its schema. Struct prototype `800247E4` uses
distinct physical alignment/constructor/destructor words and dynamic size
operations. Its concrete descriptor completes semaphore schema registration;
the real semaphore allocation now completes through its metadata getter and
actual inline struct member operations. Activation `+5C` now initializes the
queue/count, restores the captured exclusion bit, publishes active state and
freshly loads the canonical success status. The manager's checked continuation
propagates its fault guard through the activation callback. With explicitly
prepared initial exclusion state, the cold test completes manager construction
and singleton publication. Concrete thread getter `8003AF84` now completes its
registration, distinct physical probe and nine local defaults. The factory
then constructs the `0x388` runtime object and its owned completion semaphore.
Initializer `80077E94` captures alignment, reads the fresh stack size and tag,
selects the aligned allocation and invokes actual semaphore setter `800773AC`.
Object initialization lends its fault guard through both native operations.
Prepared tagged-pool and allocation services let the cold invocation finish
construction. Current-thread adoption `80077EFC` now reads its canonical OS cell
twice, performs the original grouped context copy, selects tagged stack release
and clears the stack pointer after release. Unknown OS state stays pending;
an explicit prepared raw context lets the cold test reach manager `+60` insertion.
Its native caller retains the incoming thread, performs the active positive
semaphore wait and reaches actual data-list growth `80041660`. Canonical memory
allocation mappings supply element destinations; there is no second backing
registry. Native wait/signal preserve signed versus unsigned count branches,
fresh status reads and exclusion transitions around explicit sleep/wake calls.
The startup caller lends its fault guard through adoption, insertion and their
checked children. Actual data-list growth now selects the canonical descriptor
ordinal and list metadata, composes the existing memory-field allocator and
preserves copy/release/capacity/count order. Raw backing copies reuse that same
allocation registry. The layout producer also publishes physical ordinal and
declaring-type stores before later consumers read them. The cold composition
inserts the first thread and enters native OS wake after signal's increment.
Wake preserves raw dequeue/enqueue stores, signed suspension, priority arithmetic
and shared optional scheduler words. Nonzero hints select explicit rescheduling;
unknown state stays pending. With a declared prepared zero hint, nested interrupt
restoration and the outer temporary-reference release complete before the next
reference-counted list registration. Original OS/pool initialization, rescheduling,
sleep and teardown remain required. See [IGB_OS_THREAD_WAKE.md](IGB_OS_THREAD_WAKE.md),
[IGB_DATA_LIST_GROWTH.md](IGB_DATA_LIST_GROWTH.md),
[IGB_THREAD_MANAGER_INSERT.md](IGB_THREAD_MANAGER_INSERT.md),
[IGB_GAMECUBE_THREAD_ADOPT.md](IGB_GAMECUBE_THREAD_ADOPT.md),
[IGB_GAMECUBE_THREAD_INIT.md](IGB_GAMECUBE_THREAD_INIT.md),
[IGB_GAMECUBE_THREAD.md](IGB_GAMECUBE_THREAD.md),
[IGB_SEMAPHORE_ACTIVATION.md](IGB_SEMAPHORE_ACTIVATION.md),
[IGB_STRUCT_FIELDS.md](IGB_STRUCT_FIELDS.md),
[IGB_STRUCT_FIELD_RUNTIME.md](IGB_STRUCT_FIELD_RUNTIME.md),
[IGB_UNSIGNED_SHORT_FIELD.md](IGB_UNSIGNED_SHORT_FIELD.md),
[IGB_PLATFORM_SEMAPHORE.md](IGB_PLATFORM_SEMAPHORE.md),
[IGB_RUNTIME_OBJECTS.md](IGB_RUNTIME_OBJECTS.md) and
[IGB_THREAD_STARTUP.md](IGB_THREAD_STARTUP.md). Physical descriptor
configuration now publishes `+1C/+08/+18` before each canonical update; missing
declaring-type addresses suspend at the reached store.
The separate `igMetaEnum` metadata object remains unconstructed.
See [IGB_MEMORY_MEMBER_COPY.md](IGB_MEMORY_MEMBER_COPY.md) and
[IGB_BUFFER_COPY.md](IGB_BUFFER_COPY.md) and
[IGB_ENUM_FIELD_DEFAULTS.md](IGB_ENUM_FIELD_DEFAULTS.md) and
[IGB_UNSIGNED_BYTE_FIELD.md](IGB_UNSIGNED_BYTE_FIELD.md) and
[IGB_NAMED_TYPES.md](IGB_NAMED_TYPES.md) and
[IGB_UNSIGNED_INT_FIELD.md](IGB_UNSIGNED_INT_FIELD.md).
This transition does not complete startup. Remaining member operations, method
fields, later registrations and the outer startup remain
required. Pool/platform and physical allocation services are explicit prepared
endpoints in this composition. See [IGB_DATA_LIST_REGISTRATION.md](IGB_DATA_LIST_REGISTRATION.md),
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
[IGB_FIELD_INIT.md](IGB_FIELD_INIT.md),
[IGB_FIELD_CLONE.md](IGB_FIELD_CLONE.md) and
[IGB_FIELD_COPY.md](IGB_FIELD_COPY.md).
Descriptor-global and declaring-type pointers are weak bindings:
unrepresented, original null and expired nonnull storage remain distinct. Guest
reference counts remain separate from host ownership. See
[IGB_TYPE_PUBLICATION.md](IGB_TYPE_PUBLICATION.md),
[IGB_METADATA_LAYOUT.md](IGB_METADATA_LAYOUT.md),
[IGB_OBJECT_DEFAULTS.md](IGB_OBJECT_DEFAULTS.md),
[IGB_META_FIELD_FACTORY.md](IGB_META_FIELD_FACTORY.md) and
[IGB_FIELD_OPERATIONS.md](IGB_FIELD_OPERATIONS.md).
Native typed list factories and raw append/reserve share canonical metadata or
field owners without reinterpreting their types. The same cold chain now
composes actual initial list constructors and reserve. A separate profile
also covers physical backing with no typed pointer mapping. The enabled decoder
fixture remains separate. An enabled core with a null fundamental metadata cache
recurses before publication; the adapter reports that invalid-startup cycle
instead of manufacturing metadata. Allocator and manager/platform operations,
field descriptors and later registrations remain explicit. See
[IGB_OBJECT_REGISTRATION.md](IGB_OBJECT_REGISTRATION.md),
[IGB_TYPE_LIST_FACTORY.md](IGB_TYPE_LIST_FACTORY.md),
[IGB_POINTER_LIST.md](IGB_POINTER_LIST.md),
[IGB_ARK_CORE_BOOTSTRAP.md](IGB_ARK_CORE_BOOTSTRAP.md),
[IGB_METADATA_REGISTRATION.md](IGB_METADATA_REGISTRATION.md) and
[IGB_METADATA_OPERATIONS.md](IGB_METADATA_OPERATIONS.md).
See [IGB_TYPE_REGISTRATION.md](IGB_TYPE_REGISTRATION.md),
[IGB_REGISTRATION_CALLBACKS.md](IGB_REGISTRATION_CALLBACKS.md),
[IGB_THREAD_REGISTRATION.md](IGB_THREAD_REGISTRATION.md),
[IGB_BOOTSTRAP_OBJECT.md](IGB_BOOTSTRAP_OBJECT.md),
[IGB_FIELD_REGISTRY_FACTORY.md](IGB_FIELD_REGISTRY_FACTORY.md),
[IGB_METADATA_FACTORY.md](IGB_METADATA_FACTORY.md) and
[IGB_FIELD_DEFAULTS.md](IGB_FIELD_DEFAULTS.md).
Pool-name table `80052554` uses the recovered memory-manager selection
and original 46-name lookup to obtain numeric handles, with list construction
and allocation still explicit.
The unsupported serialized shader field remains null, and loading is unfinished.
File creation, complete runtime registration, queue execution, remaining IGB
type/object/post-load bodies, decoder cleanup
and loader preparation/cleanup still require their reached implementations. See
[IGB_DECODE.md](IGB_DECODE.md),
[IGB_TYPE_MAPPING.md](IGB_TYPE_MAPPING.md),
[IGB_RUNTIME_BOOTSTRAP.md](IGB_RUNTIME_BOOTSTRAP.md),
[IGB_LOADING_PATHS.md](IGB_LOADING_PATHS.md), [IGB_DEPENDENCIES.md](IGB_DEPENDENCIES.md),
[LOADER_GLOBALS.md](LOADER_GLOBALS.md), [LOADER_DEPENDENCY.md](LOADER_DEPENDENCY.md),
[IGB_STRING_POOL.md](IGB_STRING_POOL.md),
[NATIVE_STAGE_SERVICES.md](NATIVE_STAGE_SERVICES.md),
[LOADER_REQUEST.md](LOADER_REQUEST.md), [LOADER_DIRECTORY.md](LOADER_DIRECTORY.md)
and [CONTEXT_INFO.md](CONTEXT_INFO.md).
See [APPLICATION_FRAME.md](APPLICATION_FRAME.md) and
[PLAYER_INPUT_MATH.md](PLAYER_INPUT_MATH.md).

The release must preserve original gameplay and local one-to-four-player input,
provide one `DW4_PC.exe` entry point, import the user's supported ROM without a
compiler or manual extraction, and keep portable saves beside the executable.
The F1 ImGui menu must offer working, persistent PC options, including freely
editable keyboard bindings and independent controller slots. Preserve existing
saves during any format migration. The native renderer must qualify 16:9 camera,
culling and HUD, higher resolution, MSAA/VSync and HD textures. Presentation up
to 300 FPS must not accelerate simulation or audio. Modding, optional modernized
behavior and online four-player co-op remain part of the full target.

The DolRecomp/ModernGekko experiment is set aside. Preserve its source patches,
local modules, packages, imported data and saves; do not continue its performance
or packaging queue unless the owner changes direction. Its CPU translation and
Dolphin-derived services are separate from `dw4_core`. Its completed import,
F1 menu and portable-profile work can inform future authored native integration,
but none of those runtime tests validates the current native engine. Historical
records remain in [RECOMPILATION.md](RECOMPILATION.md),
[ROM_IMPORT.md](ROM_IMPORT.md) and [PC_APPLICATION.md](PC_APPLICATION.md).
Aurora is an optional source-level graphics/platform candidate, not a selected
backend or an implemented migration. Evaluate a concrete recovered boundary
before making that dependency decision.

## Implemented boundary

Version 0.45 connects common collision traversal to native typed node dispatch
and the actual static geometry candidate producer. Runtime dispatch indices
belong to an explicit native class registration, independently of serialized
IGB type indices. Group/transform/geometry handlers retain their filter,
path, matrix, time and child-loop phases; unrecovered transform updates and
missing-bound construction remain reached services.

Canonical geometry attributes share live vertex, unsigned-16 index and
primitive-group owners through the existing scene cache. The original mapped
primitive scan preserves format gates, physical accesses, strip winding,
cursor publication and unmap order. Candidate production retains captured list
and matrix owners while reloading the original segment and current callback
metadata for each hit. A pending callback does not replay its triangle scan.
The September 19 extensions add native AABox scratch transforms, the original
matrix inverse, CHECKLAND floor handling and CHECKWALL's three-pass wall query.
Retained per-delivery receivers connect actual native geometry to HITFLOOR and
HITWALL. The original player-frame test registers all five stage collision infos
and completes a prepared neutral frame using the recovered global clock and
verified null-input transform updates. Retries preserve completed frame and
traversal effects. Nonnull transform input and other concrete update bindings,
other bound classes, missing-bound construction, CHECKWALLFULL and original
manager startup remain required. See [HITLAND_WALL.md](HITLAND_WALL.md),
[HITLAND_FLOOR.md](HITLAND_FLOOR.md),
[MATRIX_INVERSE.md](MATRIX_INVERSE.md),
[HITLAND_GEOMETRY.md](HITLAND_GEOMETRY.md),
[HITLAND_PRIMITIVES.md](HITLAND_PRIMITIVES.md) and
[HITLAND_NODE_DISPATCH.md](HITLAND_NODE_DISPATCH.md).

The following versioned boundaries describe earlier milestones; their missing
operations are superseded where the current boundary above connects them.

Version 0.44 represents actual `beHitLandModelInfo` owners separately from
HITLANDDELIVER. The canonical graph retains live masks, child lists, bounds and
dynamic source-transform links. `HitLandQuery` owns reusable segment/candidate
pools with query counts independent of list counts. Common traversal preserves
the original scratch preparation, name-before-kind publication, captured low
mask/bounds/counts and fresh physical child/mask reads. Visitor reset and the
retained concrete `+64` wrapper are implemented; typed node dispatch, attribute
traversal, complete CHECKLAND reduction and CHECKWALL passes remain required.
Independent segment/normal/triangle arithmetic checks do not establish that
missing complete backend. The existing original player frame still stops at
HITLANDMODEL. See [HITLAND_QUERY.md](HITLAND_QUERY.md),
[HITLAND_TRAVERSAL.md](HITLAND_TRAVERSAL.md) and
[HITLAND_INTERSECTION.md](HITLAND_INTERSECTION.md).

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

`NativeGameClock` separately owns the recovered global long timer, including
start, pause, reset, stop and restart. Its platform endpoint supplies monotonic
PC nanoseconds; it does not advance manager context or invent a frame delta.
HITLAND transform dispatch captures that clock's result and directly executes
the verified `igTransform`/`beCopyTransform` update body. Canonical nullable
transform inputs are distinct from a copy transform's source-matrix link.
Nonnull inputs retain their own reached update boundary. See
[GAME_CLOCK.md](GAME_CLOCK.md) and [ACTOR_TRANSFORM.md](ACTOR_TRANSFORM.md).

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

The provisional walking preview accepts keyboard and SDL-mapped gamepad input.
Original-game device assignment, freely editable saved bindings and verified
Xbox, PlayStation and GameCube gameplay mappings remain separate tasks. Native
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
