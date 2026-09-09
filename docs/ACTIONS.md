# Original ACTION2 execution

Version 0.20 adds `actions` to the portable core. It owns an explicitly selected
`beAction2Info` program and one prepared RAM's node clock, command cursor,
animation combiner, named timers and appearance overrides. Model sequence ACTION
requests can execute these effects through an exact source/info/RAM recipient.

Version 0.21 adds [owned actor activation and NDSP cleanup](ACTION_ACTORS.md)
through `decode_action_actor_runtime()`. The command-only factory remains available.
Version 0.22 adds [ordinary actor hierarchy traversal](ANIMATION_HIERARCHY.md)
and the read-only `bone_matrix()` query to the actor-aware runtime.

This is a prepared-runtime boundary. Complete manager registration, actor
traversal, scene attribute attachment, rendering, broadcast consumers and the
outer player/controller frame loop remain unfinished. The walking preview does
not yet use this interpreter. Four independently evaluated RAMs do not establish
connected local multiplayer or original gameplay parity.

## Original evidence

The reference is the supplied USA revision-0 `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Addresses identify research evidence;
the application never executes the DOL. Original bytes, disassembly and numeric
probes remain outside the published source.

| Original path | Address | Native effect |
| --- | --- | --- |
| Per-RAM ACTION2 initialization | `802EFDC8` | Prepared inactive state; explicit shared source offset initialization |
| ACTION dispatch | `802F1794`, ordinary path `802F1DE4` | Exact first matching name, first activation, same-data suppression, required broadcast |
| Ordered command loop | `802F1510` | Retained cursor, recognized command consumption before a yield |
| Per-RAM update | `802F16CC` | Active/freeze gate; motion, joint, fade, wait, flash order |
| MOTIONSET / motion update | `802F1160` / `802F0EB0` | Existing combiner/snapshot/source-mode effects |
| JOINTSET / joint update | `802F0D7C` / `802F0C30` | Node-time section, wrap or clamp, completion flag |
| FADESET / fade update | `802F0800` / `802F057C` | Visibility, raw and packed color, alpha timer and shared attribute lifetime |
| WAITCHECK / wait update | `802F0400` / `802F0264` | Frame, motion, joint and externally supplied random waits |
| FLASHSET / flash update | `802EFF7C` / `802EFE1C` | Damage color, texture/lighting overrides and expiry |
| Node clock | `8030FA2C` | Uncached coarse clock and signed bias arithmetic |
| Named timer speed / all timer speeds | `8031E654` / `8031E808` | Phase-preserving scale change; reset cached elapsed frames |
| Color setter and conversion | `800C6A70`, `800BDAB8`, `8012E1D8`, `8012E4F8` | Raw float channels and separately rounded ABGR8 |

The color conversion bounds are initialized at `8012F804..8012F820` in
`8012F57C`; reading the uninitialized BSS gives incorrect black results. The
independent local probe executes the original numeric initialization prefix.

## Program and ownership

`decode_action_program()` requires an explicit source string and little-endian
IGB graph. It reads info name slot 2, RAM list slot 6, database slot 8 and action
list slot 9. The database's animation list is slot 6 and combiner list is slot 9.
RAM slot 11 selects its node; node slots 13/14 supply speed/bias. Serialized live
timers/work in RAM slots 10/14 require a separate restoration path and reject.

Action data stores its name in slot 2 and an `igNamedObjectList` in slot 3.
Occurrences retain their order and object identity. Null lists are retained as
native-command placeholders, not treated as empty executable actions. Known
commands require exact types and typed, bounded fields. Unknown types remain
visible and stop at the unconsumed cursor.

Limits are 4,096 actions/animations, 65,536 total command occurrences, 1,024
prepared RAMs, 4,096 bytes per name and 8 MiB aggregate decoded text. Lists check
their type, count, capacity and memory range. Aliased RAMs, nodes or combiners
require shared actor restoration and reject this independent ownership profile.

The clip library must retain database order, clip references and names and share
the selected combiner's actual immutable library identity. Call
`sources.initialize_action_offset()` before constructing an `ActionRuntime`.
This corresponds to the original initialization store of `-1/30f`. RAM numbers
are caller-selected ordinals: all four Agumon RAM records serialize number zero
before registration assigns their runtime numbers.

Runtime copies share the immutable program/library and copy mutable action,
combiner and timer state. Several RAMs can use one `AnimationSourceRuntime`,
preserving original shared source clocks and play modes.

## Execution and timing

First activation adds the database's first animation and sets the combiner's
request-time override to `100000000`, matching `802F1F44..802F1F50`. It sets the
first-motion no-interpolation flag. Later dispatches clear that flag and restore
visibility even when the selected action is the same object and does not restart.
Command completion checks only the cursor and wait state; fade/flash/animation
effects can continue after the command list ends.

MOTIONSET resolves its authored animation name in database order and calls the
recovered [combiner path](ANIMATION_COMBINER.md). JOINTSET writes frame-derived
start/end ticks and moves the node origin to the start. Loop mode begins with
`L`; other modes clamp. Joint completion uses signed `local > end`, retains its
state and adjusts the origin after wrapping. A non-progressing original wrap
loop stops with a bounded native error instead of hanging the host.

WAITCHECK dispatches by its name's first character. `F` resets the named WAIT
timer; `M` waits until exactly one combiner state has unsigned local time at least
its clip duration; `J` waits for the joint completion flag. An unknown character
yields without inventing a new wait state or automatic continuation. The WAIT
timer survives completion and is reused. Frame expiry is strictly `end < time`.

A name beginning with `R` whose fourth character is `S` sets a shared random
lower bound in `ActionServices`. Other `R` names require `random_word`. Width
uses the original separate multiplication and fused subtraction, signed integer
truncation and unsigned remainder. Negative signed divisors retain their unsigned
interpretation. A zero divisor or a width requiring overflowing integer conversion
is an explicit unsupported numeric boundary. No replacement RNG is invented.

Node time uses the low 32 bits of `system_time >> 18`, float scale, signed
64-bit conversion, a shift back by 18 and modulo-64-bit bias addition. It has no
animation-state cache sentinel. Freeze sets node and existing timer speeds to
zero while retaining phase. Unfreeze sets them to one, not their previous speed.
Timer speed changes zero cached elapsed frames while preserving saved timestamps.

## Appearance effects

Appearance overrides record attribute presence separately from each boolean's
value. Their attachment to scene nodes and use by the renderer remain separate.
FADESET beginning with `H` hides the RAM's switch selection without deactivating
its action, clearing a timer or removing an existing effect.

Fade-in starts at alpha zero and fade-out at the authored amount. The increment
is amount divided by frames; updates use the original fused arithmetic and
elapsed-frame cache. A color write retains raw float channels, clamps separately
to `[0,1]`, rounds with `255*x + 0.5`, and stores ABGR8. Subsequent commands read
the quantized packed color; the fade accumulator retains its raw alpha.

Completed fade-in removes blend and, when texture state is absent, color and
lighting. Completed fade-out retains those attributes. FLASHSET writes red and
green to one, preserves blue/alpha, disables texture/lighting, and starts a
`66666668`-tick timer. On expiry it removes texture and removes color/lighting
only when blend is absent. A present false-valued attribute still participates
in this ownership rule. Zero-frame IEEE results are preserved where usable;
NaN at the native color boundary rejects without publishing a partial update.

## External boundaries and failure behavior

Successful commands remain committed as an ordered prefix. A missing random
service, unsupported command or thrown command error retains its unconsumed
cursor. `update()` reports the suspension; it does not silently resume or skip
the original stalled path. `resume()` explicitly retries that native boundary.
Dispatching the same action still preserves original restart suppression.

An initial dispatch broadcasts once after reaching its end or first wait.
`ActionServices::broadcast` must supply the owning runtime's event service. With
no service, execution returns `broadcast_required` with the event pending; a
throwing service also leaves it pending. Wait continuation does not broadcast
again. Effects already committed before an external call are retained. External
RNG/event side effects cannot be rolled back; callbacks must throw before
committing effects or make retries idempotent. Reentrant runtime mutation rejects.

The `ModelActionRequest` overload verifies source, info name and RAM before
dispatching. Callers must inspect the result and propagate a required service or
error before consuming an outer sequence command. A recording callback is not
an implementation of the original event consumers.

`ISCOMEND` and `ISRUN` have typed inspection equivalents (`command_end()` and
`active()`). The original command interface for these queries, appearance/bone
queries still requires outer messenger services and rejects here. The typed
`bone_matrix()` API now implements the active-gated cached bone read, while
`traverse_actor()` explicitly supplies its original hierarchy calculation.
NDSP executes only when an owned actor group was supplied. It removes actor
caches, attributes, timers, command work and combiner state while retaining the
node clock. The NDSP placeholder is never treated as an empty action list.

## Validation

The asset-free ACTION suite covers typed/bounded decoding, first/same-action
dispatch, retained cursors, frame/motion/joint waits, freeze and timer reuse,
appearance quantization and shared cleanup, missing/failed/reentrant services,
source/recipient ownership and four independent RAMs. Version 0.21 extends it
with activation/NDSP/reactivation, independent actor buffers, frozen node-clock
retention, suspended cleanup and pending-event ordering.

An ignored independent instruction probe compares 20,000 operations: 5,000
node clocks, 5,000 timer speed changes, 2,000 joint set/update pairs, 2,000 random
waits, 2,000 fade commands, 2,000 fade updates, 1,000 flashes and 1,000 flash
updates. All compared integer/float fields match. The probe supplies prepared
objects, list removal, timer start/elapsed/expiry/removal and RNG service results;
it does not independently validate those external bodies, the full command
dispatcher, actor allocation or original rendered frames.

Local Agumon integration decodes 183 action definitions, 82 clips and four
prepared RAMs. All 182 ordinary action lists complete their commands and active
motion/fade/flash phases: 369 executed command occurrences, 4,157 poses and
5,699,247 finite vertex positions. Version 0.21 additionally deactivates and
reactivates every ordinary action, with all actor buffers owned. NDSP's null list
is retained and bypassed by its native lifecycle path. Four actual weapon WAIT/WALK sequences additionally
dispatch their actions into 240 complete poses / 329,040 finite vertices with
interrupted transitions. These checks use the production codec/pose path and a
recording broadcast sink; they do not prove connected multiplayer, combat
semantics or visual effect rendering.

The original synchronous recipients, nullable matching and sound/effect
subcommand boundary are recorded in [ACTION_EVENTS_RESEARCH.md](ACTION_EVENTS_RESEARCH.md).
Those consumers remain pending implementation.

```powershell
.\build\Release\dw4_actions_tests.exe 'PATH\Extracted'
```

Next recover complete appearance/context traversal and event routing, attach owned
node/appearance state to rendering, then connect the original outer player
callbacks, input, collision and camera. The full original game remains the
acceptance target described in [ORIGINAL_BASELINE.md](ORIGINAL_BASELINE.md).
