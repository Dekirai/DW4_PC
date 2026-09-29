# Native cold ArkCore constructor and bootstrap caller

`igb_construct_ark_core` implements original USA `8003D234` against explicit
`0x3A0` physical storage and its canonical `IgbRuntimeTypeRegistry` owner. It
preserves the original ordered byte/word stores, including enabled `+00=0`,
field-initialization gate `+14=0`, type-list `+18=NULL` and vtable-offset
`+394=0`. Untouched bytes stay untouched. In particular, it does not initialize
loader `+40`, allocate the object or publish global `80562100`.

`IgbArkCoreBootstrapInvocation` implements the complete caller `8003D2F8`.
It first sets `+14=1`, calls the platform operation, then resets `+394`. It
checks canonical fallback manager `805621C8`, conditionally allocates and
constructs it, reloads that global and invokes manager `+A8`.

Two separate current default-pool lookups use actual `800607F4`, preserving
fresh handle/global reads. The first metadata-element list is published into
core `+18`; its allocator-derived backing capacity decides whether the actual
reserve boundary receives request 1024. Equality requests reserve as well. The
second list goes to `+24`. These are metadata-element lists; they are never
cast from field-descriptor registries. Two independently allocated callback
lists receive actual `8003E848` typed initialization and are published into
`+28` and `+2C`.

The crucial cold-start gate follows the original sequence: the bootstrap
executes actual callback runner `80066188` with registration body `8002A0D0`
while the core is still disabled. Only when that entire runner returns does
the caller write enabled `+00=1` at original `8003D3F4`. Thus the existing native
metadata registration/factory can create canonical `8056174C` through the
disabled direct constructor before reflected allocation becomes enabled.
Pending or failed registration does not manufacture that metadata or skip to
the enable store.

The caller then snapshots fresh main-type and field-prototype registry counts into core
`+04/+08`, executes the original 22 post-enable registration runners in order,
publishes the explicit `80027824` pointer-field prototype result to canonical `80561688`, and freshly
reloads the fallback manager before virtual `+AC`.

Platform initialization, manager allocation/construction/virtual operations,
metadata-element list construction, backing-size and reserve operations,
callback-list allocation, and getter `80027824` remain explicit reached
providers. Actual callback runners retain their selected operations and stop
on unbound registration bodies. Native layout availability remains distinct
from represented null, which faults at its reached dereference. The canonical
core holds an exclusive bootstrap lease and retains raw-overwritten owners;
failure or abandonment prevents replay of a partial prefix.

This module does not implement the outer `8003CFC0` initialized counter,
allocation/global publication, `initCore8003D624`, loader/timer creation, or
complete game startup. The existing reconstructed metadata lookup fixture is
still not a cold-start execution. Prepared success in caller tests proves
only ordering under the explicitly supplied service results.

Permanent tests cover untouched constructor bytes and loader state, original
constants, ordered registration gates and count snapshots, distinct list
publications, capacity equality, fresh pool handles, conditional manager
creation, retained pending callbacks, null manager/field-registry failure prefixes,
abandonment and recursive invalidation.

The local raw-instruction comparison covers 759 constructor/bootstrap cases,
including 586 bounded failure prefixes, with zero differences over 262,345
original instructions at 447 distinct instruction addresses. It compares the
entire physical `0x3A0` core, globals and callback-list header states. Original
constructor/bootstrap, pool-selector dispatch, callback-list initializer,
registration wrappers and callback runner execute. Registration bodies return
explicitly prepared null callback lists; manager/list/allocator/platform and
final prototype-getter endpoints remain prepared. This establishes caller
ordering and failure prefixes within that profile, not complete registration.

A subsequent integration now binds actual metadata-element list factories and
native pointer-list capacity/reserve operations. Both initial lists are
constructed physically; the first grows from four to 1,536 pointer slots for
the original reserve request 1,024. Byte-only backing with no typed pointer
mapping still triggers the original capacity query. The selected size call
captures backing bytes/address/offset independently of later list replacement.
The test reaches required `8002A0D0` while ArkCore remains disabled; platform,
manager, pool/allocation and callback allocation endpoints remain prepared.
The extended metadata integration now also uses those actual initial list
constructors and reserve operations. It registers igObject through its real
specialized fields, type publication, field layout and empty callback dispatch,
then constructs its child list and inherits both fields into igMetaObject.
The current composition also completes that metadata's probe and defaults,
enters deferred list registration and registers the three reached field classes.
It constructs 52 descriptors and publishes five types. Abstract reference-field
prototype creation returns null through the actual reflected factory; concrete
object-field prototype creation now completes typed initialization `800667F4`,
the reached field passes and tail `8006A438`. Its native registry publication
and finalization produce canonical `805616EC` with ordinal and type ordinal
zero. The prototype registry contains one descriptor; the object-field class
records one instance. After actual callback cleanup through explicit deallocation
endpoints, parent list metadata `805619D4` publishes as the sixth type with
flags `7`. The composition now has 52 schema descriptors plus one reflected
prototype and 53 default buffers. Child list `805619CC` remains busy with flags
`9`. Actual name/index lookup through canonical name descriptor `805617C4`
reaches its inherited memory-reference descriptor's clone virtual `+54` with
argument 1 (`80476088` vtable). The core remains disabled at that unfinished
clone; recursive member/reference operations beyond the completed prototype
profile and later startup remain required. See
[IGB_FIELD_INIT.md](IGB_FIELD_INIT.md),
[IGB_FIELD_STRING_INIT.md](IGB_FIELD_STRING_INIT.md),
[IGB_FIELD_PROTOTYPE.md](IGB_FIELD_PROTOTYPE.md),
[IGB_TYPE_LIST_FACTORY.md](IGB_TYPE_LIST_FACTORY.md),
[IGB_TYPE_PUBLICATION.md](IGB_TYPE_PUBLICATION.md),
[IGB_METADATA_LAYOUT.md](IGB_METADATA_LAYOUT.md),
[IGB_POINTER_LIST.md](IGB_POINTER_LIST.md),
[IGB_OBJECT_REGISTRATION.md](IGB_OBJECT_REGISTRATION.md) and
[VALIDATION.md](VALIDATION.md) for current evidence and remaining boundaries.


The canonical globals have distinct element types: `8056229C` contains field
prototypes (`IgbDecodeFieldRegistry`), while `80561688` is a descriptor pointer
(`IgbRuntimeFieldSlot`). The latter initially belongs to `igRefMetaField`
prototype creation `80027454`; the late bootstrap store aliases the pointer-field
prototype returned by `80027824` from `80561698`. They are shared with the native
prototype module, not represented as metadata or a second metadata list. This
corrects the earlier host-type interpretation while preserving original count
reads and the raw final assignment. The complete bootstrap remains unverified;
see [IGB_FIELD_PROTOTYPE.md](IGB_FIELD_PROTOTYPE.md) and [VALIDATION.md](VALIDATION.md).
