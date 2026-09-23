# Native reflected field initialization

`igb_field_init` connects the original field-object virtuals to the shared
`800667F4` initializer. Files and field descriptors have distinct native
bindings and typed results. The initializer owns the represented allocation
while its physical header and member stores are published to the canonical
field view; a field is never cast to a loader file.

Evidence is the user-supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original binaries, instruction
traces and corpus data remain local.

## Object virtuals and three field passes

The object-field vtable `804762FC` binds plain metadata getter `80094090`
at `+58` to canonical `805616E8`. Its `+0C`, `+34` and `+24` entries are the
actual return-only `800667A4`, `800667CC` and `800667B0` bodies. Tail `+2C`
executes `8006A438`: after return-only `800667D0`, it reloads the physical
vtable and calls the field's `+8C` with null input. The existing native
`80063D04 -> 80063C74` chain selects size, prepares default storage and
publishes its physical pointer. It is not a successful empty tail.

The initializer visits the derived registry from the captured base-field
count through its captured derived count, retaining original live backing
reads between visits. The object-field class has 25 descriptors, including
two inherited `igObject` descriptors skipped by each pass.

| Reached operation | Native behavior |
| --- | --- |
| Six scalar families, `+84/+7C/+F0` | Exact original leaves; scalar defaults use original byte-copy semantics through `igb_field_defaults` |
| Four reference families, `+84 = 8006C56C` | Capture the destination, call the freshly selected `+64`, truncate its result to 16 bits, then zero exactly that range |
| Reference `+F0 = 8006CD00` | Read the live construct flag at `+34` without normalizing its byte value |
| String/pointer `+7C` | Exact return-only original leaves |
| Object/memory `+7C = 8006A618/80062524` | Execute the construct-flag test and zero branch; a positive branch remains pending at required recursive member allocation |
| String `+70 = 80071A9C` | Intern the captured default string when nonnull, execute `800715E8` assignment, then release the temporary |
| Object `+70 = 8006A7C4` | Capture the target member; null returns, nonnull requires unrecovered `80067098` |
| Reference mode-three `+74` | Exact `80063EB8` return-only body |

The pending positive branches retain their selected path. Changing a construct
flag or target pointer later cannot turn an already reached operation into
successful zero-branch completion. Unknown tables or virtual slots delegate
to an explicit provider; represented null dereferences fail.

## Canonical and physical member stores

Scalar copying and reference clearing refresh only affected, fully initialized
members. Padding is neither read nor normalized. Header aliases update either
file or field reference state. Zero pointer stores publish represented null;
changed nonzero pointers invalidate an unavailable typed binding rather than
retaining a stale owner or manufacturing null. Raw overwrites retain displaced
host owners without inventing guest retain/release calls. Memory-reference
element pointers and object-reference metadata pointers remain distinct.

String assignment preserves its separate original retain/release behavior;
see [IGB_FIELD_STRING_INIT.md](IGB_FIELD_STRING_INIT.md). Field prototype
reference/ordinal stores and finalization's type-ordinal store also update
their explicitly bound physical storage.

## Integration and evidence

The composed cold test now constructs the actual object-field prototype through
the reflected allocator, all three native field passes and the actual tail.
It publishes the prototype, creates/appends to its native prototype registry,
assigns its ordinal and finalizes it. Callback cleanup and the parent list's
schema/layout/publication then complete. Canonical name-descriptor `805617C4`
is shared directly between its original schema publication and the subsequent
internal-list lookup. The lookup reaches the inherited memory-reference field's
unrecovered clone virtual `+54`, with argument one.

At this boundary there are 52 schema descriptors plus one constructed
prototype, 53 default buffers, six registered types and one prototype registry
entry. Reflection remains disabled. Physical allocator, pool/platform and BSS
prerequisites are still explicit; this is not a complete game startup.

Permanent tests cover type separation, header and member publication, pending
callbacks, live virtuals, null/unknown states, string ownership, captured widths,
padding, invalidation and the cold integration. Independent comparisons execute
the original scalar/reference/string bodies and all 0x44 bytes of the reflected
field constructor, including its real default-buffer tail. Prepared endpoint
scopes and final hashes are recorded in [VALIDATION.md](VALIDATION.md).
