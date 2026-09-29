# Actual player movement collision

`ModelMovementCollisionInvocation` implements the represented outer
`80309C74` wall/floor consumer. `ModelFrameInvocation` calls it after the
original translated flag, using the frame's captured parent, RAM and work.
The helper does not replace these owners with a current RAM.parent or ONE.
It sends real retained native messages to the separately registered
[HITLANDDELIVER controller](MODEL_HIT_LAND_SCENE.md).

The first original Agumon frame now reaches this receiver and its required
HITLANDMODEL query. Completing the consumer's empty/disabled paths is not
evidence of full wall or ground response. The original query graph, triangle
traversal and result selection remain required; the walking preview's host
solver is not used to manufacture those responses.

## Ordered branches

Wall processing first reads the outer work's wall-check byte. Zero skips the
entire wall branch. Otherwise the live ride reference chooses `HITWALLFULL`
or `HITWALL`, then the captured parent's name and current RAM number supply
the qualified `HITLANDDELIVER` send. A null name becomes the original empty
literal; a null parent faults only when that name is reached.

After delivery, a null live wall-script field skips the response boolean
entirely. A nonnull field consumes physical boolean slot one; a nonzero byte
invokes the complete required `8030B6EC` wall script with event `WHit`.
The source name is read freshly before reloading the script prefix. Those
arguments and the script's pending operation survive a host-service pause.

The floor branch rereads the outer floor-check byte. When enabled, original
motion mode 3 returns early for an active vertical record with scalar mode
4. Otherwise it sends `HITFLOORCHECK` with a freshly read parent-name/RAM-number
pair. This query has its own captured packet and can observe changes made by
the preceding wall/script operation.

An empty boolean response returns before string or integer reads. A nonempty
response first checks the string list: a present null string faults, an empty
string disables the returned ride identity, and a nonempty name consumes
integer slot zero. It then independently captures physical boolean slots zero
and one. A positive boolean count does not invent an addressable second byte.

| Reached fall condition | Original continuation |
| --- | --- |
| Fall checking disabled | Continue to the optional floor script. |
| Vertical active with scalar mode 1, second captured byte zero | Continue to the optional floor script. |
| Vertical active with scalar mode 1, second captured byte nonzero | Send `HITFLOORADJUST`, perform motion mode 5, continue to floor script and ride handling. |
| Other vertical state, first captured byte nonzero | Send `HITFLOORADJUST`, perform motion mode 5, return immediately. |
| Other vertical state, first captured byte zero, current sequence `SQ_SLEP` | Return without a fall transition. |
| Other vertical state, first captured byte zero, another sequence | Execute the real ordinary `SQ_FALL` entry and player callback, then return. |

Each mode 3/4/5 call captures the current movement list separately, tests
count zero, then consumes only physical vertical entry one. The active gate
precedes its mode read. Mode 5 writes inactive and positive-zero speed in
that order, leaving mode, expiry and all other scalar fields unchanged.
It is not the existing `stop_motion` state transition. The current list
representation keeps count and physical storage together; malformed separate
counts or spare physical slots remain outside that profile.

The floor script uses the two previously captured reply bytes even after an
adjustment message changes the shared packet. Its live prefix is captured
before the new source-name read, then the required complete script/context
operation receives event `FHit`. It can mutate the outer work's subsequent
ride reference without changing the earlier reply captures.

## Required script and ride operations

`ModelMovementCollisionServices::hit_script` owns the complete actual
`8030B6EC` invocation, including its fresh ONE/context, Lua and merge effects.
The outer request preserves prefix, event and source name; all other original
arguments at these wall/floor call sites are literal zero. The prepared
target-Hit runtime is not silently substituted for this different call.

Only a landing through the active falling branch reaches ride handling.
No current ride and no returned target complete directly, matching the
original no-work branch. Otherwise the required `ride_update` service owns
the remaining `80309F58..8030A088` kind gate, release, identity comparison,
allocation and initialization. The actual ride type still has no completed
implementation. The consumer does not allocate a placeholder platform or
claim that storing a name implements attachment.

## Continuation and verification

Each invocation retains its captured work/parent, current send, reply bytes,
script arguments and nested fall entry. Resuming does not resend completed
messages, repeat landing writes or retarget an already selected query.
The same runtime and messenger are required. Recursive execution or an
exception preserves prior effects and permanently faults the invocation;
caught recursive calls cannot continue its later phases. Its runtime entry
also respects the owning frame's execution lease.

Synthetic tests cover disabled/unused owners, physical mode gates, replaced
parent/ONE/work, pending wall/script operations, malformed reached replies,
fresh landing records, preserved pre-adjust booleans, real SQ_FALL, sleep,
required ride effects, missing categories and recursive failure. Original
integration verifies the four physical HITLANDDELIVER rows share the exact
player nodes and retain their authored zero previous positions. It then
follows the first real frame to its actual query boundary. See
[VALIDATION.md](VALIDATION.md) for measured results and remaining prerequisites.

Evidence uses the supplied USA revision-0 DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, especially `80309C74`,
`8030A0A0` modes 3/4/5 and frame call site `8030D028`. Original data and
instruction probes remain outside the tracked repository.
