# Native OS thread wake operation

`IgbOsThreadWakeInvocation` reconstructs USA `8026200C`. It is the reached lower
operation after the semaphore signal increments its inline count. It uses raw
OS thread and queue storage, the same explicit interrupt state as semaphore
operations, and shared scheduler globals. It does not create host threads or
substitute a host condition variable for original queue behavior.

## Ordered queue transitions

Wake disables interrupts and captures the prior state before reading its queue.
For each nonnull head it captures the next thread, clears the next thread's
previous link or the queue tail, then publishes the new head. It sets the
removed thread's state halfword to one. A positive signed suspend count skips
runqueue insertion and proceeds to the next queue head; the original leaves
other links untouched on that branch.

Otherwise, priority selects the runqueue at `80514DB8 + (priority << 3)` with
original 32-bit arithmetic. The queue pointer is stored in the thread and read
back before loading its tail. The caller appends the thread, stores its previous
and next links, then freshly reads the queue pointer again before publishing the
tail. It reads priority again for the runbits update and finally sets run-hint
to one. These reads and writes stay distinct for aliases and partial failures.
PPC `slw` behavior uses the original six-bit shift operand; shifts of at least
32 produce zero rather than undefined C++ behavior or a normalized priority.

When the input queue becomes empty, wake reads the full run-hint word. Zero
skips rescheduling; any nonzero value selects `8026140C(0)`. Missing hint or
scheduler state stays pending after the completed queue and interrupt effects.
The captured interrupt state is restored only after that selected call returns.
An empty queue alone therefore cannot qualify as a successful no-op.

## State and continuation ownership

`IgbOsSchedulerState` owns optional runbits, run-hint and scheduler-disable words
for `80565F10/14/18`. They start unknown. The wake operation uses only the words
actually reached; an empty queue with a known zero hint needs neither runbits
nor current-thread state. Raw thread/runqueue mappings reuse `IgbOsThreadState`
from current-thread adoption. They do not require reflected `igThread` owners.

The canonical scheduler words and current-thread word `800000E4` do not have
raw-byte aliases in this representation. A reached raw read or write overlapping
one of those cells fails explicitly rather than silently updating a second,
inconsistent copy. Unused allocation overlap is allowed, and priority is not
clamped. Supporting these malformed cross-representation aliases would require
shared byte-backed global ownership; such cases remain outside the native
comparison subset.

Selected physical owners, callback choices and prior interrupt state survive
pending resumes. Checked children borrow their parent's fault guard for a single
resume. Faults preserve completed stores and do not invent rollback or early
interrupt restoration. A cooperative work limit yields after 256 completed
dequeues at the original loop-head boundary. Resumption continues from that
boundary, retaining every effect; it does not report an aliased endless queue
as completed or repeat the initial disable operation.

## Startup scope and remaining scheduler work

The cold composition first reaches an unknown run-hint after its actual empty
queue read. An explicitly prepared zero hint then exercises native wake's
return, inner restoration of disabled state, outer signal restoration, success
publication and release of the startup caller's temporary thread reference.
The list retains its own reference. The original prerequisite runner then
reaches registration wrapper `800240CC` for the separate reference-counted
`igThreadList` class. This proves the composed branch with its declared
prerequisites, not full OS startup or executable gameplay.

The subsequent native class registration and retained cold continuation are
documented in [IGB_REF_COUNTED_LISTS.md](IGB_REF_COUNTED_LISTS.md).

Actual runtime zero initialization `8000542C` and later thread initialization
`80260FB4` produce scheduler globals. They still need native integration; a C++
owner allocation or ELF BSS declaration is not their execution. Nonzero-hint
rescheduling also requires authored continuation ownership for original context
save/restore and control transfer. `8026140C` remains an explicit lower operation,
and sleep `80261F20` remains separate. No PowerPC register interpreter is added.

Primary evidence is the supplied USA DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, inspected through IDA and bounded
original execution. Original binaries and comparison corpora remain local.
See [VALIDATION.md](VALIDATION.md),
[IGB_THREAD_MANAGER_INSERT.md](IGB_THREAD_MANAGER_INSERT.md) and
[IGB_DATA_LIST_GROWTH.md](IGB_DATA_LIST_GROWTH.md).
