# Local validation

GitHub Actions is disabled for this repository at the owner's request. Hosted
workflows must not be restored or dispatched without a new explicit request.
Source pushes remain part of the development workflow.

Build and test on the local Windows machine with `scripts/build.ps1`. Use
`-Configuration Release -CoreOnly` for portable-core changes. Application and
preview changes use the regular application build and the existing finite
smoke checks. Keep generated builds, packages and original game data out of Git.

The former hosted build definition remains recoverable in repository history.
Current local validation evidence is recorded in `docs/VALIDATION.md`.
