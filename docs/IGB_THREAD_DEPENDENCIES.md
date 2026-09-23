# Native ThreadManager dependency getters

`IgbThreadDependencyLookupInvocation` reconstructs the two allocation-only
getters used by ThreadManager defaults: `80023E40` for canonical metadata cell
`80561530` (`igNonRefCountedThreadList`) and `80026020` for `805615EC`
(`igSemaphore`). Evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, ranges `80023E40–80023E78`
and `80026020–80026058`.

Both routines check the canonical pointer for null. A nonnull pointer is
returned even when the class flags are zero or busy; class registration and
ready-bit checks are not part of these getters. On a null pointer, each reads
the live metadata pool handle `805621F4`, calls actual `800607F4`, enters
metadata factory `80029E64`, publishes its result and reloads the canonical
pointer. An unknown cell or handle suspends before the relevant read. A known
null factory result remains null.

The native implementation composes those existing pool and metadata factory
callers. Selected pool/factory continuations survive suspension, and raw
publication retains any overwritten host owner without inventing a guest
reference decrement. Completed physical allocations remain available for the
descriptor `+0x38` stores in [IGB_THREAD_DEFAULTS.md](IGB_THREAD_DEFAULTS.md).
Canonical preexisting metadata can have no represented physical allocation;
the getter does not manufacture an address. Conflicting physical identities
are rejected when an address is requested.

## Bootstrap callback append

`IgbBootstrapCallbackAppendInvocation` reconstructs `80065D94 -> 8003E4D8 ->
8003E8B8`. The outer caller captures ArkCore `80562100`, then its callback list
at `+0x28`. The existing native append handles capacity arithmetic, allocation,
count publication and the indexed function store. If allocation is reached,
that inner routine reads its own fresh ArkCore and pool `+0x34`; it must not
reuse the earlier outer core snapshot. This list differs from type callbacks
at `+0x2C` and the registrar's returned callback list.

Appending a function does not execute or validate it. Known null core/list
pointers fault at their original dereference; missing state suspends. The
ThreadManager defaults append `80023A50`, whose later execution remains a
separate startup/teardown requirement.

## Validation scope

Focused tests cover both getter profiles, null-only gating, unknown state,
pending allocation, captured canonical cells, raw overwritten ownership,
null results, physical identity conflicts and permanent recursive faults.
Bootstrap append tests replace the global core and list while suspended to
check captured receivers against fresh inner allocation state.

The getter fixture uses explicit enabled metadata registration/reflected
allocation endpoints to isolate this wrapper. The cold-bootstrap integration
separately runs real reflected metadata and internal-list initialization.
Neither fixture qualifies native thread execution, semaphore behavior, full
game startup or playable gameplay.
