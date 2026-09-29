# Original animation timing and layer mixing

Version 0.18 implements the numeric animation-state clock, MOTIONSET blend
preparation, source time wrapping and ordered two-layer bone arithmetic in the
portable core. Integer-time sampling now reaches decoded Enbaya channels and
complete skin poses. These are dependencies for ACTION2, not a complete action
manager or animation combiner. The walking preview still uses its host controller
and single-clip selection.

Version 0.19 builds the prepared combiner/state and explicit MOTIONSET path on
these primitives. Source identities now have an owned library/runtime registry.
See [ANIMATION_COMBINER.md](ANIMATION_COMBINER.md) for that increment and its
remaining ACTION/actor boundaries.

## Original evidence

The implementation was recovered from the supplied USA GameCube DOL, disc ID
`GDJEB2`, revision 0, SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.
Addresses identify guest instructions, never host pointers.

| Routine or instruction range | Recovered behavior |
| --- | --- |
| `801D3628` | Shared scaled-time cache and per-state local time |
| `800A4130` | Signed saturating/truncating integer conversion, despite the reference symbol's unsigned-looking name |
| `800A3FDC` | Signed integer conversion through double followed by single rounding |
| `802F11CC..802F122C` | MOTIONSET authored frame conversion and no-interpolation branch |
| `801D00F8..801D0160` | Explicit blend origin and start time |
| `801D0668..801D06AC` | Elapsed blend fraction and fused weight arithmetic |
| `802F10D0..802F1130` | Speed change preserving the time origin |
| `801E52FC..801E53C0` | Shared source timestamp cache and integer-to-source-time mapping |
| `801C2058..801C2090`, field offsets `804AFC28/2C` | Both source cache fields default to `-1` |
| `801E4FE8` | Five source play modes and shared remainder cache |
| `802EFDC8`, store at `802EFE04` | ACTION2 initialization of the shared time offset |
| `801D10C0..801D111C` | Ordered quaternion/translation combination after source selection |

No original bytes or disassembly are distributed. The existing reference audit
is in UPSTREAM_AUDIT.md; quaternion and hierarchy evidence
is in [ANIMATION_POSE.md](ANIMATION_POSE.md).

## State and blend clocks

`AnimationStateClock` owns time scale, time bias and sampled local time.
`AnimationClockCache` belongs to an explicitly owned runtime and is shared by
its states, matching the original cache scope without process-global state.
The state clock takes the low unsigned 32 bits of `world_time >> 18`, converts
to float, multiplies by the float scale, then saturates/truncates to a signed
64-bit integer. Its bit pattern is shifted left 18 and added to the bias modulo
64 bits. The cache compares the full world timestamp and float scale; per-state
bias is applied after lookup. Its original initial sentinel is world time
all-ones, scale `-1`, cached scaled value zero.

`change_animation_speed()` adds the old scaled origin and subtracts the new
scaled origin before publishing the new scale. It leaves the last sampled local
time intact until `advance_animation_clock()` runs. Finite input overflow uses
the original saturation result instead of an undefined host float-to-int cast.

`prepare_animation_blend()` preserves single-precision multiply/divide order:
eight authored frames become `133333336` time units, rather than a rounded
double-precision eight-sixtieths. Zero frames or the explicit no-interpolation
branch produce duration one, start ratio one and range zero. Negative authored
durations remain negative. Blend start time one is the pending-start marker.
`begin_animation_blend()` establishes the requested animation origin at the
given world timestamp; it does not select which state should begin.

`animation_blend_ratio()` converts signed elapsed time and duration through
double to float, divides, clamps only values above one, then performs a fused
multiply-add with ratio range and start ratio. Negative elapsed values and
zero-duration NaN/infinity results are preserved. The layer-sampling boundary
rejects unusable weights; it does not replace them with a guessed pose.

## Source mapping and ownership

`AnimationSourceClock` owns one source's runtime mode and last mapped timestamp.
Unlike the state clock, the source uses a **signed** coarse 32-bit value. It
multiplies that value by float bits `0x3989705f`. Durations at or below float
`0x3d088889` use fallback duration `0x3d4cccce`. This is not ordinary double
nanosecond conversion.

`AnimationWrapContext` owns the remainder cache shared between sources and modes.
Its offset starts at zero. Explicit `initialize_action_offset()` installs the
original ACTION2 constant `0xbd088889`, approximately `-1/30`, without resetting
the remainder cache. Both source cache fields start at `-1`, as established by
the original nonserialized metadata defaults. An initial timestamp of `-1`
therefore returns `-1` before mapping. Subsequent cache hits compare the complete
input timestamp. Changing mode at the same timestamp deliberately retains that
cached result. Complete source-object construction and lifetime remain pending.

| Mode | Original mapped-time behavior |
| --- | --- |
| REPEAT (`0`), LOOP (`3`) | Truncated signed cycle number, fused remainder, nonpositive remainder replaced by positive zero |
| CLAMP (`1`) | Strict upper/lower comparisons before the remainder path |
| BOUNCE (`2`) | Reverse the remainder on odd signed cycle numbers |
| Offset clamp (`4`) | Add the ACTION offset to duration, then use the clamp path |

Exact CLAMP endpoints fall through to the remainder path and return zero.
Negative REPEAT values use truncation toward zero rather than floor-based modulo.
The cached remainder precedes BOUNCE reversal. These details are intentional.
Unknown numeric modes return the original default zero. Generic invalid duration,
non-finite input and out-of-range signed cycle conversion are explicit errors.

Copies of source clocks are independent. When several tracks or combiners refer
to the same original source, their caller must share one clock instance and the
runtime's wrapping context. A complete source-identity/lifetime registry remains
part of ACTION/combiner integration. Serialized IGB records stay immutable.

## Decoded poses and ordered layers

`sample_animation_at_local_time()` and `sample_animation_pose_at_local_time()`
apply source mapping before the existing checked channel/skin evaluation. The
source duration must match the decoded clip. Both caches are published only
after sampling succeeds, including hierarchy validation for the pose API.
Serialized duration rounding within the existing 1.1 ns tolerance is accepted;
short-source fallback that exceeds the decoded clip's extent is an explicit
unsupported boundary until terminal stream sampling has been verified.

`blend_animation_samples()` combines two already-bound, complete bone arrays in
their supplied order. Each clip's translation mask must already have selected
its sampled or constant translation. Quaternions use the recovered interpolation
polynomial, shortest-sign selection and original nonunit behavior. There is no
normalization or endpoint shortcut. For each translation component, the incoming
value is multiplied by the float weight first; a fused multiply-add then adds
the base value times the separately rounded `1 - weight`. The CMake source
options preserve separate rounding except for explicit `std::fma` calls.

Arrays must have the same nonzero count, at most 1,024 bones. Weights must be
finite and within `[0,1]`; channel values and results must be finite. Inputs are
unchanged on failure. This operation does not select states, retire transitions,
resolve partial bindings, or create the original interrupted-transition snapshot.

## Validation and remaining integration

The asset-free timing suite has 62 checks. The animation graph suite has 66,
including integer-time sampling, failure-atomic caches, ordered layers and a
fixed cancellation case that distinguishes fused translation arithmetic from a
separately rounded sum. Original files are optional for local integration:

```powershell
.\build\Release\dw4_animation_timing_tests.exe
.\build\Release\dw4_animations_tests.exe
.\build\Release\dw4_animations_tests.exe 'PATH\files\ndmw.afs' 850 5665
```

An ignored instruction probe compares 30,000 clock, wrapping, source-cache,
blend and speed operations in 1,000 shared-cache programs. Results match the
native implementation, excluding NaN payloads. It supplies prepared object
memory and the stream duration service, stopping before compressed sampling;
this does not validate original object construction or the whole ACTION loop.
A separate probe compares 10,000 layer tuples / 70,000 float32 components with
zero mismatches. Half use rotations sampled from the supplied decoded Agumon
clips and half synthetic rotations; translations and weights are synthetic.

All 82 Agumon clips pass five modes at six local-time fractions, including times
beyond the clip duration: 2,460 timed complete poses and finite self-layer
evaluation, alongside the existing direct-time checks. These are integration
checks, not independent codec or recorded gameplay-frame comparisons.

Next implement retained ACTION2 work, original action lookup/cursor behavior,
combiner state creation and retirement, cached-pose snapshots on interrupted
transitions, and update order. The original snapshot reads the last evaluated
pose; resampling at command time is not equivalent. WAIT/FADE/FLASH handlers,
bone priorities, cycle matching, events and complete actor/controller lifetime
remain unresolved. Integrate these with model sequence ACTION dispatch before
claiming original player animation, combat or local multiplayer.
