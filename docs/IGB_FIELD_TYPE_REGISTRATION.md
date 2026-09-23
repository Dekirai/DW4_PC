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
`80066204`. None of these three bodies calls prerequisite guard `80021B94`.

| Argument | igMetaField | igRefMetaField | igObjectRefMetaField |
| --- | --- | --- | --- |
| Wrapper | `8002A6B0` | `80027338` | `800289F8` |
| Body | `8002A6D8` | `80027360` | `80028A20` |
| Metadata cell | `805617BC` | `80561684` | `805616E8` |
| Native size | `0x34` | `0x38` | `0x44` |
| Abstract flag | `1` | `1` | `0` |
| Parent body | `80066B08` | `8002A6D8` | `80027360` |
| Parent getter | `800237D0` | `80021D70` | `80025494` |
| Field callback | `8002A744` | `800273CC` | `80028A94` |
| Layout probe | Null | Null | `80028934` |
| Defaults | `8002A764` | `800273EC` | `80028AB4` |
| Extra-callback table | `8055D290` | `8055D158` | `804640C4` |
| Extra callbacks | `8002A0A8`, zero | `80027454`, zero | `8002A0A8`, `80028B64`, zero |
| Trailing callback | Null | Null | Null |

The two abstract parents skip the layout-probe call through the original
registrar flag test. The object-reference class reaches its actual probe;
a constant vtable result is not supplied by this module. Specialized defaults
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
`805616E8` cell used by the object-field factory. The shared slot helper returns
these owners without introducing another metadata cache.

| Behavior | igMetaField | igRefMetaField | igObjectRefMetaField |
| --- | --- | --- | --- |
| Allocation-only getter | `8002A638` | Not represented | `800288BC` |
| Full getter | `8002A674` | `800272FC` | `800288F8` |
| Full-getter alias | `8002A744` | `800273CC` | `80028A94` |
| Plain parent getter | `80021D70` | `80025494` | Not represented |

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

The function adapter handles only the six exact wrappers and bodies. The
type-registration adapter binds their parent-body calls and ten metadata
getter addresses. The field-factory adapter connects a represented enabled
descriptor factory to these wrappers. Adapters use weak service ownership
until selection and retain the selected services while an operation is pending.

This module intentionally leaves callbacks `80027454` and `80028B64` available
for their actual prototype construction implementations. A successful type
registration body returns a deferred list; it does not imply that those
callbacks or the surrounding cold bootstrap have completed. The existing
object-field prototype getter `80028BFC` remains a separate descriptor lookup
in [IGB_LIST_DEFAULTS.md](IGB_LIST_DEFAULTS.md).

Permanent tests check exact profile arguments and callback tables, shared
cells, metadata construction, the three-class parent chain, gates, abstract
probe skipping, explicit defaults/probe boundaries, native callback append,
all ten getter adapters, lazy raw-store ownership, retained operations and
recursive failure. Isolated fixtures prepare physical allocations, internal
field-list allocation and selected layout/name endpoints. Those preparations
are not evidence that their omitted original bodies execute. The composed
cold path, local build results and bounded original-instruction comparisons
are recorded separately in [VALIDATION.md](VALIDATION.md).
