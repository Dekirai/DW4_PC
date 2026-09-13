# Actual paired-store differential checker

Run `powershell -ExecutionPolicy Bypass -File scripts/test-recomp-psq-store.ps1`
from the repository root on Windows x64 with the Visual Studio C++ workload.
The Dolphin checkout must contain the pinned commit
`55c7b023fa0f4eba1cf3fdbbb25b1c5ec468d5ac` and the current type-4 store change.
The checker builds and runs locally; it does not build the game module, launch
the game, require original assets, or change runtime source.

The wrapper extracts the **actual** `gqr_scale`, `psq_type_size`,
`psq_store_value`, `psq_check_enabled`, `ppc_psq_store` and exception functions
from each source revision. The baseline also extracts its original
`psq_quantize_int`, which uses `ldexpf`; the current quantizer comes directly
from the current header. Renamed external symbols let the implementations
coexist. Each variant includes its own complete original `cpu.h` and
`types.h`, so CPUState, address mapping, byte order, reservations, memory writes
and callback paths use runtime code. A CPUState source-layout mismatch fails
closed. Function extraction requires exactly one definition and balanced
braces. Helpers compile as C with MSVC `/O2 /fp:strict /W4 /WX`, in separate
translation units without LTCG. The harness uses C++20 and the same options.

The harness compares both current out-of-line and current inline execution
against the pinned out-of-line baseline, including:

- Eight GQR types and all 64 signed scales, crossed with W/indexed/LSQE and
  all 16 host rounding/FTZ/DAZ combinations. A masked high GQR index selects
  live GQR4. PSE is clear throughout, exercising its absence from the guard.
- Signed zero, double/float subnormals and boundaries, finite overflow,
  infinities, signed quiet/signaling NaNs and payloads, clamp and truncation
  boundaries with neighboring doubles/floats, and deterministic raw inputs.
- Cached/uncached RAM and EXRAM, unaligned stores, memory boundaries,
  unmapped external writes, absent callbacks, absent EXRAM, and 32-bit
  effective-address wrap. Fixtures allocate 128 bytes per RAM region.
- Valid matching/nonmatching reservations and invalid reservations, with
  cross-granule stores. An explicit address matrix supplements the rotating
  address profiles in the numerical corpus.
- Return values, all CPUState bytes (normalizing only fixture allocation
  identity), FPSCR, memory and complete MXCSR status/control; every combination
  of preexisting MXCSR status bits is covered in an additional matrix.
- Ordered journal/external callback events, byte widths, addresses, preimages
  or values, reservation state, second-lane bits, GQR, FPSCR and MXCSR at each
  event. Both callback kinds can change lane one and GQR on their first event.
  A separate known-result check requires the second byte to use the mutated
  lane with the original scale and verifies byte order, journal preimages,
  reservation invalidation and address wrap without trusting either helper.

Checks remain active in optimized builds and failures produce a nonzero exit
code. Floating-point exceptions stay masked; all status/control bits are
compared and the original MXCSR is restored. The harness does not test
unmasked host traps or concurrent CPU mutation from another host thread.

The finite timing uses the actual current header helper and current external
helper in identical loops, with live GQR4 type 4/scale 0, changing input lanes,
and an 8 KiB RAM output. Separate translation units retain the out-of-line
call boundary; a final CPU/memory comparison and a volatile checksum retain
side effects. Nine trials of two million paired stores alternate execution
order after warmup. The report records median nanoseconds per pair and their
ratio and min/max. Timing is deferred if a `DW4_PC.exe` process is present
immediately before timing, and marked contended if one appears by its end.
The checker never stops or interacts with the game. This is a synthetic helper measurement; it does not measure game FPS,
movie conversion as a whole, rendering or audio.

All extracted files, objects, assembly listings, executable and reports are
under ignored `build/recomp-psq-store-check/`. `latest.json` records results;
`invocation.json` records pinned Git blobs, both revisions' input hashes,
generated source hashes, checker hashes and compiler provenance. The wrapper
checks that source hashes remained stable during execution. The earlier
quantizer-only checker separately isolates the old `ldexpf` factor change;
this checker verifies the entire store wrapper and callback behavior.

Runtime source provenance: ModernGekko's vendored Dolphin GXRuntime at the
pinned Git revision above, GPL-3.0-or-later. This directory contains only the
new harness and runner; extracted upstream implementations remain generated
build outputs.
