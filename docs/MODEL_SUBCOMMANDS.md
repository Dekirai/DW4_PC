# Original model sound and effect subcommands

`model_subcommands` owns the ordinary MODELCTRL sound/effect queue, including
delayed execution and sequence interruption. It requires real sound and model
manager callbacks to perform external effects. Queue ownership and successful
event delivery do not themselves produce audible sound or create effect actors.
Player construction, the outer scheduler and bullet/collision subcommands remain
outside this module.

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

MODELCTRL work owns its subcommand owner at `+e4`; that object owns the list at
`+08`. Each append creates another occurrence, including repeated names. Sound
and effect entries share this ordered list. The native queue copies its payloads
and nullable strings; it does not borrow an IGB document's storage.

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
only after the complete cleanup pass. Sound and effect handles therefore have
one move-only native owner; replacing a nonempty queue requires cleanup first.

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
Destruction does not call unavailable services or claim external cleanup succeeded;
the runtime owner must finish explicit cleanup. Full player erase/deactivation
and the outer sequence callbacks remain separate integration requirements.

## Validation and limits

Public synthetic tests cover delay boundaries, uncapped and negative frame
deltas, nullable strings, completed failures, source/attachment payloads,
interruption, ordered service suspension, move ownership, callback append,
reentrancy rejection, uncertain exceptions and NI arithmetic boundaries.

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
messages and [MOTION.md](MOTION.md) for the separate named timer.
