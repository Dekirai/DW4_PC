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

The owner's current goal is an optimized DolRecomp-based PC game with correct
original behavior and local one-to-four-player play using separate controllers.
Prioritize the reported low frame rate and audio stutter. Target at least 60 FPS
on documented test hardware, with presentation options up to 300 FPS that do not
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

The owner further requires an integrated PC application: one DW4_PC.exe entry
point with the original game assets, no separate moderngekko-run.exe process
required to play, portable save data beside the executable, and an in-game
ImGui menu opened with F1. Move the current File/View commands into that menu
and apply supported settings during play. Do not satisfy this by renaming the
runner or concealing another executable. The Dolphin-derived service/runtime
dependency and the user-facing PC integration are different concerns; explain
remaining dependencies accurately. Preserve existing saves while introducing
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

The owner selected DolRecomp and the recommended ModernGekko template as the
next implementation path. Prioritize building and validating the original DW4
recompilation pipeline, using the owner's local tool sources. Keep the existing
native core and its tests as reference work. Generated C, a successful compiler
run or a boot screen does not establish a complete original game. Verify actual
gameplay, rendering, audio, saves and independent local controllers. See
docs/RECOMPILATION.md for the current source locations and evidence.
Use scripts/build-recomp.ps1 for that pipeline. It builds the runtime and
generated module locally with explicit source pins; it never launches the game.
Keep the manual-core build and recompilation validation scopes separate.
The recompiled game does not call the manually recovered engine functions.
Do not resume a manual full-engine rewrite as the main queue; use existing
analysis and tests selectively to diagnose observed original-game failures.

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
