# Working on DW4_PC

This is a native C++20 port effort, presently a data-reader/workbench milestone.
Read README.md, docs/ARCHITECTURE.md and docs/ROADMAP.md before extending scope.
Do not describe the workbench as a playable game or claim stored preferences are
implemented gameplay. Continue toward the next acceptance gate in the roadmap.

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
screenshot. Keep local game-data integration results separate from asset-free CI.
