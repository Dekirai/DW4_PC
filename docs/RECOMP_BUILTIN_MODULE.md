# Built-in game module design

**Design only. The current application still requires `data/game.dll` and the
matching Visual C++ runtime DLLs.** No static game-module link, execution test or
single-file release is established by this audit.

The read-only source audit was performed on 2026-09-13 at repository commit
`86a2200`, including its reconstructed, patched ModernGekko checkout. The next
packaging step can link the generated game code into `DW4_PC.exe` and use the
existing attached-descriptor API. Dynamic mods can remain available. This does
not replace the Dolphin-derived graphics, audio and console services, remove
their license obligations, or establish a performance improvement. Original
assets must still be validated and imported from the owner's supported ROM.

## Existing source path

Paths below are relative to `work/moderngekko/`, the local checkout reconstructed
by `scripts/build-recomp.ps1`. Its source changes are distributed through the
repository's runtime and chassis patches; generated game code stays ignored.
Function names identify the audited call sites even when later edits move lines.

| Source | Existing behavior relevant to a built-in module |
| --- | --- |
| `include/moderngekko/runtime.hpp`, `ModuleSource` | Already exposes `None`, `DynamicPath` and `AttachedDescriptor`; the attached descriptor is a borrowed pointer. |
| `src/runtime/module_loader.cpp`, `ModuleLibrary::Attach` | Validates the descriptor without opening a library; `Close` clears the pointer without owning its storage. |
| `src/runtime/dolphin_runtime.cpp`, `Runtime::Create` | Validates both module sources, creates `ModManager`, then converts the selected source into `StaticRecompModuleSource`. |
| `vendor/dolphin/Source/Core/Core/PowerPC/StaticRecomp/StaticRecompModuleSource.h`, `Attached` | Carries the descriptor and independent host-call callbacks into the CPU core. |
| `vendor/dolphin/Source/Core/Core/PowerPC/StaticRecomp/StaticRecompCore.cpp`, `Init`, `LoadModule`, `Shutdown` | Installs memory/SPR/cache/fallback/mod hooks in `CPUState`; accepts an attached descriptor and performs normal validation; releases its pointer after execution stops. |
| `vendor/dolphin/module-template/module_export.c`, `staticrecomp_get_module` | Returns a static descriptor containing dispatch, state-sync callback and static code/SMC/chunk tables. |
| `vendor/dolphin/module-template/CMakeLists.txt` | Builds a shared library from export glue, six GXRuntime sources and generated chunks; does not currently publish a static archive. |
| `tools/frontend_session.cpp`, `RunMain` called by `RunFrontendSession` | Selects an explicit `--module`, environment override or discovered DLL and currently assigns only `ModuleSource::DynamicPath`. |
| `src/runtime/mod_loader.cpp`, `ModManager::Load`, `LoadDirectories`, `Unload` | Loads and owns optional mod libraries separately from the base game's module source. |
| `tools/netplay_compatibility.cpp`, `CompatibilityFingerprint` | Supports `ModuleLibrary::Attach` already; fingerprints descriptor contents and mods rather than descriptor pointer addresses. |
| `vendor/dolphin/Source/Core/Core/PowerPC/StaticRecomp/StaticRecompLockstep.cpp`, `StaticRecompLockstepVerifier::Init` | Currently finds the memory-write journal setter through the DLL handle; attached modules need an explicit setter path. |
| `tools/moderngekko_port.cpp`, `Build`; `tools/module_source_identity.hpp`, `CollectModuleSourceIdentity` | Cache and publish a DLL using source, input, compiler, code-generator and completed-build identity checks. |

The root wrapper currently builds the launcher before producing the game module.
The root `scripts/package-recomp.ps1`, `scripts/start-recomp.ps1`,
`scripts/test-recomp-startup.ps1` and `scripts/test-recomp-ingame-menu.ps1` also
assume an external module path. Those assumptions must change alongside the
launcher; deleting the packaged DLL alone cannot implement this design.

## ABI, symbols and lifetime

The existing verified Windows x64 ABI is module version 3 and CPU version 3.
`CPUState` is 3,528 bytes with alignment 8; the module descriptor is 112 bytes
with alignment 8. The public and chassis declarations, all field layouts and
nested records must continue to match. The current local ABI report describes
the DLL build, not a future built-in build.

Declare the getter with C linkage and without `dllimport`, then pass its result
to `ModuleSource::AttachedDescriptor`. The existing getter and all tables use
static storage, which satisfies the borrowed pointer's required lifetime.
Directly referencing the getter also retains its glue and dispatch references
when linking a static archive. Do not rely on an otherwise unreferenced archive
member being retained.

Environment access goes through the `CPUState` hook pointers installed by the
chassis. The module does not require a host game-service import library. Its
current DLL imports the Visual C++ runtime, UCRT and Windows system functions.
The generated-object link must preserve the dispatch functions, CPU helpers,
chunk hashes, code coverage, SMC checks and any existing REL metadata.

Do not call GXRuntime `cpu_init` or `cpu_free` for the attached module: those
helpers allocate/free standalone guest RAM, while the chassis owns the live
game's memory. Keep `chassis_on_state_loaded` and its `ppc_fpscr_updated` call.
`StaticRecompCore_Sync.cpp::SyncIn` invokes that callback during normal register
synchronization, so it must not become a once-per-session initialization hook.

Static linkage makes GXRuntime globals live for the whole process. In
`GXRuntime/src/core/cpu.c` these include the journal callback/user data,
`g_ppc_lazy_fp_enabled` and `dolrecomp_call_depth`. Establish their session
initialization and cleanup explicitly, after the previous CPU owner has stopped;
never reset them during active dispatch or ordinary state synchronization.
Repeated sessions must not retain a pointer to a retired lockstep verifier.

The lockstep verifier currently resolves `ppc_set_mem_write_journal` from
`m_library` and disables itself when the symbol is unavailable. Provide an
optional journal setter through the attached source and retain symbol lookup for
dynamic sources. This can extend the host-side source structures without
changing module ABI 3. Merely exporting the setter from the EXE does not fix the
existing lookup through the unopened module-library handle.

Both the getter object and `cpu.c.obj` currently contain `dllexport` directives.
Those are valid EXE exports and can remain during the first object-reuse step;
no DLL startup object is needed. A later private or uniquely named getter can
recompile the small glue without recompiling generated chunks.

## Minimal implementation sequence

1. Build `moderngekko-port` as the bootstrap tool, with no built-in game link.
   Generate/build or validate the cached DLL through the existing pipeline.
   Publish a verified object manifest associated with that completed module.
2. Add an optional built-in object/archive input to the top-level runtime CMake
   project. Link it only into the branded launcher initially. Leave the generic
   runner and developer tools able to use external modules. Use a response file
   for the large object list.
3. Exclude the module's `cpu_exception.c.obj` from that link. The host already
   supplies the same source through `moderngekko_gxruntime_core`; blindly adding
   both definitions can cause duplicate symbols. Preserve one owner for
   `ppc_take_exception` and `ppc_program_exception`.
4. In the frontend's `RunMain`, select the built-in descriptor when no explicit
   developer module override was supplied. Retain ROM/disc/DOL/asset validation,
   attached-descriptor validation and generic frontend discovery. Keep
   `MODERNGEKKO_ENABLE_DYNAMIC_MODULES=ON` and normal `ModManager` callbacks.
5. Connect built-in session state and the optional journal setter as described
   above. No generated dispatch or gameplay behavior needs to change merely to
   attach the descriptor.
6. Perform the final launcher link after module preparation, then record and
   validate that EXE. Update package/start/test records to distinguish
   `module_source=builtin` from a dynamic module path. Omit `data/game.dll` only
   for a verified built-in package; preserve the developer DLL and its checks.

This first implementation can consume existing external objects without changing
the module template. A later reusable producer can share an object-library
target between shared and static outputs. Changing the template participates in
the current module source identity and invalidates its cache; do not silently
carry an old completed-build record across that change.

## Object reuse and provenance

The audited active artifact was under `work/recomp-modules/GDJEB2/`, with cache
suffix `63802febe89b27d2`. It contained 254 generated chunk objects, six GXRuntime
objects and the export object: 261 ordinary COFF objects totaling 370,206,461
bytes. All 63 source hashes listed in its manifest matched the checkout during
this read-only audit. Its `gGDJEB2_recomp.lib` was only a 2,200-byte DLL import
library and cannot supply the generated code for a static link.

The object commands use MSVC x64, C11, `/O2`, `/fp:strict` and `/MD`; no MSVC IPO
flag is present. COFF inspection of the glue/helper objects confirmed
`MSVCRT`/`OLDNAMES` default libraries and the getter/journal exports. These facts
make object reuse plausible with the current `/MD` launcher. They do not prove
an unperformed final static link or establish an independent integrity record
for every existing object.

The new object manifest must identify the exact completed build's expected
objects, not every `.obj` found by a recursive glob. Include object paths,
lengths and hashes, source identity, compiler/version/architecture and compile
options, CPU/module ABI, generated headers/tables and input/code-generator
identity. Require all expected files and reject missing, foreign or changed
inputs. Verify the existing DLL's manifest and completed-build markers, and
recheck sources and inputs before publishing the built-in result. Record the
selected object manifest and final EXE hashes in the build and package records.

Retain the original DLL as a comparison artifact during this transition. A
source hash match alone is insufficient to publish a new EXE from an arbitrary
object directory. If the measured performance work changes generated helpers or
code-generation inputs, build and identify the matching objects first.

Generated C, objects, archives, executable tables and imported game assets remain
in ignored build/work/data locations. Public deliverables are integration source,
synthetic tests, build scripts, manifests describing provenance, and disclosures;
this design does not authorize committing original game payloads.

## Acceptance gates

| Gate | Required evidence |
| --- | --- |
| Clean link and dependencies | Fresh final launcher link with exactly one CPU-exception implementation; inspect imports to confirm no game DLL dependency. Remove the packaged game DLL in an isolated test copy and verify the default selects the attached source. |
| ABI and descriptor | Extend the finite ABI diagnostic to inspect the final EXE's built-in descriptor without dispatching game code. Compare all current public/chassis layouts, game ID, entry point, ranges and chunk hashes with the validated source module. Keep the dynamic-DLL test. |
| Cache rejection | Synthetic missing/changed object, source, generated table, compiler/input identity and incomplete-build cases must fail before packaging. Existing outputs and saves must be preserved on failure. |
| Lifecycle and state | Repeated start/stop sessions in one process, plus a save-state round trip, must preserve FP behavior and reject stale journal pointers or call-depth state. |
| SMC and diagnostics | Normal chunk verification and invalidation remain active. Explicit lockstep mode must report an installed attached-source journal and perform checks, rather than silently disabling itself. |
| Dynamic mods | A synthetic dynamic mod must load against the built-in game, receive lifecycle callbacks, intercept a known guest call and unload safely. Base-module attachment must not disable mod discovery or host-call range checks. |
| Compatibility | Compare attached and dynamic descriptor fingerprints for the same code/tables, then verify that changed module/mod inputs are rejected under the intended compatibility rules. Existing descriptor fingerprinting alone is not a complete native-build identity. |
| Packaged behavior | In isolated profiles, compare bounded startup/menu/gameplay, audio telemetry, saves, input and shutdown against the same generated DLL build. Preserve the existing hidden UI/input gates. No speed or completeness claim follows from static linkage alone. |
| Distribution | Package requires no external game module, retains ROM import, portable saves, component/font licenses and source notices, and accurately lists the CRT libraries that still remain. |

CRT elimination is a separate step. Reusing `/MD` objects does not remove their
runtime imports. A static-CRT release needs a consistent compiler/runtime choice
through the launcher, generated game and linked dependencies, followed by fresh
link and clean-machine checks. Neither a linker-only `/MT` change nor concealing
the DLL inside a self-extractor fulfills this design's built-in module gate.

See [PC_APPLICATION.md](PC_APPLICATION.md) for current implemented packaging and
validation, and [RECOMPILATION.md](RECOMPILATION.md) for the existing build path.
