# Original player movement and input research

The four physical-pad state machines are implemented in `pad.hpp` / `pad.cpp`.
The movement, turning and sequence findings below are evidence for the next
native runtime boundary. They have **not** replaced the walking preview's host
movement, camera, collision response or animation selection. Four independent
input states do not establish a running four-player game.

## Source identity and method

This investigation used the owner's GameCube USA `main.dol`, game ID `GDJEB2`,
SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, and local `ndmw.afs` assets.
Addresses below are guest virtual addresses in that DOL, never host pointers.
Function boundaries and SDK symbol names were cross-checked against
[ivanno4317/dw4-gc](https://github.com/ivanno4317/dw4-gc),
`config/GDJEB2/symbols.txt`. Most game functions have only `fn_ADDRESS` names;
descriptive roles below come from their instructions, type registration,
metadata offsets and callers. New native implementations were written from
these observations; no original binary instructions or assets are included.

Local working evidence is retained under ignored `work/v13-*`: annotated DOL
disassembly, constant probes, a GameCube button-instruction interpreter and an
Agumon object-graph probe using the existing checked IGB reader. Capstone's
default decoding does not understand every Gekko paired-single instruction;
save/restore instructions must not be interpreted as VSX operations. Ordered
floating comparisons were checked against their instruction words as well.

To reproduce the observations with the same locally supplied release:

1. Verify the DOL SHA-1 and map each guest address through its DOL section table.
   Read PPC instruction words and scalar constants as big-endian values.
2. Disassemble the bounded functions listed below and follow their direct calls,
   virtual slots and metadata tables. Do not infer a class from address proximity.
3. Decode `ndmw.afs[850]`, catalog resource `_common/p_agumon.igb`, with
   `IgbDocument`. Follow the named `SEQ` data from model reference `1386` through
   the object references listed below. IGB field payloads use their decoded
   metadata format, not the DOL's byte order.
4. Run `dw4_pad_tests`. The public fixtures need no game data. They check the
   recovered numerical and transition contract, including exact float angular
   boundaries, without claiming an original frame-by-frame game comparison.

## Physical GameCube conversion

`800CD2DC` calls SDK `PADRead` (`802631CC`) and `PADClamp` (`802624CC`) before
passing each status to the device's virtual update at `800CCA6C`. There is also
a single-device read path at `800CCA08` without the aggregate clamp call; this
research does not assume every device entry point takes the aggregate path.

`__init_registers` at `8000539C` initializes `r2 = 8056E220`. Consequently the
load at `800CCB80`, `lfs f4,-0x797c(r2)`, reads `805668A4`: float **74.0**.
The four signed stick bytes are converted to float and divided by that value.
`800CC974` returns stick 0 or 1 with the same X/Y signs. Positive Y is upward.
The original conversion does not normalize the resulting vector to length one.

SDK clamp configuration at `804164E0` is eight bytes:
`30, 180, 15, 72, 40, 15, 59, 31`. The first two configure analog triggers,
which are outside the implemented pad-state profile. The stick constants are:

| Stick | Component deadzone | Axis limit | Equal-diagonal limit |
| --- | ---: | ---: | ---: |
| Left | 15 | 72 | 40 |
| C-stick | 15 | 59 | 31 |

`ClampStick` at `8026239C` first subtracts the inclusive component deadzone
from each absolute signed-byte component, flooring at zero. For remaining
components `x,y`, axis limit `a` and diagonal limit `d`, it computes
`divisor = d * max(x,y) + (a-d) * min(x,y)`. When that exceeds `a*d`, each
component becomes `component * a*d / divisor` using truncating integer division.
It restores the original signs. `gamecube_stick` implements this operation and
the subsequent division by 74; input must be raw signed status bytes.

The pure `gamecube_buttons` helper reconstructs `800CCF5C`:

| GameCube button | Raw PADStatus bit | Original logical bit |
| --- | ---: | ---: |
| Left / Right / Down / Up | `0001 / 0002 / 0004 / 0008` | `0080 / 0020 / 0040 / 0010` |
| Z / R / L | `0010 / 0020 / 0040` | `0200 / 0800 / 0400` |
| A / B / X / Y | `0100 / 0200 / 0400 / 0800` | `4000 / 8000 / 2000 / 1000` |
| Start | `1000` | `0008` |

Other raw bits have no mapping. An independent local check interpreted the
original rotate/mask instructions for all 65,536 raw button masks. A separate
check compared the clamp against the SDK's branch-based integer formulation
for all 131,072 signed-byte pairs across both sticks, including `-128`; neither
check found a mismatch. These exhaustive local checks supplement the committed
asset-free edge and transition tests.

## bePadData transitions

Registration `802C2804` identifies **bePadData**, size `0x68`, type global
`80534AC8`. Names at `804D0208` and offsets at `804D02B8` identify these fields:

| Channel | Analog value | Trigger | Repeat | Held | Previous | Countdown |
| --- | --- | --- | --- | --- | --- | --- |
| Left stick | `0C,10` | `1C` | `20` | `24` | `28` | `2C` |
| Right stick | `14,18` | `30` | `34` | `38` | `3C` | `40` |
| Buttons | — | `48` | `4C` | `44` | `50` | `54` |

`08` is connection status. Initialization `803116E0` clears masks/countdowns,
sets `_enaLen` at `58` to the float result of `sqrt(0.4f * 0.4f)`, `_adAngle`
at `5C` to float pi/4 (`3F490FDB`), and repeat periods at `60,64` to **10,6**.
`80311880` updates buttons; `803118F4` and `803119B4` store each analog pair
unchanged, classify its direction, then apply the same channel transition:

```text
previous = held
held = incoming
trigger = (held XOR previous) AND held
if held != previous:
    repeat = trigger
    countdown = 10
else:
    countdown = countdown - 1, wrapping at 32 bits
    if countdown == 0:
        repeat = held
        countdown = 6
    else:
        repeat = 0
```

These are **update counts**, not seconds. A press emits trigger and repeat
immediately; the first held repeat is ten later updates, then every six updates.
Adding a chord button triggers only the new bit but restarts the shared channel
delay. Releasing one chord button also restarts that delay, without retriggering
the remaining bit. The next held repeat contains the entire held mask. Initial
idle countdown decrements from zero to `FFFFFFFF`; it is not clamped to zero.
All three channels and all four physical slots keep independent history.

Direction classifier `80311A74` calculates separate float `x*x`, `y*y`, their
float sum and a square root; it calls double `atan2(y,x)` and rounds to float.
It emits zero when magnitude is **less than** 0.4. Let `q` be the stored float
pi/4, `t` the separately stored float 3pi/4 (`4016CBE4`), `h = 0.5f*q`, and
`angle` the rounded result. The precise branches are:

| Bit | Condition |
| --- | --- |
| 8, negative X | `angle < -t+h` or `angle > t-h` |
| 4, negative Y | `angle >= -t-h` and `angle <= -q+h` |
| 2, positive X | `angle > -q-h` and `angle < q+h` |
| 1, positive Y | `angle >= q-h` and `angle <= t+h` |

This produces overlapping diagonal sectors. It is not an eight-sector rounding
operation with uniformly inclusive boundaries. `_adAngle` controls overlap,
not the radial threshold. The native implementation preserves these comparisons
and separate float operations. Host `sqrt`/`atan2` are used; bitwise agreement
with the guest's reciprocal-square-root refinement and libm at every floating
boundary remains unproven. The committed tests construct representable analog
pairs whose rounded angles equal each exact boundary before checking inclusion.

Manager update `80311F84` checks `beSystem+2E` first and skips the entire update
when set. Frozen trigger/repeat outputs are retained as well as timers. For a
device reporting disconnected, it stores false and runs all three transitions
with zero input, rather than replacing its history with a fresh object.

`OriginalPads::update` exposes those four physical slots as a fixed array. Its
caller supplies the complete logical-device snapshot once per intended original
pad update. The native validation limit is finite axes within `[-2,2]`, covering
signed-byte division by 74 even without clamp. Invalid connected samples leave
all slots unchanged; unused disconnected or frozen axes are not read.

The original manager allocates **five** bePadData instances at `80311D10`.
Getter `80312138` returns the fifth when manager byte `1D` is set or the requested
index is at least four. This special/redirected pad behavior, device callbacks,
hardware calibration, error recovery, analog trigger events and rumble are not
implemented by the physical-slot core. Native SDL device ownership and mapping
must not be inferred from successful core tests.

## Player input and update order

Player controller registration `80338DCC` identifies `beNDMWMdlPlayer`, size
`0x38`, vtable `804E7DAC`. Preparation at `8034C2F0` resolves the pad manager
into controller `+30` and the application dependency into `+2C`. Its input
method is `8034C3C8`, vtable slot **A0**. The player RAM's slot at `+08` is
passed to pad getter `80312138`.

The player's runtime work is obtained from the RAM data dictionary's named
`ONE` record. It is **not** a ready-made state object in the asset: Agumon's
serialized `ONE` reference `1724` has a null work field. Its runtime type is
`beNDMWMdlPlayerInfoWork`, registered at `803393E8`, size `0x130`.

`8034C3C8` reads the left digital direction mask at pad `+24`. A nonzero mask
sets player work `+70` and enables heading calculation. It inverts a camera
matrix, transforms the local +Z axis, obtains camera heading with `atan2(x,z)`,
then adds input heading `atan2(leftX,-leftY)` to set desired Y rotation at
work `+84`. No analog magnitude multiplier for the ordinary movement command
was found in this input branch. The input routine also handles jump, guard,
attacks, item actions and direction-history logic; those states are not ported.

Common per-RAM update `8030CB2C` clears input flag `+70` and requested sequence
`+74` (`SQ_NONE`), runs control-mode logic at `80309168`, invokes status-effect
handling, then calls movement integrator `8030A448` for unattached models.
Subsequent helpers include action wait processing, collision-manager update
`803107E0` and moved-model handling `80309C74`. Original collision response
therefore remains part of the required integration, even with original floor
and wall triangles available to the preview.

At `80309168`, control mode `+3C = 2` requests `SQ_WALK` when input is active;
mode 3 requests `SQ_WAIT` when it is inactive. Mode 1 uses an internal state at
`+40` and four cached PDMOVE parameters at `+48` to issue Move/Stop transitions.
Full control-command dispatch and the lifecycle that sets these modes remain
prerequisites before using this fragment as a complete player state machine.

## Original scalar movement kernel

Type registration `802C86D8` identifies **beModelCtrlMoveObject**, size `0x2C`.
Its field metadata is at `804D0C7C` / `804D0CC4`:

| Offset | Field |
| --- | --- |
| `08` | `_isRun` |
| `0C` | `_endTime` |
| `10` | `_mode` |
| `14` | `_value` |
| `18` | `_acc` |
| `1C` | `_speed` |
| `20` | `_isSeqStop` |
| `24,28` | `_stopValue`, `_stopFrame` |

Setup `8030A1F4` stores mode, value and end time, clears sequence-stop, and sets
`acc = value / ((frame * frame) * 0.5f)` with separate single-precision
operations. Mode 4 sets speed to value; mode 5 sets it to negative value.
**Modes 0–3 preserve the previous speed.** It then marks the object active.

Tick `8030A2D8` returns zero when inactive. A nonzero end time first subtracts
the supplied delta before the local upper clamp. A negative result calls stop
helper `8030A254`; a result exactly zero does not expire and becomes the zero
sentinel on subsequent calls. The helper changes mode 0 to mode 2 or mode 1 to
mode 3 using the stored stop parameters. Other modes become inactive with zero
speed. It clears sequence-stop in either case.

For an object still active, tick limits delta to **3.0**, computes
`dv = float(delta * acc)` and updates speed as follows:

| Mode | Speed update |
| --- | --- |
| 0 | `min(float(speed + dv), value)` |
| 1 | `max(float(speed - dv), -value)` |
| 2 or 4 | Subtract `dv`; if the result is strictly negative, set zero and inactive |
| 3 or 5 | Add `dv`; if the result is strictly positive, set zero and inactive |

It stores speed and returns `float(speed * delta)`. Reaching exactly zero in
a stopping mode does not deactivate until a subsequent crossing. This updates
velocity **before** computing displacement. Subdividing every update into
120 Hz host steps changes acceleration travel and cannot be assumed faithful.
The original has no finite/zero-denominator guards; a native API should explicitly
reject malformed or unsupported inputs rather than inventing a fallback.

This setup/tick/stop group is a concrete bounded candidate for the next native
port, with synthetic fixtures for preserved speed, all six modes, expiry and
zero crossings. It is intentionally not integrated or implemented in this increment.

## Timing, local translation and turning

`beSystem` update `80316258` subtracts the previous application world timestamp
from the current 64-bit timestamp, stores the elapsed integer at `+20/+24`,
converts it to float and divides by **16,666,667.0f** (`804271BC`) to produce
`_frameTimeF` at `+28`. This establishes nominal 60 Hz **frame units**; it does
not independently prove the original frame scheduler always executes at 60 Hz.

Elapsed-timer query `8031E4C4`, used for `KEIKATIMER`, computes the difference
between consecutive accumulated timer values, converts and divides by the same
constant at `80452BF8`, and caps the result at 3.0 (`80452BFC`). It caches that
result against the application world timestamp. Multiple channel/rotation
queries within one world timestamp return the same delta. The underlying
pause/resume and accumulated-timer implementation at `8031E36C` still needs
complete tracing before a native clock can replace this runtime dependency.

Integrator `8030A448` accesses four movement channels through work `+C8`.
Channel 3 changes desired rotation, then `8030C85C` updates the model's rotation.
Channels 0–2 produce local X/Y/Z displacement using the same cached timer delta.
Matrix helper `80128DC8` applies translation along the **model basis**, preserving
its effect on direction and scale. For each matrix row it calculates the Y term,
adds X and Z terms with PPC fused single operations, then adds that displacement
to the existing translation. Adding a world-space Z distance is not equivalent.

Turn helper `8030C85C` uses desired Euler components at `+80,+84,+88`, previously
applied components at `+98,+9C,+A0`, and per-axis rates at `+8C,+90,+94`.
Without a force flag, a zero rate preserves the current component. Otherwise
the step is rate times timer delta; current and target angles are reduced by
the stored float tau, then interpolation follows the shortest arc and snaps
when the remaining angle is at most the step. Equal opposite arcs choose the
negative direction. A moved result is not normalized again before storage.
Default per-axis rates, all mode changes, and the Euler matrix construction
at `801292D4` remain integration work; no guessed turn speed is established here.

## Agumon sequence parameters and animation pairing

In `_common/p_agumon.igb`, model `1386` points to data list `6384`, containing
`HIT` reference `1997` and `SEQ` reference `14`. `SEQ` points to named list
`6636`, containing 28 sequences. The relevant paths are:

| Sequence | Sequence reference / command list | Common movement command |
| --- | --- | --- |
| `SQ_WAIT` | `1534 / 80` | `beModelCtrlMOVE` reference `1540`: `Stop`, value **1**, frame **4** |
| `SQ_WALK` | `169 / 123` | `beModelCtrlMOVE` reference `130`: `Move`, value **0.7f**, frame **6** |

MOVE handler `8030A6BC` recognizes the exact MOVE type global `80534F0C`.
Its `Stop` branch at `8030AA70` selects local channel 2 and mode 2; its `Move`
branch at `8030AAA8` selects channel 2 and mode 0. Both pass the asset's value
and frame parameters directly, with zero end time. The handler lazily creates
four movement objects and ensures the elapsed timer exists. PDMOVE handler
`8030AEE0` separately caches the Stop/Move parameter pairs for control mode 1.

Ordinary Agumon Move therefore has scalar maximum **0.7 source units per nominal
frame**, or nominally 42 per second at one delta-unit per 60 Hz update, before
model-basis scale and other controller effects. Its acceleration is about
0.0388889 per delta unit; Stop subtracts 0.125 per delta unit. This evidence does
not justify replacing the preview's 80 units/second constant in isolation.

The original WAIT/WALK lists branch on weapon labels and pair action names:

| Label | Label reference | WAIT action / reference | WALK action / reference |
| --- | ---: | --- | --- |
| `dual` | 4988 | `WAITD1` / 1108 | `RUNND1` / 522 |
| `slash` | 646 | `WAITS1` / 599 | `RUNNS1` / 568 |
| `arm` | 929 | `WAITA1` / 499 | `RUNNA1` / 543 |
| `gun` | 2233 | `WAITG1` / 613 | `RUNNG1` / 1575 |

The lists also contain CTRL `toWalk` reference `6499` and `toWait` reference
`622`. Automatic preview selection that combines a slash WAIT clip with a dual
RUN clip crosses these original weapon branches. The action names above must
still be resolved through their action/animation runtime, including transitions,
timing and events; they are not an instruction to guess a pair of clip substrings.

Agumon's `HIT` record points through AABox list `6395` to box `1316`, with bounds
`(-2.5,0,-2.5)` to `(2.5,7,2.5)`. This is original authored collision metadata,
not proof that the complete player response can be replaced by a capsule of
those dimensions. The preview's radius 1.5 and height interval 1–6 remain
provisional host choices.

## Next integration boundary

Keep presentation frame limiting separate from original pad-manager updates.
Next, implement and verify the bounded movement scalar objects, then connect
them to retained player runtime state, elapsed timers, original local matrices,
matching weapon/action selection and recovered collision response. Preserve
entry-point rotation and per-player ownership. Camera-relative heading depends
on the original camera runtime, not the preview orbit camera.

The outstanding acceptance checks are original frame comparisons for timing,
input, acceleration, turning, action transitions and collision, followed by
independent simultaneous players. Combat, inventory, progression, transitions,
audio and saves remain separate original systems; none are implied by this
input-core milestone.
