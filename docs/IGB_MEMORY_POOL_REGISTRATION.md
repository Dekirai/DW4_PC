# Abstract memory-pool metadata

Native startup reaches `igMallocMemoryPool` registration after enum metadata and
its two list dependencies. The malloc wrapper/body `80039D6C / 80039D94` uses
canonical cell `80561FB0` and enters the actual `igMemoryPool` parent before
its own probe or defaults. Both bodies call the generic registrar directly;
neither inserts the prerequisite guard used by several other class families.

The enclosing malloc profile preserves size `0xA0`, flag zero, parent
`8002BF70`, plain parent getter `80038F58`, field callback `80039E08`, probe
`80039CC0`, defaults `80039E28` and extra callbacks `80030D50 / 80026098`.
The profile is separate from implementations of those later endpoints.

## Abstract parent and defaults

| Property | Original identity |
| --- | --- |
| Class / canonical metadata | `igMemoryPool` / `805618B4` |
| Wrapper / body | `8002BF48 / 8002BF70` |
| Allocation-only / full / plain getter | `8002BED0 / 8002BF0C / 80038F58` |
| Size / flag | `0x70 / 1` |
| Parent body / getter | `80066B08 / 800237D0` |
| Field callback / defaults | `8002BFD8 / 8002BFF8` |
| Probe / extra / trailing callbacks | None |

The allocation-only getter tests known null, selects the live metadata pool
handle, calls the existing metadata factory and freshly reloads the published
cell. Full lookup separately tests registration bit four; plain lookup performs
no registration. Unknown storage remains distinct from a represented null.

Defaults capture metadata and its original field count, then construct fourteen
descriptors through the existing batch caller. Twelve signed 64-bit fields cover
local allocated, freed and net-allocated totals, their high-water marks, the
corresponding overhead totals, and allocation/free/net counts. Their offsets
are `0x08..0x60` in eight-byte steps. Two Boolean descriptors at `0x68 / 0x69`
control local and global statistics; their separately selected default calls
write true. The fourteen global cells occupy `805618B8..805618EC`.

The caller preserves each Boolean descriptor's reached lookup and physical
vtable selection, followed by its retained virtual `+8C` call. Pending polling
does not add an outer receiver read. The later schema uses the captured metadata
and first index. Finally, defaults reload canonical `805618B4` and store
`8002C0A0` at that fresh owner's `+3C`, physically before canonical publication
when typed storage is represented. This is a deferred factory redirect to the
arena-pool metadata getter; defaults do not invoke it or initialize an allocator.

## Signed 64-bit descriptor dependency

`igLongMetaField` uses factory `8002CEA8`, wrapper/body `8002CFA8 / 8002CFD0`,
canonical metadata `8056198C`, prototype cell `80561990`, callback `8002D05C`
and vtable `80472100`. Its metadata has size `0x34`, flag zero, the existing
base-field parent and no new defaults. The factory always registers and reflects;
it has no disabled-core direct-constructor branch. Probe `8002CF1C` performs
base reset, captures ArkCore, publishes the derived vtable, captures the selected
local word and restores the base vtable before name cleanup.

Size and alignment leaves both return eight. Native value handling uses an
explicit eight-byte buffer and the existing default-copy operation, preserving
both words and unknown bytes on null-source preparation. Host `long` width is
irrelevant. The plain prototype getter shares the existing canonical cell.
The separate lazy prototype getter `8002D0F4` is outside this implemented slice.

The shared prototype callback now captures optional physical reference ownership
as soon as reflection returns, before publication and registry lookup. Actual
retain uses that captured allocation. Creator release reloads the canonical
prototype independently, then performs physical decrement/store and a separate
fresh low23 test. This repair preserves later fresh registry, ordinal and
finalization selection; it does not add new reads after a selected destructor.

## Unsigned 64-bit dependency

The separate `igUnsignedLongMetaField` profile uses factory `80022450`,
wrapper/body `80022634 / 8002265C`, metadata `805614A8`, prototype `805614AC`
and vtable `80476F60`. The factory captures its pool and reads the ArkCore gate:
the enabled branch registers then reflects using freshly loaded metadata;
the disabled branch allocates `0x34` bytes and runs the original constructor
and freshly selected virtual tails. The class body enters the generic registrar
directly. Its size is `0x34`, with the existing base-field parent and no defaults.

Probe `800225AC` uses specialized reset `80075C84` before capturing ArkCore.
It captures the selected local word, restores the derived and base vtables,
and performs the original fresh string cleanup. Callback `800226E4` shares the
existing physical prototype-reference implementation while keeping its metadata
and prototype cells distinct from signed-long. Plain getters and the null-only
prototype lookup retain their different registration conditions.

Size and alignment are eight. Existing `80063D04` handles the represented
eight-byte source without host-width or byte-order conversion. The standalone
`80075D1C` setter and clone `800224F4` are separate entries; this profile alone
does not implement them or scalar object-member operations.

## Concrete malloc layout probe

`IgbMallocMemoryPoolProbeInvocation` implements `80039CC0` on its represented
`0xA0` local allocation. The original `8008B264 / 80058ABC / 8006665C` chain
resets the packed header, stores parent vtable `80471CEC`, stores concrete
vtable `80473790`, then initializes the `+74` and `+94` pointers to null.
Only sixteen local bytes become known. The local remains separate from runtime
allocator instances and carries no synthetic typed object binding.

The probe captures ArkCore after construction and retains its `+394` offset
owner across a pending read. It captures the selected local word, restores the
concrete vtable and processes `+94` before `+74`. Each member lookup is fresh;
the second occurs after the first selected destruction completes. Reference
handling decrements and stores the physical full word, then separately rereads
low23 before selecting `80066E1C`. Member pointers remain unchanged.

Cleanup uses explicit numeric-to-typed object bindings and retains the selected
destructor across suspension. Its polling adds no outer receiver read. Missing
bindings remain pending; changed captured physical owners fail at their reached
access. The legacy destruction endpoint still has no borrowed fault-check API,
so effects inside a callback that catches caller reentry remain outside this
adapter's guarantee. Unfinished invocations retain byte owners without retaining
callback closures inside their owning services.

The local profile bounds selected offsets to aligned words through `0x9C`.
Unknown padding is preserved. The type adapter obtains local storage once,
returns the captured layout word and lets the generic registrar continue to
malloc defaults `80039E28`.

## Scope and provenance

The supplied USA DOL is the primary source, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. IDA and direct DOL checks establish
the exact profiles, tables and callback order. Research artifacts remain local.
Canonical metadata mappings and legacy lower parent-guard limits stay explicit.
Schema tables contain the fourteen recovered rows. A mutated live list count
that drives the original beyond those tables is rejected at the native table
boundary; adjacent DOL bytes are not represented as additional schema rows.
Concrete malloc/arena defaults, runtime pool allocation, tagged-table production,
remaining scalar member operations and complete startup are separate requirements.
Class metadata alone is not an initialized memory manager or playable game.
Measured results belong in [VALIDATION.md](VALIDATION.md).
