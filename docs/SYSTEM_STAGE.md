# System stage requests and directory completion

`SystemStageInvocation` implements the caller at USA `80316368..803167B4`,
between the system time publication and storage/error polling. It owns the
actual ordered request/completion state machine; unresolved loader, pool and
registration operations remain retained service boundaries. It does not mark
a requested file as loaded because a filename or asset decoder exists.

`SystemStageState` can be embedded in the same allocation as the system.
The invocation accepts shared aliases to that state and `ModelSceneSystem`,
so registration reads the same live `system.context` field as other modules.
Current and next stage names have distinct nullable immutable string owners;
an empty nonnull next-stage string is still a request. The boot directory
retains its filename, physical export list and loading byte independently.

## Original bindings and defaults

The supplied USA executable has SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. `beSystem` field tables at
`804CF2F8/804CF3C8` establish `_stageName(+38)`, `_nextStage(+3C)` and
`_bootDirectory(+44)`. Reflected construction initializes all three to null;
`802B78D8..E8` explicitly supplies a null string default for `_nextStage`.
Actual initialization `80315E60` subsequently requests `ndmw.igb`. The null
factory profile therefore does not establish complete game startup.

The loader singleton is `80562104`. The object-pool selection byte is
`80562298`, named pool handles are `80562258/8056225C`, and the string-pool
singleton is `80562140`. Native services represent these shared owners;
constructing a service container does not discover or initialize them.
Loader directory creation uses factory `8002EBEC`, whose metadata registration
`8002F1A0` names `igIGBFile`. The native directory represents only the fields
consumed by this caller, not that entire reflected class or its decoder.

## Request path

The caller first reads next-stage. If it is null and no boot directory exists,
this stage portion completes without needing services. With a request:

1. Compare its captured contents with the exact literal `ndmw.igb` at
   `804271B0`. This decision remains fixed for the request, even if a later
   service replaces the next-stage owner.
2. Call object-pool lookup `80068430(system)`, then read the live pool-mode
   byte. A nonzero byte selects that returned pool; zero resolves the selected
   live common/stage handle through `800607F4`. Retain the result before
   releasing and replacing the live loader's `+40` field.
3. Repeat the complete lookup/selection operation independently for loader
   `+3C`. Neither the mode byte, named handle, resolved pool nor loader owner
   is reused from the first assignment.
4. Capture the current loader for `8006F794` preparation. After it completes,
   reload next-stage and loader for directory request `8006FD34`.
5. Retain the returned directory, release the current boot-directory owner,
   and publish the new owner at `+44`, including a null result if the actual
   service returns null.
6. If the string-pool singleton is null, reach its allocation/initialization
   boundary before publishing the returned singleton. The known
   `80054094(pool,null)` leaf returns null without reading the pool object.
   Release the live next-stage string and publish null at `+3C`.

The update then advances to storage polling. It does not process the newly
requested directory's exports in this same invocation, even if its loading
byte already equals zero. It also does not replace `_stageName` on this path.

## Completion path

For an existing boot directory, native leaf `80070130` tests only its `+30`
byte: zero is ready, every other byte is still loading. Its loader argument
is unused. A still-loading directory completes this frame's stage portion
normally, retaining the directory for a future update.

When ready, capture the current directory's `+1C` info-list owner. Each
iteration reloads that captured list's signed count. A negative or zero count
visits no rows. For each reached row, capture its current backing, reload the
system context, read the physical slot, and call `8028A7DC(context,info)`.
Null and duplicate slots remain actual arguments. Replacing the directory's
list field does not redirect this already captured list, while replacing its
backing or count can change later rows. Registration can replace the context
seen by the next row.

After the loop, reload the current boot directory and its filename, and capture
the current loader for claim release `80070140`. After that call completes,
release and clear the boot directory that is then live at `+44`. Finally
reload the loader for cleanup `8006F8CC`. Earlier captures must not redirect
these later reads. A pending final cleanup observes an already cleared boot
directory and does not replay registration or claim release.

The original also has a defensive branch when a second boot-directory read
is null after the first nonnull test. It clears the current-stage string and
boot directory, with the same lazy string-pool preparation. No callback
separates those reads in normal single-threaded execution. The implementation
retains the branch without adding a suspension or invented callback between
them; ordinary tests do not claim to exercise a concurrent guest-memory race.

## What the service operations still require

The factory receives typed captured arguments and returns one retained
operation. A missing factory pauses at the reached operation. Its selected
callback survives suspension; replacing the factory or global owner does
not redirect an already entered operation. Subsequent original call sites
read the live service state again. Result owners are consumed only when the
operation reports completion.

| Service | Original behavior beyond the caller |
| --- | --- |
| Object pool | `80068430` reads the object's pool identifier and resolves it through `8005929C`. |
| Named pool | `800607F4` obtains its singleton service and calls virtual `+5C` with the selected handle. |
| Prepare loading | `8006F794` creates an `igQueue`, establishes its worker and asynchronous loader state. |
| Load directory | `8006FD34` searches existing filenames/claims or creates and schedules an `igIGBFile`. |
| Create string pool | `80054140(16)` and conditional `80053F28`, before singleton publication. |
| Register info | `8028A7DC` visits live managers' virtual `+6C` handlers, then appends the info to the context list. |
| Release claim | `80070140` finds the named directory and decrements its loading claim through `80043DE4`; this can remove it from the loader list. |
| Finish loading | `8006F8CC` clears asynchronous mode, stops/releases queue and worker owners. |

These services are not supplied with empty successful defaults. A host asset
read is insufficient to complete registration, queue or loader cleanup unless
it performs their required native effects. Prepared tests name their supplied
endpoints and do not qualify them as the game's actual startup operations.

## Ownership and continuation profile

Pool and directory replacement retains incoming owners before releasing the
old field. If a final object release would reach an unrepresented destructor,
the invocation faults before destroying it. Any already acquired incoming
reference is retained in the state even after the failed invocation is
destroyed. This matches the supported prefix; it does not emulate packed
guest reference counts or claim unknown destructor effects.

Immutable native strings have known host storage destruction. Clearing one
does not call a fabricated arbitrary-object destructor. Guest allocator,
intern-table and string-block free internals remain outside this storage
profile. A raw string-pool singleton overwrite retains the overwritten native
owner rather than inventing an original decrement.

Physical lists are bounded to 262,144 reachable slots and validate reached
backing accesses without replacing counts or filtering rows. Pending frames
retain selected arguments and earlier stores. Recursive calls, exceptions and
abandonment poison that state; overlap is rejected without invalidating its
already pending owner. Completed calls are idempotent. These are native
continuation contracts, not console hardware exception semantics.

## Validation

The permanent suite passes 376 checks covering all nonzero loading bytes,
empty versus null requests, exact filename selection, both pool modes,
independent/live lookups, pending operations, list/backing/context replacement,
null exports, cleanup ordering, retained-reference failure prefixes and
continuation ownership. Source ordering also received independent inspection
against the original caller.

An ignored local oracle executed 307,993 original instructions across 231
addresses for 4,096 prepared fixtures. A driver linked only against the final
production `dw4_core.lib` matched every compared service trace and published
owner/list/global state. The raw execution includes the stage caller,
readiness `80070130`, and null-string leaf `80054094`. String comparison,
pool lookup, allocation, loading, registration and cleanup services are
explicit prepared endpoints; their bodies are not part of that execution
claim. Synthetic reference counts prevent final destructors, and the
unreachable single-threaded defensive null-directory branch is not covered.
Permanent tests separately cover pending callbacks and native failure
contracts. The composed core build passed all 129 CTest tests.
