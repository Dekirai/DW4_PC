# Native data-list registration

This module reconstructs the three registration bodies reached from the first
ThreadManager extension callback. Evidence is the supplied USA `main.dol`,
SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. It preserves the previously
allocated ThreadManager dependency metadata and composes the existing native
registration, callback, pool, metadata factory, and base reset implementations.

## Exact profiles

All three bodies call prerequisite guard `80021B94` before generic registrar
`80066204`. They pass abstract flag zero and native size `0x14`.

| Class | Canonical cell | Wrapper / body | Parent body / plain getter | Field callback | Probe | Defaults |
| --- | --- | --- | --- | --- | --- | --- |
| `igDataList` | `80561D3C` | `339EC` / `33A14` | `66B08` / `237D0` | `33A84` | `339AC` | `33AA4` |
| `igNonRefCountedObjectList` | `80561730` | `2966C` / `29694` | `33A14` / `24D1C` | `29704` | `29614` | `29724` |
| `igNonRefCountedThreadList` | `80561530` | `23F28` / `23F50` | `29694` / `23FDC` | `23FBC` | `23EB8` | zero |

Function addresses in the table have prefix `800`. None passes a trailing
callback. The thread-list extension table at `8055D080` contains `80023C40`
and a zero terminator; the other two have no extension table. The native
generic registrar builds its returned callback list, and the actual `66188`
wrapper handles deferred callbacks and cleanup.

Thread-list services resolve `80561530` directly through the shared
`IgbThreadDependenciesServices::thread_list` cell. Registration consequently
operates on the metadata previously allocated by ThreadManager defaults.
The two other cells are explicit shared `IgbRegistrationMetaSlot` bindings.
No internal-list profile or preconstructed ready class substitutes for these
data-list classes.

## Getters and publication

Full getters `33970`, `295D8`, and `23E7C` test their canonical pointer and
ready bit four. Null or not-ready values enter the original registration
wrapper, then reload the canonical cell. Field callbacks `33A84`, `29704`,
and `23FBC` call those corresponding full getters. Plain getters `24D1C`
and `23FDC` read the DataList and non-reference-counted object-list cells
without registering them.

DataList lazy getter `33934` allocates only on a known null pointer: read
metadata pool handle `805621F4`, call `607F4`, call actual metadata factory
`29E64`, raw-publish the result, and reload the cell. Thread-list lazy getter
`23E40` composes the already recovered dependency invocation. Neither tests
class readiness. Unknown state remains pending. Raw overwritten host owners
are retained without adding an original reference release.

## Physical layout probes

Every probe calls actual `6665C` on its represented 20-byte stack object.
That reset preserves the original physical read and write order for the
packed reference header. It does not initialize the list's member bytes.
The subsequent vtable stores and ArkCore capture differ:

| Probe | Stores before fresh ArkCore capture | Stores after capture |
| --- | --- | --- |
| `339AC` | none | `80472FA0` |
| `29614` | `80472FA0` | `80476E0C`, `80476DA8` |
| `23EB8` | `80472FA0`, `80476E0C`, `80476DA8` | `80476D44`, `80476CE0` |

After the remaining stores, each reads the captured ArkCore's `+0x394` byte
offset and returns the selected physical word. Offset zero observes the final
vtable; offset four observes the actual reset header. Offsets eight, twelve,
and sixteen observe untouched represented bytes. Unknown bytes fault when
read. Misaligned or out-of-profile offsets fault instead of reading beyond
the original local. No heap allocation, destructor, or member initialization
is added to the probes.

Pending ArkCore or offset bindings preserve the already executed stores.
Changing the current ArkCore after capture does not replace the captured
owner. A known null ArkCore faults only after the later original vtable stores.

## Validation and boundary

Synthetic tests cover original profiles and canonical aliases, prerequisite
ordering before ready/busy registrar gates, actual metadata construction with
an explicit reflected allocation endpoint, missing parent/default operations,
lazy and full getter suspension, raw publication lifetime, probe capture
prefixes, preserved local bytes, unknown physical reads, strict bounds, adapter
selection, and persistent reentrant faults. Original-DOL comparisons and the
cold startup integration are recorded separately in [VALIDATION.md](VALIDATION.md).

Parent defaults `33AA4` and `29724` are separate implementations described in
[IGB_DATA_LIST_DEFAULTS.md](IGB_DATA_LIST_DEFAULTS.md). Reached descriptor class
registrations, extension `23C40`, and native threads remain separate operations
supplied through the composed factories.
Completing these registration callers alone does not establish full startup
or playable gameplay.
