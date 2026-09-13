# Project review and porting strategy

Reviewed 2026-09-13 against repository baseline `16a6360`. This review covers
tracked source, build scripts, local runtime integration and existing measured
reports. It does not constitute a new gameplay or audible-audio test.

## Decision

Continue the selected DolRecomp path as the playable original-game baseline.
Retain the manually recovered core as a source of format knowledge, independently
checked behavior and potential replacements. Add targeted source recovery to
this pipeline instead of restarting a complete engine implementation.

The current application is **not yet independent of Dolphin-derived services**.
Its original CPU code is compiled for Windows, but it retains guest CPU state,
memory addresses, timing hooks and console graphics/audio services. Removing
filenames, embedding resources or linking the game DLL into the EXE does not
remove those dependencies. A fully native service implementation requires
explicit, validated replacements at those boundaries.

## Repository and build inventory

| Location | Role | What it establishes |
| --- | --- | --- |
| `src/`, `include/dw4/` | Manually recovered C++20 core: assets, AFS/IGB, Lua 4, animation, actors, scripts, movement and collision | Selected recovered behavior; not a complete original engine |
| `tests/`, `tools/` | Synthetic checks, inspection tools and targeted instruction/runtime comparisons | Correctness within each check's stated scope |
| Root `CMakeLists.txt` | Version 0.45.0; `dw4_core`, inspection tools and optional `dw4_pc` SDL/OpenGL workbench | Separate native reference application |
| `scripts/build.ps1` | MSVC environment, CMake/Ninja, CTest, optional `dist/DW4_PC` install | Manual-core/workbench build; four jobs by default |
| `scripts/build-recomp.ps1` | Pinned runtime, patched local DolRecomp/template, generated module, ABI and input checks | Original-game recompilation; four jobs by default; no launch |
| `patches/` | Tracked ModernGekko and runtime integration changes | Reproducible modifications to the external service runtime |
| `work/moderngekko/`, `work/moderngekko/vendor/dolphin/` | Ignored source checkouts used by the recompilation build | Current frontend and service implementation |
| `work/recomp-modules/`, `work/recomp-build-record.json` | Ignored generated game C/module and verified build identities | Local build evidence, not redistributable source recovery |
| `dist/DW4_Recomp/` | Preserved experimental package snapshots | User-facing application with locally supplied assets |
| `docs/`, `licenses/`, `THIRD_PARTY_NOTICES.md` | Research, acceptance evidence and component provenance | Keep alongside subsequent integrations |
| `.github/` | Local validation instructions only | No hosted workflows |

The core deliberately has no SDL/ImGui dependency. Its optional workbench uses
SDL3 (`fa2c02bb6e21974a89ea9824bc53c9932abe5f9c`), Dear ImGui
(`f1cc2ae15e53a861a874c3034aae6798fde194ab`) and OpenGL. CMake verifies the
downloaded source archive hashes. The recompilation is a separate dependency
tree: ModernGekko `5417826c31187d4dadf8588c7aa25bf107782936`, service chassis
`55c7b023fa0f4eba1cf3fdbbb25b1c5ec468d5ac`, and their pinned transitive
dependencies, including graphics backends, Cubeb and ImGui. The local build
record captures the dependency revisions and patch/module identities.

The recompiled game currently **does not call `dw4_core`**. Linking that library
would not automatically connect recovered functions to the original game.
Each replacement needs an explicit guest/host adapter and behavior checks.
Static recompilation also retains runtime fallback machinery. A run reporting
zero interpreter fallbacks does not prove removal of all JIT or console services.

`Play-DW4-PC.cmd` and `Start-DW4-PC.cmd` retain their manual-preview behavior.
`Play-Recompiled-DW4.cmd` explicitly selects the original-game developer build,
validates its recorded identities and uses `work/recomp-user`. Packaged previews
use their own adjacent `savedata`; launch their EXE to retain that profile.
The developer launcher is not a save migration tool.

## What existing work remains useful

- Preserve original-data import, exact input identification, ABI checks, guarded
  module caching, finite diagnostics and all saved comparison reports.
- Preserve the integrated F1 menu, saved keyboard bindings, controller-profile
  handling, ROM import, portable saves and selected embedded resources.
- Retain AFS/IGB parsing, asset identities, texture/mesh/animation interpretation,
  Lua/action analysis and independent arithmetic checks for targeted recovery
  and future mod loading. Do not substitute provisional preview movement for
  original-game behavior.
- Preserve every older package, imported asset directory and user save. New
  runtime formats need explicit migration and backward-compatibility checks.

The earlier [upstream audit](UPSTREAM_AUDIT.md) inspected `ivanno4317/dw4-gc` at
`794294e17fef0b3ab5eeb15fda9bd57b0dc98a2b`. It found mostly SDK/library source
and incomplete Alchemy wrappers, not complete gameplay source. Its counts are
historical pinned evidence, not a fresh matching build or completion percentage.
Its initial asset-viewer recommendation has already been addressed by retained
work; it is not the next development task. Use its symbol/split/checksum evidence
without treating arbitrary generated function names as recovered source.

## Tool comparison

Upstream documentation consulted on 2026-09-13. These are tool capabilities;
integration with DW4 must still be demonstrated.

| Tool or approach | Useful contribution | Required boundary / limitation |
| --- | --- | --- |
| [DolRecomp](https://github.com/ExpansionPak/DolRecomp) | Translates original PowerPC CPU code into C; supports hooks/replacements | CPU translation requires a runtime. It does not itself replace GX, audio, OS or saves. Keep the working integration. |
| [decomp-toolkit](https://github.com/encounter/decomp-toolkit) | DOL/REL analysis, symbols, relocations and splitting into objects for reconstruction | Build a pinned matching-reference lane. Splitting objects does not generate complete portable game source. |
| [objdiff](https://github.com/encounter/objdiff) | Compare rebuilt objects with original objects while recovering functions | Use a suitable original-target compiler/configuration. Matching PowerPC objects and native x64 behavior are separate checks. |
| [Ghidra](https://github.com/NationalSecurityAgency/ghidra) | Disassembly, decompiler, type/call-graph investigation and scripting | Recover signatures, aliases and service ownership; pseudocode still needs manual validation and implementation. |
| [Aurora](https://github.com/encounter/aurora) | Source-level GameCube/Wii compatibility: SDL3, GX through WebGPU/Dawn, PAD, DVD and CARD implementations | Evaluate at recovered SDK-style call boundaries. The current generated guest/MMIO stream is not a drop-in source-level integration. Audio and OS coverage for DW4 require separate investigation. |
| Full manual source port | Direct control over PC services, data structures and gameplay modifications | Largest recovery effort. Use incremental verified replacements; no need to discard the working original-game baseline first. |

Aurora may reduce graphics/platform implementation effort after suitable source
boundaries exist. Switching renderers now would combine unresolved game timing,
guest-memory assumptions and a new graphics integration in one change. First
prove a narrow boundary with a small comparison, then decide from evidence.
A complete matching decompilation is useful but is not a prerequisite for every
native replacement. Conversely, a matching PowerPC function is not automatically
portable across endianness, pointer width, alignment or floating-point behavior.

## Ordered continuation plan

1. **Maintain an identified baseline and reproducible benchmark.** Keep the
   current package/source/module hashes. Separate boot, movie, main menu and
   actual gameplay scenes, with scene bounds, resolution, frame times, simulation
   speed and audio-source/device counters. Continue finite hidden checks; build
   occasional previews for manual use. Acceptance: a repeatable gameplay scene,
   verified keyboard play, independent controller slots, save/reload and audible
   assessment. A short main-menu counter rate alone cannot close this checkpoint.
2. **Recover the measured movie bottleneck.** Investigate `0x8040154c`, identified
   by the existing CPU probe as original movie pixel conversion. Confirm its
   enclosing function, complete callers, buffers and effects before replacing it.
   Start with the smallest measured conversion/quantization overhead. Preserve
   strict floating-point behavior, memory bounds, guest-state publication,
   callback ordering and timing. Compare output/state against the existing
   implementation, then compare the same movie intervals. Keep a guarded fallback
   for unsupported inputs or hooks/mods. Acceptance: no state/output regression
   and a repeatable improvement in CPU/frame time and source-audio continuity.
3. **Add a targeted recovery workspace.** Pin the existing decomp reference and
   compatible DTK/compiler versions in an ignored workspace. Validate the DOL
   checksum before using its symbols/splits. Produce original objects with DTK,
   configure objdiff for one selected function, and import the same addresses
   and types into Ghidra. Keep original objects, DOL and generated disassembly
   local. Track authored source, configuration and provenance only. Acceptance:
   a reproducible original-target comparison plus a separate native behavioral
   check for the first function. Matching success must not be invented from
   configured status labels.
4. **Qualify one native service slice.** Map actual DVD/PAD/CARD/OS/GX/audio
   callers and shared state. Begin with a bounded read-only DVD/filesystem
   operation: define guest-buffer validation, asynchronous completion order,
   errors and ownership, then compare with the current service. Reuse native
   asset readers only where their behavior fits that contract. Follow with input
   and save interfaces, preserving slot ownership and old saves. Acceptance:
   verified original behavior through a selected PC service and an inventory of
   remaining console-service calls. Graphics/Aurora gets a separate isolated
   GX proof with original textures, display lists and shader/material behavior.
5. **Replace remaining services by vertical slices.** Extend tested adapters to
   graphics, audio, timers, threading, memory and interrupts as recovered game
   boundaries permit. Prove actual runtime paths no longer require each removed
   service before removing it from the build. Acceptance: original gameplay and
   saves remain correct, with no dependency on the replaced implementation.
   A fully independent release requires this for every remaining required service.
6. **Complete PC and content features against the playable baseline.** Validate
   16:9 camera/culling/HUD, 4K, display modes, VSync/MSAA and persistent settings.
   Decouple presentation from simulation/audio before offering higher frame
   rates. Add stable asset IDs and versioned mod contracts; then HD packs, custom
   maps/Digimon/weapons, randomizers and optional balance/bug fixes. Validate
   local four-player ownership before networking, including deterministic state,
   RNG, mod compatibility, disconnects and save ownership. Use the detailed
   acceptance gates in [ROADMAP.md](ROADMAP.md).

Single-file module integration remains a separate packaging task described in
[RECOMP_BUILTIN_MODULE.md](RECOMP_BUILTIN_MODULE.md). It is not implemented by
this review and cannot establish performance or removal of console services.

## Current evidence and first adjustments

The existing September 13 final-package report reaches the original main menu
at about 59.74 frame-counter advances/s in a roughly ten-second 1x window,
near normal simulation speed, without audio replay in that window. Real-device
telemetry was muted; audible quality and actual gameplay remain unverified.
The opening movie still falls to roughly 11–14 advances/s in recorded later
intervals. The latest cache batching change reduced calls but did not establish
a performance gain. See [RECOMP_PERFORMANCE.md](RECOMP_PERFORMANCE.md) and
[RECOMP_CPU_PROFILING.md](RECOMP_CPU_PROFILING.md). These are existing results,
not tests rerun for this architecture review.

This review adds an explicit developer recompilation launcher, distinguishes
the two local validation paths, and bounds the manual build to four jobs by
default. It leaves runtime patches, generated modules, assets and saved games
unchanged. Follow-up optimization and source-recovery integration remain work
to perform, not completed features. No hosted workflow is introduced.

Review validation: Windows PowerShell parsed the adjusted build script; the
four-job default and batch launch target were checked without executing a game.
`scripts/start-recomp.ps1 -ValidateOnly` passed the existing launcher, module,
patch, DOL and ABI-report identity checks. All 239 local Markdown links in the
README, architecture, roadmap and this review resolved. No C++ source changed,
so no full rebuild or gameplay acceptance is claimed for this documentation and
entry-point update.
