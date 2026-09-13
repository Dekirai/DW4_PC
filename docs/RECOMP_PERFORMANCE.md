# Guarded scheduler optimization: local measurements

The guarded idle optimization reduces CPU work during the verified original
boot sequence while preserving the compiled loop's timing. It does not uncap
presentation or validate the owner's reported main-menu audio stutter.

The subsequent 90-second real-device tests expose a later opening-movie
slowdown that these logo measurements did not cover. See
[RECOMP_AUDIO.md](RECOMP_AUDIO.md) for the 1x/3x comparison and source-starvation
evidence. The measurements below remain scoped to the earlier boot logo.

The later guarded cache batching comparison uses matching binaries and explicit
neutral controller profiles. It confirms that batching executes and preserves
the isolated instruction/timing tests, but shows no clear frame-rate gain in
the opening movie. See the [neutral-input cache measurements](RECOMP_AUDIO.md#neutral-input-cache-batching-comparison)
for source-audio observations and the startup/scene alignment limitations.

## Method and result

The final comparison used the same packaged runner and unchanged `/O2 /fp:strict`
game DLL, with the optimization disabled and enabled in separate fresh Unicode
profiles. Each run was limited to 30 seconds, used headless Vulkan at 640x528
(1x EFB), NullSound, no mods and normal emulation speed. The runner discovered
the DLL beside itself without an explicit module argument. Both runs loaded
the files imported from the owner's ISO, outside the package.

Host: Windows 11 x64, AMD Ryzen 7 7800X3D and 32 GB RAM. The host inventory
includes an RTX 5070 Ti and an AMD integrated adapter; the active Vulkan adapter
was not captured in this measurement. This is a CPU-efficiency comparison,
not a GPU performance guarantee.

The final approximately ten-second window uses actual observation durations:

| Metric | Disabled | Enabled |
| --- | ---: | ---: |
| Unique-frame counter advances | 614 | 608 |
| Frame observation duration | 10.16884 s | 10.10543 s |
| Counter advances per second | 60.3805 | 60.1657 |
| Process CPU time | 5.65625 s | 3.015625 s |
| CPU observation duration | 10.16867 s | 10.10602 s |
| Average busy CPU cores | 0.55624 | 0.29840 |
| Process CPU milliseconds per counter advance | 9.21213 | 4.95991 |
| Final reported simulation speed | 0.998986 | 0.999106 |

This is about **46% less process CPU time per counter advance**. It is not a
46% increase in gameplay FPS or a machine-wide CPU-utilization percentage.
The final five-second CPU costs were 9.11885 and 4.55943 milliseconds per
advance. Differences in startup and screenshot timing limit precision; this
is a bounded local pair, not a statistical benchmark suite.

Both final runs stopped cleanly, retained the expected input hashes, reported
no runtime alerts or SMC failures, and produced the same visibly rendered
Intrinsic Alchemy boot logo at 640x527. The screenshots were captured near
27 seconds and decoded for inspection. Earlier screenshots near 21 seconds
caught a dark transition and were not accepted as meaningful visual evidence.

Enabled execution accepted 482,658 guarded idle skips, representing 24,211,799
elided 258-cycle repetitions. Native-dispatch counters still count actual
calls; elided repetitions have their own counters. Zero reported interpreter
fallback steps does not exclude fallback JIT execution or host-hook handling.

## Correctness and limits

The actual-header checker passes 10,345 checks across 2,048 timing/state
equivalence cases and 67,200,954 independently interpreted original instructions.
It compares the locally emitted C loop and verifies the actual DOL chunk hash.
Whole-cycle batches retain the final real queue poll, rounded timing overshoot
and timebase remainder. GPU synchronization is followed by fresh guard reads.
The existing ABI checker still passes all CPU and module layout comparisons.
See [the patch notes](../patches/README.md) for guards and invalidation behavior.

NullSound disables host queuing, mixing, resampling and device callbacks.
The manual frontend defaults to 1920x1080 (3x EFB) and normally uses Cubeb audio.
Neither audible output nor the main-menu/gameplay workload is established by
this test. The owner's tested binary was not identified by hash. Original
timing, real audio, longer gameplay, local four-controller play, modded profiles
and online co-op still require separate acceptance. The 300 FPS target remains
future work with presentation separated from simulation timing.

## Reproducible evidence

Final package: `dist/DW4_Recomp/PC-Preview-20260913-061351/`.
Disabled run: `work/recomp-startup/20260913-061527-ef10375a/`.
Enabled run: `work/recomp-startup/20260913-061641-1d159c65/`.
Import checks: `work/recomp-import/20260913-061422-1e7c4dcd/`.
Combined CPU comparison: `work/v46-package-performance.json`.
These local artifacts are ignored and contain no source deliverables.

The runner SHA256 is
`DAB8E7D60567DFB5FC3D2065A0342E7F344590425DCB6E9A6AF755DB14309421`.
The unchanged optimized module SHA256 is
`526A56A0BCE6C675CAF1274F1E76F54A455C243DBF84A1235A5721EA97C6DAA9`.
The build and package records retain source pins, patch hashes and ABI evidence.
See [RECOMPILATION.md](RECOMPILATION.md) for the bounded harness and
[ROM_IMPORT.md](ROM_IMPORT.md) for package preparation.

## Empty cache metadata guard

The fallback JIT now skips its per-address metadata erasure loop when all three
live metadata sets are empty. The guard runs after the original physical block
erasure. Translation, valid-bit updates, native SMC invalidation, guest state,
cycle accounting and populated-set handling remain on their existing paths.
The checker compiles the actual pinned and changed invalidation methods with
instrumented dependencies: 42,603 checks across 3,276 cases pass under PowerShell
7 and 5.1. Cases include populated/mixed sets, metadata added by block erasure,
forced invalidation, page/BAT boundaries, wrapped ranges and callback ordering.

The local comparison does **not** demonstrate a frame-rate gain. The earlier
`DC44D97D…` EXE completed a 70-second neutral run at about 9.17 unique counter
advances/s in its final ten seconds. The new `128E85A8…` EXE reached its first
status substantially later and ended at about 6.69 advances/s, in an earlier
movie scene. Approximate matching counter intervals also show higher CPU cost
in the new runs, so startup differences alone do not explain the result.
The cause remains unresolved; this is not an accepted performance improvement.

Evidence is under `work/recomp-startup/20260913-084815-6fa640ab/` (earlier build),
`20260913-091018-20188f4e/` (new build, probe disabled) and
`20260913-091311-19329903/` (new build, probe enabled). The local derived
`work/recomp-cache-probe-aligned-comparison.json` preserves the selected counter
ranges and observation times. All three use the unchanged optimized game DLL,
1x Vulkan on the observed RTX 5070 Ti, neutral bindings and Cubeb volume zero.
The completed [in-process probe](RECOMP_CPU_PROFILING.md#first-completed-local-probe)
identifies the original movie conversion block as the next measured candidate.

## Exact paired-single quantizer factor

The next candidate changes only the power-of-two factor in GXRuntime's
`psq_quantize_int`, called by integer paired-single stores. It replaces
`ldexpf(1.0f, scale)` with `f32_value((u32)(scale + 127) << 23)`. The decoded GQR
scale is a signed six-bit value in `[-32, 31]`, so every factor is an exactly
representable normal binary32 value. The double-to-float cast, float multiply,
NaN handling, clamp comparisons and final integer conversion retain their
original order and expressions. This change does not alter guest timing or
replace the movie conversion algorithm.

`scripts/test-recomp-quantizer.ps1` extracts the actual pinned and changed
helpers from `GXRuntime/src/core/cpu.c` and compiles them separately as C with
MSVC x64 `/O2 /fp:strict`, the actual GXRuntime types and no link-time
optimization. The local result has zero differences across **876,544 comparisons
of returned integers and all six MXCSR sticky exception flags**, plus **1,024
comparisons of factor bit patterns**. Coverage includes all 64 scales, four
integer bounds, four rounding modes and four FTZ/DAZ combinations, with boundary
values, subnormals, infinities, signed quiet/signaling NaNs and deterministic
random double bits.
The checker restores the original MXCSR after execution. See
[the checker notes](../tools/recomp_quantizer_check/README.md) for the corpus
and compilation method.

Evidence is in `build/recomp-quantizer-check/latest.json` and the adjacent
`invocation.json`, which records source, compiler and executable identities.
This verifies helper equivalence for the tested compiler and corpus. The game
module was subsequently rebuilt and passed bounded original startup. The
comparison below measures the combined build; it does not isolate a quantizer
FPS gain from the accompanying dispatch-probe isolation change.

## Module source identity for the next build

The module cache now includes a SHA256 identity of the actual required and
recursively collected GXRuntime/module-template sources, build inputs and ABI
headers. The observed tree contains 63 inputs. Sorted relative UTF-8 paths and
their content hashes make the identity independent of the checkout location;
new eligible inputs also change it. This ensures that a local `cpu.c` change
selects a different module cache entry even when upstream revision pins are
unchanged.

The manifest records every input and the completed DLL hash. A cache hit must
match both source and binary provenance. Publishing an existing DLL from
`module-build` requires matching source and completed-build records, including
the cache key and DLL hash. Source identity is checked again before compiling
and after compilation, including before publication; mismatches fail while
preserving the output.

The local `scripts/test-recomp-module-source.ps1 -InspectRuntimeSource` report
passes 188 synthetic checks and records the 63-file source identity. Three
symlink fixtures were skipped because Windows denied the required privilege
(error 1314). Evidence is in `build/recomp-module-source-check/latest.json` and
the adjacent logs. The checker covers source collection and provenance-record
matching without building or launching a game.

The complete local build finished on 2026-09-13 at 10:04:24 UTC and selected a
new cache entry ending in `3ec65bd201094460`. The resulting module SHA256 is
`4772F7340AA284DEDF32E74BC68D4EABEFEA234479862685FBBE469F11A7FACE`.
The actual loaded descriptor passes all 49 CPU-field and 16 descriptor-field
comparisons (ABI 3, CPU size 3528). A subsequent identical port command reports
a cache hit and preserves the DLL hash. Source and completion manifests agree
with the recorded 63-file identity and DLL hash.

Package `dist/DW4_Recomp/PC-Preview-20260913-100511/` contains that module and
launcher SHA256
`1104466CB5227E7EB81B0C9EC8BE74F9F4B20883F070B8DD91F2AE932178A53B`.
The package has one EXE entry point and its existing DLL/resource dependencies;
packaging neither includes original assets nor launches the application. Build
and cache evidence remains in `work/recomp-quantizer-build.log`,
`work/recomp-quantizer-cache-hit.log` and `work/recomp-build-record.json`.

## Rebuilt module comparison

Two subsequent 90-second neutral runs used 1x Vulkan on the same observed RTX
5070 Ti/Ryzen 7 7800X3D host, fresh profiles and verified Cubeb volume zero. The
new package ran first (`20260913-100635-cc822ac6`); the preserved `128E85A8…`
reference followed (`20260913-100825-a3aab091`). Both exited normally with no
failed SMC chunks or device errors. Captured images show original movie content.

The approximate corresponding unique-frame counter interval is:

| Metric | Preserved reference | Rebuilt module and runtime |
| --- | ---: | ---: |
| Actual counter bounds | 1150–1251 | 1151–1250 |
| Observation duration | 21.4003 s | 11.8839 s |
| Unique counter advances/s | 4.7196 | 8.3306 |
| Process CPU ms/counter advance | 137.8403 | 104.7980 |
| Fresh DMA frames/s | 5,048.5 | 8,868.4 |
| Audio replay events over that interval | 496 | 239 |
| Device callback gaps over two periods | 0 | 0 |

That interval shows about 24% less process CPU time per counter advance in the
combined new build. Wall-time results and startup vary substantially across
runs. This pair has different runtime and module hashes, slightly different
counter bounds and no repeated statistical trials; it does not isolate the
factor optimization or establish a general gameplay FPS gain. Earlier `DC44D97D…`
measurements were faster still, under earlier observed conditions.

The full-run final ten-second windows are in different movie phases: 4.92
counter advances/s in the reference versus 4.49 in the new build. They must not
be presented as a same-scene comparison. Neither package sustains original
movie speed, and audio-source replay remains. Muted device telemetry cannot
establish audible quality. Evidence and the exact selected samples are retained
under `work/recomp-startup/` and in `work/recomp-quantizer-comparison.json`.
