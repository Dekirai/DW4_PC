# Native player input callback

`ModelPlayerInputInvocation` implements the represented original player
virtual `+A0`, USA address `8034C3C8..8034D19C`. The actual per-RAM frame
consumer calls this callback through its input/control stage. It is separate
from device polling, the pad-manager update, and the enclosing game scheduler.
Code and tests require no copied original game data in the repository.

## Ownership and timing

The callback ensures the actual RAM data list, finds the first named `ONE`, and
captures its current work (`8034C4B8/C4BC`). It does not read RAM.parent. A
replacement ONE or work during a service does not retarget this invocation.
The original controller owner stays fixed; its current pad-manager field and
the live RAM number are consumed at `C4C0/C4C4`.

`ModelPadManager`, `ModelPadList`, and `ModelPadState` represent mutable owners
for the reached original pad fields. `model_pad()` follows `80312138`: a
nonzero redirect byte or signed number greater than or equal to four chooses
physical backing slot four. Otherwise it reads the numbered physical slot.
Logical count and capacity do not control this lookup. Nullable entries remain
nullable, repeated entries retain identity, and unsupported negative physical
addresses fail at the reached read. This includes the fifth pad's selection
semantics; it does not implement that pad's producer or synthesize its input.

Input captures the selected pad's direction and left-stick values before
movement processing. Its held/trigger words are read later, after camera work.
Thus service callbacks can change later button input without changing the
already consumed stick sample. The original main-lobby byte must have been
initialized; an absent value in the existing optional preparation profile is
an explicit boundary at its first read.

## Implemented operations

Nonzero direction updates the captured work's eight turn-attack counters using
the original direction table and duration 30. It counts positive entries before
their decrement, including entries that reach zero. Eight positives select the
spin-attack path. Stationary input does not access this list or camera data.

Movement computes the desired Y heading from the original camera inverse's
transformed +Z and the captured stick direction, then sets the input byte. It
preserves the original zero products and fused transform operations. The two
atan2 results use the existing finite native math profile, without a claim of
bit-identical PowerPC libm behavior at every boundary.

Outside the main lobby, B trigger requests `SQ_GUARD` unless currently jumping.
Held B runs the four-state body-attack policy, including the 15-frame direction
window, a live system-delta read only for a positive timer, and the
`SQ_ATCK3` request/control resets when the direction pattern completes. The
subtraction uses the timer value captured before the delta provider; later
state decisions read the captured work's current fields. NaN does not enter
the positive-timer branch.

After the original jump/fall gates, button handling follows the original
priority and uses native messenger delivery:

| Input or state | Operation |
| --- | --- |
| Open panel | Query `NDMWPANEL / ISCHECK`, then request `SQ_ITEM` or set the live `SQ_WAZA` label and request it when the returned flags select an action |
| X trigger | Request `SQ_JUMP` |
| Y trigger | `NDMWPANEL / GENEEMGHEAL` |
| L trigger | `NDMWPANEL / GENERATEITEM` |
| R trigger | `NDMWPANEL / GENERATEWAZA` |
| Z trigger | `NDMWPANEL / GENERATESTAT` |
| A trigger in the lobby | Append the actual hitbox subcommand: kind 60000, delay/length 1, offset `(0,4,7)`, size `(3,3,3)` |
| A trigger outside the lobby | `DROPITEM / ACTION1`; if no response boolean exists and `ATDELAY` is expired, request the ordinary/spin attack, read `ATTACKDELAY`, start the named timer, and send the corresponding `i_stpnl05` icon sequence |
| Stationary Start with no other held button | Query panel state and either play `CURSOR_NG` or send `SUBWND / GENERATE` with the live player number and `STATUS` |

An open panel retains its original restricted shortcut branch even if an
ISCHECK response clears the panel flag. Panel generation success tests the
boolean-list **count**, so a present false value still opens it. Similarly,
any DROPITEM response boolean prevents the attack fallback. ISRUN instead
requires the exact first value one for the rejection-sound branch.

Panel creation transfers the captured `shared_ptr<ModelPlayerWork>` as the
actual opaque object payload. `GameDataObject` now represents that owner
directly. It is neither a registration identifier nor a copied script facade;
the existing messenger retains its type and shared allocation.

The callback publishes `key_event_name` for the outer `80309168` control
consumer. It does not enter that sequence itself, call player `+B8`, or run
another frame. Previously committed messenger sends, counter writes, requests,
timer starts, and packet identities survive a missing-service pause.

## Explicit external services and supported limits

`ModelPlayerInputServices` supplies original operations whose surrounding
owners/producers are not yet connected here, plus an optional frame-time override:

- `camera_matrix` returns the actual matrix at controller camera-manager
  `+2C -> work+14 -> matrix+6C`. The returned owner is retained before inversion.
- `inverse_camera` performs original `8012A92C`. There is no identity or host
  orbit-camera fallback. A missing provider or unavailable result suspends
  after the completed movement-counter prefix.
- `frame_delta` optionally overrides the original system frame-time read for
  instrumentation or an explicit service boundary. Without an override, the
  callback reads the invoking controller's current `system->frame_time_f`
  (`+10 -> +28`). It captures the body timer first at `8034C778`, then reads
  the system and its field at `8034C788..78C`, only while B is held and that
  captured timer is positive. A missing reached system faults after prior
  guard publication; unused paths do not require it. A replacement RAM manager
  does not redirect the fixed invoking controller. An installed override that
  returns `nullopt` still suspends and never falls back to the system field.
- `player_row` reads the actual player-data manager row at controller
  `+34 -> list+44 -> live RAM slot`. The returned row is retained and its first
  named `ATTACKDELAY` integer wrapper is consumed.

Missing service implementations are distinct from returned null original
owners. The former suspend; reached null owner dereferences fail and preserve
prior changes. Required downstream panel, sound, pickup, model-icon, and window
providers must execute their real operations. Merely logging a message does
not satisfy this contract.

The direction and icon tables use their recovered normal initialized values.
The represented icon range contains the original thirteen `SQ_DLTM01..13`
entries. An out-of-range table index fails after the already completed timer
start. Turn-attack history currently uses the existing shared vector profile;
it does not separately represent a malformed guest logical count and spare
capacity. Eight reached physical reads are required, with earlier writes kept
if a later read fails. Original string-pool allocation and guest heap reference
counts are represented by owned native strings/shared objects, not emulated
allocator calls.

## Validation

`model_player_input_tests.cpp` covers physical fifth-pad/redirect selection,
nullable and aliased owners, captured stick versus later button fields,
camera/delta pauses, current and replaced work, body-attack transitions,
actual current system frame time, replacement RAM/system owners, reached null
system failures and unused-system gates,
panel response counts/flags and opaque work identity, retained query packets,
pickup/attack timer ordering, the spin path, lobby hitbox fields, status/sound
packets, and reached malformed-data and recursion failures. Synthetic providers
are declared fixtures and do not stand in for completed original gameplay.

The initial focused build passed 72 checks with `/W4` clean compilation against
the refreshed native core. This is native synthetic verification and local
original-instruction review, not a new instruction-oracle comparison or
physical-console test. Camera inversion, manager/device production, and the
remaining original frame services retain their explicit integration boundaries.
