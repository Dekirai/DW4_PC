# Native IGB runtime metatype lookup

The runtime-type module reconstructs the actual metatype lookup and field-count
helpers used by the IGB type constructor. It provides canonical runtime owners
shared by field metadata and object-type construction; serialized IGB type
records remain a separate representation.

| Original entry | Native implementation |
| --- | --- |
| `80065D60 -> 8003E474 -> 80053910` | `IgbRuntimeTypeLookupInvocation` |
| `80065D88` | `igb_runtime_slot_count` |
| `800658E4` | `igb_runtime_field_at` |
| `8006603C` | `igb_runtime_local_field_count` |

Evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Disassembly and original bytes remain
outside the public repository. The functions perform the recovered owner and
field reads directly; they do not substitute serialized slot counts or create
an unknown runtime type to make lookup succeed.

## Canonical runtime owners

`IgbRuntimeMetaType` carries the reached runtime metadata fields: name `+1C`,
major/minor versions `+10/+14`, byte `+1A`, class flags `+24`, field registry
`+28`, native size `+48`, and native alignment `+58`. `IgbDecodeRuntimeType`
aliases this same type, so a field instance's metadata result and the object
registry can share one allocation and identity.

`IgbDecodeRuntimeField` preserves the represented native instance owner and its
offset `+08`, ordinal `+12`, size `+14`, and bytes `+25/+26`. The metadata and
field owners also have an independently mutable packed `reference_word`,
initialized to one for a represented constructed owner. Lookup and field
queries do not change that original word. Host `shared_ptr` handles preserve
native continuation lifetimes without being used as guest reference counts.

These structures do not themselves execute engine constructors or initialize
the engine registry. Their C++ member defaults are representational defaults,
not evidence that every original constructor assigned those values. Verified
registration/bootstrap code must supply the reached runtime state.

The lookup registry has its physical backing at `+08` and signed logical count
at `+0C`. This differs from the file's `igMetaObjectList` layout. The two list
representations must not be interchanged. The lookup never consults capacity.

## Lookup and the actual name descriptor

`80065D60` reads singleton global `80562100`, then `8003E474` tests its byte
`+00`. Zero returns null without touching the registry list, name descriptor,
or requested C string. Every nonzero byte admits the lookup. The admitted path
captures the singleton's registry owner at `+18`, then reads descriptor global
`8056176C` and calls `80053910`.

The descriptor's selected offset is established by the actual `igMetaObject`
registration callback `8002A164`. Its call to `800659C0` uses the parallel name,
global and offset tables at `80464478`, `804644DC` and `80464540`. Entry seven
names `_name` at `80464494`, publishes descriptor `8056176C` through
`804644F8`, and supplies offset `0x1C` at `8046455C`. This is registration
evidence rather than an inferred offset from a convenient native field.

The lookup loop compares its index with the live signed registry count on every
iteration. For each candidate it reads the live backing, the captured
descriptor's live offset, the physical row, and the string at that offset.
`strcmp` selects the first exact C-string match. On a match, the original loads
the registry backing and row again to produce its returned owner; the native
implementation preserves that reload. It does not filter null rows or null
stored names, normalize case, or interpret a null string as an empty string.
Embedded NUL bytes terminate comparisons in the usual C-string manner.

The scratch-source lookup constructor captures an `IgbRuntimeNameSource`
allocation and offset without reading the name. It reads bytes only at the
reached comparison, stopping at an early mismatch or NUL. This preserves the
disabled/empty-registry unread-name paths and sees changes made before a
pending lookup resumes. The separate `igb_runtime_read_name` helper is for an
explicit operation that actually consumes the whole C string, such as dynamic
metatype naming; pointer capture alone does not call it.

The represented string field is `+1C`. A different reached descriptor offset
fails as an unrepresented field access instead of silently reading the name.
An empty or negative logical count never dereferences the descriptor, backing,
or requested name. A null list still fails when its count is read. A null
descriptor is therefore harmless only when the loop body is not reached.

`IgbRuntimeTypesServices` distinguishes an absent global binding from an
explicitly represented null pointer using optional shared owners. An absent
singleton pauses before reading it. Once the nonzero gate and list capture
have occurred, a missing descriptor binding pauses without replaying that
prefix: replacing the singleton or its list does not redirect the retained
lookup. The captured list's later count and backing reads remain live.
The global descriptor read precedes the count check, so an absent descriptor
binding pauses even if the eventual list count will be zero; a represented
null descriptor permits the original empty-loop return.

The original body has no arbitrary user callback between comparisons. The
native implementation likewise introduces none. Live loads follow the source
order; the tests do not claim concurrent unsynchronized mutation is supported.
Completed lookups keep their canonical result and do not replay. Reached
malformed state faults that invocation permanently. Lookup is read-only and
does not poison the shared registry when an independent lookup is abandoned.

## Runtime field reads

`igb_runtime_slot_count` implements the two loads in `80065D88`: metadata
`+28`, then that registry's count at `+0C`. It returns the raw signed 32-bit
word, including negative values. It never inspects backing, calculates a C++
object size, or uses an IGB descriptor's serialized slot count.

`igb_runtime_field_at` implements `800658E4`: load the current registry from
metadata `+28`, read backing `+08`, and return the physical indexed field.
It does not compare the index with logical count. A represented null physical
row is a legitimate getter result until a caller actually dereferences it.

`igb_runtime_local_field_count` implements `8006603C`. Its loop reloads the
runtime slot count on every condition, obtains each field through the physical
getter, and increments its result only when that field's byte `+25` is exactly
zero. Byte `+26` does not participate. Signed nonpositive counts return zero
without reading backing. A null field becomes a failure at the reached `+25`
read, rather than being filtered from the count.

## Validation and limits

Permanent tests cover all 256 singleton gate bytes, first matching identity,
duplicate names, case sensitivity, embedded NULs, empty names, canonical field
metadata aliasing, and unchanged packed reference words. Continuation cases
replace the singleton/list while waiting for the descriptor and separately
change the captured list's count/backing before their original read sites.

Malformed and boundary cases cover absent versus null globals, signed
nonpositive counts with null backing, null rows/names/descriptors, short
physical backing, unsupported descriptor offsets, and a match before an
unavailable later row. Field-query cases cover every byte `+25` value,
independence from byte `+26`, physical access despite a negative logical count,
raw signed count words, null getter results, and live field-owner replacement.

The host profile bounds physical visits to 262,144 entries and one reached
C-string comparison to 4,096 bytes. Comparisons stop on an earlier mismatch or
NUL, so an unread long suffix is not rejected preemptively. These are explicit
native safety bounds, not claims about safe behavior of malformed original
memory. Engine registration, dynamic type construction, field constructors,
and object deserialization are separate responsibilities.

Final validation passed 335 permanent checks and all 139 core CTest suites.
An independent comparison of the final core against 4,096 original-instruction
cases found zero differences across 193,584 executed instructions and 108
instruction addresses. It executes the lookup, runtime count, physical getter
and local-field count bodies with only `strcmp` supplied as its explicit
C-string contract. This corpus uses static valid physical inputs and the
verified `+1C` descriptor; pending globals, malformed state, lazy scratch names
and lifetime behavior are covered separately by the permanent tests.
