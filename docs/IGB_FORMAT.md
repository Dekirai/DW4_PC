# IGB v6 metadata: implemented scope and measured evidence

`read_igb_metadata` is a native C++20 reader for the header, field-type table and object-type table. It accepts a filesystem path or an in-memory prefix. It does **not** load object instances, resolve references, decode textures/meshes or execute gameplay. Header object counts are declarations from the file, not decoded object counts.

The implementation was written independently from observed file structure. An exploratory [IGB format parser by mateon1](https://gist.github.com/mateon1/b123a53d824305767989ebc2ba3e5814) provided format evidence; its source code is not included. The local USA game files provided the integration inputs. Public tests use independently constructed synthetic names and bytes.

## Verified inputs

The local DOL SHA-1 is `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, matching the audited decompilation checksum. All recognized original IGB inputs used little-endian metadata and format version 6. Big-endian support has been checked with synthetic tests only.

| Input | IGB inputs parsed | Failures | Maximum field types | Maximum object types | Maximum metadata prefix |
| --- | ---: | ---: | ---: | ---: | ---: |
| `files/ndmw.igb` | 1 | 0 | 48 | 20 | 2,916 bytes |
| `files/area.afs` | 1,379 | 0 | 48 | 99 | 8,254 bytes |
| `files/ndmw.afs` | 894 | 0 | 48 | 226 | 17,924 bytes |

The archive tests inspect all entry headers and parse every entry with the cookie at offset `0x28`. The first recognized IGB is entry 3 in `area.afs` and entry 79 in `ndmw.afs` (indices start at zero). Unrecognized audio/video or other entries are not counted as IGB failures or claimed as supported. No archive is extracted or changed by these tests.

Standalone `ndmw.igb` declares 11 entries, 2,297 object instances and 108 memory references. Its metadata includes Alchemy AFS setup types. This gives a concrete initial target for reconstructing the game's logical resource catalog.

## Header

Twelve 32-bit words occupy `0x00..0x2f`. The reader checks the `0xFADA` cookie in **both** byte orders at `0x28`; the file does not begin with that cookie.

| Offset | Meaning used by the reader |
| --- | --- |
| `0x00`, `0x04` | Entry buffer byte size, entry count |
| `0x08`, `0x0c` | Object-type buffer byte size, type count |
| `0x10`, `0x14` | Object-instance buffer byte size, instance count |
| `0x18`, `0x1c` | Memory-reference buffer byte size, reference count |
| `0x20`, `0x24` | Field-type buffer byte size, type count |
| `0x28` | Magic cookie `0x0000FADA` |
| `0x2c` | Version in low 16 bits; flags in high 16 bits |

The native reader requires version 6 and preserves all flag bits. It reports counts and sizes for later payload blocks without validating their contents. A complete metadata prefix is valid input even when the later object payload is unavailable.

## Metadata blocks

Immediately after the header is the field-type block, with its byte size supplied by header word 8. It begins with `count` descriptors of three 32-bit words: name byte length, major version, minor version. Names follow the descriptor array in the same order. Names are NUL-terminated and may have trailing NUL padding. Original version numbers are preserved, including a zero major version.

Next is a length-prefixed alignment block. Its 32-bit byte length includes the length word itself. This implementation validates its boundary and skips its contents; it does not infer runtime or GPU buffer alignment from it.

The object-type block follows, with byte size/count from header words 2/3. Each fixed descriptor occupies 24 bytes: name length, major version, minor version, field count, signed parent index, and slot count. Parent `-1` means no parent; otherwise the parent must precede the child. After all fixed descriptors, each type's name and then its field descriptors appear sequentially. A field descriptor has three 16-bit values: field-type index, slot, and serialized size. The reader checks field-type and slot bounds.

Slots are identifiers within the serialized class layout; they are not assumed to be host byte offsets. Likewise, serialized field sizes must not be interpreted as the size of a native pointer or C++ class. The original game's 32-bit PowerPC object layout and a 64-bit host ABI differ. Metadata byte order also does not by itself establish the byte order of later graphics/audio payloads.

## Limits and tests

The parser limits metadata to 16 MiB, field types to 4,096, object types to 65,536, total object fields to 1,048,576 and individual names to 4,096 bytes. Every byte read is bounded by its declared block and supplied input. The filesystem overload reads only the required metadata prefix. Archive inspection currently requests a bounded 16 MiB prefix, avoiding an entire archive allocation.

Synthetic tests cover both byte orders, every truncated prefix of a valid fixture, corrupt cookies and versions, counts and size overflows, invalid parents/type indices/slots, malformed names and file-read failures. Later payload bytes are deliberately not consumed by a metadata-only operation.

Run public tests with CTest. Optional read-only integration checks require the user's own data:

```powershell
.\build\Debug\dw4_igb_tests.exe 'PATH\files\ndmw.igb'
.\build\Debug\dw4_igb_tests.exe --archives 'PATH\files\area.afs' 'PATH\files\ndmw.afs'
```

## Object and asset loading

The separate [object-document reader](IGB_OBJECTS.md) now consumes the entry,
index, object-instance, memory and optional directory/pool blocks. The metadata
API remains useful for bounded inspection without loading those payloads.

The [resource catalog](RESOURCES.md) resolves the actual AFS setup, and the
[texture decoder](TEXTURES.md) uses validated image objects and memory records.
Scene geometry and the missing Alchemy/gameplay runtime still require
reconstruction.
