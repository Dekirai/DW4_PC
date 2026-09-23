# Reflected native metadata initialization

The enabled `80029E64` metadata factory now composes the actual native
`8006546C` reflected allocation caller and `800667F4` initializer. The typed
result is an `IgbRuntimeMetaType` with its original physical allocation, not a
file object or a preconstructed schema. `IgbObjectInstance` also represents
internal field lists and type lists as distinct owners.

Evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original instructions and comparison
corpora remain outside version control.

## Initialization and ownership

The existing reflected caller selects allocation size, prefix, pool and
initialization order. The generic initializer validates a single coherent
typed binding at the adjusted object address, retains its construction lease
across pending callbacks, and faults abandoned or invalidated construction.
The file-only result adapter rejects metadata and list instances explicitly.

For metadata table `804761D8`, virtual `+58` executes the plain `80094080`
getter of canonical cell `8056174C`. The reached `+0C`, `+24`, `+2C` and `+34`
methods are the original return-only functions. They do not reset derived
members or substitute a descriptor's separate object-field constructor tail.

The internal field-list table `80475CB0` uses the plain `80094038` getter of
canonical cell `805619CC` and the same four return-only methods. The adapter
reads the live list metadata cell and validates a field-list typed owner. It
does not supply the list's `_data` backing; that member follows the actual
positive memory-reference constructor in
[IGB_MEMORY_FIELD_CONSTRUCT.md](IGB_MEMORY_FIELD_CONSTRUCT.md).

Concrete scalar defaults, reference clears and string defaults update the
physical storage and the corresponding canonical metadata members. Numeric
pointer identity remains separate from a bound host owner. Null stores clear
the canonical view; an unknown nonnull pointer remains unbound. Displaced host
owners are retained rather than being destroyed implicitly by a raw original
store. Unrelated padding stays untouched.

The instance vtable at `+00` is distinct from metadata's `+5C` type-word member.
The original `igMetaObject` schema has 27 inherited/local descriptors. Its
`_metaFields` member at `+28` uses the positive object-reference constructor
`8006A618`, including actual nested reflected allocation, assignment, retain
and temporary release. See [IGB_OBJECT_FIELD_CONSTRUCT.md](IGB_OBJECT_FIELD_CONSTRUCT.md).

## Validation scope

Focused synthetic tests exercise typed results, physical/canonical scalar and
pointer state, string nulls, allocation prefixes, padding, instance counts,
pending allocation, canonical binding replacement and abandonment. Prepared
empty list schemas isolate the generic initializer; they are not evidence for
the original list's member allocation or full startup.

The reflected adapter retains completed physical allocations in its services.
The allocator and each class's reached virtuals must still be supplied through
explicit native services. No ready field list is inserted to bypass recursive
construction. Local integration results and remaining original boundaries are
recorded in [VALIDATION.md](VALIDATION.md).
