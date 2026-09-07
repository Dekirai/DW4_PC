# Native IGB v6 object documents

`igb_document` extends the metadata reader with actual serialized object and memory
records. It creates an owned, indexed host representation of the file. It does not
instantiate original Alchemy C++ classes, execute their constructors, load another
file automatically, or implement gameplay.

The implementation is independent C++20. Format observations from
[mateon1's exploratory IGB parser](https://gist.github.com/mateon1/b123a53d824305767989ebc2ba3e5814)
were checked against the user's local files; that parser's source code was not
copied. Committed tests generate synthetic metadata and payloads. No original
object records, memory payloads, images or archive entries are included.

## Implemented loading sequence

After the metadata blocks described in [IGB_FORMAT.md](IGB_FORMAT.md), the reader
processes these parts in file order:

1. Optional external-directory names, enabled by flag `0x40000000`.
2. Optional memory-pool names, enabled by flag `0x10000000`.
3. Shared directory-entry records, using the header's entry size/count.
4. A length-prefixed index of 16-bit directory-entry indices.
5. An optional 32-bit info-list reference, enabled by flag `0x80000000`.
6. Object-instance records, using the header's object size/count.
7. Memory-reference payloads, using the header's memory size/count.

The shared-entry flag `0x20000000` is required by this implementation. Version 6
and both metadata byte orders are supported. All measured original inputs use
little-endian metadata; big-endian documents are verified with synthetic tests.

An external-directory block contains its total byte size, a preserved-but-unused
format word, a name count, then 32-bit name lengths and the names. A memory-pool
block contains its total byte size, name count and consecutive NUL-terminated
names. Both blocks are bounded before individual names are read. Names are data;
they are never treated as filesystem paths to open by the document parser.

## Shared entries and references

Directory entries act as templates for the indexed records. Several index entries
may select the same template, producing distinct objects or memory records. The
index count must equal the sum of the header's object and memory counts.

| Directory type | Relevant field slots |
| --- | --- |
| `igObjectDirEntry` | 11: object-type index; 12: pool assignment |
| `igMemoryDirEntry` | 7: payload byte size; 10: memory-type identifier; 11: reference-counted flag; 12: alignment identifier; 13: pool assignment |

The referenced directory kinds currently supported are `igObjectDirEntry` and
`igMemoryDirEntry`. Other directory kinds produce an explicit unsupported-format
error. Parsing external-directory **names** does not implement external-object
loading. Pool/alignment identifiers are recorded without being confused with host
allocator handles or pointer values.

`IgbDocument::references` uses the original serialized index space and maps each
index to a dense `objects` or `memories` entry. A dense object index is therefore
not interchangeable with a serialized reference. Each object and memory record
also records its original `reference_index`.

## Object fields and memory

An object record begins with a 32-bit type index and total record byte size. Its
fields follow the type's serialized metadata order. Each stored field advances
to the next four-byte boundary. Padding bytes between fields must be zero.

Most field sizes are supplied by the metadata. An `igStringMetaField` has a
32-bit byte-length word followed by that many inline string bytes. Its alignment
includes the length word and inline bytes. Empty strings and null references are
different encodings. Single object/memory references occupy four bytes; the
`0xffffffff` value denotes null. Every non-null object/memory reference is checked
for both bounds and the correct record kind. Arrays of references receive the
same per-element validation.

Memory records consume their declared byte sizes in reference order, each rounded
up to a four-byte stream boundary. Their payload is preserved without assuming a
scalar type or byte order: graphics buffers can have their own encoding. The
parser requires exact consumption of the directory, index, object and memory
blocks and rejects unexplained bytes at the end of the document.

## API and ownership

```cpp
auto document = dw4::read_igb_document(std::move(archive_bytes));
for (const auto& object : document.objects) {
    const auto& type = document.metadata.object_types[object.type_index];
    // Interpret only fields whose schema has been established for this type.
}
```

The document owns its backing bytes. Field values and memory records contain
checked byte offsets and lengths; the implementation never casts disk bytes to
native structs or guest pointers. Copies and moves of the document remain valid.
Returned `span` and `string_view` values borrow the receiving document's storage;
they must not outlive that document or survive an assignment that replaces it.

- `object(reference)` and `memory(reference)` resolve a serialized index with a
  kind check.
- `field(object, slot)` locates a field; `field_bytes(field)` returns its serialized
  bytes, including an inline string's length word.
- `field_u32`, `field_i32`, `field_u16`, `field_i16`, and `field_f32` decode exact
  scalar widths using the document's byte order.
- `field_string` returns the original inline bytes with terminator/padding removed.
  Encoding is not inferred; presentation code must handle legacy encodings and
  invalid UTF-8 safely.
- `field_reference` preserves nulls and validates object/memory kind.
- `is_a` checks metadata inheritance with a bounded traversal.
- `object_list` resolves an `igObjectList` through slots 2 (count), 3 (capacity)
  and 4 (memory reference), validates its capacity, and returns nullable object
  reference indices. It does not recurse through arbitrary cycles.

The object graph may contain cycles. Callers traversing relationships should keep
a visited set rather than recursively assuming a tree. Field slots describe the
serialized layout and do not establish host offsets or original field names.

## Limits and verified coverage

The document reader limits an input to 256 MiB, references/objects/memory records
to 1,048,576 each, field values to 4,194,304, and strings to 1 MiB. Metadata has the
additional limits documented in `IGB_FORMAT.md`. These limits apply before the
associated allocations or reads. The path overload reads one file; an archive
caller reads a selected entry with its own bound and moves that buffer into the
document. The entire AFS archive is never required in memory.

Synthetic checks cover both byte orders, optional-table presence/absence, shared
entry reuse, inherited types, strings, signed scalars, floats, nullable references,
cycles, object lists, and storage ownership after copy/move. Malformed checks
include every truncated prefix, invalid counts/type indices, root and field kind
mismatches, nonzero field padding, bad string lengths and extra trailing bytes.

The following read-only native integration checks passed against the local USA
files on 2026-09-08. Every document's object lists were also traversed and checked.

| Input | Documents | Parsed objects | Parsed memory records | Failures |
| --- | ---: | ---: | ---: | ---: |
| `ndmw.igb` | 1 | 2,297 | 108 | 0 |
| `area.afs` | 1,379 | 3,456,158 | 1,229,981 | 0 |
| `ndmw.afs` | 894 | 3,516,157 | 1,099,549 | 0 |

These are totals across serialized asset documents, not counts of reconstructed
gameplay entities or completed game functions. Non-IGB archive payloads are
excluded. Standalone `ndmw.igb` additionally yields 223 external-directory names,
46 memory-pool names, 11 shared entries, 2,405 references and 108 valid object
lists. Its info-list reference is 156, and the parser consumes all 107,527 bytes.

```powershell
.\build\Debug\dw4_igb_document_tests.exe 'PATH\files\ndmw.igb'
.\build\Debug\dw4_igb_document_tests.exe --archives 'PATH\files\area.afs' 'PATH\files\ndmw.afs'
```

Resource-catalog and texture consumers can now use real object and memory records.
Scene construction, mesh transforms, animation, collision and the original game
runtime remain separate implementation work.
