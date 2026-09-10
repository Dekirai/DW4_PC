# Original collision box arithmetic

`original_hitbox_box(matrix, offset, box_size)` returns an owned `CollisionBox`
with three-component `minimum` and `maximum` arrays. It prepares the numeric
world box used by an original MODELCTRL hitbox subcommand. Collision recipients,
damage, scripts and frame scheduling remain separate services.

The source matrix is the current MODELCTRL ONE node matrix. The authored offset
is transformed first; `box_size` then supplies full extents on the world axes.
Neither actor skinning caches nor a preview camera matrix belong in this call.
Negative extents deliberately produce inverted bounds; no min/max sorting or
positivity requirement is added.

The target-body helpers use the same scalar arithmetic profile:

| API | Original operation |
| --- | --- |
| `original_transform_box(matrix, box)` | `80122D04`, general AABox transform. |
| `original_box_center(box)` | `801238A4`, rounded maximum half followed by fused minimum half. |
| `original_box_distances(box)` | `80307F3C..7FE8`, separate absolute center-to-minimum and maximum-to-center distances. |
| `original_body_box(matrix, box, distances)` | `80308144..8278`, transform then restore captured local distances. |
| `original_boxes_overlap(incoming, body)` | `8012305C`, six ordered inclusive comparisons. |

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

## General transform and target bodies

The general transform first compares only minimum X with maximum X at
`80122D6C`. When minimum X is larger, it returns all six input words unchanged.
An inverted Y or Z does not take that branch. All inputs still pass the native
finite-input validation before the original branch.

For other boxes, each edge length is a rounded `maximum - minimum`. The routine
multiplies those lengths by the corresponding matrix columns separately. It
transforms the minimum point once through `8012D498`, including homogeneous W,
then constructs eight candidates in this reduction order: minimum, minimum+Z,
minimum+Y, minimum+X, (minimum+Y)+Z, (minimum+X)+Y, (minimum+X)+Z,
((minimum+X)+Y)+Z. Each addition rounds independently. The edges receive no
homogeneous division. This deliberately differs from projecting all eight
original corner positions or replacing the arithmetic with a center/radius
formula.

Reduction starts with finite `FLT_MAX` minima and `-FLT_MAX` maxima. It follows
the original strict replacement branches, retaining earlier words on equality.
Finite inverted inputs are accepted; nonfinite transformed point or final
bounds remain explicit checked failures.

Local distances use the rounded center, then two independently rounded
subtractions per axis. A compare with zero and conditional negation implements
the absolute value, retaining negative zero. For adjacent endpoints `1` and
`0x3F800001`, the center rounds to `1`: the minimum distance is zero and the
maximum distance is `2^-23`. The two distance vectors cannot be collapsed into
one half-size.

Target-body reconstruction computes the transformed box center, then subtracts
the captured local minimum distance and adds the captured local maximum
distance. The original `frsp` instructions explicitly flush subnormal loaded
distances before these final sums. Rotation and scale affect the transformed
box center; they do not rotate or scale the restored local distances. This
helper performs no timestamp comparison, list allocation or target service.

Overlap compares incoming minimum X with body maximum X, then body minimum X
with incoming maximum X, followed by the corresponding Y and Z pairs. Every
comparison is inclusive; touching points, edges and faces overlap. No separate
empty-box predicate is added. In particular, the original factory bounds
`minimum=FLT_MAX, maximum=-FLT_MAX` can overlap an incoming box spanning the
entire finite range, while ordinary world boxes miss them.

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

The final Release test suite passes 231 asset-free checks. Tests cover
matrix order, world-axis extents, inverted/zero sizes, homogeneous division,
epsilon boundaries and sign loss, reciprocal rounding/underflow, signed zeros,
subnormal loads, fused cancellation, center flushing and every invalid input word.
The target cases additionally cover the X-only early return, projective edge
construction, asymmetric distances, explicit distance rounding, finite factory
bounds and touching/separated overlap on all axes.

The new ignored `work/v27-collision-math-oracle.py` runs the raw general
transform, center, local-distance, body-restoration and overlap instruction
ranges against the standalone native test executable:

- 10,048 cases; 44,137 exact output words; zero mismatches.
- 1,101 finite centers, 1,101 finite distance pairs, 2,744 finite transformed
  boxes, 2,744 finite reconstructed bodies and 1,300 overlap results.
- 222 nonfinite-input rejections and 836 unusable-result rejections.
- 2,544,172 executed guest instructions.

Its numeric reference uses exact integer/rational arithmetic with the same
explicit startup NI/RN scope. It prepares the numeric call boundaries directly;
factory, list, cache lifecycle, allocator, recipient and Lua bodies are not
substituted or claimed. The supplied DOL is read locally; the bounded report
is retained in ignored `work/v27-collision-math-oracle.json`.

The ignored raw oracle runs the actual producer instructions, AABox wrapper,
complete corner reduction and homogeneous point body. Reference arithmetic uses
exact integers and rational quotient rounding, independently of C++ float/FMA
results. It executes no collision, allocator or script service hooks.

The source-HITBOX comparison below was repeated successfully against the new
standalone numeric build. Its previous linked-core comparison used
`build/Release/dw4_collision_math_tests.exe`; that report remains locally in
`work/v26-math-final-core-oracle.log`:

- 4,599 cases; 3,788 finite boxes and 22,728 exact output words.
- 66 nonfinite-input and 745 unusable-box rejections.
- 2,222,379 executed guest instructions; zero mismatches.
- 693 cases use the 231 original authored Set vectors with three explicit synthetic
  current-node matrices. These are numeric input cases, not gameplay replay.

Local reproduction after a parent-owned build:

```powershell
& .\build\Release\dw4_collision_math_tests.exe
& '<PYTHON>' work/v26-hitbox-math-oracle.py --native build/Release/dw4_collision_math_tests.exe
& '<PYTHON>' work/v27-collision-math-oracle.py --native build/Release/dw4_collision_math_tests.exe
```

The `--oracle` test mode reads 22 decimal binary32 words per row (16 matrix
components, 3 offset components, 3 full extents), returning six words (minimum
XYZ, maximum XYZ) or `ERROR`.
The probe reads the local DOL and saves its bounded report in ignored
`work/v26-hitbox-math-oracle.json`.

The additive `--box-oracle` mode accepts an operation letter and decimal binary32
words: `T` (16 matrix + 6 box), `C`/`D` (6 box), `B` (16 matrix + 6 box + 6
distance), or `O` (6 incoming box + 6 body box). It returns exact component
words, the overlap integer 0/1, or `ERROR` for the checked finite boundary.
