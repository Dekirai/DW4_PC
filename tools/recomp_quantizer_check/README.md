# Quantizer equivalence checker

Run `powershell -ExecutionPolicy Bypass -File scripts/test-recomp-quantizer.ps1`
from the repository root. This compiles only this small MSVC x64 checker and
never builds a game module, exports data, or launches the game. Generated C,
objects, executable, logs, and JSON evidence stay in ignored
`build/recomp-quantizer-check/`.

The wrapper extracts `psq_quantize_int` from the nested Dolphin repository at
commit `55c7b023fa0f4eba1cf3fdbbb25b1c5ec468d5ac` and from the current
`GXRuntime/src/core/cpu.c`. The helpers retain their original bodies, with only
their names/linkage changed. Each is compiled as C with the actual GXRuntime
`core/types.h`, `/O2 /fp:strict`, and no link-time optimization. Independent
factor checks use the extracted expressions with volatile inputs.

The checker compares returned integers and all six MXCSR sticky exception
flags for all 64 signed GQR scales, all four integer quantizer bounds, all four
rounding modes, and all four FTZ/DAZ combinations. Each invocation begins with
cleared sticky flags and masked exceptions. Inputs include signed zero, double
and float extrema/subnormals, infinities, signed quiet/signaling NaN payloads,
double and float neighbors of clamp thresholds and rounding midpoints,
truncation boundaries, and deterministic raw random double bits. Every factor
is also checked in all 16 environments. The original host MXCSR is restored.

Failures return a nonzero exit status and are checked in optimized builds.
The report records case counts; invocation metadata records compiler options,
source and executable hashes, and the pinned baseline Git blob. This establishes
helper equivalence on the tested Windows x64 compiler/runtime and corpus; it
does not establish game performance or gameplay correctness.

Source provenance: the extracted helper and GXRuntime type utilities are from
the local Dolphin/GXRuntime source, licensed GPL-3.0-or-later.
