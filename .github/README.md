# Local validation

GitHub Actions is disabled for this repository at the owner's request. Hosted
workflows must not be restored or dispatched without a new explicit request.
Source pushes remain part of the development workflow.

The native source port is the active local validation scope:

- Core, engine and runtime: use `scripts/build.ps1 -Jobs 4`, which runs
  CTest after building. Add `-CoreOnly` for portable-core changes. Engine and
  graphics changes require hidden `dw4_game --smoke-test` checks.

The DolRecomp/ModernGekko experiment was removed from the source tree; it
remains in Git history only. Do not continue that build or optimization queue.

Core CTest success alone does not validate complete original gameplay. Builds never
start an interactive game. Keep generated builds, packages and original game
data out of Git. See `docs/ARCHITECTURE.md` and `docs/PORTING_STRATEGY.md`.

The former hosted build definition remains recoverable in repository history.
Historical test records of the former core are kept in
`docs/research/VALIDATION.md`; they do not establish engine gameplay.
