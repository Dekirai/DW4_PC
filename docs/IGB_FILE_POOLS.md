# IGB file post-default pool ownership

`IgbFilePoolsInvocation` implements original `80051698`, the `+34` callback of
`igIGBFile` vtable `804724C4`. It operates on the canonical `LoaderRequestFile`
pool fields. It does not replace the preceding metadata allocation or reflected
field initialization with a host default constructor.

## Current-pool lookup

Each of the three assignments calls `80058C80` independently. That wrapper
passes handle `2` to `800607F4`, obtains the selected memory manager through
`800607D4 -> 800605E8`, then calls its `+5C` virtual operation. The represented
globals are shared with the existing `IgbMemoryPoolsServices` name lookup:
selector `805621DC`, fallback `805621C8`, and three active entries at `804F2294`.
The initial selector is captured before reading the chosen owner. Invalid
selectors produce the original null manager and fault at its reached virtual
dereference. Missing native storage suspends instead of becoming null.

The concrete manager vtable `80471BDC` binds `+5C` to `8005B060`. For handle two,
the function reads the pool pointer at `+134`. A nonnull pool returns immediately
without reading flags, names, a success word or another global. Typed pointer
identities live in the registry's independent optional `pools` slots. The
name-only registry factory does not supply these identities or activate pools.

For a null slot, the getter reads flag word `+1C0`. Bit `0x04000000` selects
manager `+D0`, concretely `8005E7AC`, with the same handle. This operation includes
pool creation through `8005E088` and remains an explicit `prepare_pool` boundary.
After it returns, the getter ignores its status and reloads the captured
manager's pool slot. It does not fall through to another manager when that
reloaded pointer remains null. In particular, `8005E07C` is the different
`+C8` status-only virtual; it is not a valid replacement for `+D0`.

Without the lazy flag, the getter reads the selector again. For values above
one it searches backward through the active entries for the captured manager,
then calls the preceding distinct manager's `+5C`. A preceding identical owner
ends the search with null; a preceding null faults at virtual dispatch. Each
physical entry is read where the original reads it. The search is not a host
map lookup, and previous-manager calls may select an explicitly bound unknown
virtual operation. Missing active entries remain pending. Reads outside the
three represented physical entries remain an explicit representation boundary.

Malformed repeated manager aliases can form an original recursive cycle.
Native execution bounds that chain to 64 retained caller owners and faults;
this guard is not an original successful return or equivalent termination.

## Three ordered assignments

For `file+118`, then `+11C`, then `+120`, the callback performs:

1. A fresh current-pool lookup and full 32-bit wrapping increment of its
   nonnull result's packed reference word.
2. A fresh read of the old field, followed by its full-word decrement.
3. `80066E1C(old)` when the decremented word's low 23 bits are zero. The selected
   final destructor remains pending until its actual operation is bound.
4. A raw store of the captured incoming pool into the live field.

The acquire happens before the old-field read and release, including same-owner
assignment. Destructor callbacks can mutate the field or globals. The store
still publishes the captured incoming pool and does not decrement a replacement
written by that callback. The next assignment performs its own fresh lookup.
Unknown manager virtuals and destructors retain selected callbacks across native
suspension. Return status words do not substitute for pool identities.
Completed operation captures are released before the caller's fresh pool read
or raw publication; their modeled teardown effects belong to that completed
call, rather than a later lookup or invocation destruction.

Abandoning an unfinished callback preserves acquired incoming ownership and
pending destructor owners on the file, and marks the prefix invalid. Exceptions
and recursive entry also invalidate it. Host `shared_ptr` lifetime is separate
from the original packed reference arithmetic. No rollback, synthetic final
destructor or implicit pool allocation is performed.

## Callback binding and validation scope

The same `igIGBFile` vtable maps `+0C`, `+24` and `+2C` to `800667A4`, `800667B0`
and `800667D0`, respectively. Each original target contains only a return
instruction. These no-op bindings are distinct from the nontrivial `+34` pool
callback. They do not prove the entire generic object initializer.

Permanent tests cover selector capture and fresh reads, pool-slot availability,
prior-manager aliases and nulls, explicit preparation, callback capture, full
reference wrapping, low-23-bit destruction, delayed and abandoned publication,
same-owner assignment and recursive invalidation. The independent ignored
`work/native-igb-file-pools-*` proof executes the supplied USA DOL's actual
post-default callback, current-pool wrappers, selected getter and fallback
branches. Unknown manager virtuals, actual pool preparation and final
destructors are explicitly prepared endpoints. Nonterminating original alias
cycles are reported separately from the finite comparison corpus.

The final production library matches all 2,092 finite original-instruction
cases with zero differences, including 725 reached failure prefixes. This
corpus executes 294,427 instructions across 177 addresses. Four original alias
cycles were excluded as nonterminating; the native depth guard is tested
separately. Each of the three no-op virtual bindings is also checked directly
against its original table entry and executed return instruction.

The source executable has SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Public source and tests contain authored
logic and synthetic fixtures, not original instruction words or game assets.
