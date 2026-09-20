# Native field default values

`igb_field_values` reconstructs the reached default-value virtuals and the
string default setter as authored C++. It composes the existing native buffer
preparation, pool selection and string-pool services. It does not initialize
unknown allocation bytes or infer a registered prototype from a class name.

## Exact virtual dispatch

The eight verified vtables share actual default-copy body `80063D04` at `+8C`.
Their prototype getters are plain global reads; missing bindings suspend and
represented nulls return null. Size and alignment are separate original leaves.

| Field class | Vtable | Prototype getter / global | Size leaf / value | Alignment leaf / value |
| --- | --- | --- | --- | --- |
| Unsigned integer | `80476E6C` | `80094140` / `805614C0` | `800759D8` / 4 | `80075AF8` / 4 |
| Signed integer | `80475B60` | `80094028` / `805619E4` | `80053564` / 4 | `80053684` / 4 |
| Boolean | `80473E30` | `80093E84` / `80561E4C` | `8003EC60` / 1 | `8003EC9C` / 1 |
| String | `8047693C` | `80094100` / `805615B8` | `8007168C` / 4 | `8006CD08` / 4 |
| Object reference | `804762FC` | `80094098` / `805616EC` | `8006A538` / 4 | `8006CD08` / 4 |
| Pointer | `80476630` | `800940D0` / `80561698` | `8006C3D4` / 4 | `8006CD08` / 4 |
| Unsigned short | `80477054` | `80094160` / `80561498` | `80075E84` / 2 | `80075FA4` / 2 |
| Memory reference | `80476088` | `80094070` / `8056181C` | `800624E8` / 4 | `8006CD08` / 4 |

Unknown vtables and virtual slots delegate or remain pending. The existing
unsigned-integer prototype cell stays canonical through
`IgbFieldOperationsServices::unsigned_int_prototype`. The seven additional cells
belong to `IgbFieldValuesServices`; they remain distinct from class metadata.

## Buffer copy

`IgbFieldValueInvocation` follows actual `80063D04`: retain the original source
identity, execute `80063C74`, then reload destination `field+20` and unsigned
16-bit size `field+14`. Null input still prepares the buffer. A nonnull input
copies exactly that live size through original byte-moving body `800031E8`.
The source bytes remain live during pending pool operations.

`800031E8` compares unsigned pointer addresses and copies backwards when source
is lower than destination, forwards otherwise. Same-allocation views preserve
overlap and derive missing arithmetic identities from their shared base when
possible. Reads and writes happen one byte at a time, preserving the committed
prefix before a reached invalid byte. Explicit source/destination identities
must agree with represented nullness and aliasing; validation follows the
already-required buffer preparation.

Two fully accessible disjoint native buffers have identical results in either
direction, so their complete copy does not require a fabricated original stack
address. A potentially failing disjoint copy with unknown ordering remains
pending until arithmetic bindings are available. Late bindings identify the
captured buffers; they cannot redirect a pending operation to a replacement
field pointer. Zero-length copies read or write no buffer byte.

## String default ownership

`IgbFieldStringDefaultInvocation` implements actual `80071694` independently
of the generic copy virtual. It captures the string-pool null test and
`field+20` before lazy pool construction, and initializes/interns through the
canonical pool even for a null input name. It then reads the old pointer from
the captured buffer, decrements the old header's full 32-bit reference count,
calls `8007381C` only on zero, and publishes the captured returned pointer into
that same buffer. It neither prepares a new buffer nor acquires another
reference to the returned string.

The address/header map represents nonzero pointers explicitly. A physical zero
needs no header. Unknown old pointers suspend before decrement; unknown returned
addresses suspend at publication after the earlier ownership work. Pending
releases retain their exact continuation and do not repeat the decrement.
Changing the field's live buffer during interning or release cannot redirect
the captured destination.

The metadata-default adapter connects exact `80071694(field,0)` from
`8002A164`. Nonnull names can be passed to the direct invocation through a
represented `IgbDependencyName`; a numeric argument without a name binding is
not converted into a fabricated host string.

## Verification scope

The original seven-profile Release suite passed 108 checks. Current results are
recorded in [VALIDATION.md](VALIDATION.md). Tests cover all eight virtual
profiles, canonical missing/null
prototype cells, fresh destination/size reads, retained pool operations,
overlap in both directions, exact partial-fault prefixes, zero-sized copies,
late arithmetic binding, contradictory pointer identities, string-pool
construction on null input, captured-buffer ownership, reference wrapping,
self-assignment, pending release and publication, recursive failure propagation
and abandoned temporary lifetime. Pool and interner callbacks in focused cases
are explicit prepared endpoints; their outputs are not proof of allocator or
interner implementation.

String/object constructor `+2C` tails (`8007144C` / `8006A438`) invoke their
fresh `+8C` with null input. This gives the string setter its default allocation
before metadata customization. A zero-filled physical allocation used by an
integration fixture is a declared allocator precondition; this module does not
infer that original allocation universally clears memory.

Evidence comes from user-supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original binaries and generated
disassembly remain untracked. These operations advance native type setup;
they do not establish complete game startup or gameplay.
