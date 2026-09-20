# Native specialized metadata-field construction

`IgbMetaFieldFactoryInvocation` implements eight original USA descriptor
factories with explicit physical allocations and canonical
`IgbDecodeRuntimeField` bindings. The default profile remains unsigned integer,
preserving existing callers. `IgbMetaFieldKind` selects a verified class profile;
an unknown factory identity is not mapped to a generic descriptor.

| Kind | Factory | Constructor / reset | Vtable | Bytes | Registration / body | Metadata global |
| --- | --- | --- | --- | --- | --- | --- |
| Unsigned integer | `80022A90` | `800759E0` / `80075A2C` | `80476E6C` | `34` | `80022CB0` / `80022CD8` | `805614BC` |
| Signed integer | `8002E1AC` | `8005356C` / `800535B8` | `80475B60` | `34` | `8002E390` / `8002E3B8` | `805619E0` |
| Boolean | `800385E0` | `8003EB7C` / `8003EBC8` | `80473E30` | `34` | `80038800` / `80038828` | `80561E48` |
| String | `800251EC` | `80071DF8` / `80071E44` | `8047693C` | `3C` | `800253DC` / `80025404` | `805615B4` |
| Object reference | `8002879C` | `8006AC50` / `8006ACA4` | `804762FC` | `44` | `800289F8` / `80028A20` | `805616E8` |
| Pointer | `800274EC` | `8006C46C` / `8006C4B8` | `80476630` | `38` | `800276DC` / `80027704` | `80561694` |
| Unsigned short | `80021E10` | `80075EC0` / `80075F0C` | `80477054` | `34` | `80021FF4` / `8002201C` | `80561494` |
| Memory reference | `8002AC34` | `8006324C` / `800632A4` | `80476088` | `50` | `8002AE84` / `8002AEAC` | `80561818` |

All table values are hexadecimal. Evidence is the supplied USA executable,
SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. The enabled branch runs the
profile's registration wrapper, using actual callback runner `80066188` when
bound, then reloads that profile's canonical metadata cell before reflected
construction `8006546C`. Each class has a distinct cell. The unsigned-integer
cell retains the original `metadata` service member; the remaining classes use
`metadata_slots`. Missing registration or reflection remains pending.

The disabled branch executes `80066D60(profile size,0,pool)` followed by the
profile's specialized constructor with argument one. A null object skips construction but still
reaches the original tail dereference. The completed descriptor executes fresh
virtual `+2C`, then a separately reloaded virtual `+24(false)`. The exact base
and scalar/reference-base vtables use verified return-only leaves `800667D0`
and `800667B0`. String `+2C=8007144C` and object `+2C=8006A438` instead call
`800667D0` and then freshly load virtual `+8C(NULL)`, which prepares their
default buffers. Those two tails execute through the native field-operation
services before the separately reloaded `+24`; they are not treated as leaves.
Unknown tables remain explicit operations. Enabled reflected results do not
execute the disabled branch's constructor or tails again.

The default unsigned-integer profile retains these original entry points:

| Constructor entry | Actual chain | Final vtable |
|---|---|---|
| Specialized `800759E0` | `8006388C`, then `80075A2C` | `80476E6C` |
| Base `8006388C` | `80066710`, intermediate stores, `800638E0` | `80471914` |
| Reset `800638E0` | `8006665C`, descriptor reset stores | `80471914` |
| Specialized reset `80075A2C` | `800638E0`, derived vtable store | `80476E6C` |

Scalar specialized constructors perform the base descriptor reset twice.
String, object, pointer and memory-reference constructors traverse reference base
`8006CBF4` and reset `8006CC40`, with vtable `80471384` and bytes `+34/+35=0`.
Their complete construction therefore executes base reset `800638E0` three
times. Direct reference-base and reference-reset entry points are also exposed.
String reset then writes `+38=1`, `+27=2`, `+35=1`, in that order. Object
construction clears `+40` before the third reset; object reset clears it again,
writes `+3C=1`, then clears the object-type pointer `+38`. These are raw stores
with nonowning metadata links, not reference assignments.
Memory-reference construction clears words `+48/+4C` before its third reset.
Its final reset clears those words again, sets byte `+42=1`, clears words
`+38/+3C`, sets mode `+24=3`, bytes `+41/+40=1`, word `+44=FFFFFFFF` and
byte `+27=2`, in that order. Its `_data` use in list registration later changes
these defaults through [IGB_LIST_DEFAULTS.md](IGB_LIST_DEFAULTS.md).


Each base reset preserves the original store order, including `+10=0xFFFF`,
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

Typed header/vtable state follows the represented physical construction. The
descriptor keeps a weak physical-storage binding, allowing native default
buffer operations to publish size and pointer bytes at their actual stores,
including before a pending allocator returns. This does not create a
descriptor/allocation ownership cycle.
Successful factories retain physical allocations for subsequent descriptor
operations. The descriptor's global-slot pointer is nonowning: its binding
distinguishes unknown, known null and expired nonnull cells without creating
a descriptor/global ownership cycle. Raw reset clears that binding without
dereferencing the old cell. Construction leases reject recursive or abandoned partial
reconstruction, and distinct typed allocation identities cannot alias the
same physical offset. Tests exercise each class's complete allocation, untouched
padding, both reset passes, pending callbacks, enabled-gate mutation, canonical
registration, live name replacement/release and malformed physical prefixes.

Physical allocators, enabled reflected construction and arbitrary virtual
tables remain explicit boundaries. This module does not establish complete
field registration or startup.

The earlier unsigned-integer-only local comparison executes its original factory, all four
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
the corresponding historical build passed all 164 CTest suites without warnings.
This older result does not qualify the subsequently added profiles; current
family validation is recorded separately in [VALIDATION.md](VALIDATION.md).
