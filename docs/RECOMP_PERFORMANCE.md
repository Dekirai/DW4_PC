# Guarded scheduler optimization: local measurements

The guarded idle optimization reduces CPU work during the verified original
boot sequence while preserving the compiled loop's timing. It does not uncap
presentation or validate the owner's reported main-menu audio stutter.

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
