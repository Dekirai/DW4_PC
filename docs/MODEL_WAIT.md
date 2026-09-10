# Prepared model wait runtime

`ModelWaitRuntime` implements the retained wait checks in the original common
model controller (`8030BDB8`) and its player fallback (`8034D214`). It resolves
the existing wait and invokes the retained command cursor or sequence service
only when that branch requires it. It has no clock, frame timer or polling loop.

This is a prepared runtime boundary. It does not construct an original player,
implement sequence-entry callbacks, resolve collision/status ownership or supply
the full original frame scheduler. Its callbacks must perform the real qualified
ACTION query, selected-player pad read, command resume and outer sequence dispatch.

## State and common waits

`ModelWaitState` owns these original fields independently for each model RAM:

| Native field | Original field | Default |
| --- | --- | --- |
| `name` | work+4C, nullable pooled `_wCheckName` | null |
| `control_enabled` | work+44 | 0 |
| `attack_state` | player work+128, `_wCheckSt` | 0 |
| `attack_mode` | player work+12C, `_wCheckMode` | 0 |

The companion sequence command is exact `beModelCtrlWCHECK`. Its only serialized
value is inherited slot2 `igStringMetaField`, the nullable name at guest+08.
Handler `8030BCFC` substitutes empty for a null command name, acquires the new
pooled string before releasing the old one and assigns work+4C at `8030BD98`.
The command runner advances the cursor and stops; it does not query the wait
condition immediately. Assignment preserves the three scalar state fields.
Ordinary sequence entry clears only the wait name among these four fields.
The sequence decoder/runner owns those assignment and entry effects; this kernel
owns subsequent wait resolution.

A null runtime name skips the wait branch, matching the common updater's caller
guard. An engaged empty name is a real wait value. Common names are exact and
case-sensitive:

| Wait | Completion condition |
| --- | --- |
| `TurnY`, `Landing` | Y motion channel is inactive. |
| `Action` | Qualified ACTION `ISCOMEND` boolean[0] is true. |
| `Moving` | All XYZ motion channels are inactive. |
| `Rotation` | Rotation channel is inactive. |

The motion predicates implement `8030A0A0` modes 2, 0 and 1 using the prepared
`MotionChannels` owner. An unallocated channel list is inactive. Successful
completion clears the live name before invoking the live retained command cursor
(`8030C3AC`). A new wait assigned by that command callback remains intact.

## Player fallback

Every other engaged name enters the player fallback. When `control_enabled` is
nonzero, the original samples the selected player's held logical buttons before
comparing the player wait name, including unknown names and attack states that
will not progress. Disabled control supplies zero held buttons without a service
call. The tested mask is logical A=0x4000, produced by the recovered GameCube pad
adapter from raw PADStatus0x0100; it is not an SDL button index. The caller must
retain separate selected-player input ownership.

- `Atck01`: state0 sets mode0 and state1, then follows the state1 path. State1
  latches mode1 when A is released. Other signed states return without an ACTION
  query. A true ACTION completion clears the live wait and state, then reads the
  live mode: mode0 resumes commands; every nonzero mode dispatches `SQ_WAIT`.
  Mode is preserved after completion and across wait assignment/sequence entry.
- `Atck02`: A release dispatches `SQ_WAIT` first. The function still queries
  ACTION afterward, even if that nested dispatch changed the live sequence or
  wait. A true result clears the current live name and resumes the current live
  cursor. A false result preserves the state left by the nested callback.
- `Atck03`: A release clears the live name and resumes commands. Held A retains
  the wait. It does not query ACTION or modify attack state/mode.
- Unknown names, including empty, retain the wait after any required pad sample.

The kernel does not replace live work with a saved state snapshot after a
callback. Its retained local values are the completed pad sample, selected query
branch and next service phase. Those correspond to the original function's
control flow and local registers.

## Service pauses and ownership

`update(state, channels, services)` starts one pass. `resume(state, services)`
continues a pending pass at the next unfinished operation. The result identifies
missing held buttons, ACTION completion, command resume or sequence dispatch.
`complete()` means the pass finished; it does not necessarily mean the wait
condition became true or the wait name was cleared.

Missing callbacks and `nullopt` read results leave the relevant service pending.
A supplied false ACTION result is a completed query and finishes this pass while
retaining its wait. A false command/sequence service result means no new effect
completed; a callback that performs several effects must retain its own internal
continuation. The kernel never repeats a completed pad read, query or nested
sequence send merely because a later service is unavailable.

The move-only runtime binds a pending pass to the same stable `ModelWaitState`
address. The state must remain alive; resuming with a different object is rejected.
Moving an idle or suspended runtime transfers its continuation, not the bound
state. An executing runtime cannot be moved or entered recursively, and a pending
destination cannot be overwritten by move assignment.

Any exception from a service faults the runtime and preserves the completed
prefix and live state for diagnosis. Subsequent update/resume is rejected because
the throwing service may have committed an effect. No automatic replay, guessed
rollback or fabricated completion is performed.

## Provenance and validation

The implementation follows local inspection of the supplied USA revision-0 DOL,
SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Key boundaries are common
wait `8030BDB8`, motion query `8030A0A0`, player fallback `8034D214`, WCHECK
assignment `8030BCFC`, and ordinary-entry wait clear `8030E9C4 -> 8030F418`.
Original binaries, data extracts and disassembly remain outside tracked source.

The asset-free `model_wait_tests` suite covers common predicates, null/empty and
unknown names, service pauses, same-owner enforcement, moves, signed attack
states, A-release latching, live callback mutations, nested `Atck02` ordering,
reentrancy and uncertain service outcomes. The final-core Release candidate
`build/Release/dw4_model_wait_tests.exe` passed 73 checks.

An ignored local comparison executes the actual instruction control flow of
`8030BDB8`, `8030A0A0` and `8034D214` in the existing bounded guest interpreter.
It matched 1,835 cases, executing 165,190 instructions at 416 unique instruction
addresses, against that final-core Release candidate. Its explicit helper
boundaries are prepared ONE lookup, pad sampling, messenger dispatch/result,
string comparison/pool operations, command resume and outer sequence dispatch.
Selected callback mutations test the original handling of live state. This
comparison does not execute the complete original external managers; native
service suspension and fault behavior are checked by the synthetic suite.

The public test executable supports an asset-free `--oracle` text protocol for
rerunning that local comparison against a final-core build. Both the synthetic
suite and instruction comparison were rerun successfully against the candidate
executable, SHA-256
`8B72BF0352204E556468E1810840BCCDC38F1A74887BA026B024B3415FFC1B08`.
These results do not claim application/CI acceptance or physical-console execution.
The ignored probe, detailed case output and rerun instructions are in
`work/v25-wait-oracle.py`, `work/v25-wait-oracle.json` and
`work/v25-wait-final-core-oracle.log`.
