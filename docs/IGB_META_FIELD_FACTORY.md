# Native specialized metadata-field construction

`IgbMetaFieldFactoryInvocation` implements USA factory `80022A90` with an
explicit physical allocation and canonical `IgbDecodeRuntimeField` binding.
The enabled branch runs registration wrapper `80022CB0` (actual callback runner
`80066188` with body `80022CD8` when bound), reloads canonical metadata cell
`805614BC`, and reaches the explicit reflected-factory `8006546C` endpoint.
Missing registration or reflected construction remains pending.

The disabled branch executes `80066D60(0x34,0,pool)` followed by specialized
constructor `800759E0(object,1)`. A null object skips construction but still
reaches the original tail dereference. The completed descriptor executes fresh
virtual `+2C`, then a separately reloaded virtual `+24(false)`. The exact base
and specialized vtables use verified return-only leaves `800667D0` and
`800667B0`; unknown tables remain explicit operations.

| Constructor entry | Actual chain | Final vtable |
|---|---|---|
| Specialized `800759E0` | `8006388C`, then `80075A2C` | `80476E6C` |
| Base `8006388C` | `80066710`, intermediate stores, `800638E0` | `80471914` |
| Reset `800638E0` | `8006665C`, descriptor reset stores | `80471914` |
| Specialized reset `80075A2C` | `800638E0`, derived vtable store | `80476E6C` |

The specialized constructor therefore performs the base descriptor reset
twice. Each reset preserves the original store order, including `+10=0xFFFF`,
zero offset, ordinal and size, known null name/default/global-slot pointers,
and the original flag bytes. Padding bytes remain untouched. The second base
reset clears any pool tag written by the first `80066710` call.

Each reset checks canonical string-pool global `80562140`, conditionally
allocates and initializes it through explicit `80054140`/`80053F28` endpoints,
then executes the actual `80054094(pool,NULL)` leaf. A callback can replace
the descriptor's name before its fresh `+0C` read. The native code preserves
the full reference decrement, possible `8007381C` release boundary, and raw
null publication after callback teardown. Raw overwritten owners remain
retained without adding guest reference operations.

Typed header/vtable state follows the represented physical construction.
Successful factories retain physical allocations for subsequent descriptor
operations. The descriptor's global-slot pointer is nonowning: its binding
distinguishes unknown, known null and expired nonnull cells without creating
a descriptor/global ownership cycle. Raw reset clears that binding without
dereferencing the old cell. Construction leases reject recursive or abandoned partial
reconstruction, and distinct typed allocation identities cannot alias the
same physical offset. Tests exercise complete 0x34-byte results, untouched
padding, both reset passes, pending callbacks, enabled-gate mutation, canonical
registration, live name replacement/release and malformed physical prefixes.

Physical allocators, enabled reflected construction and arbitrary virtual
tables remain explicit boundaries. This module does not establish complete
field registration or startup.

The local independent comparison executes the original factory, all four
constructor entries, shared bases, raw allocation caller, allocation-tag
caller, null-name interner and reached return-only virtual leaves from the USA
DOL. Across 1,453 cases, including 657 bounded failure prefixes, the native
driver matches 138,761 executed original instructions covering 284 addresses
with zero differences. It compares complete initialized 0x34-byte descriptor
allocations, canonical globals, full string reference words and endpoint
order. Registration, reflected allocation, physical pools/allocators,
string-pool construction and final string release are prepared endpoints.
The final comparison used local `dw4_core.lib` SHA-256
`47535AEED92FC884FD5A9BCE4940F14C270154DAE59E13418AED1A4AE4583A77`;
the corresponding local build passed all 164 CTest suites without warnings.
