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

The owner's current goal is the complete original-faithful game, including local
one-to-four-player play with separate connected controllers. Prioritize original
behavior and reference investigation over quality-of-life additions. The walking
preview's host movement, camera and timing do not satisfy that goal. Keep unknown
native effects explicit and compare recovered animation, collision, camera,
combat, progression and persistence behavior against original evidence.

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
