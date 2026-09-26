# Struct-field registration and defaults

`igStructMetaField` is the concrete descriptor used for the inline semaphore
structure. Its metadata cell `80561560` and vtable `80470E00` are distinct from
object references, raw references and memory-reference fields. The native
factory/profile are appended to the existing enums without changing earlier
indices or canonical cells.

Factory `8002450C` always calls registration wrapper `8002460C`, reloads its
class cell, and reflects through `8006546C` using the captured pool. There is
no enabled-state branch, specialized raw constructor or post-factory tail.
Registration body `80024634` executes the original prerequisite `80021B94`.
Its class is size `0x40`, inherits `igMetaField`, uses probe `80024580`, defaults
`800246C8`, and deferred prototype callback `800247E4`. Full and plain getters
remain separate; the class has no represented lazy-allocation getter.

## Probe

The probe runs actual base-field reset `800638E0`, captures ArkCore, then
publishes the derived vtable. It reads the selected local word using the
captured core's `+394` offset, saves that result, and restores the base-field
vtable before name cleanup. Its three derived words at `+34/+38/+3C` remain
untouched. Unknown core state, null core and unknown derived bytes preserve
their different pending/fault prefixes. Name cleanup reads the real physical
pointer and performs the existing string-reference release.

## Defaults

Original `800246C8` captures the class and inherited field count, then appends
three descriptors before specializing any inherited field:

| Factory | Field | Instance offset |
| --- | --- | --- |
| `8002E1AC` | `_alignment` | `34` |
| `800274EC` | `_cppConstructor` | `38` |
| `800274EC` | `_cppDestructor` | `3C` |

It finds inherited `_size`, reloads the registry for the descriptor's index,
then calls that descriptor's fresh virtual `+54(1)`. The returned clone receives
the actual two-byte default value `1` through `+8C`. The code clears byte `+26`,
stores its distinct global destination `80561568` at `+1C`, replaces the
captured registry position through `8006588C`, and releases the temporary's
whole packed reference word. A zero low-23-bit count reaches `80066E1C` before
continuing. A destructor may legitimately release the temporary storage.

The first local descriptor then receives the four-byte alignment default `1`
through its fresh `+8C`, has byte `+26` cleared, and enters the actual local
schema publication. Its width differs from the inherited size setter.
Locals use global slots `8056156C/70/74`; the base class's canonical `_size`
descriptor and sibling schemas remain separate. The final schema does not
write a metadata redirect. Pending callbacks retain their selected inputs and
completed stores are not rolled back on a later fault.

## Current cold dependency

The retained native start test now completes unsigned-short registration and
its prototype before running inherited `_size` clone `80021EB4`. The clone's
independent two-byte default becomes `1`, its specialized global is published,
and all three local schema fields complete. The base `_size` descriptor/global
remain unchanged, as do the other 19 inherited descriptor identities.

Struct metadata has flags `7` and 23 fields. Prototype callback `800247E4`
and its concrete descriptor construction now finish, producing two struct
descriptor instances and completing the semaphore's seven-field schema.
The runtime semaphore's metadata getter and inline member initialization now
complete. Subsequent **activation `+5C`** now initializes queue/count and status
with explicit exclusion state, allowing its manager to complete and be published.
There are 27 published types, 92 descriptors, 104 default buffers, twelve
prototypes and 96 enabled allocations. Both the thread list and semaphore are
constructed; concrete main-thread getter `8003AF84` is the next missing operation.

Dynamic descriptor size/alignment and distinct constructor/destructor words
are implemented through [IGB_STRUCT_FIELD_RUNTIME.md](IGB_STRUCT_FIELD_RUNTIME.md).
Inline member handlers retain constructor/destructor callbacks and exact byte
copying; see [IGB_STRUCT_MEMBERS.md](IGB_STRUCT_MEMBERS.md). See
[IGB_SEMAPHORE_ACTIVATION.md](IGB_SEMAPHORE_ACTIVATION.md) for activation and its
explicit platform state. Continue with concrete thread registration and the
subsequently reached platform operations.
See [IGB_UNSIGNED_SHORT_FIELD.md](IGB_UNSIGNED_SHORT_FIELD.md),
[IGB_PLATFORM_SEMAPHORE.md](IGB_PLATFORM_SEMAPHORE.md),
[IGB_RUNTIME_OBJECTS.md](IGB_RUNTIME_OBJECTS.md) and [VALIDATION.md](VALIDATION.md).

Primary evidence is the supplied USA DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original artifacts remain local.
