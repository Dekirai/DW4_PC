# Native reflected field member copies

`IgbFieldMemberCopyInvocation` reconstructs the member operations reached by
the reflected `igCharMetaField` copy pass. It receives a captured descriptor,
destination and source `IgbObjectInstance`, and the selected original function.
It does not allocate a clone or replace the reflected object initializer.

The implementation covers these original USA executable bodies:

| Function | Behavior |
| --- | --- |
| `800642E4`, `80064318`, `8006434C` | Copy a member using captured descriptor offset and unsigned halfword size. |
| `80071B58`, `80071BC4` | Retain a captured source string, execute string assignment `800715E8`, then release the temporary. |
| `8006A81C` | Read a source object member and execute object assignment `8006A47C`. |

`8006434C` performs a real byte copy. It is not a no-op. Other function
identities, including memory member copy `80062824`, remain explicit retained
operations through `IgbFieldMemberCopyServices::factory`. An absent function
implementation suspends at that boundary.

## Live native member representation

The existing schema and naming operations update canonical field members.
Parts of their original allocation bytes can still contain constructor values.
The new reader therefore uses the live canonical `igMetaField` base members
through offset `+33`, with explicit arithmetic bindings for pointer values.
Generic object bytes, base-layout padding, and derived field members at `+34`
and above use represented physical storage. This module does not claim
canonical synchronization for those derived layouts.

Names resolve through `IgbFieldValuesServices::strings`. Declaring metadata
resolve through object allocations or pointer-list metadata bindings. Global
cells retain their explicit address and weak owner; default buffers retain
their buffer/address identity. Unknown nonnull pointers are not converted to
null or host pointer casts. String and declaring-type source identities remain
captured while their arithmetic bindings are supplied later.

Raw copies follow the original unsigned address comparison and byte order.
Each reached source byte is read immediately before its destination store.
Zero-size copies perform no source or destination access. Guest arithmetic
wraps at 32 bits, while host allocation offsets remain independently bounded;
a reached effective null pointer faults. Physical source initialization and
destination bounds are checked at their respective accesses, preserving the
completed write prefix of a failure.

Each byte store updates affected canonical base members without deserializing
stale neighboring physical bytes. Aliasing field views in the same allocation
observe the store. An optional scalar whose previous value is unknown becomes
known only after all its bytes have been written. Padding is never inferred or
initialized by that synchronization. A completed raw global-cell copy also
preserves its known nonowning cell binding.

## Ownership and suspension

The string wrappers retain the original temporary before composing the
existing `IgbFieldStringAssignInvocation`. That assignment performs its own
retain, destination release and publication; the wrapper then releases the
temporary using full 32-bit string counts. This deliberately preserves both
retains. It does not intern the copied string. Reached release callbacks are
retained across suspension, and completed arithmetic is not replayed.

The object wrapper composes `IgbObjectFieldAssignInvocation` with the captured
incoming allocation. It preserves that operation's live reference-policy
gates, old-member release and destination-offset reload. The current inherited
`_parentMetaObject` descriptor has both ownership gates clear, so its copy
publishes the declaring metadata pointer without changing guest references.

Pending operations preserve source and destination identity. Replacing their
typed allocation binding, or removing a captured physical field binding,
faults before later stores. Abandoned or faulted invocations retain captured
allocations and acquired string owners in their service lifetime storage.
Unimplemented function callbacks keep their selected continuation and remain
responsible for their own reached argument accesses.

## Current reflected schema scope

The registered `igCharMetaField` has 20 inherited fields: two from `igObject`
and 18 from `igMetaField`. The mode-one object copy skips the two base-object
members. Of the 18 remaining descriptors, 15 select the raw leaves, one selects
string copy `80071B58`, one selects object assignment `8006A81C`, and one
selects the still-explicit memory copy `80062824` for `_default`.

Descriptor copy policy byte `+28` selects the member virtual. It is distinct
from the copied object's value at that offset, the default-initialization
policy at `+24`, and the shallow-copy policy at `+27`. The reflected caller
must read the live policy and vtable rather than hardcode this schema's
current sequence.

## Validation scope

The asset-free `igb_field_member_copy_tests` suite exercises canonical values
whose physical image is stale, exact halfword and byte widths, untouched
padding, raw global-cell identity, both overlap directions, and physical
failure prefixes. It also covers partial canonical stores, suspension after
completed stores, aliasing typed views, zero-size malformed bindings and
wrapped effective null addresses.

Ownership tests compose the actual string and object assignment modules.
They cover delayed old-string release, captured source and destination
identity, late arithmetic mappings, string self-copy, metadata assignment,
pending physical-binding removal, unimplemented memory-copy suspension and
continuation retention. Allocator/destructor endpoints in these fixtures are
explicit synthetic services. Build results and any original-executable oracle
comparisons are recorded separately in the validation log after they run.

Original executable provenance is the user-supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original instructions and generated
comparison inputs remain outside the tracked source deliverables.
