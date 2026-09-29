# Native ThreadManager activation

The authored C++ caller reconstructs USA `80023A30 -> 800755F8` and the two
object factories `8002380C` and `80023B90`. The concrete manager registration
reconstructs `8003AEF8`, full getter `8003AE48`, parent getter `8003AF68`,
layout probe `8003AE84`, defaults `8003AF70` and redirect `80023A10`.
Primary evidence is the supplied USA DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.

## Caller and ownership

Activation reads handle `805621F0`, calls the existing native `607F4` lookup,
and invokes the manager factory. It raw-publishes that returned pointer at
`805622F8` without retaining it or releasing a displaced manager. The host
retains displaced storage separately so a C++ lifetime change cannot invent
an original destructor call.

The caller then freshly reads the handle and performs a second pool lookup
for the thread factory. The proven static store `800947E8` supplies handle
four; the metadata pool uses a separate cell with handle five. Neither store
constructs a pool or makes the pool registry ready.

After the thread factory returns, activation writes byte one at thread
`+2C`, reads its current vtable and invokes virtual `+DC`. A null returned
thread faults at that byte write, after the manager has been published.
After the virtual returns, the caller reloads the manager singleton and its
vtable and invokes `+60(manager, thread)`. Finally it decrements the thread's
entire reference word, stores and reloads it, and invokes `80066E1C` only
when the low 23 bits are zero. A suspended destructor does not replay the
decrement. Unknown storage, represented null and unavailable operations
remain distinct.

The caller uses `IgbBootstrapPointer` because these operations access physical
bytes and arithmetic identities. This preserves the allocation and offset;
it does not assert an implemented manager, thread, semaphore or OS record.
The factory bridge returns storage only after the actual reflected initializer
has produced a typed instance. Unimplemented virtuals remain reached operations.

## Factories and concrete metadata

Both factories capture their incoming pool, always run their registration
wrapper through `80066188`, reload their canonical metadata and invoke the
existing native `8006546C` reflected factory. The manager's `+3C` redirect
returns concrete metadata via `8003AE48`. It is called before the abstract
class check and does not itself allocate the manager object.

`igGamecubeThreadManager` has size `0x10`, concrete flag zero, abstract
ThreadManager as parent, and vtable `804700A8`. Its registration body runs
`80021B94` before capturing registrar arguments. Its probe resets a 16-byte
local through `8006665C`, captures ArkCore, stores the base-manager and
concrete vtables, and reads the word selected by the captured core's `+394`.
Local `+08/+0C` bytes remain untouched. Defaults freshly read canonical
`80562070` and write `80023920` at metadata `+40`, including its represented
physical allocation. There are no new local field descriptors.

## Remaining runtime work

The manager allocator now uses a distinct native runtime-object binding.
Its inherited `_threadList` is recursively initialized and assigned through
the real field passes. `_sema` follows concrete metadata redirect
`80026160 -> 8003B238`; its metadata, struct dependency and runtime initializer
now complete. See
[IGB_PLATFORM_SEMAPHORE.md](IGB_PLATFORM_SEMAPHORE.md).
The recovered manager initializer's `+2C` caller now activates that semaphore
through the actual queue/count/status sequence with explicit initial exclusion
state. Original startup then publishes the completed manager. The runtime thread
list has count/capacity/backing at `+08/+0C/+10`, which differs from the
metadata-list layouts. See [IGB_RUNTIME_OBJECTS.md](IGB_RUNTIME_OBJECTS.md).

Concrete thread registration and its nine local defaults now finish; see
[IGB_GAMECUBE_THREAD.md](IGB_GAMECUBE_THREAD.md). Actual initializer `80077E94`
now completes with prepared stack-pool services and the recovered completion
semaphore setter; see [IGB_GAMECUBE_THREAD_INIT.md](IGB_GAMECUBE_THREAD_INIT.md).
The original `+DC` operation now copies the selected OS record and releases the
temporary stack in original access order; see
[IGB_GAMECUBE_THREAD_ADOPT.md](IGB_GAMECUBE_THREAD_ADOPT.md). The actual manager
insertion caller now retains the thread and enters the original wait/list/signal
sequence; see [IGB_THREAD_MANAGER_INSERT.md](IGB_THREAD_MANAGER_INSERT.md).
Original OS state, list growth, scheduling and teardown remain required. Replacing the OS record with an
arbitrary host thread identifier does not reproduce its observable fields.
Metadata registration and caller reconstruction alone do not satisfy
the playable native game gate.

Validation and explicitly prepared endpoint scope are recorded in
[VALIDATION.md](VALIDATION.md).
