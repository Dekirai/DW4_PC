# Native source-port strategy

Updated 2026-09-19 for the owner's September 18 direction: reconstruct Digimon
World 4 as a complete native PC game with authored, maintainable source. Resume
`dw4_core` and the existing native application. Set DolRecomp/ModernGekko aside
and preserve its work, rather than extending it as the active implementation.

This document supersedes the September 13 continuation plan. It does not claim
new gameplay, rendering, audio or completed collision acceptance from the change
of direction. Each implementation still needs its own original-data evidence.

## Decision and deliverable

The deliverable is the original game and engine behavior reconstructed in native
C++20, with explicit PC services and the complete source needed to maintain and
extend that implementation. Original game assets remain user-supplied data;
they are not public repository content. Generated PowerPC C, a translated game
module or an embedded console runtime does not satisfy this source deliverable.

Keep the working native asset, Lua, animation, actor, player and collision code.
Complete its reached original behavior and connect it to original startup and
frame scheduling. The original-asset walking preview is useful for inspection,
but its provisional movement and camera cannot become the fidelity baseline.
Do not manufacture successful no-ops or skip required effects to reach a visible
scene. Retain unresolved behavior as an explicit implementation boundary.

The earlier recompilation integration remains historical reference. It compiles
original CPU code for Windows while retaining guest state, addresses, timing and
Dolphin-derived services. Packaging or branding changes cannot turn that into
the requested authored source reconstruction. Do not optimize, rebuild or extend
that experiment as the active queue unless the owner changes direction.

## Repository and build inventory

| Location | Active role and evidence boundary |
| --- | --- |
| `src/`, `include/dw4/` | Authored C++20 native core and application: assets, Lua 4, animation, actors, scripts, player state and collision. Selected behavior is recovered; the complete game is not. |
| `tests/`, `tools/` | Synthetic checks, original-data inspectors and focused comparisons with original instructions. A check proves only its stated profile and operations. |
| Root `CMakeLists.txt` | Version 0.45.0; `dw4_core`, inspection tools and optional `dw4_pc` native SDL/OpenGL application. This is the active build. |
| `scripts/build.ps1` | Local MSVC/CMake/Ninja build and CTest; optional `dist/DW4_PC` install; four build jobs by default. |
| `Play-DW4-PC.cmd`, `Start-DW4-PC.cmd` | Native walking-preview and workbench launchers respectively. Neither establishes original gameplay. |
| `docs/`, `licenses/`, `THIRD_PARTY_NOTICES.md` | Recovery evidence, acceptance requirements and component provenance. Preserve these alongside implementation. |
| `.github/` | Local validation documentation. No hosted workflows are permitted. |
| `scripts/*recomp*`, `Play-Recompiled-DW4.cmd`, `patches/` | Set-aside runtime integration and developer tooling, retained for provenance and explicit reference work. Not the active build or acceptance lane. |
| `work/moderngekko/`, `work/recomp-modules/`, `work/recomp-build-record.json` | Ignored historical source checkouts, generated modules and build identities. Generated code is not recovered game source. |
| `dist/DW4_Recomp/` | Preserve every existing package, imported asset directory and adjacent save. The native workbench does not migrate these profiles. |

The native core has no SDL or ImGui dependency. Its optional application uses
SDL3, OpenGL and Dear ImGui; source pins and archive hashes live in CMake.
Host structures and serialized IGB layouts remain separate. Original addresses
identify evidence or data locations, never native pointers. Explicit byte order,
field widths, physical storage, object lifetime and strict arithmetic remain
part of the reconstruction contract.

Build the active project with `scripts/build.ps1 -Jobs 4`. Use its CTest suite
and the relevant Python verifiers. Keep original-data integration results
separate from asset-free tests. Graphics changes require a finite hidden smoke
run and inspection of an application-rendered image. Build refreshed previews
occasionally for manual use; do not launch interactive previews automatically.

## Current native boundary

Version 0.45 connects original static HITLAND geometry to canonical attributes,
vertex/index/group owners, typed node dispatch and actual query candidates.
The producer retains query segments, matrix elements, path aliases, primitive
cursors and callback order. This is part of the original collision pipeline,
not the walking preview's separate mesh-based solver.

The September 19 extensions implement the original matrix inverse, canonical
AABox scratch transform, CHECKLAND floor response and CHECKWALL wall response.
Prepared native geometry integrations reach HITFLOOR/HITWALL. The original player
frame now completes its prepared neutral update with all five stage collision
infos registered, using the recovered global clock and verified null-input
transform updates. See [GAME_CLOCK.md](GAME_CLOCK.md),
[ACTOR_TRANSFORM.md](ACTOR_TRANSFORM.md), [HITLAND_WALL.md](HITLAND_WALL.md),
[MATRIX_INVERSE.md](MATRIX_INVERSE.md) and [HITLAND_FLOOR.md](HITLAND_FLOOR.md)
for original-instruction and supplied-data evidence. The next collision work is
nonnull transform inputs and other update bindings, missing geometry bounds, other bound classes
and CHECKWALLFULL. Deliver those actual responses through HITLANDDELIVER to the
recovered player frame. Dynamic operations are not complete merely because a
test provider supplies a matrix.
See [HITLAND_GEOMETRY.md](HITLAND_GEOMETRY.md),
[HITLAND_NODE_DISPATCH.md](HITLAND_NODE_DISPATCH.md),
[HITLAND_QUERY.md](HITLAND_QUERY.md) and [HITLAND_DELIVER.md](HITLAND_DELIVER.md).

The core already owns the player construction/GENERATE continuation, ordinary
sequence execution, ACTION and selected player-frame operations. Four prepared
original Agumon slots are integration evidence for those calls. Original device
assignment, manager attachment, complete startup, frame scheduling, camera,
rendering, audio, combat, transitions and saves remain required. Prepared
providers and completed prepared neutral frames do not establish a gameplay session or
connected local multiplayer. See [MODEL_FRAME.md](MODEL_FRAME.md),
[MODEL_DISPATCH.md](MODEL_DISPATCH.md) and [VALIDATION.md](VALIDATION.md).

The prepared directional update now completes using the native viewer owner,
inverse and finite MSL heading operations. Another 48 per-RAM updates cover
held movement and release for four independent slots, original SQ_WALK/RUNND1
and SQ_WAIT/WAITD1 transitions, system time and actual collision. Viewer setup
is explicit initial-state preparation, not active camera selection/publication.
The application update publishes its time prefix before the dispatcher and
scheduler. Its serial scheduler now reaches the original common manager
info/RAM traversal and the actual player-frame binding. Seven prepared batches
exercise all four authored player occurrences, including retained second-RAM
suspension. The camera's specialized view inverse is recovered independently.
Original manager registration, other manager bodies, complete startup and
active camera publication remain required. See
[MODEL_MANAGER_FRAME.md](MODEL_MANAGER_FRAME.md), [CAMERA_MATH.md](CAMERA_MATH.md),
[VIEWER.md](VIEWER.md), [SERIAL_SCHEDULER.md](SERIAL_SCHEDULER.md),
[APPLICATION_FRAME.md](APPLICATION_FRAME.md) and [PLAYER_INPUT.md](PLAYER_INPUT.md).

The scheduled system update now coordinates its original reset handling,
time publication, stage request/completion and storage-error phases. Native
messenger operations carry the original pad/camera/sound commands; missing
loader, registry and viewer operations remain retained service boundaries.
Seven supplied-data player batches now enter through the application and
system update with prepared platform samples. Original initialization must
still request and actually load `ndmw.igb`; a null factory request is not
completed startup. See [SYSTEM_FRAME.md](SYSTEM_FRAME.md),
[SYSTEM_STAGE.md](SYSTEM_STAGE.md) and [SYSTEM_STORAGE.md](SYSTEM_STORAGE.md).

The next loader layer is now connected: original name lookup and loading-claim
operations, new/cached directory requests, synchronous path fallback, and the
application's live manager info-registration traversal. The native stage adapter
uses these real bodies. The concrete `igIGBFile` decoder `800528DC` now enters
native stream/header processing, early tables and runtime metatype/slot mapping.
The bounded bootstrap metadata profile recovers twenty object layouts and
forty-seven field-class identities from original registrations. List factories,
full global initialization, object population and post-load effects remain
required. Directory-path preparation `800505E0`, dependency-table caller
`800521EC` and pool-name table `80052554` are now native. Native ArkCore loader
access, string-table interning and synchronous request `8006FA6C` now lead to
the native file factory `8002EBEC` and its required registration `8002F178` in
the prepared original-data prefix. Reflected allocation and object header/field
pass orchestration now compose with the file's concrete post-default pool
assignments. Current-pool lookup shares the canonical manager globals and
retains explicit pool preparation. Actual file/parent metadata registration,
field defaults, pool initialization/allocation/release and complete dependency
decoding still need connection. See [IGB_FILE_FACTORY.md](IGB_FILE_FACTORY.md),
[IGB_OBJECT_INIT.md](IGB_OBJECT_INIT.md) and [IGB_FILE_POOLS.md](IGB_FILE_POOLS.md).
The actual registration runner/once guard now composes native ThreadManager
registration `800238A8` and handle-based pool access with explicit prerequisite
flag and manager inputs. Metadata factory `80029E64` preserves the enabled
runtime and reaches its required `igMetaObject` registration `8002A0A8`. The generic type registrar
and callback-list constructor/append preserve original flag and publication
order. Concrete int/bool default handlers copy verified physical bytes inside
initialization. Separate typed metadata/internal-list constructor chains preserve
their real allocation, physical header, default and backing-store operations;
allocator and reflective registration endpoints remain explicit. They do not
prove a complete cold bootstrap. The actual ArkCore constructor and bootstrap
caller now register fundamental metadata with reflection disabled, enabling it
only after registration and callback cleanup return. A separate composed test
enters the real metadata wrapper and direct constructors, publishes busy metadata
and now executes actual `igObject` parent registration, its root callback-list
construction, native name interning and layout probe. Defaults `80066B7C` now
construct `_refCount` and `_memPoolIndex` through the actual specialized factory,
name setter, unsigned-integer default-buffer preparation, finalization and raw
append. The cold chain reaches type publication `800660AC` with reflection still
disabled. See [IGB_OBJECT_DEFAULTS.md](IGB_OBJECT_DEFAULTS.md),
[IGB_META_FIELD_FACTORY.md](IGB_META_FIELD_FACTORY.md) and
[IGB_FIELD_OPERATIONS.md](IGB_FIELD_OPERATIONS.md).
Native parent inheritance and name assignment compose with raw
child/field list append and growth. Typed metadata-list constructors now feed
actual ArkCore initial capacity/reserve in a separate cold-prefix integration;
allocator virtuals remain explicit. Continue from that cold path through the
runtime registry publication, remaining metadata schema, layout and the outer
ArkCore creation/publication wrapper, then connect initialized state to the
decoder. Preserve the existing enabled decoder fixture as separate evidence;
enabled reflection with a null fundamental metadata cache recursively requests
itself before publication. See [IGB_ARK_CORE_BOOTSTRAP.md](IGB_ARK_CORE_BOOTSTRAP.md),
[IGB_METADATA_REGISTRATION.md](IGB_METADATA_REGISTRATION.md) and
[IGB_METADATA_OPERATIONS.md](IGB_METADATA_OPERATIONS.md).
See also [IGB_OBJECT_REGISTRATION.md](IGB_OBJECT_REGISTRATION.md),
[IGB_TYPE_LIST_FACTORY.md](IGB_TYPE_LIST_FACTORY.md) and
[IGB_POINTER_LIST.md](IGB_POINTER_LIST.md).
Continue reference-driven directory list allocation; do not substitute
name-only metadata or null pointers for those constructed owners. See
[IGB_TYPE_REGISTRATION.md](IGB_TYPE_REGISTRATION.md),
[IGB_REGISTRATION_CALLBACKS.md](IGB_REGISTRATION_CALLBACKS.md),
[IGB_THREAD_REGISTRATION.md](IGB_THREAD_REGISTRATION.md),
[IGB_BOOTSTRAP_OBJECT.md](IGB_BOOTSTRAP_OBJECT.md),
[IGB_FIELD_REGISTRY_FACTORY.md](IGB_FIELD_REGISTRY_FACTORY.md),
[IGB_METADATA_FACTORY.md](IGB_METADATA_FACTORY.md) and
[IGB_FIELD_DEFAULTS.md](IGB_FIELD_DEFAULTS.md).
Reuse existing document decoders and
canonical native owners where their behavior agrees; parsing alone must not
set the original ready/success fields. Pool/factory operations, loader lifecycle,
queue execution, manager registration and remaining concrete info handlers
remain part of original startup. See [NATIVE_STAGE_SERVICES.md](NATIVE_STAGE_SERVICES.md).

## Recovery tools and platform choices

These tool descriptions retain the upstream sources consulted for the September
13 review. They describe possible research aids, not installed or validated DW4
integrations. Choose tools for a concrete reached function rather than stopping
native implementation to rebuild the entire toolchain.

| Tool or approach | Role in the active source port | Limit |
| --- | --- | --- |
| [decomp-toolkit](https://github.com/encounter/decomp-toolkit) | Analyze the supplied DOL/REL, use identified symbols/relocations, and split original objects for targeted reconstruction. | It does not recover complete portable source automatically. Keep binary inputs and generated objects local. |
| [objdiff](https://github.com/encounter/objdiff) | Compare a recovered function built for the original target with its original object where suitable compiler/configuration evidence exists. | A matching PowerPC object and correct native x64 behavior require separate evidence. |
| [Ghidra](https://github.com/NationalSecurityAgency/ghidra) | Investigate instructions, signatures, types, call order and shared state. | Decompiled pseudocode is research input; implement and validate its behavior explicitly. |
| [Aurora](https://github.com/encounter/aurora) | Optional source-level graphics/platform candidate after a recovered SDK-style boundary is available. | Not a selected backend or implemented migration. Confirm required GX behavior and DW4 platform coverage before adoption. |
| [DolRecomp](https://github.com/ExpansionPak/DolRecomp) | Preserve the earlier experiment and its recorded observations as reference. | CPU translation is not the requested authored game source. Runtime optimization and packaging are set aside. |

The [upstream audit](UPSTREAM_AUDIT.md) examined `ivanno4317/dw4-gc` at
`794294e17fef0b3ab5eeb15fda9bd57b0dc98a2b`: mostly SDK/library source and
incomplete Alchemy wrappers, not a complete gameplay reconstruction. Reuse its
verified symbols, splits and provenance. Its counts and configured matching
labels are historical evidence, not a fresh matching build or completion rate.

A complete byte-matching PowerPC build is not a prerequisite for each native
function. Recover the original behavior from sufficient evidence, then verify
native ownership, byte order, aliasing, arithmetic and effects separately.
Conversely, compiling a function successfully or producing the same typical
output cannot establish its full original behavior.

## Ordered continuation plan

1. **Complete the reached collision operations.** Build on the implemented
   inverse, AABox transform and CHECKLAND/CHECKWALL responses, preserving
   arithmetic, type dispatch, ownership and publication order. Recover missing bounds,
   nonnull transform input and other concrete updates, and CHECKWALLFULL. Acceptance:
   focused original-instruction comparisons, meaningful synthetic edge cases,
   and supplied-data queries whose actual responses reach HITLANDDELIVER and
   the player consumer. A static candidate list alone cannot close this step.
2. **Connect the original player frame.** Complete the remaining position-lock,
   ride, wall/floor and script/context operations in their original order.
   Preserve manager/work captures and completed effects across suspension;
   retain independent player and pad owners. Acceptance: original input,
   motion, collision, waits and script effects execute together through the
   recovered frame, with compared state and call order. Four prepared neutral
   frames, the first directional frame and 48 repeated held/released per-RAM
   updates now complete. The actual manager traversal and serial scheduler
   also complete seven prepared four-player batches. Next connect active
   camera publication and qualify additional movement/combat paths. Do not replace the
   preview controller until the original path actually supplies its behavior.
3. **Recover startup and the game scheduler.** Implement the reached manager,
   sound, layer/text and later startup-script services. Connect actual manager
   registration, device polling, time advancement, ACTION traversal, camera,
   HUD and rendering. The application timing prefix and serial manager batch
   and common player-manager traversal are available; registered order and
   the remaining actual manager bodies still require recovery.
   System update orchestration now uses its concrete stage/storage state
   machines and native messenger, but their reached loader/scene operations
   and actual initialization still require connection.
   Acceptance: an authored native boot-to-game path using
   original assets, original player creation and actual scheduled frames; no
   prepared test provider standing in for a required game operation.
4. **Complete the original playable game.** Reconstruct combat and its damage/
   status services, enemies, inventory, equipment, progression, scene transitions,
   original audio and persistence. Qualify every game route and supported player
   character, including independent local one-to-four-player sessions. Acceptance:
   compared original behavior and progression through the complete game, plus
   save/reload, controller ownership and audible output verification. One room,
   one character or one successful attack is an increment, not completion.
5. **Qualify native PC presentation and controls.** Integrate persistent F1
   graphics/input/QoL controls, freely editable keyboard bindings and controller
   handling into that engine. Verify 16:9 camera/culling/HUD, 4K, display modes,
   VSync and MSAA. Measure original simulation and audio timing before separating
   presentation frequency; target at least 60 FPS on documented hardware and
   options up to 300 FPS without speeding up the game. Acceptance uses actual
   gameplay frame times, input, applied settings and persistence, not sliders or
   results from the historical runtime. Confirm supported languages against
   actual text, fonts and the original selector.
6. **Complete content extensions and online play.** Build versioned asset/mod
   contracts around recovered loading and game state. Add HD packs, custom maps,
   Digimon and weapons, seeded randomizers and optional balance/bug fixes while
   preserving an original behavior mode. Validate local four-player ownership
   before networking; verify multi-machine state, RNG, compatible mods, latency,
   disconnects and save ownership. Acceptance requires the requested extensions
   operating in the native game, not only recognized manifests.
7. **Finish the native release path.** Provide one EXE entry point that validates
   the user's supported ROM and prepares required assets without a developer
   toolchain or manual extraction. Preserve imported data and portable saves,
   with explicit versioned migration where needed. Verify repeat launch and a
   clean-machine release containing the native game, required dependencies and
   notices, without original game assets in the public repository.

These steps define integration order, not a reduction in scope. Preserve later
requirements in today's ownership, timing and data interfaces. Full acceptance
remains in [ROADMAP.md](ROADMAP.md) and
[ORIGINAL_BASELINE.md](ORIGINAL_BASELINE.md); none of the above gates is declared
complete by this document.

## Preserved historical evidence

The September 13 project review established two separate builds. The original
recompiled game did not call `dw4_core`. Its package reached the main menu in a
short 1x Vulkan window near original speed; later opening-movie intervals still
ran substantially below original speed, and audible/gameplay acceptance remained
open. Its F1 controls, ROM import, embedded resources and portable-profile tests
belong to that runtime only. Preserve their reports and known limitations in
[RECOMPILATION.md](RECOMPILATION.md),
[RECOMP_PERFORMANCE.md](RECOMP_PERFORMANCE.md),
[RECOMP_CPU_PROFILING.md](RECOMP_CPU_PROFILING.md),
[RECOMP_INGAME_MENU.md](RECOMP_INGAME_MENU.md), [ROM_IMPORT.md](ROM_IMPORT.md)
and [PC_APPLICATION.md](PC_APPLICATION.md).

The explicit recompilation launcher, four-job local build default and earlier
identity checks remain recorded in repository history. The proposed built-in
module remains a historical packaging design in
[RECOMP_BUILTIN_MODULE.md](RECOMP_BUILTIN_MODULE.md), not an active implementation
task. Preserve every package and save; the new direction neither invalidates
those observations nor transfers their features to the native game.
