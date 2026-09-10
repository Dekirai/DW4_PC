# Original HITBOX box arithmetic

`original_hitbox_box(matrix, offset, box_size)` returns an owned `CollisionBox`
with three-component `minimum` and `maximum` arrays. It prepares the numeric
world box used by an original MODELCTRL hitbox subcommand. Collision recipients,
damage, scripts and frame scheduling remain separate services.

The source matrix is the current MODELCTRL ONE node matrix. The authored offset
is transformed first; `box_size` then supplies full extents on the world axes.
Neither actor skinning caches nor a preview camera matrix belong in this call.
Negative extents deliberately produce inverted bounds; no min/max sorting or
positivity requirement is added.

## Recovered operations

Evidence is the supplied USA revision-0 DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original bytes and generated
instruction listings remain outside tracked source.

| Original range | Numeric effect |
| --- | --- |
| `80310004..80310028` | Initialize both AABox endpoints from the offset. |
| `80310034 -> 80123840 -> 80122D04` | Transform the degenerate box. |
| `8012D498..8012D538` | For each XYZW row: rounded Y product, fused X, fused Z, separate translation add. |
| `8012D53C..8012D590` | Skip division only for W exactly one; otherwise apply the epsilon branch, round reciprocal `1/W`, then multiply XYZ separately. |
| `80122DFC..80122F6C` | Construct eight corner candidates and reduce in order using strict comparisons. |
| `80310040..803100CC` | Center=`fma(0.5,min,rounded(0.5*max))`; min=center-halfExtent; max=halfExtent+center. |

The epsilon word at `805669E4` is exactly `0x350637BD`, approximately
`5e-7`. If `abs(W)` is strictly smaller, the denominator becomes **positive**
epsilon. This loses a small negative W's sign; negative W exactly at the
threshold retains its sign. Zero W follows the same epsilon branch. A computed
infinite W can yield finite zero XYZ through the reciprocal and is preserved.
The separate reciprocal rounding matters: numerator 3 and W 7 produce the word
`0x3EDB6DB8`, rather than the directly divided float word `0x3EDB6DB7`.

For this degenerate finite box, each edge difference is positive zero. Its
products may carry either zero sign, but every finite corner compares equal.
The original reduction retains the first transformed point on equality, including
its zero sign. The helper uses that proven simplification. The ignored oracle
still executes all eight corners and their ordered comparisons independently.

The subsequent center arithmetic is retained. Replacing it with the transformed
point would change NI behavior: a min-normal point becomes zero when both half
operations flush, while a point twice min-normal survives.

## Numerical scope and primary evidence

The DOL's startup/new-thread profile is NI=1 with RN=00 (nearest-even). Scalar
loads retain finite subnormal inputs. Arithmetic flushes a result whose exact
magnitude is strictly below `2^-126` to signed zero before single rounding.
Scalar stores retain the represented word. The source is compiled strictly;
ordinary host nearest rounding and enabled subnormal inputs are prerequisites.
It does not change host FP control registers or emulate original exception flags.

Dolphin's primary scalar implementation distinguishes `fmuls`, fused `fmadds`,
`fadds`, `fdivs` and `frsp`; division uses the quotient followed by ForceSingle.
The helper retains these boundaries rather than using a reciprocal estimate.
[Scalar interpreter](https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/Source/Core/Core/PowerPC/Interpreter/Interpreter_FloatingPoint.cpp).

ForceSingle applies NI flushing before single rounding. The related hardware-test
source checks retained subnormal inputs and the multiplication boundary that
rounding alone would turn into min-normal. Scalar load/store conversion is
distinct from unconditional PSQ-store flushing.
[FP helpers](https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/Source/Core/Core/PowerPC/Interpreter/Interpreter_FPUtils.h),
[NI test](https://github.com/dolphin-emu/hwtests/blob/f28077b139eec18967f60db6ce1e15b182dfeac0/cputest/ni.cpp),
[Scalar loads/stores](https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/Source/Core/Core/PowerPC/Interpreter/Interpreter_LoadStore.cpp).

The exact FMA min-normal deficit handling follows the same pre-round rule as
the established animation matrix helpers. Extreme deficits such as
`2^-126 - 2^-298` are an inference from that rule, checked by integer arithmetic;
they were not measured on physical Gekko hardware here. A double-only interpreter
can lose such a deficit. The native helper uses the exact double product and
TwoSum residual only at the min-normal boundary, preserving ordinary float FMA
rounding elsewhere. For reciprocal `1/W`, exact tininess is equivalent to
`abs(W) > 2^126`.

Nonfinite inputs, transformed XYZ or final bounds throw `CollisionMathError`.
This is a checked usable-box boundary, not a claim about the game's treatment of
NaNs in the later corner comparison loop. Finite computed W overflow alone does
not cause rejection when the recovered operations still produce usable XYZ.
No physical-console execution, alternate FPSCR mode, complete ABI/PS1 emulation
or downstream collision fidelity is claimed.

## Validation

The strict Release test suite passes 92 asset-free checks. Tests cover
matrix order, world-axis extents, inverted/zero sizes, homogeneous division,
epsilon boundaries and sign loss, reciprocal rounding/underflow, signed zeros,
subnormal loads, fused cancellation, center flushing and every invalid input word.

The ignored raw oracle runs the actual producer instructions, AABox wrapper,
complete corner reduction and homogeneous point body. Reference arithmetic uses
exact integers and rational quotient rounding, independently of C++ float/FMA
results. It executes no collision, allocator or script service hooks.

The final comparison used `build/Release/dw4_collision_math_tests.exe` linked
against the current `dw4_core` Release library. The successful linked-core
report is retained locally in `work/v26-math-final-core-oracle.log`:

- 4,599 cases; 3,788 finite boxes and 22,728 exact output words.
- 66 nonfinite-input and 745 unusable-box rejections.
- 2,222,379 executed guest instructions; zero mismatches.
- 693 cases use the 231 original authored Set vectors with three explicit synthetic
  current-node matrices. These are numeric input cases, not gameplay replay.

Local reproduction after a parent-owned build:

```powershell
& .\build\Release\dw4_collision_math_tests.exe
& '<PYTHON>' work/v26-hitbox-math-oracle.py --native build/Release/dw4_collision_math_tests.exe
```

The `--oracle` test mode reads 22 decimal binary32 words per row (16 matrix
components, 3 offset components, 3 full extents), returning six words (minimum
XYZ, maximum XYZ) or `ERROR`.
The probe reads the local DOL and saves its bounded report in ignored
`work/v26-hitbox-math-oracle.json`.
