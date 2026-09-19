# Native metadata parent and name operations

The native registration adapter implements original `80065640` parent
attachment and `80065788` name assignment. Evidence is the supplied USA DOL,
SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. These operations work on the
canonical metadata identities used by the registrar and runtime lookup.

## Parent attachment

A null parent returns before reading the child. For a nonnull parent, an empty
child-list pointer at `+34` selects the current metadata-pool handle, calls
`800607F4`, then reaches child-list factory `8002DE88`. The returned list is
published even when null. The subsequent raw `80053BC0` append receives that
list and the captured child; the caller does not retain the child metadata.

After append returns, the caller publishes child `+38`, copies the captured
parent's `+5C` type word, then reloads child `+38` for each alignment, prefix and
size read. The field count is captured once from the original parent. Every
iteration reloads the original parent's current field backing through
`800658E4`. Native `80065820` captures the child's current field registry before
`80053E6C` increments the incoming field's full 32-bit reference word. Null
fields skip that retain but still reach raw append. A failing or suspended
append does not roll back the increment or earlier parent/layout publication.

Child-list construction and raw list append/growth remain explicit services.
An empty native vector is not an implementation of their allocation, capacity
or initialized-pointer requirements. The pool adapter composes the actual
manager lookup and preserves the distinction between absent and null state.
The metadata constructor initializes the new canonical `children` field at
the original raw `+34=0` store; an absent field is not an implicit empty list.

## Name assignment

The caller captures the desired C-string pointer without reading it. It lazily
allocates and initializes canonical string pool `80562140` through the existing
dependency operations, then interns the captured name. Only after interning
returns does it read the live old metadata name. A nonnull old string loses
one full 32-bit reference; zero enters the final container-release operation.
The captured incoming string is published after release completes, without
another guest retain. Native interning can return the same owner, balancing
its retain against the subsequent old-name decrement.

`name_string` binds the canonical header to metadata `+1C`; existing string
identities can resolve through the shared interned-string registry. A missing
header pauses after capturing the old pointer, without replaying interning.
Raw name publication retains displaced host owners, including a different
string installed by a release callback, without altering their guest counts.
Prepared pointers do not manufacture string headers or reference values.

Installing `make_native_igb_string_factory` composes the recovered string-table
search/insertion implementation. Remaining arena allocation, pool construction
and container release retain their own explicit boundaries. Null desired names
still pass through the original lazy pool initialization order.

## Validation and boundaries

Permanent tests cover captured counts with live field replacement, layout
reloads, wrapping field/string references, null children and fields, failure
prefixes, retained append/release callbacks, abandonment, raw-overwrite
lifetime, native interning and registrar adapters. Independent original
instruction comparisons qualify the caller bodies and field-retain wrapper;
their raw append and string-service endpoints are explicitly prepared.
The local comparison passed 906 cases, including 369 bounded failure prefixes,
executing 29,675 original instructions at 124 addresses with zero differences.
It covers `80065640`, `80065788`, `800658E4`, `80065820` and `80053E6C`;
`80053BC0` append/growth and external string services remain prepared endpoints.
See [VALIDATION.md](VALIDATION.md) for the qualified library identity and reports.

These operations replace required registration endpoints. They do not establish
complete field descriptor construction, type publication/layout, an implemented
list allocator or full engine startup. The cold ArkCore bootstrap must register
the fundamental metadata classes before enabling reflected construction.
