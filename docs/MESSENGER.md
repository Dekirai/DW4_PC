# Original messenger

The portable `messenger` module implements original ordered registration,
cached recipient selection, synchronous ordinary delivery and retained deferred
messages. See [DEFERRED_MESSENGER.md](DEFERRED_MESSENGER.md) for queue timing,
packet ownership and explicit manager updates. It supplies the
dispatch boundary used by ACTION event consumers. A completed messenger send
means its required recipient callbacks completed; it does not imply audio
playback, effect creation or a complete original game frame.

The reference is USA revision-0 `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original bytes and generated
disassembly remain outside tracked source. See also
[ACTION_EVENTS_RESEARCH.md](ACTION_EVENTS_RESEARCH.md).

## Registration and selection

`803050A8` appends category, nullable name, signed index and recipient rows.
Registration order and duplicate occurrences are significant. Categories and
non-null names compare exactly, including case. Either a null requested name
or null registered name matches any name; an empty string is an exact key.
Request or registered index -1 is an ordinary index wildcard. There is no
same-file restriction: qualified native IDs identify ownership, not routing.
Adapters canonicalize serialized references that alias one RAM object before
assigning IDs; every original registration occurrence still remains a row.

`ControllerObjectId` preserves serialized source/reference fields and adds a
separate process-allocated native identity for generated owners such as GAMERAM.
Native identities never alias serialized addresses; copied IDs retain the same
owner. Mixed native/serialized fields reject before registration or append.
`ActionEventRuntime::register_native_controller` retains live activity and
manager providers independently of category rows. Attaching a missing provider
does not select, clear or replay an already prepared send.

`80305344` selects through 64 stable cache records in move-to-front order. Its
key is category plus nullable requested name, excluding index. Null and empty
are distinct cache keys. A hit changes the request index and moves the record
to the front. A miss reuses the last record and replaces its keys/recipient
rows. Selection leaves the record's previous command unchanged until send.

A nonempty cached row list is a captured snapshot. Later registration does
not invalidate it. Empty cached lists are recollected on selection. Eviction
and recollection can therefore reveal later rows while an existing nonempty
cache hit cannot. Captured indices remain independent of later RAM aliases.

## Delivery and shared values

Object-list entries explicitly distinguish null, a controller RAM, a retained
`GameDataList` and an immutable `CollisionBox`. GETPLAYER appends the existing
data row; resetting the player's current slot cannot retarget that old object.
HITBOX uses the retained box; a temporary
AABox never receives a fabricated RAM identity. The box survives paused sends
and remains valid until its native owners and shared buffer release it. Box
payloads require finite coordinates but preserve signed/inverted extents.

`80305A28` captures the current selected record and its initial row count,
then reads rows and request index during iteration. Enabled-only skips inactive
ordinary recipients only when its byte equals exactly 1. Request index -2
instead invokes the first inactive recipient without the ordinary index/gate
tests, then normally stops.

For each eligible recipient, the current parent manager is captured before
calling its structured hook at virtual +90. A zero low return byte invokes
that captured manager's command fallback at +8C. Higher return bits do not
select fallback, and fallback's return value is ignored. Ordinary delivery
continues through every matching occurrence. The `80305ED0` wrapper selects
and immediately sends; it does not enqueue a deferred event.

The single shared argument/result buffer has byte booleans, int32, float,
int64, float3, matrix4x4, object IDs and nullable strings. Arguments and results
use the same typed lists. Selection clears all lists and enabled-only; send
does not. The original integer-only reset (`clear_integers`, `80306AB8`)
preserves the other seven lists and enabled-only flag, while releasing its host
entry/storage accounting. Strings and values are copied into owned native storage. RAM owners
must outlive their registered/cached IDs and delivery.

Finite scalar/vector copies retain their words. Original matrix append/get
uses `80128CF4`, a GQR0 paired-single copy whose stores flush subnormal words
to signed zero. The native matrix append preserves this distinction. Nonfinite
floating payloads reject as an explicit host boundary; NaN payloads and floating
exception flags are not modeled by this transport.

Two messenger-level commands bypass controller hooks:

| Command | Effect on shared results |
| --- | --- |
| `SEARCH` | Count ordinary wildcard-index matches; append count!=0 and int32 count. Ignore activity/gate; -2 has no special inactive meaning. |
| `FIND_SPACE` | Append the first inactive captured object; append nothing on a miss. Ignore request index and enabled-only. |

## Nested sends and service continuation

Nested selection/send uses the same buffer and does not automatically restore
it. A nested same-key selection can change the outer record's index; nested
send changes its structured command. Outer fallback still gets its original
command argument. The sender reloads the record's index after callbacks, so a
nested change to -2 can stop the outer loop at that point.

Even a retained cache record can be reused by nested eviction. The sender
keeps its initial count but reads the replacement rows. The native runtime
supports valid replacement accesses and faults explicitly if a retained index
would exceed the replacement rows, rather than reading an invalid guest pointer.

The explicit `push_buffer()` / `pop_buffer()` path preserves whole buffer
objects. Original preparation `80303C0C` creates a current buffer at messenger
+10 and four separate spare buffers in the list at +20; depth is at +24.
`80306604` takes the pool's last object as current and inserts the old current
at the pool's front. It neither copies arguments nor clears the installed
object. `803067A0` first clears the current child through `80304618`, restores
the pool's first object and appends the cleared child to the pool's tail.
Both operations leave selected-message records, cache order and registrations
alone. In particular, pop does not restore a previous category, index or command.

The native implementation owns five stack slots for shared `MessengerBuffer`
objects and rotates their indices. Deferred scheduling/delivery can swap a
current slot's owner with a queue record's owner. A parked reference names that
same object; its lists, nullable values, enabled-only byte and retained box
owners survive nested selections. Pop releases the child's retained owners
and clears every represented list before restoring the parent. Push itself
does not promise cleared contents; callers select their nested message as
the original call sites do. `buffer_depth()` reports explicit push depth,
independently of nested delivery depth. Four nested pushes fit the original
pool. The native API rejects a fifth push, an unmatched pop and use before
selection preparation before changing any state; the original has no such
depth guards.

`begin_send` creates a move-only `MessengerDispatch`; `resume` executes it
synchronously. A service-required result retains completed recipients and the
current structured/fallback phase. Consumers that suspend after partial
internal or nested effects must own their own continuation. Dispatch ID and
row index distinguish those consumer occurrences, including duplicate rows.
The result's completed-recipient count is native bookkeeping, not an original
typed result.

Activity resolution returns nullopt when unavailable. An empty structured
callback returned by manager resolution also suspends before capturing that
manager. Once resolved, the hook/fallback pair is retained through suspension;
later readiness belongs in that stable callback's explicit service result.
Throwing faults the handle because callback effect completion is uncertain.
Unrelated top-level work must not overwrite a suspended shared buffer; native
runtime adapters must preserve synchronous ordering across that service pause.
An explicitly pushed child remains installed across both service suspension
and a faulted callback. Its caller retains the matching pop and any expected
depth check; no dispatch destructor or exception handler automatically restores
it. Push/pop are valid inside an ordinary recipient callback. Reentry during
buffer-object release rejects, including mutations through a retained buffer
reference, so an owner deleter cannot alter a partially cleared/rotated buffer.

## Cleanup, bounds and scope

Cleanup `80304C38 -> 803040C0` releases registrations, cached messages and the
shared buffer as well as original deferred-message/buffer-stack storage.
Native `reset()` releases buffer and deferred-record owners, drops represented state and invalidates outstanding
handles. It is not a cache-only invalidation API. Reset inside an executing
callback rejects. Construction describes a fresh native context with lazy
selection preparation, not complete original system/manager startup.

Timed-message updating (`80304C58`) requires an explicit deferred update; ordinary
send does not advance it. The original auxiliary buffer string
at +0C is outside this prepared ordinary-path representation.

Explicit native bounds are 65,536 registration rows, 8 MiB copied registration
text, 64 cache records, 256 live handles, 64 nested resume calls, 32,768 typed
values and 8 MiB storage per buffer. Strings allow 65,535 bytes and reject
embedded NULs. Those host bounds are not claimed original engine limits;
the five initial stack buffers and four safe nested levels follow original preparation.
The deferred pool initially has 32 records and may grow to a checked 65,536.

## Validation

Asset-free tests cover duplicate/null/index/case rules, cross-source identity,
live activity, byte gating, both queries, low-byte fallback, cache snapshots and
eviction, nested live mutation, captured fallback command/manager, resumable
prefixes, faulted exceptions, reset, typed copies and resource bounds. Explicit
stack tests also cover all typed parent values, stable object identity, nested
LIFO rotation, nullable/retained owners, mutation through parked references,
child release, live nested selection, suspended and faulted sends, move/reset,
overflow/underflow and release-time reentry. The final Release messenger test
executable passes 166 checks. The deferred kernel and runtime suites add 85
and 151 checks; see [DEFERRED_MESSENGER.md](DEFERRED_MESSENGER.md).

An independent local probe executes original registration, selection, sender
and buffer-reset instructions: 84 scenarios, 2,722 operations, 2,389,520 guest
instructions and 2,028 exact callback/result rows match the native runner
linked against the final Release core library without source overrides.
Allocation, list operations, string interning/category lookup and controller
callbacks are explicitly prepared services in that probe. Their original
helper bodies, complete constructor/cleanup execution and physical-console
execution are not independently validated by these comparisons.

A second independent probe executes original push, pop, clear, selection and
send instructions across 101 synthetic scenarios. Its 7,525 exact state rows
match the runner linked against that same final Release core library after
2,232,011 guest instructions. The comparison includes every represented typed
list, current and previously seen
buffer identities, selected message fields, pool size and object reference
counts. Five initial buffers, typed-list payloads, list/string operations and
object allocation are explicitly prepared services. This does not independently
validate their helper bodies, full startup/cleanup, invalid original stack
depths, guest destruction at reference count zero or physical-console execution.
