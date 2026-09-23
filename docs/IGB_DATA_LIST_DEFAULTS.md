# Data-list class defaults

This module reconstructs the original USA `igDataList` defaults at
`80033AA4` and `igNonRefCountedObjectList` defaults at `80029724`. Evidence
was read from the supplied `sys/main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. The implementation is authored
native C++; original instructions are research and validation evidence.

The canonical class cells belong to `IgbDataListRegistrationServices`:
`80561D3C` for `igDataList` and `80561730` for its object specialization.
These differ from the existing internal field-list classes and globals.

## DataList: 80033AA4

The caller captures its metadata receiver and its original field count, then
uses the native `80065924` batch and `800659C0` schema implementations.

| Local field | Factory | Name | Descriptor global | Member offset |
| --- | --- | --- | --- | --- |
| 0 | `8002E1AC` | `_count` | `80561D40` | `8` |
| 1 | `8002E1AC` | `_capacity` | `80561D44` | `12` |
| 2 | `8002AC34` | `_data` | `80561D48` | `16` |

The function tables begin at `80467478`, names at `80467484`, globals at
`80467490`, and offsets at `8046749C`. After all three constructors return,
the caller reloads local field `first + 2` and retains that receiver while
calling the unsigned-character prototype getter `80023358`. It stores the
returned nonowning pointer at `+3C`, then stores `1` at `+35`, `+40`, and
`+41`, in that order. It does not set `+34`, `+38`, or `+42`.

`80023358` tests the distinct prototype global `805614D4`. On a represented
null it invokes wrapper `8002320C`, which calls native `80066188` with
registration body `80023234`, and finally reloads the prototype cell. The
registration body and its eventual prototype creation remain explicit
required operations. A signed-byte prototype does not satisfy this lookup.
A represented null returned after registration is the original null result;
an unrepresented cell suspends execution.

## NonRefCountedObjectList: 80029724

The specialization captures its canonical metadata and searches inherited
fields for `_data` through `800658F8` / `80053910`. The name descriptor is
the existing canonical `805617C4`. Search retains the selected registry and
name descriptor, reloads the live count/backing at the original reads, and
uses the existing native string comparison. The following identity search
`80053998` captures the owner's then-current count and backing. Physical
field access is bounded to 262,144 represented slots after the original
32-bit index scaling. For example, original index `80000002` scales to byte
offset `8` and selects represented slot `2`; discarded high index bits do
not establish an out-of-range physical access.

The original clone virtual `+54` receives argument `1`. Its retained result
is passed through the separate object-prototype getter `80028BFC`:
canonical `805616EC`, wrapper `800289F8`, and native callback body
`80028A20`. The specialization then performs these stores in order:

1. `clone + 3C = returned object-field prototype`.
2. `clone + 38 = 0`.
3. `clone + 42 = 0`.
4. `clone + 1C = 80561734`.

It composes the existing `IgbFieldReplaceInvocation` for actual `8006588C`,
including finalization, global publication, retain/release, allocator size
queries, and physical list replacement. Finally it decrements the clone's
live packed reference word and calls `80066E1C` only when its low 23 bits
become zero. Parent descriptors and internal-list globals stay distinct.

## Composition and validation

`IgbDataListDefaultsServices::lists` shares the existing schema, field values,
canonical name descriptor, clone dispatch, and destructor services. The
existing native memory-field clone adapter can be bound there; its enabled
registration and reflected-clone dependencies remain required. No prepared
clone result is supplied by this module.

Physical descriptor bindings are optional only for canonical-only test
fixtures. Selecting the descriptor captures its binding without accessing
the destination allocation. The prototype getter runs first, even when that
allocation is malformed; validation begins at the original first `+3C` store.
Once reached, stores validate the captured allocation identity,
use explicit big-endian physical writes, and update canonical members after
each successful store. A nonnull element needs its real arithmetic identity
before a physical pointer can be published. Late bindings preserve completed
constructor and getter work. Null results, short storage, and callback faults
retain the original committed prefix. Overwritten guest references are not
silently released on abandonment.

The asset-free tests exercise the real batch/schema and replacement callers
with clearly prepared constructor, clone, string-pool, and allocator leaves.
They cover both exact tables, wrapping original indices, distinct prototype registration bodies,
unknown versus null cells, receiver capture across suspension, physical
pointer binding, ordered partial stores, inherited replacement ownership,
pending destruction, null-clone ordering, getter effects before malformed
destination faults, invalidated physical bindings,
and recursive continuation faults. These caller tests do not establish the
unrecovered field registration or enabled reflected-clone bodies.
