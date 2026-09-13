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

Explicit `--headless-audio --audio Cubeb` diagnostics preserve real device
mixing in an otherwise headless run. A fresh stream generation must confirm
actual Cubeb initialization/start; fallback and device failures stop the host
loop. With `--mute`, successful zero-volume application is required before
playback. Ordinary headless runs retain NullSound. Automation reports audio
counters and the existing performance headroom estimate.

The frontend adaptations add a branded GUI entry point, explicit ROM import
and validation diagnostics, a hidden two-frame image check, and bounded import
paths with checked export failures and preservation of existing folders.
The launcher and optional developer runner share `RunFrontendSession`.
Graphical Play destroys launcher SDL/ImGui resources before starting the game
in-process, with a shared diagnostic log; `DW4_PC.exe --run` keeps inherited
output pipes. The default profile is `savedata` beside the actual EXE, while
explicit profile paths and legacy data remain supported/preserved. Both
Windows entry points use SDL's GUI startup and UTF-8 path boundaries. The
manual PowerShell launcher waits for the integrated game's exit. See
[ROM_IMPORT.md](../docs/ROM_IMPORT.md) for packaging and test scope.

Automation applies and clears each complete pad state while holding the same
recursive controller-state lock as the input readers. The lock is released
before waits, acknowledgments or host callbacks; this prevents a reader from
observing an intermediate clear during publication.

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

The port helper also hashes the actual module inputs through
`tools/module_source_identity.hpp`. Required GXRuntime and ABI files are
combined with recursively collected source, include and module-template build
inputs; the current tree contains 63 files. The sorted relative paths and
SHA256 content hashes contribute to the cache key and are listed in the
manifest. A local source edit therefore invalidates the module cache without
requiring an upstream revision change. Missing required inputs, unsupported
symlinks and invalid reads fail collection.

Cached modules must match their recorded source identity and DLL hash. Pending
modules require `build-source-identity.txt` and `build-complete.txt` to match the
cache key and source manifest before publication; the completion record must
also match the completed DLL hash.
The helper checks source stability around compilation and publication and
preserves output when provenance validation fails. The source checker passes
188 synthetic checks, with three Windows symlink fixtures skipped for missing
privilege; it does not build or launch a game. See
[RECOMP_PERFORMANCE.md](../docs/RECOMP_PERFORMANCE.md#module-source-identity-for-the-next-build)
for local evidence and the remaining integration scope.

## RecompCore adaptations

`recompcore-headless-shader-progress.patch` applies separately to the pinned
RecompCore revision above. `Presenter` deliberately creates no ImGui context
for a headless graphics backend, so the shader compilation progress callback
must not read ImGui state in that mode. The patch skips only that visual
callback body. Shader compilation, waiting and result retrieval remain active;
ordinary windowed progress rendering is unchanged.

The audio diagnostics retain separate host counter ownership across stream
shutdown and observe DMA/DTK activity, mixer queue events and Cubeb callback
timing. Resampling, buffering and simulation defaults are unchanged. A
headless-only Vulkan log identifies the actual initialized adapter. See
[RECOMP_AUDIO.md](../docs/RECOMP_AUDIO.md) for interpretation and test scope.

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

`StaticRecompDw4CacheBatch` targets the verified USA cache continuations at
`0x8025D3A4` (`dcbst`), `0x8025D318` (`dcbf`) and `0x8025D2EC` (`dcbi`).
After one real loop-back it combines at most 256 additional cache lines,
within the same timing slice and one identity-mapped MEM1 BAT region. The real
`InvalidateICacheLines` path retains native SMC and fallback-JIT invalidation.
Every original code range is excluded from the batched target, and changed or
hooked loop code, enabled data-cache emulation, pending exceptions, generic
idle overrides, REL modules and unsupported mappings reject batching.

The final loop exit remains a real generated dispatch, including `dcbf`'s
four-cycle system-call path. EE may be clear and guest/PPC MSR may differ only
in EE/RI, which the guarded invalidation path does not consult. Other MSR
guards and pending-exception rejection remain. Native two-cycle charges
and direct five-cycle cache-hook charges remain separate, matching the current
generated/runtime path and its timebase residue. Actual dispatch and hook
counters do not include batched repetitions; the separate `dw4_cache` shutdown
line reports their count and cycle components. `-CacheMode Disabled` and
`Enabled` select the option in the startup harness for a same-binary comparison.
`scripts/test-recomp-cache.ps1` compares the actual emitted loop, independent
original instruction execution and the policy's register/address/timing effects,
including all 16 independent guest/PPC EE/RI combinations. The expanded check
passes 73,728 cases without differences. Bounded optional cache-guard snapshots
exist only in the active diagnostic probe; ordinary execution does not sample
or log them. Actual movie measurements have not established a frame-rate gain;
see [RECOMP_PERFORMANCE.md](../docs/RECOMP_PERFORMANCE.md).

The GXRuntime `psq_quantize_int` helper now constructs its power-of-two factor
directly as `f32_value((u32)(scale + 127) << 23)` instead of
`ldexpf(1.0f, scale)`. GQR decoding limits the signed scale to `[-32, 31]`, where
every factor is an exact normal binary32 value. The existing cast, multiply,
NaN handling, clamps and integer conversion remain unchanged. The checker
extracts both actual helper bodies and uses MSVC x64 `/O2 /fp:strict` without
link-time optimization: 876,544 comparisons of returned integers and all six
MXCSR sticky exception flags, plus 1,024 comparisons of factor bit patterns,
have zero differences across the tested scales, integer bounds, rounding modes
and FTZ/DAZ settings. This is helper equivalence evidence; original-game
integration and any FPS gain from
the quantizer change remain unverified. See
[the checker notes](../tools/recomp_quantizer_check/README.md) and
[performance evidence](../docs/RECOMP_PERFORMANCE.md#exact-paired-single-quantizer-factor).

The subsequent PC application integration embeds an allowlisted set of service
resources, omits the loose upstream `Sys` tree from new packages, supplies
DW4-specific setup/error text, and preserves component attribution in licenses.
The shared INI parser can consume embedded defaults with the same merge order.
Portable runtime settings use `Runtime.ini`; checked legacy migration preserves
existing settings and saves. The keyboard editor updates actual controller
expressions through the F1 menu, with checked persistence and the existing game
input gate. Legacy native-window speed/display hotkeys are disabled when the
integrated UI owns application controls. See [PC_APPLICATION.md](../docs/PC_APPLICATION.md)
for scope and validation evidence.

The type-4 paired-store path shares the exact quantizer in `cpu.h`. Its whole
store checker verifies 17,301,504 baseline comparisons including memory,
exceptions, reservations and callback mutation/order; this is not evidence of
an original-game FPS gain.

The affected Dolphin runtime files identify their license as GPL-2.0-or-later;
GXRuntime's `cpu.c` identifies its license as GPL-3.0-or-later. These adaptations
retain their respective upstream licenses, separately from this repository's
MIT source; see the [RecompCore license text](../licenses/RecompCore-GPL-2.0-or-later.txt),
the [GPL version 3 text](../licenses/ModernGekko-GPL-3.0.txt) and
[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md).

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
