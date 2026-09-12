# Original motion channels and elapsed timer

Version 0.14 introduced the recovered scalar movement objects, MOVE/MOVEEX
parameter decoding, movement channels, local matrix translation and
the elapsed timer used by those channels. This portable C++ core has no SDL,
graphics, Dolphin or executable-DOL dependency. It is not yet connected to the
original player lifecycle or the provisional walking preview.

## Evidence and implemented boundary

Source: the owner's GameCube USA `GDJEB2` revision-0 extraction, DOL SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Guest addresses below were mapped
through its DOL sections. Big-endian instruction/scalar reads, type registrations,
field metadata and callers establish the reconstructed roles. Function boundaries
and SDK names were checked against [dw4-gc](https://github.com/ivanno4317/dw4-gc),
commit `794294e17fef0b3ab5eeb15fda9bd57b0dc98a2b`. No reference code or original
instructions/assets are distributed by this increment.

| Source address | Recovered operation | Native implementation |
| --- | --- | --- |
| `802C86D8`, metadata `804D0C7C / 804D0CC4` | `beModelCtrlMoveObject`, size `0x2C` | `MotionState` |
| `802C8548`, work metadata `802C6F5C..802C6F70` | Allocated empty `beModelCtrlMoveObjectList` at work+`C8` | `MotionChannelList`, with distinct shared entry owners |
| `8030A1F4` | Configure scalar mode/value/acceleration | `configure_motion` |
| `8030A254` | Stop or transition into deceleration | `stop_motion` |
| `8030A2D8` | Expire, advance speed, integrate displacement | `advance_motion` |
| `8030A6BC` | MOVE/MOVEEX command branch effects | Decoder and `MotionChannels::apply`; timer/work/attachment preparation is separate |
| `8030A448` | Rotation channel, turning, translation channels | Scalar outputs here; version 0.15 model integration in [MODEL_MOTION.md](MODEL_MOTION.md); callbacks remain |
| `80128DC8` | Translate along a 4x4 model basis | `translate_model_local` |
| `802E3580`, members `802E3614` | `beBaseInfoRamTimer`, size `0x40` | `MotionTimer` |
| `8031DE44` | Start/restart a timer | `start_motion_timer`; named lookup and creation are caller responsibilities |
| `8031E36C` | Scaled coarse clock plus bias | `motion_timer_time` |
| `8031E4C4` | Cached nominal-frame delta | `motion_timer_frames` |
| `8031E238` | Strict end-time comparison | `motion_timer_expired` for an existing timer |

The API represents shared object identity without guest pointers. Serialized
command references are local to the supplied document. The eventual actor runtime
must retain the document's source identity, choose sequences and establish the
actual RAM timer. Complete player-work construction remains separate.

## Movement list and entry ownership

`MotionChannels` now owns or explicitly binds one `MotionChannelList`. The default
constructor allocates an empty list, matching the auto-created work+`C8` field;
it does not create four movement objects. At `8030A81C..8030A9F0`, the first MOVE
checks the count, allocates four distinct movement objects, and appends them to
the same list before selecting the command name. This includes an unknown MOVE
name. Successful later commands, ticks, model integration and sequence stops
publish into those same entry allocations. `allocated()` reports whether the
four entries exist; it does not report whether the list itself exists.

`list_owner()` exposes the actual binding for a future canonical player-work
owner. Another `MotionChannels` constructed from that shared list observes and
mutates the same entries. Const bindings expose const entry owners. `states()`
returns a detached value snapshot, including zero values for an empty list;
reading it does not allocate entries or create an authoritative cached array.

Copy construction detaches the list and each distinct entry for validation.
Copy and move assignment publish values while preserving the destination list
and compatible existing entry owners. Assigning an empty snapshot clears the
entries, preserving the list and any independently retained removed objects.
Move construction transfers the list binding. A moved-from binding rejects use
until assignment gives it a new value-owned list.

Raw copies preserve unusual counts, null entries and alias relationships without
consuming or repairing movement data. This allows an unrelated control command
to stage its state without adding a movement lookup absent from the original.
Raw copies are bounded to 4,096 entries. Actual movement consumers support only
an empty list or exactly four distinct non-null entries; other shapes stop
explicitly. A null list binding is never silently replaced by an empty list.
These are checked host boundaries, not implementations of malformed guest memory
access or the guest allocator's exact backing-capacity growth.

Arithmetic uses detached value candidates. Validation or matrix-publication
failure preserves the live entries, list identity, timer and model pose. Allocating
the first four entries is published only after native parameter validation;
this checked failure policy does not claim original allocator-failure behavior.

## Scalar arithmetic

Motion fields correspond to original offsets `08` active, `0C` end time, `10`
mode, `14` value, `18` acceleration, `1C` speed, `20` sequence-stop flag and
`24/28` stored stop value/frame parameters. Initial fields are zero.

Configure computes `value / ((frames * frames) * 0.5f)` using separate float
operations. It preserves speed in modes 0–3, sets speed to value in mode 4 or
negative value in mode 5, clears sequence-stop and marks the channel active.
Previously stored stop parameters survive configuration.

An inactive tick returns zero. Otherwise a nonzero end time subtracts the
**supplied** delta before movement clamps it. A strictly negative remainder calls
stop; an exact-zero remainder becomes the no-expiry sentinel. Stop maps mode 0
to mode 2 or mode 1 to mode 3 using stored stop parameters. Other modes become
inactive with zero speed. Stop clears end time and sequence-stop.

For a still-active channel, delta is capped above at 3; negative values retain
their original meaning. Set `change = float(delta * acceleration)`:

| Mode | Update |
| --- | --- |
| 0, accelerate positive | Add change; clamp to value only if greater |
| 1, accelerate negative | Subtract change; clamp to negative value only if less |
| 2 / 4, stop / impulse positive | Subtract change; deactivate and set zero only if negative |
| 3 / 5, stop / impulse negative | Add change; deactivate and set zero only if positive |

Store the updated speed, then return `float(speed * delta)`. Exact zero does
not deactivate a stopping channel until a subsequent crossing. This order is
not invariant under time subdivision: one update at delta 1 can travel farther
than two updates at delta 0.5 despite reaching the same final speed.

### Valid zero-frame commands

The original archives contain **131 commands with frames equal to zero**. These
are valid authored records, including `Move`, `TXMACC`, `TXPACC`, `RYMACC` and
`RYPACC`; a nonzero-denominator parser restriction would reject real game data.

For positive value and zero frames, acceleration becomes positive infinity. A
positive delta then reaches the ordinary speed cap or stopping crossing in the
same tick. Rejecting the intermediate infinity before that branch is incorrect.
A zero delta instead produces NaN from `0 * infinity`; ordered comparisons do
not clamp that speed or deactivate the channel. Later ticks retain the NaN until
an operation actually replaces the speed. Zero value and zero frames also
produce NaN, including a stop transition that consumes zero-initialized fields.

Native configuration/decoding requires finite authored parameters and valid
modes; public tick deltas must be finite. Derived infinity/NaN is preserved in
the scalar state and output. The matrix boundary explicitly rejects non-finite
input/results before publishing any matrix change. It does not replace the
displacement with zero or repair the scalar state. Full original scheduler and
sequence behavior is still needed to establish when such cases occur in play.

These routines compile with separate float rounding and without fast-math
assumptions. NaN payloads, floating exception flags and guest subnormal modes are
outside this numerical profile. Tests compare NaN classification, not payloads;
they do not establish every possible Gekko floating-point bit pattern.

## Serialized command profile

The decoder accepts only exact little-endian `beModelCtrlMOVE` and
`beModelCtrlMOVEEX` records. Type registration identifies MOVE at `802CBB00`
(handle `80534F0C`, size `0x14`) and MOVEEX at `802CB8DC` (handle `80534EF0`,
size `0x24`). MOVEEX member setup `802CB970` uses offset table `804D100C`.

Both types have name field 2, value field 3 and frame field 4. The checked name
is at most 64 bytes, with bounded serialized storage and exact field types.

| Exact MOVE name | Channel | Mode |
| --- | ---: | ---: |
| Move | 2, local Z | 0 |
| Stop | 2, local Z | 2 |
| Push | 2, local Z | 4 |
| Back | 2, local Z | 5 |
| Jump | 1, local Y | 4 |
| Fall | 1, local Y | 1 |

An unknown name on the **exact MOVE type** changes no channel values after the
original lazy entry allocation. It still creates the four actual entry owners
when the list was empty. This does not ignore unsupported controller types.

MOVEEX also reads end time at field 5, sequence-stop bool at 6, stop value at 7
and stop frames at 8. Its first four name bytes drive channel/mode selection;
the observed names have six bytes, such as `TXPACC` and `TYMTGD`. The channel is
`second_byte - 'X' + (first_byte == 'T' ? 0 : 2)`, checked within 0–3. Fourth
byte A/D/T selects base mode 0/2/4; third byte M adds one. Short, out-of-range or
unknown-mode codes stop explicitly. The decoder does not invent meaning from
the trailing bytes. MOVEEX stores its stop fields after scalar configuration.

`MotionChannels` advances rotation (3) first, then X/Y/Z (0/1/2), sharing one
delta. `translation_updated` records whether a translation channel began active,
even when the resulting displacement is zero. Empty movement lists do
not query the timer. Invalid native input does not partially publish state.
Sequence-stop is retained data in this decoder. Version 0.17 adds
`MotionChannels::on_sequence_change`: stop flagged XYZ channels using the stored
stop parameters, and deactivate rotation with zero speed while preserving its
other fields. This is the loop at `8030EA1C..8030EA74`, not full controller dispatch.

## Timer and local transform

Timer offsets are `10` scale (default 1), `18` bias, `20` end time, `28` saved
world time, `30` previous accumulated time and `38` cached frame delta (default
zero). The caller supplies the original world timestamp in nanosecond units.

Time query takes the low 32 bits of `world_time >> 18`, converts to float,
multiplies by scale in single precision, truncates/saturates to signed 64 bits,
shifts left 18 with wrapping arithmetic and adds the signed bias with wrapping
arithmetic. The original helper labeled `__cvt_dbl_usll` at `800A4130` uses signed
saturation on this path. Finite scale can overflow to infinity; saturation
still happens before the wrapping shift. A full-precision host stopwatch is not
equivalent: the original loses low timestamp bits and later coarse float bits.

Start stores negative scaled clock as bias, the requested end time, current
world timestamp and cached delta **1**. Reusing an existing timer preserves
scale and previous accumulated time. A frame query at the same timestamp returns
the cached value. Otherwise it subtracts previous accumulated time with signed
64-bit wrapping, converts through rounded double then float (`800A3FDC`), divides
by the stored float `16666667.0f` and caps above at 3. Zero elapsed time caches
zero. A capped update still consumes all elapsed time; it leaves no catch-up
backlog. Expiry is strictly `end_time < accumulated_time`, not equality.

The timer API does not implement named dictionary lookup, a global clock,
pause/rate-change setters, manager lifetime or an original frame scheduler.
Non-finite externally supplied scale/cache values are rejected explicitly.

Local translation uses column-major matrices. For each of all four rows it
computes the Y term, fuses X with that term, fuses Z with the result, then adds
the old translation component. It preserves model scale and rotation, including
the fourth translation component. Only these explicitly recovered multiply-adds
are fused; no collision response or basis normalization is added.

The original outer integrator also converts channel-3 output to radians using
`0.01745329238474369f` (`80426804`), sets Y turn rate to absolute radian delta
times `1.25f` (`80426808`), then calls turning helper `8030C85C`. Those actor
fields, the turning helper, matrix assignment callbacks, trace output and
attachment/MOVERL branches are not implemented by `MotionStep.rotation`.
Version 0.15 adds a separate `advance_model` path for the recovered rotation,
turning and translation order; see [MODEL_MOTION.md](MODEL_MOTION.md). Original
full model preparation, setter/trace callbacks and attachment behavior remain pending.
Version 0.16 adds a supported command preparation/control layer, including
timer creation and position-lock release; see [MODEL_CONTROL.md](MODEL_CONTROL.md).

## Validation and next integration

`dw4_motion_tests` runs asset-free mode/expiry/zero-frame, schema, ownership,
timer and matrix checks in CTest. Ownership checks cover empty versus populated
lists, first and repeated unknown MOVE, shared bindings, detached copies,
publication into retained entries, raw malformed snapshots, consumer rejection,
cross-list entry aliases, lifetime release and atomic model-integration failure.
Local arithmetic evidence additionally includes:

- All **3,796** MOVE/MOVEEX records in **2,273** recognized IGB archive entries
  decode successfully: 3,599 MOVE and 197 MOVEEX, including all 131 zero-frame
  records. This scan excludes the separate catalog IGB.
- A bounded local instruction interpreter of the supplied scalar setup/stop/tick
  routines agrees with native output and state for **18,000 setups and 54,000
  ticks**, including signed deltas, expiry, overflow and unordered branches.
  Finite results and infinities are compared by bits; NaN payloads are excluded.
- All 16 Agumon MOVE records decode. Its original `SQ_WALK` Move reference 130
  (value 0.7, frames 6) travels approximately 6.65 local scalar units over 18
  delta-1 updates and reaches speed 0.7. `SQ_WAIT` Stop reference 1540 (value 1,
  frames 4) then travels approximately 1.625 over six updates and stops.

These are arithmetic and data comparisons, not a captured original gameplay
comparison. Local probes and disassembly remain ignored under `work/v14-*`.
Public tests use independently generated synthetic data only.

Next connect full retained per-player work, named timer ownership, input/sequence execution,
matching weapon/action animation, recovered model turning and original collision
response. Preserve original spawn basis and independent player ownership.
The preview's 120 Hz solver, speed 80 and host camera remain provisional; changing
one constant alone would not establish original movement. See
[PLAYER_MOVEMENT_RESEARCH.md](PLAYER_MOVEMENT_RESEARCH.md) and
[ORIGINAL_BASELINE.md](ORIGINAL_BASELINE.md).
