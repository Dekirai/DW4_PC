# Local validation

GitHub Actions is disabled for this repository at the owner's request. Hosted
workflows must not be restored or dispatched without a new explicit request.
Source pushes remain part of the development workflow.

The native source port is the active local validation scope:

- Native core/workbench: use `scripts/build.ps1 -Jobs 4`, which runs
  CTest after building. Add `-Configuration Release -CoreOnly` for portable-core
  changes. Graphics changes require the existing finite smoke checks.

The DolRecomp/ModernGekko experiment is set aside. Its scripts and historical
evidence in `docs/RECOMPILATION.md` remain preserved; do not continue that build
or optimization queue as the native source-port task.

Core CTest success alone does not validate complete original gameplay. Builds never
start an interactive game. Keep generated builds, packages and original game
data out of Git. See `docs/PORTING_STRATEGY.md` for the architecture and sequence.

The former hosted build definition remains recoverable in repository history.
Native-core evidence is recorded in `docs/VALIDATION.md`. Historical recompilation
results do not establish native gameplay, performance or UI implementation.
