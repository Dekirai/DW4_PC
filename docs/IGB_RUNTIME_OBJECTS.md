# Runtime objects in reflected construction

The native core represents runtime managers, thread lists, threads and
semaphores with a distinct `IgbRuntimeObject` identity. This extends the
existing allocation binding instead of treating runtime objects as files,
metadata or field descriptors. The immutable layout kind identifies which
checked native view may access the object. It does not initialize members.

Each owner retains a weak physical storage/offset binding, header views and
the existing construction lease/fault state. Original member bytes remain
authoritative. Conflicting, expired or removed bindings and faulted storage
are rejected. Header writes through the bootstrap, reflected initializer and
field-copy paths synchronize the same owner. Strict file-result adapters
reject runtime objects. Unknown allocator bytes are not zero defaults.

## Manager and nested list

The original `800667F4` reflected initializer now accepts those identities.
The thread runtime adapter binds only the verified hooks from manager vtable
`804700A8` and thread-list vtable `80476CE0`. Plain getters `800933F0` and
`80094128` read the live canonical metadata cells without registration.
Verified return-only hooks are separate from actual manager hook `800755C0`.

That manager hook captures manager `+0C`, resolves its semaphore identity,
reads the physical vtable and invokes `+5C`. The original ABI passes a hidden
four-byte result destination before the receiver. Its native scratch buffer
starts with unknown bytes and remains the same across suspension. The caller
does not inspect the returned status or modify reference counts. Null objects
and null vtables fault at the reached accesses; missing mappings or method
implementations remain explicit operations.

The cold composition allocates an initially unknown 16-byte manager and
20-byte `igNonRefCountedThreadList`, then runs the real field passes. Both
manager object-reference members are cleared before nested construction.
The list inherits `_count`, `_capacity`, and `_data` at `+08/+0C/+10`;
metadata-list layouts cannot substitute for this layout.

## Memory-member defaults

Original default body `80062798` now composes existing element release
`800626DC` and native buffer free/copy helpers. It reads live free flag `+41`,
conditionally frees the old member, dereferences the descriptor's default
pointer, duplicates a nonnull source through `8005647C`, reloads the member
offset for publication, and calls element retain `80062638` afterward.
The copy method's `+40` flag is not this default method's `+41` gate.

The retain helper checks live ownership and element state, performs the
original metadata ancestry test, obtains the physical allocation size, and
increments entire reference words for nonnull elements. Missing later element
bindings preserve earlier increments. Buffer/free/destructor callbacks retain
their captured inputs through suspension; publication and partial faults are
not rolled back.

Nested buffer and element helpers also check the enclosing invocation after
factory and callback returns. A callback that catches an attempted recursive
resume still invalidates the parent; later allocations, reference changes and
publication cannot continue beyond that fault. The check is borrowed for each
resume and does not introduce a retained parent/child ownership cycle.

In the reached thread-list instance, construct flag `+34` and element-ownership
flag `+42` are zero, free flag `+41` is one, and the actual default buffer
contains a null pointer. Consequently no backing allocation occurs. Original
scalar defaults set count and capacity to zero; memory defaults publish zero
at `+10`. The list completes initialization, increments its metadata instance
count and is assigned to manager `+08` with balanced retain/temporary release.

## Current boundary

The same cold invocation now runs semaphore redirect `80026160` and concrete
metadata getter `8003B238`. Its registration constructs three local descriptors
and reaches struct-field factory `8002450C`; defaults/schema and runtime
initialization remain required. The manager still owns its construction lease,
its instance count remains zero and the startup singleton is unpublished.
The main-thread factory and manager's semaphore hook have not executed in this
cold composition. Focused hook tests have explicitly prepared method endpoints.

There are 24 registered types, 87 descriptors, 94 default buffers, ten
prototypes and 84 enabled allocations. Only the thread-list runtime instance
has completed; this is neither complete startup nor playable gameplay.
See [VALIDATION.md](VALIDATION.md) for measured checks and comparison scopes.
See [IGB_PLATFORM_SEMAPHORE.md](IGB_PLATFORM_SEMAPHORE.md) for the concrete class.

Primary behavior evidence is the supplied USA DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`; original artifacts stay local.
