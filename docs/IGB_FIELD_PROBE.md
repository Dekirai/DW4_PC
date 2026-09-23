# Native descriptor layout probes

`igb_field_probe` reconstructs the stack-object probes used by
`igObjectRefMetaField` and `igIntMetaField` registration. Its evidence is the
supplied USA DOL,
SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original instructions,
assets and differential fixtures remain local.

| Class | Probe | Actual reset | Stack object | Aligned word offsets |
| --- | --- | --- | --- | --- |
| `igObjectRefMetaField` | `80028934` | `8006ACA4` | `0x44` bytes | `0` through `0x40` |
| `igIntMetaField` | `8002E308` | `800535B8` | `0x34` bytes | `0` through `0x30` |

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

Pending operations retain the selected core, return word, string pointer,
release factory and continuation. The object-reference cache release may
replace the later name; the second cleanup reads that live name. Missing address bindings suspend
at their actual read, while represented null or released headers fail. String
counts use full-word arithmetic, separately from packed object reference counts.
Physical vtable stores also update the canonical descriptor binding.

`IgbFieldProbeServices::profile_local_storage(kind)` supplies the appropriate
stack representation once. The existing `local_storage` callback remains the
object-reference fallback; it does not supply a guessed integer local.
The service's `strings` map resolves nonnull arithmetic addresses without casting them
to host pointers. Existing canonical field-name bindings may supply `+0C`.
Services retain residual owners after raw local overwrites.

The permanent tests cover actual reset, physical offsets, captured and live
core reads, padding faults, ordered string cleanup, callback mutations, late
bindings, unsigned wrap, recursive failure and adapter retention. Cleanup
mutation fixtures explicitly pause after reset before inserting string owners;
they do not claim the uninterrupted original reset produces nonnull pointers.
Integer-specific checks also cover the shorter physical range, direct base
cleanup, exact scalar virtual identity, and a suspended name release without
a repeated decrement.
See [VALIDATION.md](VALIDATION.md) for measured results and comparison scope.
