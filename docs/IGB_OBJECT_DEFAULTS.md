# Native igObject field defaults

`igb_object_defaults` implements the actual USA `80066B7C` caller for the
specialized `_refCount` and `_memPoolIndex` descriptors. Evidence is the supplied
`main.dol`, SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original binary
content remains outside the repository; the source and fixtures are authored C++.

Each of the two blocks independently reads metadata-pool handle `805621F4` and
executes native `800607F4` followed by specialized field factory `80022A90`.
The completed allocation supplies both a canonical field owner and its explicit
original arithmetic address. That identity is registered with the raw pointer
list implementation; a host pointer is never substituted for an original word.

The caller then performs the original operations in order:

1. Store field offset four and the appropriate descriptor-global cell pointer,
   `805622A8` or `805622AC`.
2. Inspect canonical string-pool global `80562140`, construct and initialize it
   only if required, then reload it for interning the corresponding literal.
3. Place the interned header in a mutable temporary slot and execute native
   `80063D54`. The name setter retains its initial incoming header, releases the
   old field name, then reloads that same slot for publication.
4. Reload the caller's temporary and decrement its complete 32-bit string
   reference count. Zero selects the explicit `8007381C` release endpoint.
5. Store field byte `+26 = 0` and default mode `+24 = 3`, reload canonical
   igObject metadata `805622A4`, and invoke native `80065844`.
6. Decrement the creator's complete reference word, then test its low 23 bits.
   A zero masked count selects explicit destructor `80066E1C` before the next
   field begins.

String references use a full-word zero test; field destruction uses the packed
object count. Overflow and underflow are retained as original unsigned word
arithmetic. The two metadata-pool reads and two canonical metadata reads remain
independent. A callback changing either global affects the next reached read,
while already selected pool, factory, name, finalization and append operations
retain their arguments and continuations.

The mutable string slot is required even for this literal-name caller: a reached
release callback can change the slot before the name setter and caller reload
it. Missing represented storage pauses at the actual read. Represented null
metadata is passed to finalization/append, so completed field finalization is
preserved before the later null metadata dereference fails. Faults do not undo
earlier offset, name, flag, global publication or reference-count updates.

The module composes the real specialized factory and
[field operations](IGB_FIELD_OPERATIONS.md). It does not return prepared
completed descriptors or replace field virtuals with generic success. Allocation,
unknown pool operations and destruction remain explicit boundaries. Global
publication and list ownership preserve the actual descriptor identity.

Descriptor `+1C` is a nonowning binding to a canonical global cell. Unrepresented
storage, an original null pointer and an expired nonnull binding are distinct:
the first pauses, null skips publication, and an expired binding faults when
finalization reaches its store. A weak control-block identity preserves that
distinction without forming a descriptor/global-cell ownership cycle. These
host lifetime rules add no original reference-count operations.

The cold-bootstrap integration begins without ready metadata. It runs native
ArkCore startup, registration and distinct metadata/list constructors, then both
specialized descriptors, actual string interning, default-buffer preparation,
field finalization and raw append. The unsigned-integer prototype global is
explicitly represented as its original cold BSS null. Physical allocators are
prepared endpoints. Type publication `800660AC` and metadata layout `80065A58`
now continue this chain: igObject finishes with flags `7`, its fields are
inherited by igMetaObject, and the latter reaches its constructor probe
`80029FFC` with flags `9`. Initial type-list construction/reserve and the
inherited child-list construction also execute natively in the same fixture.
ArkCore remains disabled; complete runtime registration and asset decoding
are not implied. See [IGB_TYPE_PUBLICATION.md](IGB_TYPE_PUBLICATION.md) and
[IGB_METADATA_LAYOUT.md](IGB_METADATA_LAYOUT.md).

Permanent tests cover concrete two-field construction and arithmetic identities,
name/default-buffer suspension, missing global-cell storage, live metadata and
pool changes, null metadata failure after finalization, full-word string wrapping,
packed creator destruction and exact adapter routing. Separate prepared-string
fixtures isolate abnormal reference words; normal and cold-integration cases
execute the native interner. Results belong in [VALIDATION.md](VALIDATION.md).
