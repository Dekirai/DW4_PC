# Native memory-reference member copies

`IgbMemoryMemberCopyInvocation` reconstructs the original USA function
`80062824`. `IgbMemoryMemberReleaseInvocation` reconstructs its element-release
helper `800626DC`. The general reflected member-copy factory selects the
former for the memory-reference field virtual. Neither operation supplies a
finished descriptor or bypasses reflected object construction.

## Recovered caller order

The copy first executes the element-release helper. It then reads the live
descriptor flag at `+40`. When enabled, it reads the old destination pointer
using the current descriptor offset and frees a nonnull allocation through
`IgbBufferCopyInvocation`. The source offset is read again after that free.
A nonnull source is copied through the actual buffer-copy helper; a null
source produces a null pointer without allocator calls. The destination
offset is read once more at publication.

These offset reads are deliberately separate. Pending frees retain their old
pointer, pending copies retain their source pointer, and allocator callbacks
can change the descriptor before its next original read. The caller does not
clear the destination when freeing its old allocation, snapshot source bytes
before allocation, or special-case self-copy.

The buffer module owns `8005641C`, `8005645C`, `80056508`, and `8005647C`.
Consequently a copy performs all three original allocation-owner lookups and
both size queries; its allocation uses pool virtual `+D8`, and its old-buffer
free uses `+108`. The caller does not substitute a field-default allocator or
cache equal-looking queries. See [IGB_BUFFER_COPY.md](IGB_BUFFER_COPY.md) for
that module's separate behavior and evidence scope.

## Element ownership and deep-copy branches

The release helper reads ownership flag `+42` first. Zero returns without
reading the element descriptor or destination. A represented null element
also returns without destination access. A nonnull element executes the
original `80068128` ancestry test against the object-field metadata cell.
The metadata getter is a reached `+58` virtual; its returned parent chain is
walked natively with the original captured comparison target.

For an object-field element, release captures the destination backing and
one size result, then visits its current pointer slots. Each nonnull object
receives a full 32-bit reference decrement. Its low 23 bits are then tested;
zero selects destructor `80066E1C`. The count and backing remain captured
across pending destructors. This helper does not clear slots, free backing,
or change the descriptor's ownership policy.

After publishing the copied buffer, the main caller reloads the element
descriptor. Object elements are tested first; a failed test reloads the
element before testing memory-reference ancestry. These are independent
getter calls. The verified object and memory getters read their represented
canonical class cells through `make_native_igb_memory_member_virtual_factory`.
These getters are `80094090` for object-field table `804762FC` and
`80094068` for memory-field table `80476088`; their separate `+5C` prototype
getters are not used. Unknown metadata cells and parent bindings remain pending.

The object branch captures source and destination backings before checking
them for null. Each live nonnull source slot selects the object's current
physical vtable `+54` with argument one. The returned pointer replaces the
destination slot without an invented reference increment. The nested-memory
branch has no corresponding null-backing guards. Each nonnull source slot
executes the actual buffer-copy helper and publishes its independent result.
Null slots store zero in either branch.

## Native representation and service boundaries

Arithmetic addresses remain separate from native owners and host offsets.
Field object member `+20` uses its canonical buffer binding because existing
schema operations can make the canonical member newer than its constructor
image. Its reached four-byte physical range must nevertheless exist and must
not be released. Unknown canonical pointers suspend; represented null
pointers remain distinct from absent information. Other members use physical
storage, including physical initialization checks. Publication synchronizes
overlapping canonical base-field views with the existing byte-merge helper.
A view whose complete `+20` is replaced receives the exact returned buffer;
partial pointer changes retain their arithmetic value and leave an unmapped
owner unknown. Untouched canonical bytes are not replaced by stale physical
neighbors. Stores crossing the base-layout end also synchronize their touched
derived suffix from its authoritative physical image. The original word store
precedes this synchronization.

The element loops use physical backing bytes and retain their captured
allocation owners. Explicit allocation-address bindings permit later
callbacks to resolve copied buffers. Reached null addresses, expired object
identities, conflicting mappings, uninitialized reads, and unavailable
physical ranges fault without discarding the completed prefix. Positive
element accesses exclude a preceding allocation's one-past address when
resolving an adjacent allocation. Parent walks
and element visits retain the existing bounded native representation limit.

Pool leaves, unrecovered element getters, destructor `80066E1C`, and object
clone virtuals remain explicit retained services. Missing operations suspend
at the exact reached boundary. Synthetic test services for those leaves do
not constitute production implementations of an allocator, destructor, or
object clone. Abandoned or faulted invocations preserve captured owners in
the service lifetime storage.

## Validation scope

The asset-free suite covers the cold one-byte null-element copy, live offset
changes during free and allocation, retained pending callbacks, physical
member faults despite known canonical values, and free-before-read self-copy.
It also covers object-element reference arithmetic and destructor suspension,
object clone publication, nested independent buffer copies, null slots,
post-publication element reloads, aligned and unaligned canonical aliases,
base/derived boundary stores, adjacent allocation bindings and publication,
and the general member-copy adapter.
Pool and object virtual endpoints in these cases are explicit synthetic
services. Coordinated build results and independent original-executable
comparisons are recorded separately after execution.

Original provenance is the user-supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original instructions, generated
disassembly, and comparison payloads remain outside the tracked source.
