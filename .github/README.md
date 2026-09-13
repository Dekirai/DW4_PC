# Local validation

GitHub Actions is disabled for this repository at the owner's request. Hosted
workflows must not be restored or dispatched without a new explicit request.
Source pushes remain part of the development workflow.

The repository has two separate local validation scopes:

- Original recompiled game: build with `scripts/build-recomp.ps1 -ExtractedRoot
  '<local extracted game directory>' -Jobs 4`. Check the recorded launcher
  inputs without starting the game with `scripts/start-recomp.ps1 -ValidateOnly`.
  Run the focused recompilation checks relevant to a runtime change and the
  bounded integration checks documented in `docs/RECOMPILATION.md`.
- Retained manual core/workbench: use `scripts/build.ps1 -Jobs 4`, which runs
  CTest after building. Add `-Configuration Release -CoreOnly` for portable-core
  changes. Graphics changes require the existing finite smoke checks.

Core CTest success does not validate the recompiled original game. Builds never
start an interactive game. Keep generated builds, packages and original game
data out of Git. See `docs/PORTING_STRATEGY.md` for the architecture and sequence.

The former hosted build definition remains recoverable in repository history.
Manual-core evidence is recorded in `docs/VALIDATION.md`; original-game evidence
is recorded in `docs/RECOMPILATION.md` and its linked performance, audio and UI reports.
