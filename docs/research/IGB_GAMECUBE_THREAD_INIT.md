# Concrete thread initialization

`IgbGamecubeThreadInitInvocation` reconstructs object initializer `80077E94`
for the concrete thread registered in [IGB_GAMECUBE_THREAD.md](IGB_GAMECUBE_THREAD.md).
Its runtime owner uses `IgbRuntimeObjectKind::thread`; physical storage and guest
addresses remain independent of C++ pointers.

## Stack and completion semaphore

The initializer preserves the original sequence:

1. Read the thread vtable and invoke captured virtual `+A8`. The concrete
   `80093400` leaf returns 32 without reading its receiver.
2. Freshly read stack size `+24`; truncate the returned alignment to 16 bits.
3. Read the single reference-tag byte at `+04` through original `80068430`.
4. Select the corresponding tagged pool and invoke its aligned allocation
   virtual `+DC` with captured size and alignment.
5. Write the returned arithmetic stack address to thread `+20`, including a
   null result. Stack contents are not read or initialized by this operation.
6. Freshly read completion semaphore `+380`, resolve that captured owner and
   invoke its captured virtual `+68` with argument zero and a four-byte result.

Both the ordinary and hidden semaphore results are ignored by the caller.
The hidden result must still be written by the selected semaphore operation.
The separate `+58` metadata getter `800933F8` and three verified object BLRs
are also admitted. None reads an otherwise unused receiver.

Tagged pool lookup `8005929C` and actual pool allocation remain explicit lower
services. They reuse the existing memory-construction request shapes and
canonical status cells. Tagged lookup is distinct from the startup's pool-
handle lookup `800607F4`, and aligned `+DC` allocation is distinct from `+D8`.
These service bindings do not establish a complete native allocator.

## Initial-count setter

Semaphore virtual `+68` is actual `800773AC`. It reads active byte `+14`. When
inactive, it writes the full incoming count at `+0C`, freshly reads canonical
success `805622D0` and writes the hidden result. When active, it leaves the
count unchanged and returns fresh failure `805622D4` through the same result.

This setter does not activate the semaphore, initialize its inline queue/count
or acquire an exclusion bit. A newly constructed completion semaphore can
therefore receive initial count zero while remaining inactive. Missing status
or a late invalid result retains the completed branch effects.

Selected callbacks and physical identities survive suspension. The object
initializer now supports an optional checked continuation and lends its fault
guard through the thread initializer to the semaphore operation. Legacy
zero-argument callbacks retain their prior behavior and selection. A caught
recursive failure stops subsequent native stores; children do not retain
their caller's guard.

## Remaining startup work

The retained cold test constructs the thread through the actual reflected
factory and field passes. It owns a completion semaphore distinct from the
manager semaphore. Tagged-pool lookup and aligned allocation suspend separately;
repeated resumes do not reselect completed operations or allocate twice. After
the prepared allocation returns, the stack pointer is published and completion
count becomes zero while that semaphore remains inactive. Stack payload bytes
remain unknown. Construction releases its lease and publishes one thread
instance before the original caller sets byte `+2C` to one and reaches `+DC`.
The composition has 28 types, 101 descriptors, 113 default buffers, 12 prototypes,
110 enabled allocations and five runtime objects.

Original current-thread adoption `80077EFC` is implemented separately in
[IGB_GAMECUBE_THREAD_ADOPT.md](IGB_GAMECUBE_THREAD_ADOPT.md). It reads the original
current-thread pointer twice, copies the actual context with its ordered
word/halfword accesses, then frees the fresh stack and clears `+20`. A blank
context or a host thread identifier does not implement that behavior.
The manager insertion caller and semaphore count transitions are implemented in
[IGB_THREAD_MANAGER_INSERT.md](IGB_THREAD_MANAGER_INSERT.md). Original OS
initialization, list growth and scheduling remain required.
These initializers alone do not establish complete startup or playable gameplay.

See [VALIDATION.md](VALIDATION.md) for measured scope. Primary evidence is the
supplied USA DOL, SHA1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.
Original artifacts remain local.
