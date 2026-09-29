# Actual model wait invocations

`ModelCommonWait` consumes the original common helper at `8030BDB8` using
captured parent, RAM and work owners. `ModelPlayerWait` consumes the player
virtual `+A4` helper at `8034D214`. Both retain their invocation state across
required native services. They use the actual scene graph, messenger, sequence
executor and pad owners; the [prepared wait runtime](MODEL_WAIT.md) remains a
separate API.

The common helper belongs to the connected player-controller profile. Its
unknown-name fallback directly enters a separate player invocation. The latter
captures the RAM's current parent first, ensures RAM.data, finds its first named
ONE and captures that record's work. These can differ from the common helper's
incoming captures. Neither a later ONE/work replacement nor a pending query
changes either retained capture.

The original frame caller skips a null common wait name. Directly invoking the
helper with a null name is a checked dereference failure. An engaged empty name
is compared normally and reaches player fallback. The fallback reads enabled
pad input before comparing even an unknown or null player wait name.

## Reached reads and continuation

| Wait | Actual operation |
| --- | --- |
| `TurnY`, `Landing` | Read movement entry 1's active field. |
| `Moving` | Read active fields 0, 1 and 2 in order, stopping at the first active entry. |
| `Rotation` | Read movement entry 3's active field. |
| `Action` | Send qualified ACTION `ISCOMEND`; consume physical boolean slot 0 after completion. |
| Other common names | Independently capture and enter the player helper. |

The narrow motion reader implements only the three reached `8030A0A0` modes.
It reads no clock, matrix, mode or numerical motion payload. An empty list
returns inactive. A null list or reached null/missing entry faults; unused
entries may be null and repeated entry owners are allowed. `MotionChannelList`
currently represents logical count and physical storage together, so negative
counts and spare physical entries beyond count are outside this representation.

Successful wait completion clears the captured work's live wait and begins a
direct `8030C3AC` continuation using its then-current data and cursor. The same
actual execution bundle retains command data, local cursor and pending command
services. This path performs no ordinary-entry resets or automatic player
`+B8`. A reached command `JUMP` still invokes the real ordinary entry.

ACTION queries capture the retained parent's then-current name and the live
RAM number at their original argument reads. A null name field supplies the
original empty string; a null parent faults. The query retains its exact
`MessengerDispatch` and packet across suspension, without adding a packet
push/pop. A false result preserves callback-produced wait state. A nonzero raw
boolean completes the wait; an absent addressable boolean is a checked fault.
`MessengerBuffer` does not currently model separate boolean count and capacity.

## Player branches

The player helper reads the captured work's control byte first. Zero supplies
held buttons zero without looking up a pad. Otherwise it uses the captured
controller's live pad manager and current RAM number. The actual pad helper
reads physical slot 4 when its redirect byte is nonzero or the number is at
least 4. It ignores the list's count/capacity fields. Null manager, backing or
selected pad and an unredirected negative index are checked at the reached
read. The controller registration index is not an input index cache.

`Atck01` accepts state 0 or 1. State 0 writes mode 0 followed by state 1;
released A latches mode 1. After a true ACTION reply it clears the live wait,
writes state 0 and rereads mode. Mode 0 resumes C3AC; any other mode invokes the
real `SQ_WAIT` ordinary entry, including its required `+B8` tail.

Released `Atck02` first completes a real nested `SQ_WAIT` invocation, then
queries ACTION using the retained player parent and the newly read RAM number.
A true reply clears and resumes the old captured player work even if nested
entry installed a different ONE/work. A false reply preserves nested or
callback-produced wait state. Held `Atck02` starts with the query.

Released `Atck03` directly clears and resumes commands. Held `Atck03` returns.
Neither branch reads a parent name or queries ACTION. Unknown player names
return after the enabled-pad prefix.

## Lifetime and checks

An invocation keeps strong parent/RAM/work and child-continuation owners but a
weak controller-manager reference. It must resume through the same runtime,
messenger and controller. Recursive use, including an inner failure caught by
a callback, poisons the invocation. Exceptions preserve completed original
prefixes and prohibit replay. Abandoning a begun command continuation leaves
the canonical executor lease faulted; abandoning a pending messenger delivery
does not manufacture its completion. No work-to-invocation ownership cycle or
second per-work wait registry is introduced.

`model_wait_actual_tests` uses synthetic scene owners to check short-circuit
reads, actual first-name lookup, independent common/player captures, mutable
reply fields, physical pad selection, suspended queries, real nested ordinary
entry, old-work command continuation and failure/abandonment behavior. These
regressions and source-order review are separate from the earlier prepared
wait instruction oracle; that oracle is not evidence for the new graph-capture
path. The module does not by itself implement the rest of the original frame
pipeline.
