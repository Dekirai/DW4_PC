# Native field copy constructor

`igb_field_copy` reconstructs the shared USA field copy constructor `800639E4`.
It composes object copy `80066690`, the existing header reset `8006665C`,
default allocation `800681C4`, and directional byte copy `800031E8`. The
memory-reference, signed-byte and Boolean clone constructors compose this body
through [IGB_FIELD_CLONE.md](IGB_FIELD_CLONE.md). Their distinct wrappers and
derived-table stores remain outside this shared base copy.

Evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original instructions and comparison
fixtures stay local; the repository contains authored C++ and synthetic tests.

## Header and pool identity

The constructor first publishes the base object table and executes the original
header reset. It then copies only source reference byte `+05` bit 7 into the
destination. The source packed reference count and pool byte are not copied.
When source and destination alias, the preceding reset affects this source read.

The live ArkCore `80562100` enable byte selects the remaining header work.
Disabled reflection skips allocation classification. Enabled reflection uses
fresh virtual `+58`, the returned metadata's live allocation prefix, and
`800590A0` to find the allocation pool. It then calls `8005920C` and stores the
low byte of its result at destination `+04`. This classification operation is
distinct from the pool virtual `+16C` used by ordinary construction. It is
reached even when the allocation-owner endpoint returns null; the endpoint's
own core gate and classification behavior remain explicit.

The known base table `804717F0` reads canonical metadata `805622A4`. Unknown
tables retain their selected metadata callback. Metadata lookup, allocation
ownership and classification do not fabricate initialized runtime globals.

## Member and name ordering

The base field table `80471914` and a null name pointer are published before
copying the scalar members. The implementation preserves the original order:
offset `+08`, byte `+25`, type ordinal `+10`, size `+14`, ordinal `+12`, and the
nonowning global pointer `+1C`. Signed halfword loads retain their 16-bit store
patterns. Unwritten padding stays untouched.

The source name header is retained with a full 32-bit reference increment.
The live destination name is then released, including the reached `8007381C`
callback when its full count becomes zero. The source name pointer is reloaded
after that release and raw-published without another guest retain. Numeric
name identity is explicit through the source address or canonical string
bindings. Host ownership keeps displaced headers alive without adding guest
references or inventing destructor calls.

The declaring-type pointer `+18` is copied without a guest retain, followed by
word `+30` and bytes `+24`, `+27`, `+28`, `+29`, `+2A`, `+2B`, `+2C`, `+26`.
Declaring-type addresses use explicit pointer-list bindings. Null, unknown and
expired nonnull weak identities remain distinct. Canonical source members are
authoritative after schema configuration; a stale constructor byte image is
not used to replace later original field stores.

## Separate default allocation and copy

`800681C4` captures the destination's unsigned 16-bit size, looks up the live
packed header's pool tag through `80068430/8005929C`, then invokes the selected
pool's `+D8` allocator. It does not substitute the aligned `+E0` allocation used
by `80068254`. Returned storage and its numeric address are published at `+20`.

The subsequent copy reloads destination `+20`, source `+20`, and destination
size in that order. The first pointer remains captured when the source pointer
binding arrives later. Allocation callbacks may change the live source default
buffer and destination size; those changes affect the copy without changing
the allocator's already captured byte count.

`800031E8` chooses direction by unsigned pointer comparison and reads and
writes one byte at a time. Overlap, self-aliasing, zero length and a fault after
partial publication retain that behavior. Unavailable buffers are rejected at
their reached access; incomplete pointer identities pause without selecting an
invented copy direction.

## Continuation and verification scope

The invocation composes inside the derived constructor's construction lease.
It retains selected operations, original source owners and displaced buffers
across suspension. Recursive calls and callback errors invalidate continuation.
The captured destination field and its physical-storage identity are checked
before further work so a changed host binding cannot split canonical and
physical publication.

Synthetic tests cover enabled classification, captured allocation arguments,
late pointer/size changes, physical padding, partial backward-copy faults,
changed identity and recursive callbacks. The independent original-instruction
corpus additionally covers the entire cold memory-reference constructor and
wrapper with their documented prepared pool endpoints. Results and the
separate composed startup boundary are recorded in [VALIDATION.md](VALIDATION.md).
Enabled generic cloning `800680B4`, complete allocators and full startup remain
outside this module's completed scope.
