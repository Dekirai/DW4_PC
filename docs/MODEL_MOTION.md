# Original model turning and movement integration

Version 0.15 connects the original motion channels to recovered model turning
and local translation in an owned native pose. The portable core now performs
the ordering of `8030A448`: rotation channel, Euler turning, then translation
along the resulting model basis. It consumes explicitly prepared model work
and a timer. It does not construct a player or replace the provisional preview.

## Source and ownership

Evidence uses the owner's USA `GDJEB2` revision-0 DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Guest addresses are mapped through
DOL sections. Function boundaries and SDK names were cross-checked against
[dw4-gc](https://github.com/ivanno4317/dw4-gc), commit
`794294e17fef0b3ab5eeb15fda9bd57b0dc98a2b`. Implementations are independently
written from instructions, constants, metadata and callers. The product neither
links Dolphin nor executes the DOL. Original code/data dumps stay outside Git.

| Address | Role |
| --- | --- |
| `8030C85C` | Per-axis turning, changed/force matrix path and applied-angle state |
| `8030A448` | Rotation channel, desired Y/rate update, turning, XYZ translation |
| `8031E4C4` | Shared cached `KEIKATIMER` delta, implemented in version 0.14 |
| `80129134` | Euler matrix wrapper, including bottom-row reset |
| `801292D4` | Euler 3x3 basis with float products and explicit fused operations |
| `80128DAC / 80128D90` | Save and restore translation XYZ |
| `80128CF4 / 80128DC8` | Matrix copy and local translation |
| `8012F57C`, initialization pointer `80412E74` | Initialize the matrix helpers' global vectors |

`ModelTurn` owns desired angles, per-axis rates and applied angles, corresponding
to work offsets `80/84/88`, `8C/90/94` and `98/9C/A0`. `ModelMotionPose` owns the
model matrix and motion-written `_isHitCheck` flag (`78`, exposed as `translated`). Its constructor requires an explicit
matrix. The caller must prepare matching angle/rate state; zero-initialized
fields do not establish the original spawn or player defaults. The original
matrix path reaches the model through the RAM dictionary's `ONE` record. Named
dictionary/model ownership and full preparation remain outside this API.
Version 0.16 adds explicit player rate and matrix-heading helpers, plus the
separate `_isPosLock` flag at `A8`. See [MODEL_CONTROL.md](MODEL_CONTROL.md).

Copying a pose, timer or channel set produces independent state. No host pointers
refer into serialized game data. Matrix updates do not mutate the source IGB.

Version 0.36 separates the live node matrix allocation from per-work pose fields.
`ModelMotionPose` accepts either a value matrix or an actual shared matrix owner.
`matrix()` exposes values with const-correct access; `matrix_owner()` retains
the allocation. Distinct work poses can share the node without sharing turn or
control state. Copy construction detaches a candidate; copy/move assignment
publishes into the destination's existing matrix. Move construction retains the
supplied owner and leaves the source unavailable until reassignment. Pass a
new shared-node state through registration with `std::move`; passing an lvalue
to a by-value state API deliberately creates an independent candidate.

Retained script work now holds this same matrix directly. It does not retain
the entire controller through a matrix alias. This establishes node lifetime,
not full generation replacement: paused wait/collision callbacks still need
their old enclosing work owners retained at stable addresses. Replace a work
owner to create a new generation; assigning a pose only publishes its values.

## Turning rules

`turn_model` first copies desired Euler angles. Without force, each axis follows
these branches in X/Y/Z order:

1. Equal desired/applied values skip interpolation and do not query the timer.
2. A zero rate preserves the applied value and also skips the timer.
3. Otherwise query `KEIKATIMER`, multiply rate by its cached delta, and reduce
   current/desired angles using the stored float tau at `804268DC`:
   `6.2831854820251465f`.
4. Compare the positive and negative arc distances. The shorter arc wins;
   equality chooses the negative arc. Move by the signed step when the remaining
   distance is strictly larger, otherwise snap to the reduced target.

Negative rate/delta is preserved. A moved angle is not wrapped again before
storage. Force skips interpolation, rates and timer access, using raw desired
angles. Without force, unchanged resulting angles skip the entire matrix path,
even if wrapping queried the timer. The desired-angle fields remain unchanged.

The matrix path saves XYZ position, rebuilds the Euler basis, resets bottom-row
components, restores XYZ and stores the applied angles. It replaces prior
scale/shear; it does not multiply a new rotation into the previous basis.
The translated flag is separate and is not changed by standalone turning.
`force_model_turn` exposes that force path without creating a timer. It also
preserves the live position-lock flag and raw desired-angle words.

### Exact accumulated-angle wrapping

The original repeatedly adds/subtracts float tau. `fmod` is not equivalent:
each original step rounds separately. A fixed limit on the number of original
iterations would also reject valid accumulated rotations in long sessions.

The native implementation batches identical steps within float exponent bins
at exponent 4 and above. Tau's odd significand has no half-ULP ties there, so
each subtraction has one fixed integer ULP decrement while remaining inside
that bin. Integer mantissa updates skip those steps exactly, stopping a full
step before the boundary. Boundary steps and the smaller/tie bins use the
original float addition/subtraction. Signed angles retain the corresponding
add-tau behavior. Tests compare against the literal loop through magnitudes
`2^27`, including both signs and bin boundaries.

There is a native 4,096-iteration work guard after batching. Non-finite used
angles and original loops whose float addition/subtraction makes no progress
stop explicitly. For example, the next float above positive `2^27` cannot
decrease when subtracting this tau under round-to-nearest. These failures do
not silently select another heading or replace wrapping with a different rule.

## Matrix arithmetic and numeric profile

Initializer `8012F57C` keeps base `804F88B8` across calls. Stores at
`8012F768..8012F770` initialize `804F8AE0` to `(0,0,0)`. Stores at
`8012F854..8012F860` initialize `804F8C30` to `(0,0,0,1)`. The source floats
are zero at `805669D8` and one at `805669E0`. Reading those BSS globals from
uninitialized DOL storage would not establish their runtime values.

The Euler basis calls double `sin`/`cos` then rounds each result to float. Exact
zero angles bypass those calls and use positive zero/one, with separate special
cases for intermediate products. The native implementation preserves these
branches and explicit PPC fused multiply-add/subtract order. The wrapper resets
the bottom row to `(0,0,0,1)` while restoring the previous position.

Host double libm currently supplies sine/cosine. The original uses its MSL math
functions at `800A2AE4 / 800A257C`; bitwise agreement with those functions is not
established. The local instruction oracle supplies the same host trig boundary
to both implementations, so its exact matches prove the surrounding arithmetic
and control flow, not original libm parity.

Version 0.27 corrects the Euler basis to the recovered startup NI=1/RN=00
profile. The `frsp` instructions at `8012933C/34C/374/384/3B4/3C0` flush a
tiny double trig result before rounding it to single. The products and fused
operations at `80129388..8012942C` likewise flush exact results below `2^-126`
to signed zero before rounding. Exact-zero angle branches still choose the
original positive zero/one constants; a negative tiny nonzero angle takes the
trig branch and retains its zero sign through conversion and later negation.
Applied angles and scalar translation save/restore retain their input words.

The implementation keeps ordinary single-precision products and `std::fma`
rounding. A widened exact binary32 product resolves a product at the minimum
normal boundary. Fused boundary checks use a TwoSum residual so a tiny deficit
is not lost by an ordinary double sum. These are the same verified NI rules as
the [collision box helpers](COLLISION_MATH.md), grounded in the primary
[FP interpreter helpers](https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/Source/Core/Core/PowerPC/Interpreter/Interpreter_FPUtils.h)
and [NI hardware-test source](https://github.com/dolphin-emu/hwtests/blob/f28077b139eec18967f60db6ce1e15b182dfeac0/cputest/ni.cpp).
No physical-console execution or FPSCR exception-flag emulation is claimed here.

This correction is bounded to Euler reconstruction. Angle interpolation,
channel integration, local translation and `synchronize_model_heading` retain
their existing IEEE edge profile; their subnormal-mode fidelity remains outside
the implemented scope as in [MOTION.md](MOTION.md). The source uses strict
floating-point compilation and requires host nearest rounding with subnormal
inputs enabled.

## Combined model tick

`MotionChannels::advance_model` clears the translated flag and returns immediately
for an unallocated list. An allocated list queries the shared timer even if all
channels are inactive. If rotation channel 3 begins active, it ticks that channel,
multiplies the displacement by the float degrees-to-radians factor at `80426804`,
adds it to desired Y and replaces Y rate with `1.25f` times its ordered magnitude.
It still replaces the rate when that tick stops the channel or returns zero.

The original magnitude branch preserves negative zero and unordered values.
MSVC 19.51 reduced a float comparison/negation expression to an absolute-value
mask even with `/fp:strict`; the native implementation now uses explicit IEEE
integer classification. A regression checks the original negative-zero rate.

Turning runs next. If any XYZ channel begins active, all three scalar channels
tick using the same cached delta, and local translation uses the updated matrix.
The translated flag becomes true even for zero displacement. Consequently a
rotation command's new rate is multiplied by delta again during interpolation;
snapping directly to desired Y would skip original behavior.

Native validation failures preserve channel, pose and timer state together.
Derived scalar NaN/infinity remains supported by the scalar API, but a non-finite
model pose stops explicitly before matrix publication. This is a native error
boundary, not an invented successful zero-motion update. The original outer
function's virtual matrix-setter side effects, optional trace callback,
attachment branches and subsequent collision/controller work remain pending.

## Validation

- **111 asset-free checks** cover wrap/arc boundaries, signed steps, raw force
  angles, ignored dependencies, timer side effects, matrix reset and source
  position, negative zero, combined update order, independent model ownership,
  non-finite failure atomicity and exact batching of long accumulated angles.
  Euler cases add signed tiny trig results, exact minimum-normal survival,
  product flushing before rounding, intermediate-product flushing, unchanged
  scalar translation words and shared timer-free/timer-bearing force behavior.
- **4,932 local raw Euler comparisons** run `80129134/801292D4` and translation
  restoration `80128D90` with exact integer NI arithmetic. All 4,914 accepted
  matrices match in 78,624 output words; 18 nonfinite used-input cases reject.
  The reference executes 647,129 guest instructions with 29,004 explicit host
  trig calls. It rounds the returned host binary64 values with integer logic
  at their immediately following original `frsp` boundaries. This validates
  surrounding instruction arithmetic, not the original MSL trig bodies or ABI.
- **9,000 local instruction comparisons** cover 6,000 standalone turns and
  3,000 combined ticks. All 8,434 published outputs match the interpreted
  original state/matrices by bits; 566 original non-finite poses are rejected
  at the native boundary. NaN payloads are excluded and trig is shared as above.
- **22 optional original-data checks** use Agumon's Move 130 / Stop 1540 with
  each of the four original `PLAYERPOINT` 115/group-0 matrices in `ndmw.afs[179]`.
  After 18 Move and six Stop updates at an explicitly cached delta 1, each
  model travels approximately 8.275 local units along its source basis and
  stops. This exercises four source placements, not connected multiplayer.

Both local oracle runners were relinked against the final Release
`dw4_core.lib` after the NI correction. The Euler and older normal-domain
turn/tick comparisons both passed with zero mismatches; their local logs are
`work/v27-euler-final-core.log` and `work/v27-model-normal-final-core.log`.
The final linked test executable passes 133 checks when the 22 original-data
checks are included. The ignored Euler reference/report are
`work/v27-euler-ni-oracle.py` and `work/v27-euler-ni-oracle.json`; the old
regression is invoked through `work/v27-model-normal-regression.py`.

To repeat the data checks after building, provide your own extracted files:

```powershell
.\build\Release\dw4_model_motion_tests.exe 'E:\path\Digimon World 4 - Extracted'
```

Version 0.16 recovers player turn rates, heading preparation, CTRL/PDMOVE effects
and post-input sequence requests. Next connect full player work construction,
original input/sequence/action execution, collision response and camera state.
Model motion alone does not execute startup, animation transitions, combat,
progression, saves or the original multiplayer loop. The full acceptance target
remains [ORIGINAL_BASELINE.md](ORIGINAL_BASELINE.md).
