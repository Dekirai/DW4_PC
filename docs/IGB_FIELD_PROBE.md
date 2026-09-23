# Native descriptor layout probes

`igb_field_probe` reconstructs the stack-object probes used by
`igObjectRefMetaField`, `igIntMetaField`, `igMemoryRefMetaField` and
`igBoolMetaField`, `igStringMetaField` and `igCharMetaField` registration. Its evidence is the
supplied USA DOL,
SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original instructions,
assets and differential fixtures remain local.

| Class | Probe | Actual reset | Stack object | Aligned word offsets |
| --- | --- | --- | --- | --- |
| `igObjectRefMetaField` | `80028934` | `8006ACA4` | `0x44` bytes | `0` through `0x40` |
| `igIntMetaField` | `8002E308` | `800535B8` | `0x34` bytes | `0` through `0x30` |
| `igMemoryRefMetaField` | `8002AD90` | `800632A4` | `0x50` bytes | `0` through `0x4C` |
| `igBoolMetaField` | `80038778` | `8003EBC8` | `0x34` bytes | `0` through `0x30` |
| `igStringMetaField` | `80025348` | `80071E44` | `0x3C` bytes | `0` through `0x38` |
| `igCharMetaField` | `80037C78` | `8003FF90` | `0x34` bytes | `0` through `0x30` |

Each probe executes its actual reset through the existing specialized field
constructor. The allocating factory and default-buffer preparation are separate.
It then captures the live ArkCore owner, reads its `+394` offset, and reads the
corresponding physical local word. Uninitialized padding is not replaced with
an assumed value.

After capturing the object-reference probe's return value, the original stores
vtable `804762FC`, loads the local `+40` string pointer, decrements its full 32-bit reference
word and calls `8007381C` only on zero. It next writes `80471384` and
`80471914`, reloads the local `+0C` name and performs the same string release.
The physical pointers are not cleared. No descriptor default buffer is freed.
Normal uninterrupted reset has cleared both string pointers, so neither
release is needed in that path.

Integer probe `8002E308` instead stores derived vtable `80475B60` and then
base vtable `80471914`, followed by the same full-word release of the live
`+0C` name. It has no object-reference cache at `+40` and does not store
reference-base vtable `80471384`. The captured word is preserved across that
cleanup. See [IGB_INT_FIELD_REGISTRATION.md](IGB_INT_FIELD_REGISTRATION.md)
for its distinct metadata and prototype identities.

Boolean probe `80038778` has the same direct-base cleanup shape as the integer
probe, with its own reset and derived vtable `80473E30`. It reads no `+40` cache
or `+48` object. Its name reference uses full-word arithmetic, and a pending
name release retains the captured return word and decrement.

Signed-byte probe `80037C78` also uses direct-base cleanup, with its own reset
and derived vtable `80473F24`. String probe `80025348` instead publishes
`8047693C`, reference-base `80471384`, and base `80471914` before releasing
the live `+0C` name. It has neither the object profile's `+40` cache nor the
memory profile's `+48/+4C` members. Its specialized reset writes bytes `+38=1`,
`+27=2` and `+35=1`, leaving surrounding padding intact. Both probes preserve
the original arithmetic member-address null branch and full-word name count.

Memory-reference probe `8002AD90` first writes derived vtable `80476088`,
then releases the live `+4C` string using a full 32-bit decrement and
`8007381C` on zero. After that callback returns, it reads the live `+48` object
pointer, decrements the represented physical reference header at object `+04`,
and rereads its low 23 bits. Only a zero low-23 result reaches object destructor
`80066E1C`. The decrement itself keeps all 32 bits, including unsigned wrap.
It then stores reference-base vtable `80471384` and base vtable `80471914`,
reloads the live `+0C` name, and performs the original string cleanup.
This ordering permits the first string callback to replace the later object,
and the object callback to replace the later name. The three physical member
pointers are not cleared. Reset `800632A4` normally clears `+48` and `+4C`;
the nonnull cleanup paths require explicitly represented post-reset state.

Pending operations retain the selected core, return word, string pointer,
release factory and continuation. The object-reference cache release may
replace the later name; the second cleanup reads that live name. Missing address bindings suspend
at their actual read, while represented null or released headers fail. String
counts use full-word arithmetic, separately from packed object reference counts.
Physical vtable stores also update the canonical descriptor binding.

`IgbFieldProbeServices::profile_local_storage(kind)` supplies the appropriate
stack representation once. The existing `local_storage` callback remains the
object-reference fallback; it does not supply a guessed local for another class.
The service's `strings` map resolves nonnull arithmetic addresses without casting them
to host pointers. Existing canonical field-name bindings may supply `+0C`.
Services retain residual owners after raw local overwrites.

For memory cleanup, `IgbFieldProbeServices::object_cleanup` reuses
`IgbObjectFieldConstructServices::objects` for explicit arithmetic and typed
allocation identities, and its `destroy_object` factory operation for reached
`80066E1C`. The represented stack object itself can supply a matching local
alias. Missing bindings suspend before changing the reference header; conflicting
identities, unavailable physical bytes and invalid typed bindings fail at the
reached access. Captured storage and the selected destructor continuation remain
owned across suspension and address-map removal. This module does not implement
or invent a successful object destructor; an unbound reached destructor remains
pending. See [IGB_OBJECT_FIELD_CONSTRUCT.md](IGB_OBJECT_FIELD_CONSTRUCT.md).

The permanent tests cover actual reset, physical offsets, captured and live
core reads, padding faults, ordered string cleanup, callback mutations, late
bindings, unsigned wrap, recursive failure and adapter retention. Cleanup
mutation fixtures explicitly pause after reset before inserting string owners;
they do not claim the uninterrupted original reset produces nonnull pointers.
Integer-specific checks also cover the shorter physical range, direct base
cleanup, exact scalar virtual identity, and a suspended name release without
a repeated decrement.
Memory-specific checks cover every aligned local word, untouched padding,
ordered string/object/name callbacks, live pointer changes, late address and
destructor bindings, packed-reference wrap, physical/canonical synchronization,
adjusted object offsets, explicit local aliasing, retained allocation lifetime,
fault prefixes and recursive callback rejection. Boolean checks cover its
distinct virtual identity, scalar bounds, adapter and suspended name release.
String and signed-byte checks cover every aligned local word, exact short
storage, the captured name pointer and return word across suspension, full-word
count wrap, zero member-address branches, adapter routing, bounds and string
padding failures.
See [VALIDATION.md](VALIDATION.md) for measured results and comparison scope.
