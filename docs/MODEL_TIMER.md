# Actual named RAM timer access

`ModelRamTimerAccess` connects the recovered timer helpers to one actual
`ModelSceneRam` and its current named data list. The endpoint retains the RAM
and a required context-clock reader. It does not retain an initial timer,
copy a timer into an optional slot, or sample an OS clock. Each helper resolves
the RAM's live data-list reference and first matching name at its original site.

The clock reader receives the actual RAM so the connected caller can read its
current registered manager/context. A timestamp captured when constructing a
sequence or starting a frame is not the same service. Construction requires a
nonnull RAM and a nonempty reader, but performs no lookup, list allocation or
clock read itself. The one-argument constructor uses `model_world_time`, which
resolves the current typed RAM manager backlink and that manager's current
`ModelSceneContext::world_time`. Missing manager/context faults at the reached
clock read, preserving any earlier named-record allocation. Replacing the
context or manager affects the next helper call. The explicit-reader overload
remains available for controlled integration and mutation checks.

## Helper order

| API | Original entry | Captures and effects |
| --- | --- | --- |
| `exists(name)` | `8031E138` | Ensure data, perform untyped first-name lookup, return presence. No clock or timer payload access. |
| `start(name, end_time)` | `8031DE44` | Ensure data and find the first name; if missing, allocate/name/append a fresh timer. Then read context time and the captured timer's scale; publish bias, saved time, cached one and end time. Preserve scale and previous time. |
| `time(name)` | `8031E36C` | Ensure data and capture the first name; read context time, then captured timer scale and bias. Do not mutate timer fields. |
| `frames(name)` | `8031E4C4` | Capture named timer T and its saved time, read W1, and return T's cache if timestamps match. Otherwise perform a complete fresh named `time` lookup/read W2 and publish the resulting delta/cache into T, saving the earlier W1. |

Untyped existence is deliberate: a different represented object named
`KEIKATIMER` counts as present and shadows a later timer. An existence-only
caller must not create a replacement or inspect either record's timer payload.
Actual field consumers check the first captured owner. Start and accumulated
time reach the clock before their first timer field read; frame delta reads
the captured saved-time field before its first clock read. Missing or wrong
timer types therefore fail on different sides of that clock boundary.

Named lookup reuses [MODEL_SCENE.md](MODEL_SCENE.md), including signed counts,
capacity-independent lookup, first-name order and checked reached-null entries.
The endpoint does not bypass those semantics or search for a later typed match.

The frame helper's two clocks are independent. On a changed W1, its nested
accumulated-time helper re-reads the current RAM data and named owner before W2.
Only the outer capture T receives previous-time/cache/saved-time updates. The
endpoint never redirects those writes to a new lookup result. Replacing work
alone leaves RAM timers intact; replacing or reordering RAM data affects later
named helper lookups, including operations initiated by retained older work.

## Numerical fields and failures

The numerical stages reuse [MOTION.md](MOTION.md): low 32 bits of the coarse
world timestamp, original float scaling, signed saturation, wrapping bias and
elapsed arithmetic, double-to-float elapsed conversion, division by
`16666667.0f`, and an upper frame cap of three. Negative deltas are not clamped.
Zero elapsed publishes positive zero while preserving previous time.

`original_scaled_motion_clock` validates only the scale it actually consumes.
`publish_motion_timer_frames` accesses only the outer timer's previous-time,
cache and saved-time fields. Start overwrites an old cache without validating
it. Accumulated time does not inspect cache. A matching frame timestamp returns
the stored cache without consulting scale, including a stored nonfinite cache;
the downstream consumer applies its own relevant checks. Nonfinite scale is a
checked unsupported arithmetic boundary when that scale is actually read.
Existing prepared value helpers retain their established stricter validation.

Published prefixes remain visible after failure. A failed clock read following
fresh timer creation leaves the allocated named default record in the actual
data list. A failed W2 read leaves the outer frame timer unchanged because its
write stage has not yet been reached. There is no whole-helper rollback.

The endpoint becomes permanently faulted when a helper throws. Later calls
cannot silently repeat that partially published operation. Recursive entry is
also rejected and poisons the endpoint. If a clock provider catches that inner
exception, the outer operation still fails before further publication. A new
independent endpoint may be constructed only as an explicit caller decision;
the failed endpoint never resumes automatically.

## Verification and scope

Evidence is the owner's USA GameCube `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Lookup, allocation and timer field
ordering were inspected at the original entries above. No original bytes or
instruction listings are distributed with this module.

`model_timer_tests` covers real list/record ownership, lookup after data-slot
replacement, untyped wrong-type shadows, publication before clock failure,
typed-check/clock ordering, W1/W2 read counts, cached and zero/negative frame
paths, unrelated nonfinite fields, permanent fault state and caught reentry.
Separate motion/control/sequence integration tests exercise the recovered
per-command and per-axis call sites. The endpoint itself does not implement
the original frame scheduler, combat or game progression. Actual GENERATE now
uses this endpoint through [MODEL_SEQUENCE_ENTRY.md](MODEL_SEQUENCE_ENTRY.md).

The final v0.40 core also passed 12,576 cases against 1,313,041 original
instructions, with zero differences. This includes 13,422 named lookups,
8,505 clock reads and 4,389 checked boundaries. Mutation cases replace data
lists or names between helper reads and change saved/cache/previous/scale/bias
fields at their consumption sites. Explicit allocator/name/string and clock
services bound this comparison; it does not execute the complete original
manager or prove allocator ABI/capacity growth. See [VALIDATION.md](VALIDATION.md).

The constant-timestamp `start_model_timer` and `model_timer_frames` graph
helpers remain prepared compatibility conveniences. They do not replace this
live endpoint's clock and lookup scopes. The endpoint is retained outside
canonical `ModelPlayerWork`; adding a strong RAM-to-execution-bundle edge later
must avoid a RAM/bundle/timer-access ownership cycle.
