# Prepared player ownership and ACTION events

Version 0.25 connects explicitly prepared MODELCTRL work, live ACTION queries,
retained waits, loaded Se/ActionStarter tables and owned sound/effect subcommands. It does not
finish original player construction or startup. The walking preview still uses
its provisional input, movement and camera. Audio playback, generated effect
actors, collision callbacks and the outer player scheduler remain required.

## Graph ownership

`decode_player_runtime_binding` requires explicit MODELCTRL and ACTION info
exports, a RAM ordinal, a LOD branch and a loaded source occurrence. It follows
the actual MODELCTRL RAM's ONE/node graph and requires the corresponding ACTION
RAM to use the same node. Model/action names, all instance ownership, combiners,
actors, appearances and the selected skin are checked. A matching name alone
cannot bind unrelated graphs. Reference aliases are checked through their
resolved objects, not just through different serialized numbers.

The binding owns the decoded SEQ and ACTION programs, selected skin and small
metadata records for all ordered actors. Selecting a LOD branch resolves its
actual actor reference into ACTION actor order; those indices need not match.
For original Agumon, branch 1 selects high-detail actor index 1 and geometry
5665, while the first ACTION actor remains the shadow. Immutable skin data is
retained only for the selected appearance. Loaded source occurrence distinguishes
repeated loads of the same logical path.

`ActionEventRuntime::register_model` consumes that immutable binding and an
explicitly prepared `ModelControlState`. It owns a sequence runtime and a
move-only subcommand queue for that MODELCTRL RAM. The supplied activity and
attachment state are caller preparation, not effects of original GENERATE.
`register_action` retains a shared live ACTION runtime under the binding's actual
ACTION RAM, separately from MODELCTRL RAM. Its node, combiner and ordered actor
ownership are validated, including after external reassignment. The same runtime
cannot be registered under another RAM or repeated source occurrence. Activity
queries read that runtime's live state; explicit model activity does not replace
it. The animation library remains caller-owned.

## Live completion and prepared wait processing

`query_action_complete` sends `ACTION / model name / RAM ordinal / ISCOMEND`.
The original structured hook returns zero; the fallback appends a boolean
without advancing animation or applying an activity gate. Selected work with a
nonzero ACTION wait returns false; otherwise cursor equality with the selected
command count supplies the result. Missing selected work suspends before append.
An authored empty action name remains distinguishable from missing work.

Message matching retains all original name/index matches across sources. The
caller consumes the current shared buffer's **first** boolean after the entire
send completes. It never combines results or substitutes false for a missing
result. A pending query retains its dispatch and completed recipient prefix;
other top-level operations cannot overwrite its shared arguments.

`prepare_wait_state` explicitly supplies represented control/attack fields.
`update_wait` and `resume_wait` own the [model wait kernel](MODEL_WAIT.md), bind
the completion service above and read the binding's RAM ordinal from four
already processed `OriginalPads` slots. A null pad owner preserves the required
input boundary. The callback is reached only when the original branch reads
input; disconnected slots retain the original processed zero/release behavior.

Wait services supply actual command resume and full outer sequence dispatch.
An unfinished pass retains the same state address, completed pad/query phases
and live callback changes, including a nested Atck02 sequence selection. It
blocks unrelated top-level work until resumed; synchronous callbacks may still
perform their original nested operations. A complete pass can retain a wait
whose condition is still false.

`run_action_sequence` sends sequence ACTION requests to the registered live
runtime, using dispatch once and `ActionRuntime::resume` after a service pause.
It preserves the initial world time and animation source owner across retries.
The caller supplies `ActionServices` by reference so original shared random
lower-bound changes survive; only its broadcast hook is temporarily replaced
and restored. The same source and service objects must survive a suspended call.
An opaque state identity detects external ACTION progress or reassignment during
that continuation, including replacement by another state for the same RAM.
Read-only queries leave the identity intact. An older unrelated ACTION broadcast
must finish before a fresh sequence invocation; it cannot be mistaken for
acceptance of the new command. Invalid initial selection/ownership is checked
before retaining an invocation. Missing random/broadcast services retain the command prefix and
return `action_required`; `last_action_result` exposes the underlying reason.
An exception during ACTION execution marks uncertain work as non-replayable.
These methods do not update ACTION animation clocks automatically.

The outer `dispatch_sequence` hook still requires player entry/status effects.
An explicit test callback may select and run a prepared sequence to exercise
continuation ordering, but it does not establish the complete original dispatch.

## Loaded consumers and message delivery

`action_event_tables` reads exact exported `beSeInfo` and
`beActionStarterInfo` graphs. Its immutable programs retain nullable strings,
signed delays, ordered RAM/model/action occurrences, original references and
source identities. A parallel canonical RAM identity list makes distinct
serialized aliases share one native owner without deduplicating registrations.

Se selects the last matching model table row; ActionStarter selects the first.
Both emit every matching action row in order. DATA null model/action keys use
the original empty fallback. Nullable sound and sequence payloads remain
distinct from empty strings. Only exact `beActionStarterData2` emits the second
rotation vector; other rows retain the original zero rotation.

Each consumer RAM occurrence registers `ACTION_BROADCAST / null / -1`. Repeated
same-kind registration reuses the RAM's owner and updates its parent program,
while old and new registration rows survive. Replacing an owned RAM's manager
class or conflicting with prepared MODELCTRL ownership is outside this checked
profile. Activity remains explicit caller state.

The recovered hooks capture the incoming model, action and RAM number before
their first nested send. The native continuation likewise owns these values and
its matching rows. Se emits `MODELCTRL SETSOUND` with nullable sound and float
delay. Starter emits `MODELCTRL SETEFFECT` with effect-info name, nullable sequence,
delay, offset, optional rotation and lifetime/position flags, setting enabled-only
to one. The target's active model work appends each typed request in order.
Inactive model work acknowledges the handled command without appending.

The underlying messenger preserves cross-file matching, repeated registrations,
the original 64-entry selection cache, live activity and the single shared
argument/result buffer. See [MESSENGER.md](MESSENGER.md). Original raw getters do
not provide defaults for missing required arguments; native handlers reject an
incomplete SETSOUND/SETEFFECT message. Unknown MODELCTRL commands stop for their
required outer manager instead of being acknowledged as completed.

## Suspension and sequence interruption

Original sends are synchronous. Native suspension exposes missing services
without replaying completed work. Each consumer retains its nested send and
matching-row cursor; each bound ACTION RAM retains its broadcast handle.
`ActionServices::try_broadcast` returns false while delivery is incomplete,
leaving the event pending after committed ACTION commands. A successful retry
finishes that same send. The legacy void callback remains available separately.

An unfinished top-level send blocks new top-level broadcasts and mutable
messenger access, protecting its shared arguments from another player's send.
Actual nested callbacks may still select/send and change the live shared buffer,
as the original does. Callers must not retain a mutable messenger reference
across a suspended send. Missing manager/activity services can be supplied before
resuming; an exception indicates an uncertain effect and faults the dispatch.

Sequence entry uses two visible phases. After validation, `begin_select`
publishes the new sequence name, data/cursor, label count and CTRL/PDMOVE/wait-name reset.
Cleanup callbacks therefore see the new sequence, as at `8030E980..8030E9D0`.
The owned fall-check and motion-channel changes follow completed cleanup through
`finish_select`; no new command runs beforehand. An ordinary local JUMP enters
the same phases through its owner hook and retains its original command metadata
across suspension. Omitting a required cleanup hook on retry cannot bypass it.
`run_sequence` resumes a pending local jump; `resume_sequence` completes an
external selection. Legacy `ModelSequenceRuntime::select` performs both phases
atomically for callers without an outer cleanup owner.
These APIs implement the owned cleanup/selection effects only. Original
collision, player and virtual entry callbacks are still separate required
work. In particular, player virtual `+B8` (`8034E1B0`) later appends the original
`i_pcnumic / SQ_OPENP%d` idle effect and invokes status handling; these effects
are not represented by prepared selection. See [MODEL_SUBCOMMANDS.md](MODEL_SUBCOMMANDS.md).

## Reference and validation boundary

Reference: USA revision-0 DOL SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.

| Recovered operation | Original address |
| --- | --- |
| Se registration / structured hook | `8031308C` / `803130D4` |
| Se event field snapshots | `803130F8..80313100` |
| Starter registration / structured hook | `802F2244` / `802F228C` |
| Starter event field snapshots | `802F22B0`, `802F22B8`, `802F22C0` |
| MODELCTRL SETEFFECT / SETSOUND | `8030DD34` / `8030DE1C` |
| Ordinary sequence selection and cleanup | `8030E944..8030EAA0`, `803108A0` |
| Local JUMP re-enters controller virtual dispatch | `8030C814`, `8030BCB8..8030BCE4` |
| ACTION structured fallback / ISCOMEND | `802F2238`, `802F1828..802F186C` |
| Common wait / player attack fallback | `8030BDB8`, `8034D214` |

Synthetic tests cover table selection, aliases, owned bindings, routing,
interleaved typed queues, missing services and sequence interruption. Optional
original-data tests load Agumon's four RAMs, Se from archive entry 178 and all
58 Starter exports from entry 179. They drive explicit weapon sequence actions
through native event routing into the original delayed footstep requests and
test-manager sound handles. Actual KATCA3 additionally emits `PC_SUBURI02` and
`x_100armk01 / SQ_ATCK01`, exercising find/generate/erase service suspension and
the original source/attachment payload. Test services do not establish audible output or
rendered effect actors. Selected actor traversal and skin deformation continue
alongside this headless integration. See [VALIDATION.md](VALIDATION.md) for
measured results, and [ORIGINAL_BASELINE.md](ORIGINAL_BASELINE.md) for the full
unfinished game acceptance target.
