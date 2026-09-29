# Native field-type registration

`igb_field_type_registration` reconstructs the actual registration chain
requested when the object-field prototype getter reaches `800289F8`:

```text
800289F8 wrapper
  -> 80028A20 igObjectRefMetaField
     -> 80027360 igRefMetaField
        -> 8002A6D8 igMetaField
           -> 80066B08 igObject
```

The enabled DataList constructor also reaches `8002E390 -> 8002E3B8`,
`igIntMetaField`, whose parent is the same `igMetaField` class. It uses a
separate metadata cell, scalar layout probe and prototype callback; see
[IGB_INT_FIELD_REGISTRATION.md](IGB_INT_FIELD_REGISTRATION.md).

The memory-reference and Boolean profiles extend that same registration
framework with bodies `8002AEAC` and `80038828`. Their complete argument tables,
getters and distinct canonical cells are documented in
[IGB_MEMORY_FIELD_REGISTRATION.md](IGB_MEMORY_FIELD_REGISTRATION.md).

String and signed-byte descriptor classes add bodies `80025404` and
`80037D28`; their profiles and following clone dependencies are documented in
[IGB_STRING_FIELD_REGISTRATION.md](IGB_STRING_FIELD_REGISTRATION.md).

These are metadata registrations for the descriptor classes. They are separate
from constructing an individual descriptor and from publishing a shared field
prototype. In particular, metadata cell `805616E8` cannot substitute for the
object-field prototype at `805616EC`.

Evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. The source, profile constants and
synthetic fixtures are authored C++; original binary data and disassembly stay
outside the repository.

## Original registration profiles

Each wrapper calls native callback runner `80066188` with the corresponding
body. Each body passes the following exact arguments to native registrar
`80066204`. None of the eight represented bodies calls prerequisite guard
`80021B94`. The original four profiles are listed here; the memory-reference
and Boolean, string and signed-byte profiles have their own linked tables above.

| Argument | igMetaField | igRefMetaField | igObjectRefMetaField | igIntMetaField |
| --- | --- | --- | --- | --- |
| Wrapper | `8002A6B0` | `80027338` | `800289F8` | `8002E390` |
| Body | `8002A6D8` | `80027360` | `80028A20` | `8002E3B8` |
| Metadata cell | `805617BC` | `80561684` | `805616E8` | `805619E0` |
| Native size | `0x34` | `0x38` | `0x44` | `0x34` |
| Abstract flag | `1` | `1` | `0` | `0` |
| Parent body | `80066B08` | `8002A6D8` | `80027360` | `8002A6D8` |
| Parent getter | `800237D0` | `80021D70` | `80025494` | `80021D70` |
| Field callback | `8002A744` | `800273CC` | `80028A94` | `8002E420` |
| Layout probe | Null | Null | `80028934` | `8002E308` |
| Defaults | `8002A764` | `800273EC` | `80028AB4` | Null |
| Extra-callback table | `8055D290` | `8055D158` | `804640C4` | `8055D3C4` |
| Extra callbacks | `8002A0A8`, zero | `80027454`, zero | `8002A0A8`, `80028B64`, zero | `8002E440`, zero |
| Trailing callback | Null | Null | Null | Null |

The two abstract parents skip the layout-probe call through the original
registrar flag test. The object-reference and integer classes reach their actual
probes; a constant vtable result is not supplied by this module. Specialized defaults
and probe implementations are composed through their exact service identities.
Unbound operations retain their reached continuation.

The generic registrar still resolves the metadata pool before checking the
ready or busy flags. A published busy owner can therefore break a legitimate
registration dependency without being declared fully initialized. Missing
canonical state, parent bodies, allocation services or specialized defaults
do not create ready metadata. See [IGB_TYPE_REGISTRATION.md](IGB_TYPE_REGISTRATION.md).

## Canonical globals and getters

`IgbFieldTypeRegistrationServices::base_metadata` represents `805617BC`, and
`reference_metadata` represents `80561684`. Object metadata always uses
`IgbMetaFieldFactoryServices::metadata_slots[object]`, the existing canonical
`805616E8` cell used by the object-field factory. Integer metadata similarly
uses `metadata_slots[signed_int]`, canonical `805619E0`. Memory-reference and
Boolean metadata use `metadata_slots[memory_reference]` (`80561818`) and
`metadata_slots[boolean]` (`80561E48`). String and signed byte use
`metadata_slots[string]` (`805615B4`) and `metadata_slots[signed_byte]`
(`80561E1C`). The shared slot helper returns these
owners without introducing another metadata cache.

| Behavior | igMetaField | igRefMetaField | igObjectRefMetaField | igIntMetaField |
| --- | --- | --- | --- | --- |
| Allocation-only getter | `8002A638` | Not represented | `800288BC` | Not represented |
| Full getter | `8002A674` | `800272FC` | `800288F8` | `8002E2CC` |
| Full-getter alias | `8002A744` | `800273CC` | `80028A94` | `8002E420` |
| Plain parent getter | `80021D70` | `80025494` | Not represented | Not represented |

Allocation-only getters return an existing nonnull owner immediately. A known
null cell captures the current metadata pool handle, executes actual
`800607F4`, invokes native metadata factory `80029E64`, publishes its captured
result with a raw store and reloads the cell. Callback replacement does not
cancel that store. A displaced owner remains retained without a guest reference
decrement. The getter does not register the new metadata.

Full getters inspect ready bit `4`; null or not-ready metadata calls the
corresponding wrapper and callback runner before reloading the canonical cell.
They do not check readiness again after that call. Plain getters only read their
original parent cells. An unrepresented cell pauses at the read, while a
represented null return remains null. Selected pool and callback operations
survive suspension, and completed results retain their identity.

## Integration and evidence limits

The function adapter handles only the profiles' exact wrappers and bodies. The
type-registration adapter binds their parent-body calls and represented metadata
getter addresses. The field-factory adapter connects a represented enabled
descriptor factory to these wrappers. Adapters use weak service ownership
until selection and retain the selected services while an operation is pending.

This module intentionally leaves callbacks `80027454`, `80028B64`,
`8002E440`, `8002B178`, `800388B0`, `80025628` and `80037DB0` available for their actual prototype
construction implementations. A successful type
registration body returns a deferred list; it does not imply that those
callbacks or the surrounding cold bootstrap have completed. The existing
object-field prototype getter `80028BFC` remains a separate descriptor lookup
in [IGB_LIST_DEFAULTS.md](IGB_LIST_DEFAULTS.md).

Permanent tests check exact profile arguments and callback tables, shared
cells, metadata construction, the three-class parent chain, gates, abstract
probe skipping, explicit defaults/probe boundaries, native callback append,
the getter adapters, lazy raw-store ownership, retained operations and
recursive failure. Isolated fixtures prepare physical allocations, internal
field-list allocation and selected layout/name endpoints. Those preparations
are not evidence that their omitted original bodies execute. The composed
cold path, local build results and bounded original-instruction comparisons
are recorded separately in [VALIDATION.md](VALIDATION.md).
