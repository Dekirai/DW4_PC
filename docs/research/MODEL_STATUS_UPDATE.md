# Actual player status update

`ModelStatusUpdateInvocation` implements one player virtual `+C0` invocation,
`803ACD88`, against the actual registered RAM and canonical mutable work. This
is the status phase called by the common MODELCTRL frame entry `8030CB2C`.
It is distinct from the `803AE1A4` reconciliation performed after ordinary
sequence entry by [the player +B8 callback](MODEL_SEQUENCE_ENTRY.md).

The invocation independently ensures the RAM data list, resolves the first
named `ONE`, and captures its current work. A null status-timer list returns
before reading HP. A present list requires the actual HP wrapper. A zero HP
value follows the twelve-icon death path; other values enter live status
processing. No work snapshot replaces those captured mutable owners after a
nested message or sequence changes the current RAM graph.

## Original processing order

The player vtable at `804E7DAC` stores `803ACD88` at `+C0`. The separate base
MODELCTRL `+C0` is an empty function and is not used as a player substitute.
The represented player callback follows this order:

1. On death, inspect twelve display IDs in order, send `MODELCTRL/SQ_ERAS`
   for each nonnegative-or-other-non-`-1` ID, then reset current timer and
   display-list values. Each iteration reads the captured work's current list.
2. Initialize the captured work's messenger reference if it is null.
3. Process POISON, SLEEP, STUN, BIND, ATKDOWN, POWDOWN, SPEEDUP, POWERUP,
   REFRACT and METAL, in that exact order. Each block reads control-enabled and
   its current status slot when reached; disabled control does not read those
   block's timer/flag fields.
4. Update all twelve status icons, including the aggregate icon in slot 11.

POISON starts a two-billion-unit interval on its first event. A due tick uses
the original wrapped signed HP subtraction/clamp, sends the player's meter and
number updates when applicable, then handles death or the remaining pulse
count. Nonplayer lethal poison restores HP to one. The two UI messages read
live HP and RAM fields at their separate consuming sites.

The other duration fields use signed integer `frames * 1,000,000,000 / 60`.
SLEEP enters `SQ_SLEP` on its first event and enters `SQ_WAIT` before removing
an expired timer. STUN processes only `SQ_STUN`; its expiry clears the status
and removes the timer before entering `SQ_WAIT`. BIND preserves the original
damage-sequence dictionary gate and stops every captured movement-list entry
except physical index one when its timer has not expired.

Expired attack/power reductions and beneficial effects clear their respective
DOWNDATA/UPDATA fields before the original stat-refresh helper `803AEB00`.
For a player, that helper pushes the current messenger buffer, sends GAMERAM
`RECALCPLAYER`, selects GAMERAM again, sends `ADDHP` with a separately read RAM
number and zero increment, then restores the original buffer owner. Missing
native calculation services preserve that unfinished operation.

METAL begins with end time zero, clears DOWNDATA slot six, and subsequently
increments that slot by four whenever its timer expires. It queries the real
GAMERAM `GETPLAYER` row and reads the first typed `ATTACKDELAY` value. The
original wrapped 32-bit multiplication by 120 precedes the integer duration
conversion and timer restart. Its stat refresh follows the query/restart.

## Actual timers, messages and nested entries

[ModelRamTimerAccess](MODEL_TIMER.md) supplies start and expiry operations.
Each helper resolves the RAM's current data/manager/context at its original
site. No timestamp is sampled once for the complete status update. Expiry
captures a present timer's signed end value before performing the complete
fresh named-time lookup; a missing timer is expired without reading a clock.
Equality does not expire a timer.

Every status transition uses an actual `ModelBaseInvocation`, including its
ordinary sequence entry and required player `+B8` callback. The status updater
continues only when that nested invocation returns. For example, an unavailable
`SQ_WAIT` effect keeps an expired SLEEP timer in place because its removal
comes after that entry; the corresponding STUN status was already cleared.

Native message delivery retains its dispatch and packet across host retries.
Already completed damage, timer starts, list stores, queries and buffer pushes
are not replayed. The first nonnull `ModelStatusUpdateServices` owner remains
bound to a suspended invocation. Throwing services and recursive entry fault
the invocation permanently, including when a provider catches an inner
recursive-call exception.

## Status icon helper

The recovered `803ACB30` helper performs a fresh RAM `ONE`/node lookup and
captures its matrix translation before querying or moving an icon. Its outer
status work remains the earlier `803ACD88` capture.

For display ID `-1`, the real messenger `FIND_SPACE` operation searches the
registered MODELCTRL rows for an inactive RAM. An empty result returns `-1`.
Object zero must otherwise identify an actual RAM; `resolve_ram` supplies that
retained owner. The helper reads its current number for `GENERATE`, supplies the
captured position and original indexed sequence string, then reads the same
RAM number again after the message returns. The outer callback publishes that
result into its current display list.

Existing icons optionally receive the original `SQ_LOOP` position sequence,
then `MOVETRANS` with the captured position and zero offset. The returned ID
remains the earlier captured value. Original adjacent OPEN/LOOP constant-table
words are represented where an index reaches them; a reached invalid pointer
or unrepresented index fails explicitly.

The updater does not manufacture an inactive icon RAM, execute unsupported
controller commands as successful animation, or invent a result when an icon
provider/resolver is missing. Source registration, actual icon creation and
all required downstream controllers remain responsible for those operations.

## Verification and limits

Evidence comes from the owner's USA GameCube executable, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, including `803ACD88`, icon helper
`803ACB30`, refresh helper `803AEB00` and timer-expiry helper `8031E238`.
Original bytes and local instruction probes remain outside the repository.

Focused tests cover captured work versus replacement, death-loop live reads,
poison UI order and arithmetic, stat reset ranges and retained buffer owners,
duration conversion, BIND channel ownership, METAL row lookup, real messenger
FIND_SPACE and icon identity retention, nested SLEEP/STUN entry boundaries,
clock-failure prefixes and caught re-entry. Timer expiry additionally passed
12,144 local original-instruction cases with zero differences: 1,767,498
instructions, 18,868 named lookups and 6,724 clock reads, including 4,000 graph
or timer mutation cases. This bounded oracle uses explicit allocator/name and
clock seams and checked unsafe-pointer/nonfinite-scale boundaries; it does not
prove the complete status callback by instruction comparison.

The existing canonical status payloads are shared vectors: their represented
logical count equals available backing size. They preserve nullable fields and
shared payload aliases, but cannot represent a separate malformed signed count
or unused backing capacity. Reached unavailable indices are checked failures.
No whole-list repair or silent padding is performed. The movement list retains
its existing represented owner profile. Messenger nonfinite payload and null
command restrictions remain checked native boundaries.

This module supplies a real status callback for the frame invocation. It does
not by itself schedule a frame, implement original player input/camera/combat,
or change the walking preview.
