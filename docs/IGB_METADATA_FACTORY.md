# Native metadata construction

`igb_metadata_factory` implements the original metadata factory `80029E64`,
direct constructor `800651B4`, and reset routine `80065218`. It returns a
canonical `IgbRuntimeMetaType` bound to explicit physical bootstrap storage.
It does not reuse the file-only reflected instance initializer or infer runtime
metadata from a serialized IGB file.

## Factory and construction order

The factory captures its input pool, then reads the current ArkCore enabled
byte. The enabled branch calls registration `8002A0A8`, reloads metadata global
`8056174C`, and reaches an explicit typed reflected-allocation operation at
`8006546C`. Those operations remain pending without their real implementations.

The disabled branch composes the native raw allocator with size `0x64` and
prefix zero. It constructs a nonnull allocation, then invokes the fresh physical
virtuals `+2C` and `+24`; the latter receives zero. The exact metadata vtable's
two callbacks are verified original no-ops. Unknown vtables require an explicit
binding. A null allocation still reaches the original null virtual-table read
and faults rather than reporting successful construction.

Direct construction composes the original base constructor, installs metadata
vtable `804761D8`, clears its name pointers, and runs the metadata reset. Reset
performs a second base reset, erasing an allocation tag installed by the first
base constructor. It publishes each field default in original order, including
major version `1`, minor `0`, unknown native size `0xFFFFFFFF`, alignment `4`,
and zero references to inherited/runtime state. Padding at `+1B`, `+5A`, and
`+5B` remains untouched.

After the reset, construction calls the actual field-registry factory with a
null input pool and publishes its physical pointer and canonical owner at
metadata `+28`. When `field_registries` is supplied, this composes
`IgbFieldRegistryFactoryInvocation`; its allocation and remaining reflected
operations retain their own explicit boundaries. An optional prepared field
factory is useful for isolated tests and does not establish list construction.

## String ownership and continuation

Lazy string-pool access uses the same
`IgbDependenciesServices::globals->string_pool` as the loader and stage callers.
Allocation `80054140(16)` and construction `80053F28` use the existing dependency
operations. Their absence pauses after the stores already reached. The
subsequent `80054094(pool, nullptr)` is the concrete native null-name leaf and
does not require a readable nonnull pool.

The reset then reads the live `+60` pointer, since a string allocation callback
may have replaced it. A represented old string loses one full 32-bit reference;
zero reaches the original container-release operation `8007381C`. Null
publication waits for that call to complete. `igb_metadata_set_pool_name`
publishes a physical pointer and its canonical header mapping for represented
callbacks. It performs no guest retain/release itself. An unmapped nonnull
pointer pauses at that read instead of discarding an unknown owner.

Raw publication retains displaced host owners, including a replacement installed
by the release callback and an old string whose decremented guest count remains
nonzero. This host retention does not reverse a guest release or imply that an
explicitly released container remains usable.

Other constructor stores are raw overwrites. Host retention preserves displaced
owners without inventing guest reference-count updates or final destructors.
Completed operation teardown occurs before subsequent caller stores and fresh
reads. A selected continuation is retained through a pause and is not restarted
when additional services become available.

Construction holds a lease on the canonical metadata owner across nested base,
string, and field-list work. Recursive, overlapping, faulted, or abandoned
construction cannot silently restart on that owner. This is an explicit native
failure boundary; arbitrary original heap corruption is not reproduced.
Factories capture their returned canonical identity before completion, so later
changes to a physical binding cannot change an already returned result.

## Validation and remaining scope

The permanent suite passes 62 checks covering direct defaults and dirty padding, both factory branches,
fresh metadata/vtable reads, null allocation, delayed string ownership,
full-word release, malformed physical storage, retained failure prefixes,
abandonment, stable returned identity, and composition with the actual native
field-list constructor.

An independent local comparison executes **1,326 fixtures**, including **671
failure prefixes**, through **103,331 original instructions** at **261 distinct
addresses**, with **zero differences** against the production core. It compares
all `0x64` metadata bytes, callback order and arguments, live global changes,
packed references, and old string reference counts. The original factory,
direct/reset/base helpers, raw allocator caller, base metadata getter, and
null-name string leaf execute as original instructions. Registration, reflected
allocation, physical allocation and pool services, string-pool allocation and
construction, field-list factory, and final string release are explicitly
prepared endpoints in this comparison. Separate list and pool proofs cover
their own implemented bodies; no complete bootstrap claim follows from this
caller proof.

The adapter `make_native_igb_metadata_factory` handles the registrar's
`create_metadata` request. Real type registration, field descriptors, inheritance,
registry publication, layout, and unrecovered allocator/arena internals remain
separate work. Constructing the metadata owner alone does not complete engine
startup or game loading.
