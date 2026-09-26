# Native data-list growth and backing copy

The native manager insertion now uses recovered `80041660 -> 8004155C`,
composing the existing `80062BB8` memory-field allocator. It operates on the
same physical thread-list object, canonical metadata and allocation mappings
used by startup and insertion. This is separate from metadata pointer lists,
which have a different layout.

## Capacity and publication order

Growth starts with the signed maximum of current capacity and four. It doubles
below 1,024, then adds 1,024 until the requested signed count is covered. All
arithmetic retains the original 32-bit wrapping. Repeating wrapped states remain
an explicit pending boundary; they do not produce an allocation or a fabricated
successful count. Repeated additions can be collapsed up to the first signed
wrap without changing the selected capacity or introducing observable calls.

For nonzero capacity, the capacity setter captures the metadata getter, old
backing and signed physical ordinal from the canonical `80561D48` descriptor.
It selects the corresponding field in the list's own metadata. The outer size
element is captured before pool lookup; the nested allocator independently
reloads its size, alignment and ancestry elements. The byte product wraps before
unsigned division by the outer element's low sixteen size bits. A zero divisor
is rejected without inventing a PowerPC division result.

The nested allocator's status is read in its original order and ignored by the
capacity setter. If an old backing exists, its copy size uses the fresh signed
minimum of old and requested capacity, and its destination comes from the
freshly published backing pointer. Old backing release selects a fresh pool tag.
Capacity is stored after copy and release; the outer growth caller stores the
requested count last. The zero-capacity branch releases first, then clears the
backing and capacity. It does not write count.

## Raw copy and shared ownership

`IgbAllocationCopyInvocation` reconstructs `800031E8`. It returns the destination,
including for zero-length copies. Direction follows the unsigned source versus
destination comparison. Each source byte is read before its corresponding
destination write, preserving overlap and partial failure effects. Physical
owners use the existing memory-allocation registry; unavailable mappings can
arrive later, while unknown bytes, conflicting mappings and invalid extents are
not treated as valid memory.

Allocator endpoints explicitly register returned physical owners. Neither the
growth wrapper nor the existing allocator silently creates another registry or
substitutes a host vector for original list storage. Checked continuations lend
the enclosing fault guard through growth, capacity, allocation, copy and release
without retaining it between resumes.

## Integration and prerequisites

Integration exposed an earlier layout omission: the computed field ordinal
existed only in the canonical C++ descriptor. Original `80065A58` also stores
the halfword at `+12`, then the declaring metadata address at `+18`. That producer
must publish both physical effects in order; the consumer must not replace a
physical read with the cached ordinal. Inherited fields skip these stores.

The cold composition retains its existing prepared raw OS context and allocator
endpoints. Actual growth requests sixteen aligned bytes for four pointer slots,
the existing allocator clears them, and manager insertion writes the first
thread address. Signal increments the semaphore before reaching the explicit
OS wake operation. Original OS initialization, tagged-pool producers and
scheduling remain required. These tests do not establish complete startup or
playable gameplay.

Primary evidence is the supplied USA DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original binaries, disassembly and
comparison corpora remain local. See [VALIDATION.md](VALIDATION.md) for measured
results and [IGB_THREAD_MANAGER_INSERT.md](IGB_THREAD_MANAGER_INSERT.md) for the
caller and semaphore order.
