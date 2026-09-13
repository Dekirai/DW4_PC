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

The frontend adaptations add a branded GUI entry point, explicit ROM import
and validation diagnostics, a hidden two-frame image check, and bounded import
paths with checked export failures and preservation of existing folders. Both
Windows executables use SDL GUI entry points and consistent UTF-8 path
boundaries. The manual PowerShell launcher still waits for the actual runner
exit. See [ROM_IMPORT.md](../docs/ROM_IMPORT.md) for packaging and test scope.

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

## RecompCore adaptations

`recompcore-headless-shader-progress.patch` applies separately to the pinned
RecompCore revision above. `Presenter` deliberately creates no ImGui context
for a headless graphics backend, so the shader compilation progress callback
must not read ImGui state in that mode. The patch skips only that visual
callback body. Shader compilation, waiting and result retrieval remain active;
ordinary windowed progress rendering is unchanged.

The same patch bounds file-system-table recursion before a disc constructor
traverses untrusted directory entries, rejects zero entry counts, and uses a
64-bit table-size product. Frontend validation happens after disc construction
and cannot replace these parser checks. Synthetic malformed ROMs cover deeply
nested directories and invalid counts; original-disc export has separate tests.

The guarded `StaticRecompDw4Idle` option targets only the verified USA scheduler
loop at `0x80261558`. It requires a completed native zero-mask loop-back, the
original executable chunk identity, unchanged live instructions and expected
scheduler/context/interrupt state. Pending exceptions, unverified or hooked
code, forced fallback, lockstep and unsupported module profiles reject the
optimization. It adds only whole 258-cycle repetitions to the ordinary cycle
charge and retains a final actual native poll; it does not set downcount to
zero or call the generic `CoreTiming::Idle`. Normal timebase accounting remains
in one shared path. Configured GPU synchronization must finish before the
eligibility checks are read again.

`scripts/test-recomp-idle.ps1` compares the actual policy header with the locally
emitted C loop and an independent original-opcode execution. Its fixtures and
generated code remain ignored. State/timing equivalence is separate from CPU
performance: use paired hidden runs with `-IdleMode Disabled` and `Enabled` to
measure actual process CPU cost while checking frame progress and images.

The affected upstream files identify their license as GPL-2.0-or-later. This
upstream-derived patch retains that license, separately from this
repository's MIT source; see the [upstream license text](../licenses/RecompCore-GPL-2.0-or-later.txt)
and [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

The build wrapper verifies the exact patch independently within each owning
Git repository. It rejects unexpected root or recursive dependency changes,
staged/untracked source files and initialized submodules at different pins;
it does not hide unrelated changes behind the expected modified submodule.
Both patch hashes are recorded alongside the resulting runner and module.
Expected and observed patches are read explicitly as UTF-8, with full object
IDs so comparisons do not depend on PowerShell's console encoding or Git's
repository-dependent abbreviation length. `tests/recomp_source_patch_tests.ps1`
checks the actual guard functions on isolated repositories, including valid
new files added with intent-to-add and rejection of unrelated changes.
