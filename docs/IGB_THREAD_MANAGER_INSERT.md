# Native thread-manager insertion and semaphore access

This slice reconstructs manager virtual `+60`, original USA `80075460`, and
concrete semaphore wait/signal `8007761C/800774B0`. It follows current-thread
adoption in [IGB_GAMECUBE_THREAD_ADOPT.md](IGB_GAMECUBE_THREAD_ADOPT.md).

## Manager ownership and list publication

Insertion first increments the thread's full packed reference word with
32-bit wrapping. This precedes manager access and semaphore waiting, and is
not rolled back if a later operation fails. The manager's current semaphore
pointer and wait virtual `+8C` are captured together with a four-byte hidden
result. The caller ignores that status; it does not skip insertion on a
failure status.

After wait returns, the manager's current thread-list pointer is captured.
Signed count `+08` is compared with signed capacity `+0C`. Available capacity
allows the count increment immediately. Otherwise actual growth `80041660`
is selected with requested count `old_count+1` and element size four. Growth
uses [the native data-list adapter](IGB_DATA_LIST_GROWTH.md); metadata pointer-list
growth has a different layout and is not a substitute.

The backing at captured list `+10` is read fresh after count/growth work. The
captured thread address is written at the original wrapped four-byte element
offset. Count changes remain visible if the element write fails. The manager's
semaphore pointer and signal virtual `+7C` are then loaded fresh, with a distinct
hidden result. They may differ from the earlier wait selection. Only after
insertion returns does the original outer startup caller release its temporary
thread reference.

Physical element destinations use the existing `memory->allocations` mappings.
Their address/offset pair permits a negative original index to reach a represented
prefix before the logical backing pointer. Missing physical bytes suspend;
conflicting mappings, a released buffer or a reached incomplete word fail before
any byte of that word is written. No second backing registry or typed metadata
list is fabricated for the runtime thread list.

## Wait and signal

An inactive semaphore returns the fresh canonical failure status without
touching its inline count or interrupt state. Active wait executes original
`80260E00`: disable interrupts, test signed inline count `+18`, sleep through
`80261F20` while it is nonpositive, then reload the count, decrement, and restore
the captured exclusion bit. The separate test and decrement loads remain fresh.
Only afterward does the wrapper read the canonical success status and write
its result.

Active signal first reads inline count and then maximum `+10`, comparing them
unsigned. At or above the maximum it returns fresh failure. Otherwise original
`80260EC4` disables interrupts, freshly reads and increments the inline count,
calls `8026200C` to wake the queue at `+1C`, then restores the captured exclusion
bit. Success publication follows restoration. No cleanup path restores state
earlier than the original operation, and a late result fault preserves earlier
count and interrupt effects.

Both operations share canonical status cells with memory construction and the
same explicit exclusion state used by semaphore activation. Selected callbacks
and borrowed fault guards survive pending resumes without replaying completed
effects or retaining a caller's guard.

## Growth composition and scheduling dependencies

`80041660 -> 8004155C` requires the real data-list descriptor, element-size,
allocation and copy/free path. Its implementation remains separate from this
caller and composes the existing memory allocator. Sleep and wake remain
separate operations. [Native wake](IGB_OS_THREAD_WAKE.md) preserves queue changes
and explicit rescheduler selection; sleep remains a lower dependency. An empty wake queue alone
does not establish a no-op: the original still reads the scheduler hint and may
reschedule. Missing state must remain pending rather than becoming a fabricated
successful wait or wake.

The retained cold startup test preserves its earlier growth checkpoint, then
allocates four pointer slots through the real growth path and writes the first
thread address. Signal increments inline count from zero to one and reaches
OS wake before restoring exclusion. Thread reference count is two: the extra
retain already happened. The test then installs native wake, first requiring
its unknown canonical scheduler hint. A declared prepared zero hint completes
inner/outer restoration and signal success, followed by the outer temporary
release. The list retains reference one. Repeated resumes at the next registration
boundary do not replay this sequence.
There are 28 types, 101 descriptors, 113 default buffers, 12 prototypes,
110 enabled allocations and five runtime objects at this boundary.

Manager insertion, complete startup and playable gameplay are not established
until those reached operations and original OS/pool initialization are connected.
Primary evidence is the supplied USA DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original evidence remains local;
measured results and prepared comparison boundaries are in
[VALIDATION.md](VALIDATION.md).
