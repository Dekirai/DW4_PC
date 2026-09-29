# Native internal-list defaults

`igb_list_defaults` implements original list defaults `8002E108` and
`8002DDD4`, sharing the existing canonical metadata cells in
`IgbTypeListServices`. The generic registration caller's metadata argument
does not replace the original global read. The native source path retains
the existing field-schema, buffer, string, pointer-list and finalizer work.

## Two-field base-list schema

`8002E108` captures metadata global `805619D4` and its initial field count,
then executes actual `80065924` with two constructors. The original tables are:

| Table | Address | Entries |
| --- | --- | --- |
| Constructors | `8055D3A4` | `8002AC34`, `8002E1AC` |
| Names | `8055D3AC` | `_data`, `_count` |
| Descriptor globals | `8055D3B4` | `805619D8`, `805619DC` |
| Object-member offsets | `8055D3BC` | 8, 12 |

The first constructor creates the memory-reference descriptor profile
(`6324C`, `632A4`, vtable `80476088`, allocation size `50` hex). It is distinct
from the ordinary object-reference descriptor. After both fields are appended,
the caller reloads the field at its initial count and invokes `80028BFC`.
That getter reads object-field prototype `805616EC`, calls registration wrapper
`800289F8` only when it is null, and reloads the global afterward. An unknown
binding is not a null prototype or a successful registration.

The selected first field receives the returned descriptor as a nonowning
`+3C` link, word `+38 = 40` hex, bytes `+34/+35/+40/+41 = 1`, and byte
`+42 = 0`. Actual `800659C0` then configures and finalizes both local fields.
Inherited fields remain before the captured initial index. Changes to the
metadata global cannot redirect this already-selected receiver.

## Internal field-list override

`8002DDD4` captures metadata `805619CC`, then executes the actual name lookup
`800658F8 → 80053910` for `_data`. The name descriptor is the separate global
`805617C4`. The search keeps its selected registry, reads its live count and
backing at each original site, compares with the shared native `8009D5D8`
string implementation, and reloads the matched pointer. The name descriptor's
represented member offset must reach the field's `+0C` string pointer.

Actual index search `80053998` subsequently captures the metadata's current
registry, count and backing, and compares field identities. The selected
descriptor's fresh virtual `+54` receives argument 1. This clone operation
remains an explicit original service; tests supplying a clone do not establish
the original clone constructor/copy body.

After cloning, the caller stores byte `+42 = 1`, binds descriptor global
`805619D0`, invokes the native replacement below, and releases the creator's
reference. The release decrements the full 32-bit word and selects destruction
only when the resulting low 23 bits are zero.

## Native replacement

`IgbFieldReplaceInvocation` reconstructs
`8006588C → 80063F14 → 80053EA0 → 80053C44`:

1. Finalize the replacement, including its descriptor-global publication.
2. Reload the receiver's registry, retain the replacement, read the old slot,
   decrement the old reference and invoke `80066E1C` on low-23-bit zero.
3. Capture backing for the allocation-size query. If required, call the
   existing native `8005383C` reserve, preserving its additional size queries
   and original growth sequence.
4. Reload backing and publish the physical pointer and canonical field owner.

Replacement never changes the logical field count. Pending destruction keeps
the old slot until the later store; callbacks may replace backing before that
store. The replacement's guest reference is retained once. Self-assignment,
wrapped reference words and a negative/wrapped index preserve the original
arithmetic and reached failure prefix. Physical pointer identity is explicit,
separate from the host owner mapping.

For descriptors with represented physical storage, the list callers also
publish their individual descriptor stores through validated canonical bindings.
Parent defaults store the element pointer at `+3C`, word `+38 = 0x40`, then bytes
`+34`, `+35`, `+40`, `+41`, and `+42` in that order. A missing nonnull element
address suspends before either physical or canonical publication; a late binding
uses the already captured prototype. Its weak element reference remains
nonowning. The clone caller physically writes flag `+42` and global pointer
`+1C` before replacement. Replacement retain, old release, and the caller's
creator release keep the represented physical reference words synchronized
with their canonical owners. Bounds or binding failures preserve the completed
store prefix; padding and later members remain untouched. Canonical-only
prepared descriptor fixtures remain a separate supported test profile.

## Verification scope

The local suite composes native batch creation, schema, name ownership,
lookup, index search, descriptor finalization, replacement and pointer-list
growth with explicit prepared descriptor constructors, allocator callbacks and
clone results. It checks the original tables, inherited offsets, prototype
registration pauses, captured receiver, clone selection, global publication,
old/new reference counts, retained pending destruction, live backing changes,
three-query reserve growth, unchanged list counts, null and unknown bindings,
self-assignment, adapters and recursive-failure propagation.
Focused physical-storage cases cover late element addresses, captured prototype
identity, ordered partial-store faults, clone flag/global publication, suspended
old release, creator release and full-word self-replacement wrap. Measured local
build results are recorded in [VALIDATION.md](VALIDATION.md).

The original object-field metadata registration body and its parent registrations
now complete in the composed cold test. Its wrapper also completes the concrete
prototype callback through actual typed `800667F4` initialization, reached field
passes and `8006A438` tail, then native prototype publication and callback cleanup.
The parent list's two-field defaults and publication complete; metadata
`805619D4` becomes the sixth type with flags `7`. Child `805619CC` remains busy
with flags `9`. The actual name descriptor published at `805617C4` feeds native
name/index search, reaching the inherited `_data` memory-reference descriptor's
clone virtual `+54`, argument 1, vtable `80476088`. That clone remains unbound
in the cold integration; subsequent child replacement/publication and other
deferred registrations remain unfinished. ArkCore stays disabled. The chain
contains 52 schema descriptors plus the reflected object-field prototype and
53 default buffers. Recursive reference/member operations beyond that completed
prototype profile remain explicit. A successful prepared clone test is not
proof of its original body or complete native startup. See
[IGB_FIELD_INIT.md](IGB_FIELD_INIT.md) and
[IGB_FIELD_PROTOTYPE.md](IGB_FIELD_PROTOTYPE.md). Source
evidence is the user-supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`; original bytes and generated
disassembly remain untracked.
