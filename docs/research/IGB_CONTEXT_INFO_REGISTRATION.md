# Context extension information registration

This module reconstructs original `80037330 / 80037358` for `igContextExtInfo`.
It uses the existing native registrar, field constructors, schema operations and
canonical metadata/prototype owners. Its metadata cell `80561DE8` is distinct
from the context-info list cell `80561DE4` and the extension list cell `80561DE0`.

The original profile has flag zero, native size `0x14`, parent registration
`80066B08`, parent getter `800237D0`, field callback `800373CC`, layout probe
`80037268` and defaults callback `800373EC`. Prerequisites `80021B94` precede
the generic registrar `80066204`; the returned extra callback is `8002A0A8`.
The full getter `8003722C` conditionally registers and then reloads the canonical
metadata. Plain getter `80093EE0` only reads that cell.

## Original defaults and shared dependencies

Defaults capture the metadata and its initial field count, then append all three
descriptors using actual `80065924` before customizing any of them:

| Factory | Name | Global | Member offset | Customization |
| --- | --- | --- | --- | --- |
| `8002879C` | `_pMeta` | `80561DEC` | `08` | Actual `80029F84` metadata at descriptor `+38`; preserve `+34` |
| `800251EC` | `_pName` | `80561DF0` | `0C` | Actual `80071694(NULL)`, then descriptor byte `+35 = 1` |
| `8002AC34` | `_pVendorName` | `80561DF4` | `10` | Actual `80037E48` prototype at descriptor `+3C` |

Each field is selected freshly from the captured metadata's current field list.
Once selected, its identity survives its dependent call. Physical stores use
the existing allocation/address bindings and cannot publish a host pointer.
Schema `800659C0` runs only after these customizations finish.

`80071694` first reads string-manager presence, then captures the default buffer
at descriptor `+20`. It retains the initial manager branch across suspension,
and reloads the manager for interning. Even null input preserves this lazy
manager path. It releases the old string using its full reference word and
waits for any reached release callback before publishing null. An enclosing
fault check prevents later null, flag or schema stores after caught reentry.

The signed-byte getter aliases the existing `values->signed_byte_prototype`
cell `80561E20`. A known-null prototype invokes wrapper `80037D00` and reloads
that same cell. It must not return the separate class metadata at `80561E1C`.
Unknown cells remain pending; no ready dependency is fabricated.

## Layout probe

Probe `80037268` uses a `0x14`-byte local allocation. It executes base reset
`8006665C`, captures ArkCore, stores vtable `804741D4`, and clears object/string
members `+08 / +0C`. Member `+10` stays untouched. It reads the word selected by
the captured core's `+394` offset before restoring the vtable. The represented
profile accepts aligned offsets `0, 4, 8, 12, 16`; unavailable bytes fail at their
reached access instead of becoming zero.

The original string cleanup tests a retained literal null. The object cleanup
freshly reads local `+08`, decrements its full reference word and tests freshly
read low 23 bits before the reached destructor `80066E1C`. The selected probe
result stays captured across suspended destruction. A wrapped null address for
the local `+08` cell skips this cleanup exactly as the original branch does.

## Scope

This is class registration and default configuration, not a complete runtime
context or the factory/initializer for context-info instances. Existing lower
registrar, allocation and string services retain their own documented scope.
Canonical descriptor reference/name comparisons do not establish equality of
every optional raw descriptor word; the remaining physical synchronization work
is documented in [IGB_CONTEXT_REGISTRATION.md](IGB_CONTEXT_REGISTRATION.md).

Primary evidence is the supplied USA DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, with IDA output checked against its
original bytes. Local research artifacts remain ignored. See
[VALIDATION.md](VALIDATION.md) for measured integration and comparison results.

The retained cold startup now completes both context-list registrations and
resumes enum defaults at their list getter `80024B94`. At this point it has
36 published types, 111 descriptors, 123 default buffers, 12 prototypes,
141 enabled allocations and six runtime objects. Local class registration
does not imply that a gameplay context has been created or started.
