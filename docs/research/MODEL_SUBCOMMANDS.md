# Original model subcommands

`model_subcommands` owns the ordinary MODELCTRL sound/effect/HITBOX queue, including
delayed execution and sequence interruption. It requires real sound and model
manager callbacks to perform external effects. Queue ownership and successful
event delivery do not themselves produce audible sound or create effect actors.
HITBOX timing and selective clearing are described in [MODEL_HITBOX.md](MODEL_HITBOX.md).
Player construction, the outer scheduler and actual collision target responses
remain outside this module.

## Original boundary

The implementation was recovered from the supplied USA GameCube DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Addresses are provenance identifiers,
not native pointers or executable code shipped with the port.

| Original routine | Recovered behavior |
| --- | --- |
| `80310FAC` / `80311084` | Construct and append sound work |
| `80310B50` / `80310CF0` | Construct and append effect work |
| `80310754` / `803107E0` | Advance entries and remove completed work in list order |
| `803105DC` / `803106C0` | Sound start / cleanup |
| `80310340` / `80310588` | Effect start / cleanup |
| `803108A0` | Sequence-interruption cleanup of the complete list |
| `80316320` / `800A3FDC` | System frame delta / signed integer conversion |

MODELCTRL work owns its subcommand wrapper at `+e4`; that object owns the list
at `+08`. Each append creates another occurrence, including repeated names.
Sound and effect entries share this ordered list with HITBOX entries. Append
owns its supplied payload and nullable strings; it does not borrow an IGB
document's storage.

## Actual wrapper, list and entry ownership

`ModelSubcommandWrapper` represents the allocated `beModelCtrlSubCom`, and
`ModelSubcommandList` represents its distinct `igObjectList`. The default queue
allocates both immediately, with no initial entries. This matches work-field
metadata `802C7028..802C703C`, wrapper type registration `802C53E8` (size `0x0c`),
and `_comList` metadata `802C5484` / `802C54DC..802C54F4`, which auto-creates the
object list through `80028F84` / `8002907C`. The inherited list begins with zero
count/capacity and null data; no initializer seeds subcommands. The native list
models the real empty owner and subsequent ordered entries, without exposing
host container capacity as a recovered guest allocation policy.

The actual ordered container holds separate shared `ModelSubcommandEntry`
allocations. There is no second authoritative deque of entry values alongside
identity tokens. Starting a command changes the same entry observed through
every retaining handle. Selective removal shifts list positions while surviving
entry addresses remain stable. Removing an entry releases the list's reference;
an independently retained entry remains intact.

`ModelSubcommandQueue` is a move-only facade over this shared wrapper. Its
`wrapper_owner()`, `list_owner()` and `entry_owner(index)` accessors retain the
actual allocations. Constructing another queue facade with the wrapper handle
operates the same list and the same native pending cursor, elapsed-time prefix,
selected effect RAM, reentrancy guard and fault state. `entries()` remains a
live read-only indexed/range view, dereferencing those actual entries rather
than constructing payload snapshots. Entry identifiers are diagnostic numbers
within a wrapper; equal identifiers in two fresh wrappers are different objects.

The canonical-slot constructor accepts an aliasing
`shared_ptr<shared_ptr<ModelSubcommandWrapper>>` to the actual work `+E4` field.
New append, clear, update and cleanup operations resolve that field's current
wrapper. `wrapper_owner()`, `list_owner()` and `entries()` describe the live field;
`pending_wrapper_owner()` separately identifies an already captured pass.
Const facade access returns const owner handles. Binding a null field does not
allocate a replacement wrapper: a new operation that needs it rejects.

Replacing the field with a fresh wrapper neither mutates nor cleans the retained
old wrapper. A pending update or cleanup resumes its captured wrapper/list even
after the live field changes or becomes null. Completed prefixes, count snapshot,
cursor, shared fault and reentrancy guards remain with that wrapper. Callback
appends can therefore enter a newly installed wrapper while the original pass
finishes its old list. A new facade can operate the new wrapper independently.

One facade retains at most one resumable pass. Starting another update before
that pass finishes rejects. Same-wrapper cleanup can still interrupt an update;
cleanup against a different live wrapper rejects until the retained pass finishes,
or the caller uses a separate facade for that new operation. This explicit host
boundary avoids discarding a continuation or cleaning the wrong work allocation.
Neither a slot replacement nor another facade bypasses a same-wrapper service
fault or recursive-operation guard.

`require_cleanup_startable()` performs the same read-only null-slot, retained
wrapper, fault, reentrancy and pending-HITBOX checks used by cleanup. Enclosing
sequence operations call it before publishing their pre-cleanup fields. It does
not cancel a pending update or start cleanup, so a rejected host overlap cannot
later be mistaken for a suspended sequence cleanup.

Merely copying or releasing shared handles performs no sound/effect calls.
Move construction transfers both the actual slot binding and any pending capture;
the moved-from facade owns a fresh independent empty wrapper. Groups own their
wrapper field, never a facade, so aliasing slot retention creates no back-cycle.

Base metadata `802C5D70` names byte `+08` `_isOneSet`, float `+0c` `_runFrame`
and float `+10` `_startFrame`. Fresh values are false, zero and zero. Append
overwrites the start frame with the requested delay. Sound metadata `802C571C`
adds nullable `_sndName` at `+14` and a zero 64-bit `_sndHandle` at `+18`.
Effect metadata `802C5BD8` adds `_geneRamNo` at `+14`, initially −1, followed
by model/sequence strings, offset, rotation, sequence-dead and position-lock
flags. These defaults come from the original field metadata initialization;
they are not assumed contents of uninitialized guest memory.

## Timing and ordered updates

`update` receives the original system's `_frameTimeF`. The system computes a
wrapping 64-bit world-time difference, interprets it as signed, converts it through
binary64 and then binary32, and divides by `16666667.0f`. The shared helpers
`original_elapsed_frames` and `original_system_frame_delta` preserve this order.
The existing named motion timer reuses the conversion while retaining its own
cache and three-frame cap. **The system delta used by subcommands is uncapped.**

An entry adds this delta to its elapsed frames before checking the delay. At
elapsed ≥ delay it sets its started flag before invoking its start operation.
An already started entry invokes its update operation. Both supported retained
types have the original always-true update callback; they do not poll completion.
Negative finite deltas and delays are not clamped. Delay 6 is six nominal frame
units accumulated from system deltas, not six seconds or necessarily six calls.

The elapsed addition follows startup NI=1/RN=00 scalar arithmetic: retain
subnormal operands, round to binary32 and flush a tiny result to signed zero.
This happens at each addition, rather than only at final output. The native
boundary rejects non-finite inputs and arithmetic overflow before mutating an
update pass. It does not emulate exception flags or alternate FPSCR modes.

The updater snapshots the original list count. An entry returning false is
removed immediately without skipping the following entry. Entries appended by
a callback wait until the next pass.

## Sound lifetime

Sound start selects `SOUND`, supplies the nullable sound name, and sends `PLAY`.
It reads the returned 64-bit handle and integer type:

| Completed service result | Queue behavior |
| --- | --- |
| Handle zero or all-one bits | Remove the entry |
| Other handle, type other than 4 | Remove the entry |
| Other handle, type 4 | Retain the complete handle for cleanup |

A retained sound is not restarted on later updates. Cleanup selects `SOUND`,
supplies string `DOWN` and the retained handle, then sends `VOLUMECTL`. This is
the exact original command spelling. It is a volume-control operation; the
queue does not replace it with an assumed immediate stop or infer the meaning
of type 4 from a sound's name.

## Effect lifetime and attachment context

Effect start sends `MODELCTRL FIND_SPACE` using the effect model name and index
−1. An empty completed result removes the entry. Otherwise it retains the first
returned RAM number, selects that effect model/RAM and sends `GENERATE` with:

| Argument | Value |
| --- | --- |
| Integer 0 | Source model RAM |
| String 0 | Source model name, with the original null-to-empty fallback |
| String 1 | Nullable requested sequence |
| String 2 | Attachment owner name, or source model name when unattached |
| Integer 1 | Attachment owner RAM, or source model RAM when unattached |
| Vectors 0 and 1 | Offset and rotation |
| Boolean 0 | Position-lock flag |

The source and attachment identities are distinct from the effect recipient.
The caller supplies a prepared equivalent of the original ONE work's attachment
name/RAM fields `+dc/+e0`; this module does not fabricate that model work.
An optional context `resolve_attachment` callback reads live captured-work fields
after successful `FIND_SPACE`, matching `8031037C` before `803104A8/AC/F0`.
The complete `GENERATE` packet is then retained across missing or unavailable
generation services. Later changes do not alter that packet; the next effect
entry resolves its own attachment. A non-null empty name stays empty, while null
uses the prepared source name/RAM fallback. No-space results skip the read.
Without a resolver the supplied attachment value remains the explicit prepared
boundary. An unused fallback value is not validated when a resolver is present.

AER supplies a weak reference to its canonical work for this callback, avoiding
a work-to-wrapper-to-context ownership cycle. This covers live `+DC/+E0` fields
of the captured prepared work, not the future mutable RAM/ONE lookup graph.
The source identity likewise remains the explicit prepared binding. HITBOX's
later packet phase retains its separately recovered live attachment read.
For MODELCTRL SETEFFECT, a second vector supplies rotation only when the message
contains exactly two vectors; otherwise rotation is zero.

After generation, a sequence-dead effect remains in the queue. Other effects
leave the queue immediately and retain their manager-owned lifetime. Cleanup
sends `MODELCTRL SQ_ERAS` for a started sequence-dead effect whose selected RAM
is not −1. This guard also applies if a native continuation has selected a RAM
but is still waiting for generation.

## Interruption, unavailable services and ownership

Sequence cleanup visits the original list in order, skipping the cleanup callback
for effects whose sequence-dead flag is false. It releases all list ownership
only after the complete cleanup pass. The shared list owner itself survives
cleanup, and all facades observe the now-empty list. Sound/effect handles live
in the same shared entry objects throughout the operation. Retained entry
handles do not trigger another cleanup after removal.

Moving a queue facade transfers its wrapper handle and leaves a fresh empty
wrapper in the moved-from facade. The existing move-assignment guard still
requires its destination facade to be empty and have no pending operation.
This explicit facade-assignment rule does not apply to replacing a canonical
work's shared wrapper reference: that reference change has no cleanup effect.

Missing callbacks and explicit unavailable results suspend a native operation.
They must mean that no external effect occurred. `resume` preserves the entry,
pass position and completed phases: it neither advances time again nor replays
earlier playback, space selection, generation or cleanup. A completed original
negative result follows the removal rules above and is not treated as unavailable.
Cleanup can interrupt a pending update and cancel entries which never started.

These resumable operations are a native service boundary around original
synchronous calls, not evidence of an original deferred-message queue. A callback
with several internal effects must retain its own progress before returning
unavailable. Any exception faults the queue and is rethrown; later automatic
resume and cleanup are rejected because the external outcome may be uncertain.
This avoids silently repeating a partial effect.

Recursive update/cleanup and append during cleanup are explicitly unsupported.
Destruction of a facade, wrapper, list or entry does not call external services
or claim cleanup succeeded; the runtime must execute the original explicit
cleanup operation when required. Full player erase/deactivation
and the outer sequence callbacks remain separate integration requirements.

## Validation and limits

Public synthetic tests cover delay boundaries, uncapped and negative frame
deltas, nullable strings, completed failures, source/attachment payloads,
interruption, ordered service suspension, move ownership, callback append,
reentrancy rejection, uncertain exceptions and NI arithmetic boundaries.
The ownership refactor adds retained wrapper/list/entry identity checks through
work-reference replacement, aliased-facade update/cleanup pauses, selective
HITBOX clear, callback append, fault sharing and destruction. Surviving entries
retain their addresses and live handles, and distinct work generations retain
independent queues. Canonical-slot tests additionally cover replacement during
callbacks, fresh appends, old pass resumption, null live fields, explicit overlapping
cleanup rejection, independent fresh facades, retained faults and containing-work
lifetime. Focused standalone checks pass 131 subcommand assertions and
the existing 65 HITBOX assertions. The 1,005-case original lifecycle comparison
was rerun against the refactored implementation with no differences. A fresh
HITBOX comparison also matched all 2,106 cases and 1,690,336 original
instructions, including append, update, selective clear and cleanup. Its
factory and complete hitbox-check calls remain explicit service boundaries.

An ignored probe executes the actual original update, sound/effect start and
cleanup instructions with explicit messenger, prepared-ONE, reference-release
and generic list-shift hooks. **1,005 mixed lifecycle cases** matched the native
queue, covering 2,018,236 interpreted instructions. Manager hooks return valid
type-4 sounds and successful first-space effects; they do not play audio or
construct model actors.

A separate probe compares **3,008 scalar addition cases and 3,171 system-time
cases** against native results. It executes the original control flow and signed
conversion, with exact integer binary32 rounding for addition. All 6,179 cases
matched, including two expected finite-input overflow rejections. The system
division uses a separate host arithmetic oracle after the original conversion.
Both probes were rerun successfully against the final-core Release candidate
executable, `build/Release/dw4_model_subcommands_tests.exe`. These results do not
claim physical-console execution, full audio/effect manager validation, or
application/CI acceptance.

Original binaries, disassembly and probe output remain outside tracked source.
See [ACTION_EVENTS_RESEARCH.md](ACTION_EVENTS_RESEARCH.md) for the consumer
messages and [MOTION.md](../MOTION.md) for the separate named timer.
