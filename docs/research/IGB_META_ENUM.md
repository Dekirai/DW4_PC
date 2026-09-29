# Native enum metadata registration

`igb_meta_enum` reconstructs the `igMetaEnum` registration body `8002AAFC`,
wrapper `8002AAD4`, metadata getters `8002A998`/`8002AB74`, layout probe
`8002A9D4`, and defaults caller `8002AB94`. Evidence is the supplied USA
`main.dol`, SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.
The implementation is authored C++; original instructions and comparison
corpora remain local.

## Registration and lookup

The body first executes the existing native `80021B94` prerequisite guard,
then invokes `80066204` with the canonical `80561808` metadata cell. Its exact
arguments are flag zero, name `igMetaEnum`, size `0x14`, parent registration
`80066B08`, parent getter `800237D0`, field callback `8002AB74`, probe
`8002A9D4`, and defaults `8002AB94`. There is no trailing callback. The table
at `804649A8` contains `80024C64`, `80036684`, then its zero terminator.

The existing registrar performs metadata-pool selection and construction,
parent attachment, name ownership, publication and deferred callback handling.
This module does not replace an unbound factory or parent with ready metadata.
The prerequisite guard still runs when metadata is already ready or busy.

`8002A998` is a getter, not an object constructor. It reads `80561808`, calls
the registration wrapper when that pointer is null or flag `4` is absent,
then reloads the same cell. Unknown storage suspends; represented null enters
registration. The getter retains a selected registration continuation even if
the original metadata becomes ready while that continuation is pending.
`8002AB74` forwards to this getter. There is no separate lazy-allocation path.

## Defaults and schema

`8002AB94` captures its metadata receiver and original field count before
calling the native field batch with three factories:

| Factory | Name | Offset | Descriptor global |
| --- | --- | --- | --- |
| `800251EC` | `_name` | `+08` | `8056180C` |
| `8002879C` | `_names` | `+0C` | `80561810` |
| `8002879C` | `_values` | `+10` | `80561814` |

The physical tables occupy `804649C0`, `804649CC`, `804649D8` and `804649E4`.
After appending the descriptors, the caller selects the field at captured
count plus one, invokes `80024B94`, and writes its returned object type to
that captured field's `+38`. It then reloads the field at count plus two,
invokes `800365B4`, and writes that result. Finally it runs the existing
`800659C0` schema caller with the captured receiver/count and the three tables.
There are no extra string setters or reference-flag assignments in this body.

Both type getters remain explicit required operations with their original
function identities. Pending callbacks are retained, and later changes to the
global enum metadata cell do not replace the captured defaults receiver.
Missing type identity is never converted to null. When a destination field
has physical storage, the returned numeric pointer or an explicit canonical
pointer-list binding must exist before publishing its physical `+38` word.
Canonical weak object-type identity and that word are updated together.
Known null stores zero; conflicting numeric identities fail.

## Layout probe

`8002A9D4` uses an explicit `0x14`-byte local allocation. It calls native
`8006665C`, preserving the original read/modify/write behavior of the old
reference word. After that reset it captures ArkCore `80562100`, installs
vtable `8047617C`, and clears local words `+08`, `+0C` and `+10`. Only then
does it read the captured core's `+394` offset and fetch the selected local
word. It reinstalls the same vtable before cleanup. A null core therefore
faults after the derived member stores; missing core storage suspends before
those stores.

The probe supports aligned reads wholly within the original local object.
It does not manufacture a vtable return value, read outside that object, or
invent initialized old stack bytes. Its ordinary constructor leaves all three
owned pointers null without any intervening callback. The first cleanup uses
register-known null rather than reloading `+10`; altered nonnull `+0C`/`+08`
members are explicitly outside this inline-construction profile and fail.
General enum allocation, nonnull destruction and enum value operations are
separate work.

## Validation scope

Permanent tests exercise exact registration arguments and prerequisites,
unknown/null/ready lookup state, retained registration callbacks, native local
probe writes and fault prefixes, complete three-field batch/schema execution,
getter suspension, captured receiver/type identity, numeric pointer binding,
null types, recursive misuse, and adapter dispatch/lifetime. Prepared field
factories and object-type getters are identified in those tests; they do not
prove those bodies were recovered. Local build and original-data integration
results belong in [VALIDATION.md](VALIDATION.md). This milestone does not
establish complete game startup or playable native gameplay.
