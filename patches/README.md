# Local ModernGekko runtime patch

`moderngekko-local-runtime.patch` applies to
[ModernGekko](https://github.com/ExpansionPak/ModernGekko) revision
`5417826c31187d4dadf8588c7aa25bf107782936`, with its pinned RecompCore dependency
`55c7b023fa0f4eba1cf3fdbbb25b1c5ec468d5ac`.

The patch keeps the existing runtime and adds a configurable DolRecomp source
path with an explicit build directory. A local override is labeled `local-source`
in the port helper's provenance; its actual compiler binary SHA256 remains part
of the generated module cache key and manifest. `MODERNGEKKO_BUILD_JOBS` bounds
both code generation and module compilation, defaulting to at most four jobs.
For explicit headless runs, the runner registers a stderr alert handler before
frontend/core initialization; questions return no instead of opening Windows
message boxes. Ordinary windowed behavior is unchanged.

The pinned runtime's public CPU declaration also differed from its own embedded
RecompCore chassis and module template: it advertised ABI 4 and inserted an
unused `cycle_budget` field, while both actual execution sides use GXRuntime
ABI 3. The patch aligns that public declaration's version and complete layout
with the pinned execution ABI. The module loader's version and size checks
remain active; this does not relabel a mismatched layout or modify the local
DolRecomp sources.

These modifications are to GPL-licensed upstream files. The patch is distributed
under GNU GPL version 3, as the upstream sources, with attribution retained; see
[the upstream license text](../licenses/ModernGekko-GPL-3.0.txt) and
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). It is not covered by the
repository's blanket MIT notice. Original game data is not included.

[scripts/build-recomp.ps1](../scripts/build-recomp.ps1) pins the runtime checkout,
checks for unexpected local modifications, applies this patch idempotently,
records recursive dependency revisions and builds locally. It references the
user's local ModernGekko template as pipeline guidance without vendoring that
template. Builds and generated modules remain under ignored `work/`.

## Headless shader progress in RecompCore

`recompcore-headless-shader-progress.patch` applies separately to the pinned
RecompCore revision above. `Presenter` deliberately creates no ImGui context
for a headless graphics backend, so the shader compilation progress callback
must not read ImGui state in that mode. The patch skips only that visual
callback body. Shader compilation, waiting and result retrieval remain active;
ordinary windowed progress rendering is unchanged.

The affected `ShaderCache.cpp` identifies its license as GPL-2.0-or-later. This
small upstream-derived patch retains that license, separately from this
repository's MIT source; see the [upstream license text](../licenses/RecompCore-GPL-2.0-or-later.txt)
and [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

The build wrapper verifies the exact patch independently within each owning
Git repository. It rejects unexpected root or recursive dependency changes,
staged/untracked source files and initialized submodules at different pins;
it does not hide unrelated changes behind the expected modified submodule.
Both patch hashes are recorded alongside the resulting runner and module.
