# Native serial manager scheduler

`SerialSchedulerInvocation` reconstructs USA `8028B7B0`, the concrete
`igSerialScheduler` manager update. It sizes and fills the scheduler cache,
invokes each reached manager's actual update binding, and clears the cache
selected at completion. A missing manager implementation remains pending.
`make_serial_scheduler_update` supplies the retained service used by
`ApplicationFrameState::scheduler`.

This implements the scheduler body, not the game-specific registry or missing
manager implementations. Registered order must come from the real startup and
registration paths. Prepared tests deliberately provide their own manager
operations and do not establish the game's complete startup order.

## Original owner graph

The supplied USA `main.dol` has SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. The abstract
`igInsightScheduler` factory redirects through `80285BB4/80284728` to
`igSerialScheduler`, registration `802846AC`, vtable `804CC018`. Its virtual
`+5C` is `8028B7B0`.

| Original owner/field | Native representation |
|---|---|
| Scheduler `+08`, `_managerList` | `SerialSchedulerState::registered` |
| Ordered wrapper `+08`, physical object list | `SerialSchedulerRegistry::entries` |
| Scheduler `+0C`, `_cachedManagerList` | `SerialSchedulerState::cache` |
| List `+08/+0C/+10` | `ModelSceneList::count/capacity/backing` |
| Reached manager virtual `+74` | `SerialSchedulerManager::update` |

Metadata `80284758` auto-creates both scheduler fields. Their concrete types
are `igInfoManagerOrderedList` (`80286C34`) and `igInfoManagerList`
(`80286E04`). The native constructor provides corresponding empty owners and
zero-count/capacity lists; it does not register any manager or pretend that an
empty prepared registry represents the game's loaded registry.

## Captures, reloads and callback order

At `8028B7C4..D8`, the body reads the registered wrapper, captures the current
cache, then captures the source list's count. The count is an unsigned loop
bound but is passed unchanged as a signed 32-bit argument to cache resize.
Negative count words must not become an empty successful batch.

The copy loop `8028B7E8..86C` reloads the registered wrapper and current cache
on every iteration. It reads the source wrapper's current list/backing, retains
that source entry, and releases the existing slot in the captured cache. Only
then does it reload this cache's count. A store occurs only for nonzero count
and a nonnegative signed index smaller than that count. A retained source
reference with no store is not silently released: the native state retains it
as an unpublished reference for the lifetime of this represented owner. Full
teardown of corrupted or externally rewritten reference graphs is outside the
profile.

All source copies precede the first manager update. Source registration changes
made by a manager therefore affect a later batch. This does not make the cache
an immutable vector snapshot: `8028B87C..94` reloads the scheduler's current
cache and backing for each call. These indexed reads ignore the cache's current
count and capacity and use the captured source count as their loop bound.
Replacing the cache during one update can change the next reached manager.
Duplicates remain duplicates; null entries fault at virtual dispatch.

A pending manager retains its reached owner and selected callback. Replacing
that physical cache slot or the manager's callback does not redirect an already
entered operation. On completion, the temporary call ownership is released and
the next indexed read again uses the live cache. After all captured-count calls,
`8028B8A8..B0` reloads the current cache and resizes that owner to zero. Its live
count determines the cleanup range, even when it differs from the original
captured count or the cache owner used during copying.

## Resize and physical reference profile

Resize follows `800691E8`. A shrinking/equal request captures the old count,
releases every reached reference in the removed range, performs a separate
zero pass over that range, then publishes the new count. A growing request
publishes count directly when capacity suffices, or uses `80041660/8004155C`
to replace the allocation. It then zeros the newly exposed pointer range.

Growth selects at least four slots, doubles below 1,024, and increases by
1,024 thereafter. Replacement backing is published before the old-capacity
copy; capacity is published after that copy, followed by count. Failed reached
physical accesses keep the earlier allocation/count/reference prefix. For
example, growing a negative old count to zero publishes zero count before
reaching the invalid negative memset address. Backings are bounded to 262,144
physical pointer slots in the native profile.

Unused allocation words are internal unknown-slot markers. A reached read of
an unknown word faults until an actual store initializes it. Raw zeroing over
an unreleased owned spare pointer is likewise a boundary; ordinary shared
pointer assignment must not invent a release at the original raw memset.

The release pass uses internal weak-reference markers to preserve the original
readable pointer identity until the later zero pass. This matters when a later
slot fails: the earlier reference has been dropped but its pointer word has
not yet been cleared. `serial_scheduler_physical_object` resolves that physical
identity; direct inspection of the backing can expose these internal markers.
A repeated release of an already released word, or a reached dangling word,
is an explicit unsupported boundary.

Routine cache clearing retains managers through their registered source owners
and requires no artificial external keepalive. If a reached release would
destroy the final manager owner, its original destructor effects are not yet
represented and the operation faults before that release. It does not invoke
a guessed empty destructor. General destructor reentry, packed original
reference-count overflow, allocator callbacks and original registry teardown
remain outside this bounded ownership profile. Native allocation aliases keep
their retained backing objects, consistently with the existing list model.

## Application binding and validation

The service returned by `make_serial_scheduler_update` owns one invocation
while it is pending. Once it completes, the next service call creates a new
batch. The application invocation retains its previously published clock and
dispatcher prefix across scheduler suspension; it does not sample again or
restart the dispatcher. A completed application retry never calls this service
again, as documented in [APPLICATION_FRAME.md](APPLICATION_FRAME.md).

Overlapping batches are rejected without invalidating the already pending
owner. Exceptions, recursive entry, or abandonment of a started incomplete
invocation fault the owning scheduler. Later attempts cannot replay completed
manager calls or skip unfinished cleanup. These are native continuation
contracts, not an original hardware exception model.

Permanent tests exercise empty batches, duplicate/order retention, cache reuse,
source replacement, live cache replacement, pending target capture, missing
callbacks, reached null managers, source/cache aliasing, partial copy/release,
growth and negative-count prefixes, final-destructor boundaries and actual
application-to-scheduler suspension. Complete `beSystem`, MODELCTRL and other
manager bodies remain independently required at their reached virtual calls.

A local comparison against the supplied DOL executes `8028B7B0` and
`800691E8` for 1,200 prepared cases: 426,861 original instructions across
140 instruction addresses, with zero mismatches against the final native
`dw4_core` build. Cases vary initial counts, duplicate managers, source-count
changes, live cache replacement, live cache-count changes and replacement of
the next manager. The comparison checks callback order, selected cache owner,
source/cache counts and all 16 physical slots of both relevant caches.
Manager callback bodies and `memset` are explicit prepared endpoints. These
fixtures retain external owners and use sufficient capacity, so the executed
comparison does not claim allocator/growth or final-destructor coverage;
growth and failure prefixes have separate permanent tests and instruction
review. Local evidence is in `work/native-serial-scheduler-result.json` and
`work/native-serial-scheduler-review.md`.

## Original player-manager connection

The implemented connection for MODELCTRL/player managers is virtual `+74` at
`802F28F8`, before their per-RAM virtual `+84` at
`8030CB2C`. It gates on the manager's system through `80316808`, the manager's
byte `+1C`, and the system's byte `+2E`. The system leaf returns true only when
byte `+2D` is zero and bit zero of byte `+2C` is set; it is not a generic
nonzero test for byte `+2C`. The remaining gates require manager `+1C` nonzero
and system `+2E` zero. It then captures the manager `+18`
info-list count. Each outer iteration reloads the manager's info list/backing,
captures that occurrence's `+18` RAM list and its count, and reloads the
captured RAM list's backing before each virtual `+84` call. Both loop counts
are signed. The manager virtual target is reloaded for each call.

`make_model_manager_update` supplies this exact common traversal, and
`ActionEventRuntime::bind_player_frame_update` supplies a fresh retained
actual player operation for each reached RAM. Original-data integration
executes seven prepared batches through this chain and all four authored
RAMs. A suspension inside the second RAM preserves the first RAM's completed
effects and the scheduler's pending cache. See
[MODEL_MANAGER_FRAME.md](MODEL_MANAGER_FRAME.md).
Startup registration/order and other manager implementations still need
their own recovered bindings before the scheduler can represent a complete
application update.
