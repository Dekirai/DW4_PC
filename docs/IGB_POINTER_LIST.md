# Native typed pointer-list append and growth

`IgbPointerListInvocation` reconstructs original `80053BC0` append and
`8005383C` reserve for canonical metadata-child/type lists and field registries.
Evidence is the supplied USA DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. These are raw pointer-list operations:
they do not add or remove guest references. Parent inheritance's existing
`80053E6C` wrapper retains a nonnull field before entering this append.

Append captures count before asking the allocation pool for backing size.
The allocator's byte size, shifted right by two, determines capacity; neither
the host vector length nor the cached `capacity` view is a substitute. When
growth is needed, reserve performs its own fresh size query and, if needed,
another query to capture old capacity. Growth starts at four for zero capacity,
doubles below 512 and adds 512 thereafter until strictly greater than the
requested signed index. Original signed comparisons and wrapping pointer/count
arithmetic are preserved. Negative counts whose shifted byte index wraps to
zero can therefore store at physical slot zero. A growth sequence that wraps
into the original nonterminating cycle fails explicitly instead of hanging.

`8005641C` captures the backing pointer across allocation-owner lookup
`800590A0`, then calls that pool's size virtual `+150`. `800682A4` captures
the live backing and requested byte size before `80068430` reads the current
list reference word's high byte and selects its pool through `8005929C`.
The returned pool's reallocation virtual `+E8` receives the captured backing
and byte count. These four allocator endpoints remain explicit services.

Reserve publishes the returned backing before clearing its tail, including
a null result. Tail bounds use the captured old/new capacities. The clear
preserves original `80003130` byte/word store granularity and earlier stores
when later represented storage is missing. After reserve, append reloads the
live count and backing, writes the pointer, and only then increments the live
count. A failed store leaves that count unchanged. No reference increment is
rolled back by the raw list operation.

Native lists retain explicit backing bytes, suballocation offset and original
arithmetic address separately from their typed owner mapping. Values use
explicit address-to-owner bindings; host pointers are never cast to guest
addresses. A missing typed mapping pauses the store rather than inferring
initialized pointer entries from allocation. Raw overwritten owners remain
retained without modifying their guest reference words. Lists may have an
explicit weak binding to their physical object header. Direct and reflected
construction establish that binding when storage is known. Pointer-list writes
then keep header `+08` and `+0C` coherent with backing and count publication;
canonical-only owners remain supported without inventing physical storage.
The header store preserves original ordering: a newly allocated backing is
published before its tail is cleared, and an appended pointer is stored before
the count is incremented. An unavailable header does not erase earlier stores.
If a header store overlaps a represented pointer array, its old owner mapping
is retained and invalidated. This also applies to an incoming reallocation
mapping prepared before the publication store. A subsequent append waits for
an explicit replacement mapping instead of treating unknown addresses as null.

Adapters connect actual append to metadata parent inheritance and actual
capacity/reserve to the ArkCore bootstrap. A selected size call preserves its
captured backing even if the live list changes. Pending allocator calls retain
their selection and continuation; recursive reuse faults the invocation.

Permanent tests cover nonlinear growth, captured counts with live backing/size
changes, zero-length tails, wrapped negative indices, partial malformed clear
prefixes, null publication, retained callbacks, owner lifetimes and actual
parent field-retain plus raw append composition. Independent instruction
comparisons and their explicitly prepared allocator boundaries are recorded
in [VALIDATION.md](VALIDATION.md). These operations do not implement the pool
allocator, full class registration or a complete game startup.

The strengthened local physical-header comparison passed 1,679 unchanged
original cases, including 863 bounded
failure prefixes, with zero differences over 162,732 original instructions
at 189 addresses. Actual append, reserve, allocation-size/reallocation callers,
tag extraction and memset execute; only allocation-owner lookup, tagged-pool
lookup and pool size/reallocation virtuals are prepared. Its represented
physical regions exclude an unavailable wrapped address from null backing
with count `-1`. Callbacks keep physical and canonical headers coherent; native
header reads remain canonical with explicit alias synchronization. These
comparisons do not establish every original header-read fault or a general
MMU or arbitrary malformed-heap model.
