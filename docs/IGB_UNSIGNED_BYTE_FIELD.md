# Unsigned-byte field class

The native implementation reconstructs `igUnsignedCharMetaField` from the USA
`main.dol`, SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original binaries,
data and disassembly remain local research inputs.

| Role | Original address |
| --- | --- |
| Metadata / prototype cell | `805614D0` / `805614D4` |
| Vtable / instance size | `80470A44` / `34` hexadecimal bytes |
| Registration wrapper / body | `8002320C` / `80023234` |
| Layout probe | `80023180` |
| Full getter / callback alias | `80023144` / `800232A0` |
| Plain metadata / prototype getters | `80093A20` / `80093A28` |
| Factory / prototype initializer | `8002310C` / `800232C0` |
| Null-only prototype getter | `80023358` |

## Registration and construction

Registration calls prerequisite `80021B94` before capturing the generic
registrar's arguments. The native invocation shares its live once guards and
retains pending callbacks. The class derives from `igMetaField`, inherits its
fields, has no local defaults callback, and appends prototype callback
`800232C0`. The full getter checks the current metadata's ready bit; the plain
getters only read their canonical cells.

Factory `8002310C` unconditionally registers the class, reloads its metadata
cell and calls reflected factory `8006546C` with the captured pool. It does
not read ArkCore's enabled byte or take the other scalar factories' direct
constructor branch. No specialized raw unsigned-byte constructor, reset or
copy constructor is invented. The shared base constructor/reset remain usable
where the original caller explicitly invokes them.

Probe `80023180` calls base reset `800638E0` on its actual local storage. It
then captures ArkCore, writes the derived vtable, reads captured core `+394`
and loads the selected local word. Cleanup republishes the derived table and
then the direct base table `80471914`. The wrapped `local + 0C` null check,
whole-word string decrement and conditional `8007381C` release retain their
original order. Padding is not initialized merely to make the probe readable.

## Prototype and virtual operations

Prototype initialization resolves the selected pool, reloads metadata, and
performs actual reflected allocation and initialization. It publishes the
result before the null test, appends it to the prototype registry, reloads the
global for release, and invokes destruction only when the low 23 reference
bits become zero. Subsequent live registry/global reads assign the low 16 bits
of `count - 1` as the ordinal before finalization.

The DataList getter `80023358` and field-value services alias the same prototype
cell. A retained getter therefore sees publication without receiving an
independently prepared prototype.

Size and alignment virtuals return one. Default initialization, setters and
raw member-copy adapters use the recovered scalar operations with their
original widths and offsets. Object hooks `+14`, `+18` and `+48` are the
verified return-only entries. Clone virtual `+54` points directly to
`800680B4`; its adapter does not add registration or an ArkCore gate. Memory
field `80476088` shares those object hooks and now reaches its existing
reflected clone/member-copy path during object-list specialization.

## Integrated continuation and evidence

The retained cold-bootstrap test executes the actual registration and prototype
lifecycle, then resumes the existing DataList getter through its shared cell.
Native reflected child-list allocation and memory-field cloning complete
DataList, the object-list specialization and the thread list. Nineteen types
and nine prototypes are published before the next missing registration,
`igThread` wrapper `80023C40`. The bootstrap snapshots remain seven types and
one prototype; no additional ready class is supplied to bypass that sequence.

Five independent original-code groups compare 760 observations exactly across
49,211 instructions, including explicit bounded failures. The original inputs
and expectations were sealed before native comparison. Focused tests cover
the direct clone virtual and continuation ownership separately. See
[VALIDATION.md](VALIDATION.md) for prepared endpoints, compared state and limits.

Unsigned-byte text parsing and formatting (`80075790` / `800757E8`) remain
outside the reached startup path. This module does not claim those operations,
complete engine startup, original gameplay, or a playable application.
