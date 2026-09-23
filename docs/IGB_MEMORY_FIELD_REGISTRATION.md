# Memory-reference and Boolean field registration

The native field-registration module represents the concrete descriptor classes
`igMemoryRefMetaField` and `igBoolMetaField`. Their registration bodies share
the existing registrar and preserve distinct class metadata and prototype
cells. A registered class is not itself a constructed descriptor.

Evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. The implementation and fixtures are
authored C++; original binary data and instruction corpora remain local.

## Exact class profiles

| Property | igMemoryRefMetaField | igBoolMetaField |
| --- | --- | --- |
| Wrapper / body | `8002AE84` / `8002AEAC` | `80038800` / `80038828` |
| Metadata cell | `80561818` | `80561E48` |
| Native size / abstract flag | `0x50` / `0` | `0x34` / `0` |
| Parent body / getter | `80027360` / `80025494` | `8002A6D8` / `80021D70` |
| Metadata callback | `8002AF20` | `80038890` |
| Layout probe | `8002AD90` | `80038778` |
| Defaults callback | `8002AF40` | Null |
| Trailing callback | Null | Null |
| Extra-callback table | `804649F0` | `8055D690` |
| Extra callbacks | `8002A6B0`, `8002B178`, zero | `800388B0`, zero |
| Allocation-only getter | Not represented | `80038700` |
| Registered getter | `8002AD54` | `8003873C` |

Both wrappers invoke callback runner `80066188`; both bodies call the native
registrar `80066204` without prerequisite guard `80021B94`. The memory class
inherits `igRefMetaField`; Boolean inherits `igMetaField`. Their concrete flag
selects the actual probe path. A metadata busy flag can break registration
dependencies without declaring the owner ready.

The registered getters inspect ready bit four, call the respective wrapper
for null or not-ready metadata, then reload the same cell. Boolean's lazy
getter returns an existing owner or executes actual pool lookup `800607F4`
and metadata construction `80029E64`, raw-publishes that result and reloads it.
It does not register a newly allocated owner.

Memory metadata aliases
`IgbMetaFieldFactoryServices::metadata_slots[memory_reference]`; Boolean
aliases `metadata_slots[boolean]`. The shared registration slot helper and
enabled field factories observe these same owners. Missing state remains a
pending read, while represented null remains an original null pointer.

## Prototype publication and reflected virtuals

| Class | Prototype callback | Prototype cell / values alias | Plain metadata virtual `+58` | Plain prototype virtual `+5C` |
| --- | --- | --- | --- | --- |
| Memory reference | `8002B178` | `8056181C` / `memory_reference_prototype` | `80094068` | `80094070` |
| Boolean | `800388B0` | `80561E4C` / `boolean_prototype` | `80093E7C` | `80093E84` |

These callbacks use the native prototype pipeline: null-only gate, fresh pool
lookup, live metadata read, actual reflected factory `8006546C`, publication,
registry append, live canonical release, fresh registry count, ordinal store
and finalization. Prototype aliases belong to `IgbFieldValuesServices` and
remain separate from each class's metadata cell. Replacing a global during a
callback does not change an already captured append receiver; later original
reloads still observe the replacement.

The memory descriptor vtable is `80476088`; Boolean uses `80473E30`. Both have
the following original return-only object virtuals:

| Slot | Function |
| --- | --- |
| `+0C` | `800667A4` |
| `+24` | `800667B0` |
| `+2C` | `800667D0` |
| `+34` | `800667CC` |

Their `+58` operations read the live canonical class cell directly. The shared
reflected initializer still performs its actual member passes, physical
publication and reached field operations. The empty `+2C` body does not erase
those requirements. Memory's additional defaults and inherited method
replacements are documented in
[IGB_MEMORY_FIELD_DEFAULTS.md](IGB_MEMORY_FIELD_DEFAULTS.md).

## Scope

Registration, prototype callbacks, reflected object virtuals and the specialized
defaults caller compose independently. Unknown constructors, clone targets,
method operations and allocator/platform endpoints remain explicit reached
services. The separate lazy prototype getters `8002B210` and `80038948` are
not implemented merely by supporting their registration callbacks.

Build results, cold integration and original-instruction comparisons are
recorded in [VALIDATION.md](VALIDATION.md) after execution. Earlier integer or
object-reference validation applies to those measured profiles; adding these
two profiles does not transfer that evidence or establish complete game boot.
