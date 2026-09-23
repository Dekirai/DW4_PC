# Native object-field layout probe

`igb_field_probe` reconstructs `80028934`, the stack-object probe used by
`igObjectRefMetaField` registration. Its evidence is the supplied USA DOL,
SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original instructions,
assets and differential fixtures remain local.

The probe executes actual reset `8006ACA4` through the existing specialized
field constructor in a represented 0x44-byte stack object. This is a reset,
not the allocating factory or its default-buffer preparation. It then captures
the live ArkCore owner, reads its `+394` offset, and reads the corresponding
physical local word. The bounded profile accepts aligned offsets 0 through
0x40; uninitialized padding is not replaced with an assumed value.

After capturing the return value, the original stores vtable `804762FC`,
loads the local `+40` string pointer, decrements its full 32-bit reference
word and calls `8007381C` only on zero. It next writes `80471384` and
`80471914`, reloads the local `+0C` name and performs the same string release.
The physical pointers are not cleared. No descriptor default buffer is freed.
Normal uninterrupted reset has cleared both string pointers, so neither
release is needed in that path.

Pending operations retain the selected core, return word, string pointer,
release factory and continuation. A release callback may replace the later
name; the second cleanup reads that live name. Missing address bindings suspend
at their actual read, while represented null or released headers fail. String
counts use full-word arithmetic, separately from packed object reference counts.
Physical vtable stores also update the canonical descriptor binding.

`IgbFieldProbeServices::local_storage` supplies the stack representation once.
Its `strings` map resolves nonnull arithmetic addresses without casting them
to host pointers. Existing canonical field-name bindings may supply `+0C`.
Services retain residual owners after raw local overwrites.

The permanent tests cover actual reset, physical offsets, captured and live
core reads, padding faults, ordered string cleanup, callback mutations, late
bindings, unsigned wrap, recursive failure and adapter retention. Cleanup
mutation fixtures explicitly pause after reset before inserting string owners;
they do not claim the uninterrupted original reset produces nonnull pointers.
See [VALIDATION.md](VALIDATION.md) for measured results and comparison scope.
