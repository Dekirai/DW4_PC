# Native scalar field defaults

`make_native_igb_scalar_field_factory` binds the exact USA descriptor vtables
`igIntMetaField` (`80475B60`), `igBoolMetaField` (`80473E30`), unsigned integer
(`80476E6C`), unsigned short (`80477054`), signed short (`80476848`) and signed
byte (`80473F24`). It composes with
`IgbObjectInitInvocation`; unsupported tables or virtual slots delegate to the
remaining provider or remain pending. A serialized type name is insufficient to
select a concrete runtime field implementation.

All six tables use return-only `80064A0C` at `+84`, `80064A04` at `+7C`, and
`80063EB8` at `+74`. Their `+F0` target `80064C48` returns zero. Slots `+70` and
`+78` call `80063E50` and `80063E84`: each reads descriptor offset `+08`, unsigned
16-bit size `+14`, and the physical default-source pointer `+20`, then copies
exactly those bytes through original `800031E8`.

Pointer descriptor `80476630` shares the same `+70/+78` copies and `+7C/+74`
leaves. Its reference-specific `+84/+F0` remain in
[IGB_FIELD_INIT.md](IGB_FIELD_INIT.md). These are exact vtable bindings, not a
scalar fallback for arbitrary reference classes.

The canonical field descriptor now has optional `vtable_word` and
`default_bytes` bindings. An absent default source pauses; a represented null
source is valid only for a zero-length copy. Offset and size are retained across
a paused source-pointer read. Default bytes must be initialized and live.
The destination comes from the allocation already owned by the initializer.

The original copy selects forward or backward byte order by source/destination
addresses. The native implementation preserves overlap behavior within one
represented allocation. Distinct native allocations cannot overlap. It checks
complete physical ranges before copying; partial malformed copies across a
missing boundary are outside this host profile. Valid copies do not normalize
booleans, convert endianness, infer widths from class names, or manufacture zero
defaults. Explicit aliases of a fully initialized object reference header also
refresh its typed view. Field-object destinations also publish affected scalar
members and represented null pointers. Changed nonzero pointer identities need
explicit typed bindings; untouched padding is never decoded as object state.

Permanent tests cover exact bindings, unsupported references, actual byte copies,
captured reads, zero sizes, overlapping ranges, uninitialized/released storage,
failure retention, and composition through the full reflected initializer with
real integer and boolean field handlers. Object/pool endpoints in that focused
fixture remain explicit. This does not establish the complete original file
registration, descriptor default storage, reference/list construction, or a
successfully decoded dependency.

## Earlier two-profile instruction comparison

The final local comparison passed 1,544 cases, including 320 matching initial
null-source failures: 196,106 original instructions across 51 addresses, with
zero differences. It executes both actual copy wrappers, `800031E8`, and all
four verified leaves through the two original vtables, with no prepared callees.
The corpus includes forward/backward copies, identical and overlapping ranges,
zero lengths, and distinct physical source allocations. Partial malformed
copies remain outside the complete-range host profile described above.

Evidence is kept in ignored `work/native-scalar-field-defaults-result.json` and
`work/native-scalar-field-defaults-build.log`, including production source hashes.
The final core SHA256 is
`B545577505570763D8F046AF9646536D51A1E7633999591894DF66646C5C146D`.
See [VALIDATION.md](VALIDATION.md) for the accompanying registration and callback
runner comparisons and the complete local test results.

The September 23 expansion independently compares all six scalar profiles and
the verified pointer subset in 5,402 cases with zero differences. See the current
[VALIDATION.md](VALIDATION.md) entry for that build and the complete reflected
field-object initialization evidence.
