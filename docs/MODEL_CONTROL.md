# Original model control and movement preparation

Version 0.16 implements supported control fields for one explicitly prepared
`ONE` model: MOVE/MOVEEX preparation, CTRL/PDMOVE effects and post-input sequence
requests. These portable core functions do not construct an original player,
execute its animation/action sequences or replace the walking preview.

Version 0.26 adds MUTEKI assignment and the collision-related work fields used
by sequence entry and source hit scripts. Exact OFFWALL/ONWALL and
OFFFLOOR/ONFLOOR names set work bytes D8/D9 to zero/one. Every other name,
including null or empty, assigns the finite frame value to work 7C. The original
handler at `8030C1F0` ensures the same KEIKATIMER before either branch; an
existing timer is preserved. This assignment does not yet implement positive
invulnerability countdown or a target collision manager.

Owned BHit/WHit/FHit prefixes at work 30/34/38 distinguish null from empty.
Ordinary sequence entry clears them after queue cleanup, enables wall/floor/fall
checks, and changes strictly negative invulnerability to positive zero while
preserving negative zero. See [MODEL_SEQUENCE.md](MODEL_SEQUENCE.md).

## Evidence and ownership

Evidence comes from the owner's USA `GDJEB2` revision-0 DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, and original IGB records. Function
boundaries and SDK names use [dw4-gc](https://github.com/ivanno4317/dw4-gc) commit
`794294e17fef0b3ab5eeb15fda9bd57b0dc98a2b`. Native code is independently written
from instructions, constants and metadata. Dumps and instruction probes stay
ignored; the product does not execute PowerPC instructions or link Dolphin.

| Original address | Recovered effect |
| --- | --- |
| `802C6C7C`, names `804D0834`, offsets `804D09C4` | `beModelCtrlInfoWork` fields, size `EC` |
| `8034D61C`, player vtable `804E7DAC` slot `98` | Player preparation; rate stores at `8034D788/78C/790` |
| `8012D434` | Transform a direction by the model basis, without translation |
| `8012F57C`, stores `8012F798/79C/7A0` | Initialize global `804F8B10` to +Z `(0,0,1)` |
| `8030D910`, path `8030DAA4..8030DADC` | Synchronize Y heading after player preparation/model assignment |
| `8030A6BC` | Ensure elapsed timer, release position lock, configure MOVE/MOVEEX channels |
| `80308EC0` | Exact CTRL names, mode/phase and fall-check flag |
| `8030AEE0` | PDMOVE Stop/Move parameter pairs |
| `80309168` | Post-input control transitions and sequence dispatch decision |

`ModelControlState` owns a pose, channels, one optional `KEIKATIMER`, mode/phase
and an optional four-float PDMOVE array. Copies are independent. It is a supported
subset, not a host ABI copy of `beModelCtrlInfoWork`. The original work fields
are mode/phase `3C/40`, parameter list `48`, input byte `70`, requested sequence
`74`, target/rate/applied angles `80/8C/98`, motion list `C8` and fall-check flag
`DA`. Metadata explicitly defaults the latter to true at `802C6FE0..802C6FEC`.

Work `78` is `_isHitCheck`, the flag written by the motion tick and currently
exposed as `pose.translated`. Work `A8` is the separate `_isPosLock`, exposed as
`pose.position_locked`. Neither flag alone implements collision or attachment
updates. Generic metadata registration's temporary objects do not establish
full player defaults; the API requires an explicitly prepared model matrix.

## Player rates and heading

`set_player_turn_rates` only stores `(0,7,0)` from original constants `80456F68`
and `8045700C`. These are radians per original frame unit. At cached delta 1,
Y rate 7 can snap across any shortest arc; at delta 0.125 its step is 0.875.
The helper does not allocate a player, initialize other fields or resolve a spawn.

`synchronize_model_heading` transforms +Z with the original float/FMA order and
stores `float(atan2(forwardX,forwardZ))` into desired and applied Y. It preserves
the full matrix, X/Z angles, rates and flags. Original zero products matter:
a negative-zero X in the matrix's Z column can become positive zero before
atan2, producing +pi instead of -pi when forward Z is negative. Only the used
X/Z results must be finite; a non-finite unused temporary Y is not consumed.

Host double atan2 replaces the math call at `800A2C74`. As with host sin/cos in
[MODEL_MOTION.md](MODEL_MOTION.md), PPC libm bitwise parity is not established.
IEEE round-to-nearest is the supported profile; guest floating exception flags,
NaN payloads and subnormal modes are not modeled.

## Command effects

`decode_model_command` accepts exact MOVE/MOVEEX/CTRL/PDMOVE types. MOVE decoding
reuses [MOTION.md](MOTION.md). CTRL/PDMOVE require little-endian objects and a
string at field 2; PDMOVE also requires finite float fields 3/4. Names are bounded
to 4,096 bytes without NUL and serialized string fields to 4,104 bytes. Unknown
types, unverified byte order and malformed fields fail explicitly.

MOVE/MOVEEX creates and starts the missing elapsed timer, with initial cached
frame 1. An existing timer is retained. If position lock is set, it clears that
flag and synchronizes Y from the model before applying the channel command.
Even an unknown MOVE name performs this preparation and channel allocation.

| Exact CTRL name | Effect |
| --- | --- |
| `onWalk` | Mode 1, phase 0 |
| `toWalk` | Mode 2, phase 0 |
| `toWait` | Mode 3, phase 0 |
| `isLive` | Mode 4, phase 0 |
| `onFall` / `noFall` | Set / clear fall-check flag; preserve mode/phase |
| Any other name | Original successful no-op |

PDMOVE lazily creates four zero floats, ordered Stop value/frame then Move
value/frame. Exact `Move` writes the second pair; **every other name** writes
the first pair. Zero frames are valid. CTRL/PDMOVE do not allocate motion
channels, create a timer, unlock position or synchronize heading.

## Post-input control and motion

The caller supplies the original input byte and current/requested sequence names
after frame reset and input processing. An existing request other than `SQ_NONE`
skips mode logic. With no request, the original branches are:

| Mode / phase | Input condition | Effect |
| --- | --- | --- |
| 1 / 0 | Nonzero / zero | Configure local Z Move / Stop, enter phase 1 / 2 |
| 1 / 1 | Zero | Configure Stop, enter phase 2 |
| 1 / 2 | Exactly 1 | Configure Move, enter phase 1 |
| 2 | Exactly 1 | Request `SQ_WALK` |
| 3 | Zero | Request `SQ_WAIT` |
| Other combinations | Any | Preserve state/request |

Mode 1 loads channel 2 before checking phase/input, so requires allocated
channels even on a stable phase. PDMOVE parameters are needed only on an actual
transition. It configures the existing scalar directly, preserving its speed;
it does not run MOVE's timer/position preparation. Nonzero bytes such as 2 and
255 therefore differ from 1 on the original exact-equality branches.

The result requests dispatch only when the final name is neither `SQ_NONE` nor
the current sequence. This API does not execute the original sequence callback
or change the current sequence. That callback's actions and commands remain a
required integration boundary. `advance_model_motion` exposes only the subsequent
motion part: unallocated channels clear the motion flag; allocated channels
require an existing timer and use the version 0.15 combined model tick. Missing
dependencies and validation failures preserve owned state instead of simulating
a successful empty update.

## Validation and remaining work

- **64 asset-free checks** cover command fields, original rates, signed-zero
  heading, lazy preparation, independent state, precise input/phase branches,
  request priority, unused dependencies and atomic failures.
- **668 original records** decode across 2,273 recognized archive IGBs:
  526 CTRL and 142 PDMOVE, with zero rejections. PDMOVE has 71 Move / 71 Stop;
  CTRL has 91 isLive, 85 noFall, 71 onWalk, 140 toWait and 139 toWalk.
- **10,000 local instruction comparisons**, 2,500 each for CTRL, PDMOVE,
  post-input control and MOVE, have zero mismatches. The bounded interpreter
  uses synthetic named objects and preallocated channel/list capacity. Host
  atan2, timer creation and sequence dispatch are supplied service boundaries;
  allocation internals, full sequences and original libm are not validated by it.
- **49 optional original-data checks** use Agumon Move 130 / Stop 1540 and
  CTRL 6499 / 622 at both groups and all four stored PLAYERPOINT 115 slots.
  Explicit command effects request WAIT/WALK and travel about 8.275 local units
  over 18 Move / six Stop updates at cached delta 1. They do not execute the
  intervening original weapon/action sequences or establish multiplayer.

```powershell
.\build\Release\dw4_model_control_tests.exe 'E:\path\Digimon World 4 - Extracted'
```

Version 0.17 connects the owned control fields to a supported sequence command
runner, weapon labels and sequence-stop motion. ACTION execution and full
sequence-entry callbacks remain required; see [MODEL_SEQUENCE.md](MODEL_SEQUENCE.md).
Next connect complete player construction, source-qualified ONE/RAM ownership,
original input and the remaining sequence/action effects, then attachment/collision and
camera state. The existing walking preview keeps its provisional host movement
and clip choices. The complete game and local multiplayer remain the acceptance
target in [ORIGINAL_BASELINE.md](ORIGINAL_BASELINE.md).
