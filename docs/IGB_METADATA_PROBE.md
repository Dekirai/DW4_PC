# Native metadata constructor probe

`IgbMetadataProbeInvocation` reconstructs USA entry `80029FFC`. It executes
the existing native reset constructor `80065218` on an explicitly represented
0x64-byte stack local, reads the current ArkCore global `80562100`, captures
the word selected by that core's `+394` offset, resets the local vtable to
`804761D8`, then destroys the pool-name and name strings in that order.
The result is the captured word, including when cleanup callbacks change the
local or the global core.

Reset entry `80065218` does not create an internal field list. That allocation
belongs to full constructor `800651B4`. The probe does not call an allocator to
manufacture a heap metadata instance and does not return a prepared vtable
constant in place of construction. `local_storage` in the adapter supplies the
host representation of the original stack local, not an original game callback.

The local bytes, initialized-byte mask and typed metadata binding use the
existing bootstrap representation. Constructor stores and padding therefore
remain distinguishable. A reached unknown padding read faults; only aligned
word reads wholly inside the local object are represented by this profile.
The core identity is captured once after construction; missing `+394` state
does not cause a later global-core substitution.

Cleanup reads physical local `+60` first and physical `+1C` after the first
release returns. Each nonnull pointer resolves to its canonical string header,
decrements the entire 32-bit reference word and calls the existing release
boundary `8007381C` only on zero. Underflow wraps. Object reference masks are
not used for strings, and destruction does not add null-pointer stores.
Selected release callbacks, pointer identities, containers and the return word
survive suspension without replaying reference decrements.

`igb_metadata_set_name` and `igb_metadata_set_pool_name` explicitly bind physical
string addresses to canonical owners. Optional address bindings in the probe
services allow a captured pointer to be represented after the live slot has
changed. A host-only metadata name publication invalidates an old physical
name address rather than inferring one from a host pointer. Retained host
owners preserve residual original references; they do not add guest retains,
releases or destructor effects.

## Integration and verification scope

The cold metadata-registration test now resumes from real base registration,
child-list creation and inherited-field append through this actual reset probe.
`igMetaObject.type_word` becomes `804761D8`. It then enters actual defaults
`8002A164`, whose batch reaches the first signed-int field factory `8002E1AC`.
That unimplemented factory remains explicit. The metadata still has its two
inherited fields, flags 9 and size 0x64; the registry has only the completed
base type and ArkCore remains disabled. See [IGB_METADATA_DEFAULTS.md](IGB_METADATA_DEFAULTS.md).

`igb_metadata_probe_tests` covers actual constructor effects and lazy string
pool callbacks, live then captured core selection, initialized physical reads,
capture-before-vtable-reset, ordered cleanup, pending releases, wrapping counts,
late pointer bindings, malformed/released owners, recursive failure propagation,
adapter suspension and host lifetime. Adversarial cleanup fixtures explicitly
modify the represented local after its constructor has run; they do not imply
that an ordinary reset leaves nonnull strings.

Evidence comes from the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Generated disassembly and original
bytes remain ignored. This probe and its cold-prefix integration do not establish
complete runtime registration or playable native startup.
