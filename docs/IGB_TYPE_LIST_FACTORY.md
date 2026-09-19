# Native metadata-element list factories

`IgbTypeListFactoryInvocation` implements USA child-list factory `8002DE88`
and the metadata-element use of registry factory `8002DB4C`. Both return a
canonical `IgbRuntimeTypeList` through an explicit typed physical binding.
They never reinterpret an `IgbDecodeFieldRegistry`, even though the original
pointer-list layouts and base constructor are shared.

| Entry | Enabled registration | Canonical metadata | Disabled constructor | Final vtable |
|---|---|---|---|---|
| Child list `8002DE88` | `8002E054`, body `8002E07C` | `805619D4` | `8005368C` | `80475C54` |
| Registry `8002DB4C` | `8002DD18`, body `8002DD40` | `805619CC` | `80053CB8` | `80475CB0` |

The enabled branch preserves the captured input pool, completes registration,
reloads its canonical metadata cell, then reaches the explicit typed reflected
factory `8006546C`. When callback services are supplied, registration composes
actual `80066188` with the exact body function. An unbound body stays pending;
it does not fall through to prepared success. The cache cells are shared aliases,
so the registry cache can use the same storage as field-registry registration.

The disabled branches allocate exactly 16 object bytes with actual
`80066D60(16,0,pool)`. The common `8005368C` constructor executes `80066710`,
publishes the intermediate vtable, then executes reset `800536F8` and its
`8006665C` base. Reset clears the backing pointer and count. Its second header
reset clears any tag written by the first base constructor before `80068430`
reads the pool-index byte. That lookup uses `8005929C`, separately from the
raw allocator's `800607F4` manager handles. The captured pool's virtual `+D8`
requests 16 backing bytes. A null backing remains a valid stored null result.

The registry constructor additionally writes its derived vtable. Factory tail
`+2C` and fresh `+24(false)` resolve to verified return-only leaves for exact
known tables; unknown bindings remain explicit. A null object allocation skips
construction but still fails at the original tail dereference. A represented
null vtable is also a physical failure, not an unknown virtual call.

The canonical list retains its packed reference word, backing bytes, address,
suballocation offset and any explicitly supplied metadata-element mapping.
Allocation alone does not establish initialized spare entries or allocator
capacity. The existing host `capacity` field is not fabricated from the 16-byte
request. Raw overwritten owners stay retained without adding guest reference
changes. Successful factory allocations remain available through
`completed_allocations` after an adapter publishes only the canonical list.

The metadata adapter binds only `create_children`, allowing actual `80065640`
to consume this constructor before its separate append operation. The ArkCore
adapter binds only `create_type_list`, selecting the original registry factory
for the two bootstrap lists. Neither adapter invents reserve, append or
reflected construction. Their selected services remain alive while pending;
stored factories use weak service references to avoid permanent cycles.

Tests cover both factories, all three direct constructor entries, concrete
physical bytes, canonical cache aliases and registration runners, fresh pool
gates, backing suballocations, null and pending failures, raw overwrite lifetime,
typed identity conflicts, adapters and composition with parent attachment.
The cold-prefix integration composes the actual ArkCore constructor/bootstrap,
both registry factories and native pointer-list capacity/reserve. The first
list grows from a 16-byte allocation to 1,536 pointer slots for the original
minimum index 1,024; its previous bytes stay uninitialized while new bytes are
zeroed. The second list retains its distinct 16-byte backing. Both original
list globals and callback headers are published before the invocation stops
at missing registration body `8002A0D0`, with reflection still disabled.
Platform hooks and physical pool/allocation results remain prepared endpoints.
This module does not establish complete registration or cold startup.

The local independent comparison executes both original factory bodies,
all three constructors, shared bases, raw allocation caller, pool-tag caller
and verified return-only virtual leaves from the USA DOL. Across 1,393 cases,
including 788 bounded failure prefixes, the native driver matches 79,230
executed original instructions covering 250 addresses with zero differences.
Registration, reflected construction and physical allocator/pool operations
remain explicitly prepared endpoints in this corpus. Header bytes, callback
order, selected identities and failure prefixes are compared; this is not a
claim to execute the enabled reflected constructor or complete bootstrap.
