# Native application info registration

`ContextInfoInvocation` reconstructs USA `igInsightCore` registration
`8028A7DC`. It passes one captured info owner through the application manager
registry and appends that same owner to the application's info list after the
manager search. This is a concrete registration caller, not the implementation
of every manager's initialization callback.

## Shared owners and construction

The application fields are `+08 _managerList`, `+0C _scheduler`, and
`+10 _infoList`. Metadata `80286368` marks all three for reflected creation.
The manager registry is an `igInfoManagerOrderedList`; its wrapper `+08`
points to the physical pointer list. The application registry is distinct from
the scheduler's active-manager registry. Their native representations share
the `SerialSchedulerRegistry` shape without implicitly sharing their owners.

`ModelSceneContext::managers` and `infos` represent these fields. Existing
time-only contexts leave their construction unrepresented/null. Constructing
the complete original context, inserting managers with the correct priorities,
and activating their scheduled updates remain separate operations.

One `SerialSchedulerManager` identity now carries both its scheduled `+74`
update and its `+6C` info-handler factory. It can occur in both actual lists.
The handler receives the captured info and returns one retained operation;
missing handlers pause rather than silently declining initialization.

## Original traversal and publication

The loop reloads the context's manager wrapper and list count at each condition.
The comparison is unsigned: a negative raw count is a large bound, not an
empty list. For a reached index it reads the physical backing and manager
without checking logical capacity. Null/unsupported managers fail at their
reached virtual-call boundary. A null incoming info is an ordinary argument
in this caller; the selected concrete manager decides whether it can consume it.

Only the low byte of the manager result stops the search. For example, `0x100`
continues while `0x101` stops. Declining callbacks can change the next live
registry, count, backing or manager. An entered callback retains its selected
manager and arguments across suspension; replacing the registry or factory
does not redirect that call.

After an accepted result or exhaustion, the caller reloads the application
info-list owner and appends the captured info. It does this even when no
manager accepted, and retains repeated/null occurrences. The original return
register is the append's incidental backing address, not an acceptance flag.
`appended_backing()` exposes the corresponding native owner; the structured
completion's integer value is zero and does not fabricate a guest address.

Distinct registrations may nest inside a manager callback. There is no original
context-wide admission lock. Resuming the same invocation recursively faults
that invocation; exceptions cannot restart its already performed prefix.

## Reached append behavior

The append `80069128` acquires a nonnull incoming reference before its first
destination-list dereference. Its signed `count < capacity` fast path publishes
wrapped `count + 1` before reading the backing and storing the pointer. A late
physical-slot failure therefore preserves that increment and acquired owner.

The growth path `80041970 -> 80041660 -> 8004155C` chooses at least four slots,
doubles below 1,024 and adds 1,024 thereafter. Within the native allocation
profile it publishes replacement backing, copies the old capacity, publishes
capacity and count, then stores at the captured old index. Newly allocated
unused pointer words are represented as unknown, not proven null. Existing
backing owners remain distinct from the replacement allocation.

Original pointer stores do not release an overwritten spare reference.
Context-owned internal reference storage preserves such owners, references
acquired before failure, and old allocations retained at a failed copy boundary.
Destroying a failed invocation does not undo those ownership prefixes.

Native storage is bounded to 262,144 slots. This reconstruction does not execute
console allocator/reflection callbacks, emulate packed guest reference-count
overflow, or prove freed guest-address reuse. Malformed/truncated old-capacity
copies stop at the allocation/copy boundary; the implementation does not claim
the original `memcpy` body's byte-by-byte partial-fault effects. Those limits
are separate from the validated ordinary native append behavior.

## Integration and evidence

`make_context_info_registration` returns one retained call suitable for a stage
operation. The production [native stage adapter](NATIVE_STAGE_SERVICES.md)
uses it for each exported occurrence, alongside the recovered directory request
and claim release. Per-manager callbacks remain explicit actual implementation
requirements; appending an info to the application alone never proves that its
controller is initialized.

Permanent tests cover low-byte results, unsigned/live bounds, replacement and
suspension, nested registration, duplicate/null exports, old backing aliases,
raw overwrite ownership and failure publication. A stage integration suspends
inside the second handler and verifies that the first exported occurrence is
not registered again. Build and independent original-instruction comparison
results are recorded in [VALIDATION.md](VALIDATION.md).

The final comparison contains 2,552 cases and 255,829 original instructions
across 187 addresses, with zero mismatches: 1,845 completed cases and 707
matching reached boundaries. It includes 127 nested registrations and compares
reference retention after invocation destruction. Manager bodies and the
reflected allocation/type/copy/free machinery remain prepared endpoints.
The production suite passes 77 checks; the composed build passes 133 suites.

Original evidence uses USA DOL SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.
