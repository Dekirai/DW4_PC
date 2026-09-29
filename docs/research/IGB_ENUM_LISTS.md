# Enum list registration and defaults

The original `igMetaEnum` defaults require two separate allocation-only metadata
getters. `80024B94` creates `igStringRefList` metadata on a represented null cell;
`800365B4` does the same for `igIntList`. Neither getter performs full registration.
The enum registrar's later extra callbacks register those two classes in order.
The native adapter preserves that distinction and uses the existing metadata
factory, pool selection and pointer ownership instead of manufacturing addresses.

| Property | String references | Signed integers |
| --- | --- | --- |
| Class name | `igStringRefList` | `igIntList` |
| Metadata global | `8056158C` | `80561DC4` |
| Allocation-only getter | `80024B94` | `800365B4` |
| Full getter | `80024BD0` | `800365F0` |
| Plain getter | `80093ABC` | `80093F28` |
| Registration wrapper/body | `80024C64 / 80024C8C` | `80036684 / 800366AC` |
| Layout probe | `80024C0C` | `8003662C` |
| Defaults | `80024D24` | `8003673C` |
| Element prototype getter | `800256C0` | `8002E4D8` |
| Element prototype cell | `805615B8` | `805619E4` |
| Specialized descriptor global | `80561590` | `80561DC8` |
| Final vtable | `80470F5C` | `80474600` |

Both classes have flag zero, size `0x14`, and the existing `igDataList` parent.
They add no deferred or trailing callbacks. The probes execute the base header
reset, write the first vtable, capture ArkCore, and write the two remaining
vtables before selecting the word at the captured core's offset. Untouched list
members remain unknown. The existing aligned 20-byte local-storage profile is
explicit; it does not imply arbitrary stack access.

The specialized defaults capture metadata, find inherited `_data`, capture its
index, and load the found descriptor's physical vtable for clone virtual `+54`
with argument one. The selected clone call retains its source allocation across
suspension without refreshing a stale canonical vtable or rereading after
selection. Returned clone ownership is captured before the element getter.
That getter aliases the existing string or signed-integer prototype cell; it
does not substitute class metadata or create a duplicate prototype.

After the getter returns, defaults publish element pointer `+3C`, zero `+38`,
and the specialized global pointer `+1C`, then finalize and replace the captured
field index. Inherited flags `+35/+40/+41/+42` are preserved. The replacement
retains its incoming descriptor before selecting and releasing the old slot.
Both old-slot release and final creator release perform the original full-word
decrement/store followed by a separate fresh low23 read. Destruction and backing
publication retain their original order; a pending destructor does not repeat
reference changes or introduce later reads of a legitimately released header.

These operations use the shared physical reference access described in
[IGB_FIELD_REFERENCES.md](IGB_FIELD_REFERENCES.md). Physical stores precede cache
publication; lost allocation identity cannot fall back to stale canonical data.
Unknown globals remain explicit dependencies. Unfinished operations retain the
selected descriptor/storage/bytes without retaining callback closures in a
cycle. Existing legacy lower registration/factory parent-guard limitations are
not claimed to be resolved by these adapters.

The supplied USA DOL is the primary source, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. IDA confirmed the defaults' physical
clone dispatch at `80024D64 / 8003677C`; the separate replacement evidence covers
`8006588C`, `80053EA0` and `80053C44`. Research corpora and disassembly stay local.
Physical descriptor names and metadata/string headers retain their existing
declared mappings. Complete list instance methods and full startup remain
separate work. See [VALIDATION.md](VALIDATION.md) for measured results and the
exact limits of original/native comparisons.
