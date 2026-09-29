# Native runtime type registration

`IgbTypeRegistrationInvocation` reconstructs USA `80066204`. The native file
registration callback `8002F1A0` supplies its original arguments through
`igb_file_type_registration_arguments`. Its metadata slot aliases the existing
`IgbFileFactoryServices::file_metadata` cell; registration and later allocation
read the same canonical owner, rather than separate name-based caches.

## Registration state and publication order

Every invocation first resolves metadata-pool handle `805621F4` through
`800607F4`. This happens even when the cached type is already registered or
busy. Only after that callback returns is the metadata cell read. A null value
prepares the original `igMetaObject: ` label with the optional C-string name,
calls metadata construction `80029E64`, and publishes its result before the
reached flags access. Missing represented storage pauses; a constructed null
is published and then fails at its actual dereference.

Class flag eight is the original recursion guard. An already-busy type returns
null; otherwise registration sets that bit. Flag four skips configuration but
still clears the newly set busy bit. For an unfinished type:

1. Unless flag two is set, registration configures the type. If flag one is
   clear, it calls the parent registrar, retains the returned deferred callback
   list, calls the separate parent metadata getter, attaches the parent, and
   sets flag one.
2. It calls the name setter, stores the native size, callback `+54` and low-byte
   abstract flag. An abstract byte of zero calls the layout probe and stores
   its result at `+5C`. The optional input field/default callback runs next.
3. It sets flag two, publishes the type through `800660AC`, decrements the
   entire reference word, and reaches destruction if the remaining low23 bits
   are zero. ArkCore filter gates `+64`, `+60` and `+68` follow in that order.
   A filter match reloads ArkCore and its pool before configuration.
4. It computes layout through `80065A58`, then reads and invokes the live
   optional `+4C` callback. Layout may already have published flag four.
5. A missing deferred callback list resolves the metadata pool again, allocates
   twelve bytes, and conditionally executes actual constructor `8003E848`.
   This constructor belongs to the caller; an allocator's preexisting field
   contents are not accepted as initialized state. Null allocation skips it.
6. It reads and appends each live nonzero function in the terminated extension
   array. The trailing callback is appended only when this invocation performed
   first-time configuration. Finally it sets flag four, clears eight, and
   returns the deferred list.

The flags are not a host transaction: reached effects remain after suspension,
failure or abandonment. A nested distinct invocation can see the original busy
bit and return normally. Re-entering the same native continuation is invalid.
Callbacks and returned owners remain captured while pending; completed callback
lifetimes end before subsequent live reads or stores. Abandoned deferred lists
remain retained by their selected service owner.

## Concrete bodies and remaining boundaries

The registrar directly owns this control flow, canonical metadata writes,
reference decrement, callback-table traversal, and the conditional callback-list
constructor. `make_native_igb_registration_list_factory` connects its appends
to actual `8003E8B8`, including original growth and backing allocation order.
See [IGB_REGISTRATION_CALLBACKS.md](IGB_REGISTRATION_CALLBACKS.md).

Metadata allocation, numeric pool resolution, parent registration/getter/link,
name interning/assignment, field construction, registry publication, layout
computation, optional filter operations and final destruction remain explicit
reached operations. The existence of a prepared type does not implement these
bodies. The file's parent is `igDirectory` through `80032950`; `80021B94` is a
separate prerequisite once guard, not the parent registrar.

The original file depends on genuine list objects created by the directory's
object-reference fields. Its 65 own fields and inherited descriptor replacements
still require actual registration, default storage and reference handlers.
The scalar handlers in [IGB_FIELD_DEFAULTS.md](IGB_FIELD_DEFAULTS.md) implement
verified int/bool field operations, but do not create the missing descriptors.

The native host profile bounds registration labels to 240 name bytes and
extension-table traversal to 65,536 physical entries. Overlong original stack
corruption and reads outside represented table storage are not modeled as
successful registration. Optional canonical fields distinguish unavailable
state from represented zero/null values.

Permanent tests cover all low flag combinations, initial pool/cache ordering,
null metadata publication, abstract-byte truncation, full reference wrapping,
fresh filter state, live layout callbacks, actual callback-list construction
and appends, pending allocation, live extension entries, nested registration,
missing state, recursion and abandoned ownership. Independent ignored evidence
executes actual original registrar, string-label preparation, list constructor
and append, with the remaining engine operations explicitly prepared.
Final local evidence is recorded in [VALIDATION.md](VALIDATION.md).
