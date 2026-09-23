# Native integer-field registration

The enabled `igDataList` field factories require actual `igIntMetaField`
registration, rather than the disabled constructor used during fundamental
bootstrap. The authored implementation extends the existing field registration,
probe, prototype and initialization modules with this distinct class profile.

Evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`: `8002E2CC–8002E504`, constructor
reset `800535B8`, and vtable `80475B60`. Original instructions remain local;
the committed source and fixtures are authored C++.

## Registration and canonical state

Wrapper `8002E390` executes `66188(8002E3B8)`. The body has no prerequisite
guard. It passes canonical metadata cell `805619E0`, name `igIntMetaField`,
flag zero, size `0x34`, parent body `2A6D8`, plain parent getter `21D70`, field
callback `2E420`, probe `2E308`, null defaults and trailing callback, and
extension table `8055D3C4 = [2E440, 0]` to the existing native registrar.
Callback `2E420` calls full getter `2E2CC`, which registers only when the cell
is null or its ready bit is clear, then reloads the canonical cell.

The class metadata aliases `IgbMetaFieldFactoryServices::metadata_slots` at
the signed-integer index. Prototype callback `2E440` publishes into the existing
`IgbFieldValuesServices::signed_int_prototype` cell `805619E4`; it does not
create a second cache. The existing plain descriptor getter `94028` therefore
observes the same publication during finalization.

## Exact layout probe and reflected construction

Probe `2E308` resets the represented `0x34`-byte local through actual `535B8`,
captures fresh ArkCore after reset, reads its `+394` offset, and saves the
selected physical local word. It then stores derived vtable `80475B60` and
base vtable `80471914` before releasing the live `+0C` name string. It has no
object-reference cache at `+40` and no reference-base vtable store.
Missing state suspends at its reached read; bounds, uninitialized bytes and
represented null core faults preserve the completed prefix. A pending string
release retains its selected header and cannot repeat the decrement.

Vtable `80475B60` object virtuals `+0C`, `+24`, `+2C`, and `+34` are verified
return-only functions `667A4`, `667B0`, `667D0`, and `667CC`. Virtual `+58`
is `94020`, a plain read of metadata `805619E0`. These bindings let the generic
reflected initializer construct the class using its actual inherited schema.
An integer instance does not run the object-reference field's buffer tail.

Prototype callback `2E440` shares the recovered prototype algorithm: null-only
prototype test, live metadata pool selection, reflected allocation, raw canonical
publication, registry append/retain, fresh-global packed-reference release,
fresh registry count and prototype ordinal, then fresh-global finalization.
It returns no deferred callback list. Its separate lazy prototype getter
`2E4D8` is not needed or implemented by this registration change.

## Scope

Focused tests cover the added profile, canonical aliasing, gates, deferred
callback, full getter, scalar probe and cleanup, prototype publication, and
verified initialization virtuals. The cold-bootstrap test composes actual
reflected integer metadata, prototype and the first two DataList descriptors.
Original-comparison results and remaining boundaries are recorded in
[VALIDATION.md](VALIDATION.md). This class does not complete DataList's memory
field, byte-element prototype, later list specialization, native thread startup
or gameplay.
