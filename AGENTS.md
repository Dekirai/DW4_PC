# Working on DW4_PC

This is a native C++20 port effort with an original-asset walking preview and workbench.
Keep all source code, identifiers, comments, documentation, README content,
commit messages and application text in English, as requested by the owner.
Do not use GitHub Actions. The owner disabled hosted workflows to avoid consuming
the account's Actions minutes. Build, test and package locally. Do not add,
restore, enable or dispatch hosted workflows unless the owner explicitly changes
this instruction. Ordinary source commits and pushes remain authorized.
Read README.md, docs/ARCHITECTURE.md and docs/ROADMAP.md before extending scope.
Keep the provisional walking preview distinct from the original game baseline;
do not claim stored preferences implement gameplay. Continue toward the next
acceptance gate in the roadmap.

The owner's current goal is a complete native source port: reconstruct the
original game and engine behavior in authored, maintainable C++ source and set
the DolRecomp/ModernGekko project aside. Resume the existing dw4_core and native
application; do not replace this goal with a recompiled or embedded emulated
game. Preserve the prior recompilation work, packages and saves as historical
reference, but do not extend its runtime, optimize it or rebuild it as the active
queue. Generated PowerPC C is not the requested recovered source deliverable.
Keep correct original behavior and local one-to-four-player play using separate
controllers. Target at least 60 FPS on documented test hardware, with
presentation options up to 300 FPS that do not
speed up simulation or audio. Verify game timing before uncapping presentation;
a frame-limit setting alone is not high-frame-rate support. Keep 16:9, higher
render resolution, HD texture replacements, an ImGui menu for graphics, cheats,
save states and other QoL, content mods, and online four-player co-op in scope.
Expose the languages actually supported by the supplied game data through the
ImGui menu after verifying text/font coverage and the original language selector.
Do not infer complete language support from filenames alone. Source, README and
development documentation stay in English; in-game localization is intentional.
The final distribution should have one simple EXE entry point: the user supplies
their ROM and the application validates and prepares its required assets without
requiring a developer toolchain or manual extraction. These are acceptance
targets, not implemented claims. Preserve mod and multiplayer compatibility
while optimizing; do not remove validation or change gameplay merely for speed.
The walking preview's host movement, camera and timing do not satisfy this goal.

The owner further requires an integrated native PC application: one DW4_PC.exe
entry point with user-supplied original assets, portable save data beside the
executable and an in-game ImGui menu opened with F1. Move the current File/View
commands into that menu and apply supported settings during play. Implement
these requirements in the authored native engine; the set-aside runner's
integration is not their completion. The final engine must not depend on the
DolRecomp/ModernGekko runtime or its Dolphin-derived services. Explain any
remaining dependencies accurately and preserve existing saves while introducing
the portable layout. ROM selection and automatic asset preparation remain
required, as do modding, local/online co-op and all earlier acceptance targets.

The product must present DW4-specific setup and in-game UI. Do not ship the
wholesale upstream Sys tree or unrelated console/game resources in previews.
Keep component attribution and licenses in third-party notices; removing
branding does not remove the underlying runtime dependency. The owner also
requires keyboard play and freely editable, saved key bindings in the F1 menu.
Preserve existing controller profiles, independent player slots and input
blocking while settings are open. Verify actual input and persistence rather
than storing unused preferences.

The native core is the active implementation. Resume the original collision and
player-frame boundary documented in docs/ROADMAP.md and docs/PORTING_STRATEGY.md.
Recover reached operations from original evidence, retaining ownership, callback
order, arithmetic and failure behavior. Complete the collision response before
claiming the existing candidate producer implements original player movement.
Connect the recovered startup and frame scheduler rather than promoting the
preview's provisional movement or camera to original behavior. Use DTK, objdiff
and Ghidra selectively for evidence; a matching-build lane is a research aid, not
a replacement for the authored native engine. Verify gameplay, rendering, audio,
saves and independent controllers in that engine. Previous recompilation tests
prove only their historical implementation; see docs/RECOMPILATION.md for those
records. Do not transfer their completed-feature claims to the native core.

Use the current repository checkout. Preserve user changes and keep generated
builds in ignored build/ or dist/. Original game data is supplied outside this
repository through --data; do not commit game binaries, textures, archive
payloads or generated disassembly. Source code and synthetic fixtures are the
public deliverables. Record source provenance when adapting reference work.

Core code must not depend on SDL or ImGui. Validate file ranges before reads,
bound allocation sizes, use explicit byte order and widths, and keep guest
addresses distinct from host pointers. Parser changes need meaningful malformed
and representative valid synthetic-input checks. Never use assert-only tests
whose checks disappear in Release builds.

Build on Windows using scripts/build.ps1; the portable core can use CMake with
DW4_BUILD_APP=OFF. Run CTest and Python verifier tests after relevant changes.
For graphics integration, run a finite --smoke-test and inspect an app-rendered
screenshot. Keep original game-data integration results separate from asset-free
local tests.

Build refreshed preview snapshots occasionally for the owner to launch manually.
Do not automatically open an interactive preview unless the owner explicitly
asks to launch it. Finite hidden smoke tests remain part of graphics validation.
