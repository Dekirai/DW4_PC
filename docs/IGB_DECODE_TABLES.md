# Native early IGB decode tables

`IgbDecodeTablesInvocation` reconstructs three reached helpers in the original
USA IGB loader:

| Original helper | Native kind | Implemented behavior |
| --- | --- | --- |
| `800514AC` | `field_types` | Read the field-type block, match runtime field instances by metadata name, append first match or null |
| `80051794` | `alignments` | Read the alignment block, match loader-registered names and append the corresponding raw alignment word or default 16 |
| `8004FBE8` | `object_type_bytes` | Read the object-type metadata block into the original shared scratch-buffer alias |

This is loader execution over a canonical stream and mutable runtime owners,
not a call to `read_igb_metadata` with a fabricated success result. Runtime
constructors, lazy registry creation, metadata virtuals and list allocators
that are not yet represented remain individually named pending operations.
The subsequent runtime metatype construction at `8004FC94` is not implemented
by these helpers.

Original instruction evidence uses USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original bytes and disassembly remain
outside the public source. [LOADER_REQUEST.md](LOADER_REQUEST.md) documents the
outer request/path boundary; the stream module supplies concrete byte reads,
initialized scratch storage, endian conversion and retained buffer allocation.

## Shared state and status

`IgbDecodeTablesState` represents the field buffer `+B8`, object-type buffer
`+AC`, alignment buffer `+EC`, alignment block size/count `+F0/+F8`, runtime
field-instance list `+BC`, and alignment-word list `+F4`. The three byte-buffer
fields alias the canonical scratch allocation at `IgbStreamState::buffer`;
they are not copies or separately parsed documents.

All helper status reads use the canonical shared `IgbStreamState::statuses`.
`IgbDecodeStatusWords` holds independently optional original success/failure
words. Missing words pause when reached. `resume().completed` means that the
helper returned, while `value()` contains its original status word; neither is
an implicit declaration that the entire IGB load succeeded. A retained
`IgbBufferInvocation` prevents missing status words from replaying a completed
allocation/free prefix.

The file argument and stream state's weak file pointer must identify the same
canonical `LoaderRequestFile`. An invocation retains the strong owner while
running. Separate table state retains its published fields and failure prefix.

## Field-type registry mapping

The field header supplies count `+88` and byte size `+8C`. After successful
buffer preparation, the original helper publishes `+B8 = +108`, then calls the
stream with element size equal to the block size and element count one.
**It ignores that read's return value.** Native initialized-byte tracking allows
a short read to continue when every subsequently reached byte exists, and
rejects only a reached unknown or unavailable byte.

The helper obtains the runtime field registry through `80063B5C`. A represented
existing registry returns directly; lazy construction is a named operation.
It captures the registry's signed count at `+0C` and the field buffer, then
places the name cursor after `count * 12` descriptor bytes. On swapped input,
the actual integer-field width controls conversion of `count * 3` words.

The actual reflected list factory is `80034010`, registering `igMetaFieldList`
through `80034158` (name at `804674E0`, size `0x14`). The caller captures live
file pool `+118`, acquires the resulting list before releasing the old `+BC`
field, and publishes it. The generic factory's initializer `800667F4` sets the
low reference-count bits to one. Unlike the alignment helper, this caller does
not release that creator reference. `field_list_creation_refs` therefore retains
it beyond invocation completion, separately from the published `+BC` owner.
The original cleanup's two live `+BC` releases must consume those two references;
cleanup itself remains an explicit operation. A focused lifetime test verifies
this distinction from the alignment factory's released temporary.

If the current file count is greater than or equal to
the new list's logical count, it reaches allocator `8004155C` with that exact
capacity and element size four. This condition compares logical count, not
capacity, and includes the zero/zero case.

For each serialized field occurrence, the native loop searches the captured
registry count while reloading its backing on every visit. A selected runtime
field's virtual `+58` supplies metadata; metadata `+1C` is compared with the
serialized C string. The first matching instance wins and invokes virtual
`+58` a second time, with its result ignored. Unknown names append null. The
serialized major/minor descriptor words do not participate in this helper's
name lookup.

The append uses the live output list. Only after that append does the helper
read the descriptor's name-length word and advance the captured name cursor.
Thus a malformed next-name offset can leave an already appended entry intact.
The outer loop reloads the file count. On normal loop completion it clears
`+B8` before reading the final success word.

## Alignment-name mapping

`80051794` first reads a twelve-byte stack header as one element. Any result
other than one returns the live failure word before publishing size/count.
When swapping is enabled, it converts the three header words, then publishes
`+F0 = word0 - 12` with 32-bit wrapping and `+F8 = word2`.

A signed count less than or equal to zero immediately returns success. It does
not read a body, create a runtime list, or clear a preexisting `+EC` pointer.
Positive count publishes `+EC = +108` and reads `+F0` bytes as one element.
There is no intervening buffer-growth call. A short body returns failure with
that pointer and the header fields still published. On successful body read,
only the first `count` length words are conditionally converted.

The native loop captures the loader singleton's name list `+2C` and value list
`+30`, and captures the name-list count (zero for a null name list). Its actual
output factory is `8003657C`, registering `igIntList` through `800366AC`
(name at `8046762C`, size `0x14`). Factory/pool capture, field replacement and
initial allocator conditions match the field-table caller.

Each entry defaults to raw word `16`. A first exact name match reads the value
at the same physical index, independent of the value list's logical count.
A null name object is an invalid reached dereference; a nonnull name object
with a null string compares as the original empty fallback string
(`8055DC4C -> 8055D4C4`). A null value list is reached only when a name matches.
The append precedes the captured descriptor-length read. Normal completion
clears `+EC`, publishes the live success result, then releases the retained
value and name list owners in that original order.

## Object-type bytes and the next boundary

`8004FBE8` prepares a buffer using the live `+84` object-type byte size, compares
that helper's status with the current failure word, and publishes `+AC = +108`.
Zero size skips the stream call and returns success. Otherwise the exact
returned element count must equal one. A failed read preserves the buffer
alias. This helper performs no endian conversion or runtime type construction.

The following `8004FC94` is substantially more than parsing type names. The
inspected body creates runtime `igMetaObjectList` state at `+C0`, and for
versions at least four creates field-slot mapping lists at `+3C` plus a
short-adjustment table. It looks up actual registered metatypes through
`80065D60`, validates major/minor compatibility, translates serialized field
slots through parent records, and calls `80065D88` to calculate native slot
adjustments. When permitted by the original global flag, an unknown type can
be constructed dynamically using its parent metatype, actual field instances
from `+BC`, field constructors, native layout/alignment operations and registry
publication. Incompatible or unavailable types take failure paths with partial
state preserved.

The existing v6 document/metadata parsers provide validated serialized layout
and graph information for that implementation. They do not establish native
runtime registration, field constructors, slot-layout conversion or post-load
behavior. Successful completion of these three helpers therefore leaves
`8004FC94` as an explicit next runtime boundary.

## Continuations and validation scope

Pending calls retain their captured argument owners, selected factory and
callback. Later original read sites continue to observe live counts, pointers,
registry backing and output lists. Completed calls do not replay. Unknown final
runtime destructors fail at the reached release while preserving the incoming
reference and published prefix. Abandonment or recursive invalidation poisons
the table state instead of allowing a silent restart.

Lists distinguish signed logical count/capacity from physical backing.
The native profile bounds physical visits to 262,144 slots and C strings to
4,096 bytes. Missing/uninitialized/released scratch bytes fail when consumed.
These are explicit host bounds, not claims that malformed original memory
accesses were safe.

Permanent synthetic tests cover both byte orders, duplicate field occurrences
and duplicate registry names, the extra matched metadata call, nullable
unmatched fields, nullable alignment names, physical value lookup and default
16, one shared stream consumed by all three helpers, pending metadata with live
registry mutation, late final-status publication without replay, intentionally
ignored field read results, unknown scratch bytes, count-first output failure,
short alignment header/body and nonpositive alignment counts.

The optional `dw4_igb_decode_tables_tests EXTRACTED_ROOT` path resolves the
supplied `files/ndmw.igb` and enters the actual `IgbDecodeInvocation` through
`make_native_igb_stage_factory`. It uses concrete byte-stream opening, the native
header and all three native table helpers, with an explicitly bound scratch
pool. Its runtime field registry is deliberately prepared from independently
parsed serialized field names, and its alignment registry is deliberately
empty. This does not claim original engine registry initialization. The test
expects 48 canonical field mappings, four default alignment values, exact raw
bytes for 20 serialized object types, and stream cursor 2,916 before remaining
pending at `8004FC94`, with loading one and success zero. Repeating that pending
call must not replay the completed prefix.

Final validation passed all 136 core CTest suites. This module passed 57
synthetic checks and 116 checks with the supplied original `ndmw.igb` included.
The original-data run confirmed all 48 field mappings, four default alignments,
20 type records and cursor 2,916 at the retained `8004FC94` boundary. Its fresh
file fixture starts with null integer/short endian-field owners before the
actual header creates them; direct-table fixture owners are not reused as
preexisting original file fields.
