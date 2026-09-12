# Actual player frame invocation

`ModelFrameInvocation` connects one original player-manager virtual `+84`
call at `8030CB2C` to actual RAM, ONE and player-work owners. Start it with
`ActionEventRuntime::begin_model_frame(id)` and resume the retained invocation
through that runtime. It uses the actual registered object; it cannot fall
back to the walking preview's provisional simulation.

This is one per-RAM consumer. The outer manager traversal at `802F28F8`,
manager attachment, device polling, system delta production and context time
advancement remain separate requirements. Explicitly supplying those owners
does not implement their original producers or establish a gameplay session.

## Captures and order

The invocation captures RAM.parent before ensuring RAM data and finding its
first named ONE. It loads ONE.work before the activity gate. An inactive RAM
therefore still faults on a reached missing ONE, but may return with null work.
An active frozen work returns before reading its sequence name. `SQ_ERAS`
executes the real base `REMOVE` command and returns.

The ordinary active path retains that outer work across these phases:

1. Clear `key_input`, then publish `SQ_NONE` as `key_event_name`.
2. For nonzero mode, optionally run the actual player input callback, then
   execute the original post-input `80309168` tail on the outer work. A changed
   request enters a real ordinary sequence through `ModelBaseInvocation`.
   Mode zero with AI removes the first untyped `AITIMER` without reading time.
3. Run the actual player `+C0` status callback unconditionally. It captures
   its own current ONE/work, independently of both input and the outer frame.
4. Run actual movement `8030A448`, or the required position-lock helper.
5. Capture ride data and, when present, require its real matrix update before
   publishing the translated flag.
6. If the outer work's live wait name is nonnull, run the actual common wait
   helper and its separately captured player fallback when reached.
7. Advance the outer work's positive invulnerability countdown through the
   actual RAM timer endpoint.
8. Capture that work's then-current `+E4` subcommand wrapper and update it
   through the actual per-entry system-time adapter.
9. Require original movement collision/hit processing when the outer work's
   translated flag is nonzero. Finally reload ride data for its type-dependent
   position tail.

Input and status can act on different work generations after an intervening
ONE replacement. The original outer control, motion and wait captures do not
silently follow those replacements. A direct wait completion resumes `8030C3AC`
without ordinary-entry resets or an extra player `+B8`; a reached JUMP still
performs its real nested ordinary entry and callback.

## Time and required services

`ModelSceneManager` has separate current context, system and pad owners. Named
timers resolve current RAM.manager/context at each helper. Input's positive
body-attack timer reads the captured invoking controller's current system.
Subcommand advancement reads current RAM.manager/system separately for each
reached entry. These are distinct original reads, not a frame-wide time
snapshot. An empty subcommand wrapper needs no system or effect provider.

`ModelFrameServices` supplies remaining position-lock, ride, movement-collision
and ride-tail operations. It also retains the input camera services, status
recipient resolution and actual subcommand sound/effect/hitbox services. A
missing reached service suspends its exact phase; it cannot count as a
successful no-op. The actual motion and wait implementations run directly.

The wrapper's delayed entries can advance and return without generating an
effect before its deadline. This is a real original gate, not a substitute
provider. Required SOUND, effect generation and hitbox execution retain their
own unfinished effects when reached.

## Continuation and evidence

The same invocation, messenger and runtime must resume a suspended frame.
Completed phases do not replay; the original wrapper stays captured after
work replaces its `+E4` field. Exceptions and caught recursive entry permanently
fault the invocation. One runtime admits only one unfinished actual frame.
A competing frame invalidates both continuations before the second can reset
input. While the owning frame is paused, runtime guest execution and direct
sequence/player callback entries reject unrelated calls before effects. Nested
calls made while that frame is executing remain available, as does supplying
missing host services during a pause. Abandoning an unfinished frame leaves
its runtime lease faulted, so a
new call cannot silently repeat its already committed effects. The lease owns
no RAM/work/frame back-reference and therefore adds no ownership cycle.

Synthetic checks cover early-gate read order, empty queues, captured wrapper
replacement, effect pauses, old/new work generation splits, real direct wait
continuation, recursion and abandonment. Original-data validation starts from
four completed Agumon GENERATE calls, explicitly supplies neutral pad/system
owners and follows the first player frame to its actual remaining boundary.
Exact results belong in [VALIDATION.md](VALIDATION.md).

The checked player-controller profile does not yet dispatch different NPC or
enemy virtual implementations, position-lock/ride/collision helpers, the
original camera inverse, full manager scheduling or connected multiplayer.
The status and movement modules also document their bounded list/numeric
representations. See [PLAYER_INPUT.md](PLAYER_INPUT.md),
[MODEL_STATUS_UPDATE.md](MODEL_STATUS_UPDATE.md),
[MODEL_MOTION_ACTUAL.md](MODEL_MOTION_ACTUAL.md),
[MODEL_WAIT_ACTUAL.md](MODEL_WAIT_ACTUAL.md) and
[MODEL_SUBCOMMAND_UPDATE.md](MODEL_SUBCOMMAND_UPDATE.md).

Evidence uses the supplied USA revision-0 DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. The recovered frame body,
player vtable `804E7DAC` and named input/status/wait/motion helpers are
documented independently of the preview's host controller.
