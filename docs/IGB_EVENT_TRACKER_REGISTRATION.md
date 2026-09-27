# EventTracker registration and layout probe

The native startup chain reaches `igEventTracker` through the malloc pool's
deferred callback `80030D50`. `IgbEventTrackerRegistrationServices` shares the
existing memory-pool services owner and its canonical metadata cell `80561BA4`.
The allocation-only getter `80030C50` remains in that module. There is no second
EventTracker metadata cache or cleanup-object registry.

## Registration and getters

Wrapper `80030D50` composes `80066188(80030D78)`. The direct body executes
prerequisites `80021B94` before capturing the generic `80066204` arguments.

| Argument | Original value |
| --- | --- |
| Class name / size / flag | `igEventTracker` / `0x2530` / `0` |
| Parent body / getter | `80066B08` / `800237D0` |
| Field callback / probe | `80030DF0` / `80030CC8` |
| Defaults / trailing callback | `80030E10` / null |
| Extra callbacks, in order | `800249F4`, `80038478`, `8002D584`, `800368D4`, `800364C4`, `80038244`, `80026098`, null |

The full getter `80030C8C`, also reached by field callback `80030DF0`, registers
on null metadata or when class flag mask `0x4` is clear, then reloads the canonical
pointer. Plain getter `80093D1C` only reads the cell. Unknown cell contents stay
pending; known null is a valid plain-getter result. Class flags use the existing
canonical metadata representation, without adding physical flag mirroring.

The new adapters reuse the existing generic registrar and callback driver.
Defaults `80030E10` remains an explicit missing operation. Its 33 descriptors,
array-field dependencies and further class registrations are not implemented
by this registration/probe slice.

## Original local construction

Probe `80030CC8` uses a `0x2530`-byte local within the original `0x2540`-byte stack
frame. Native `IgbBootstrapBaseInvocation` executes the original `8006665C`
header reset, including its old packed-header read and reached failure prefixes.
The probe captures ArkCore **before** storing derived vtable `80472A58` and
zeroing the following pointers:

`08`, `30`, `34`, `38`, `3C`, `40`, `44`, `48`, `4C`, `50`, `54`, `5C`.

It then reads the captured ArkCore's `+394` offset and retains the selected
local word. On an ordinary successful unknown-padding fixture, exactly 56
local bytes are known. Unknown ArkCore stops after base construction; known
null ArkCore is dereferenced only after the derived stores. Other local bytes
remain untouched. This stack probe does not construct a runtime EventTracker
or execute its separate runtime virtual `8004BD50`.

## Cleanup and suspended operations

The probe calls the `800261F4(local, -1)` path, which restores the derived
vtable and freshly reads members in this order:

`5C`, `54`, `50`, `4C`, `48`, `44`, `40`, `3C`, `38`, `34`, `30`, `08`.

Each wrapped zero member address skips its load. Nonnull members resolve through
the existing explicit typed-object mappings. Cleanup decrements and stores the
physical full32 reference word, then separately rereads low23 before selecting
destruction `80066E1C`. Member pointers remain unchanged. Later member reads
observe mutations from earlier destruction callbacks.

The selected destruction continuation and object owners survive suspension;
polling or completion does not add an outer reference read. The probe returns
its previously captured word after all cleanup. The exposed operation implements
only the caller's nondeleting `-1` path; other destructor flags and `80066DFC`
deallocation remain separate work.

Local selection is explicitly bounded to aligned offsets through `0x252C`.
Missing physical mappings stay pending, and changed captured allocation/byte
identities fail at their reached accesses. Unfinished invocations retain owners
without retaining callback closures in their service ledger. Legacy registrar,
callback and destruction services retain their existing fault-check boundaries;
this module does not add universal guards inside already selected children.

## Provenance and remaining work

The supplied USA DOL is the primary source, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. IDA and independent DOL decoding
verify registration, lookup and probe instructions, cleanup, tables and original
dependencies. Original bytes and generated analysis stay in ignored research
storage. See [VALIDATION.md](VALIDATION.md) for measured results and limits.

The reached defaults, actual runtime instances, complete original startup,
rendering, audio and playable gameplay remain required. Class metadata and a
successful layout probe do not establish those features.
