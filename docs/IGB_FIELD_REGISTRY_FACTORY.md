# Native internal field-registry construction

`IgbFieldRegistryFactoryInvocation` reconstructs USA `8002DB4C`. Its result is
the canonical `IgbDecodeFieldRegistry`, with a distinct typed allocation binding;
it is neither an IGB file nor one of that file's serialized data lists.

The factory reads the live ArkCore enabled byte. The enabled branch calls
registration `8002DD18`, reloads metadata global `805619CC`, then reaches an
explicit typed reflected-factory operation with the original pool. That branch
does not substitute direct construction for missing registration or reflection.

The disabled branch uses real `80066D60(16,0,pool)` allocation and constructor
`80053CB8`, composed from `8005368C`, `800536F8`, and the shared actual base
constructors `80066710` and `8006665C`. A null numeric object allocation skips
the constructor but still fails at the original tail's vtable dereference.

The first base constructor may observe ArkCore becoming enabled during the
allocation callback, resolve the allocation's pool, and publish its tag. The
subsequent base reset clears that tag again before `80068430` reads object
byte `+04`. Its `8005929C` lookup is distinct from raw allocation's `800607F4`
manager-based handle lookup. The selected pool's virtual `+D8` allocates exactly
16 backing bytes. A represented null backing is stored normally; it does not
make the constructed list result null.

The constructors preserve physical vtable/header stores and reset backing `+08`
and count `+0C`. The final derived vtable is `80475CB0`; the intermediate base
table is `80475C54`. Factory tail `+2C` and fresh `+24(false)` resolve to verified
return-only original leaves for these tables. Unknown tail bindings stay explicit.

The registry retains its physical backing bytes/address separately from any
supplied mapping of pointer words to canonical field owners. Allocating 16 bytes
does not establish four initialized null fields. Construction does not inspect
these entries and leaves count zero. Overwritten backing owners remain retained
because the original raw stores do not release them. The physical reference
word remains synchronized with the typed registry through shared bootstrap
helpers.

Direct constructor entry points support derived construction, base construction,
and reset only. Native layout bindings are required, physical reads/writes are
checked, and selected operations remain captured while pending. A partial
constructor holds an exclusive typed-registry lease; failures or abandonment
poison it and retain unfinished allocation state rather than permitting replay.

Tests cover real 16-byte object/backing requests, the fresh enabled gate and
second header reset, canonical physical bytes, null object/pool/backing behavior,
pending callbacks, raw overwrite ownership, reflected metadata reload and
constructor abandonment/recursion. Pool resolution, allocator internals, and
enabled reflected construction remain explicit providers. This does not establish
all metadata registration or a completed original dependency load.

The local raw-instruction comparison covers 1,312 factory/direct-constructor
cases, including 754 bounded failure prefixes, with zero differences across
72,954 original instructions and 209 instruction addresses. It executes the
actual factory, constructor chain, shared raw allocator/base bodies, allocation
address helper, object-pool header wrapper, and reached return-only leaves.
Registration/reflected construction, allocator implementations, both distinct
pool lookup services, allocation-pool resolution, and pool-index returns are
prepared endpoints. The corpus uses initialized whole object headers and does
not prove an allocator, field-entry initialization, or complete bootstrap.
