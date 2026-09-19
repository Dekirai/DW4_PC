# Recovered runtime metadata for the bootstrap directory

`make_original_ndmw_runtime_bootstrap()` constructs a bounded native set of
runtime registrations independently of any IGB input. It supplies the twenty
object classes used by the bootstrap directory and forty-seven independently
recovered field-class metadata identities. Names, parents, sizes and field
layouts come from the supplied USA executable's registration/default routines,
not from the serialized names or slot counts in `ndmw.igb`.

This is a native registration subset for the reached metadata operations. It
does not complete ArkCore initialization, global registry ordering, original
singleton allocation, reflective instance defaults, or object deserialization.

## Original registration evidence

The executable SHA-1 is `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.
`80066188` invokes a registration function and its returned initializer list.
The common registration body `80066204` binds the parent through `80065640`,
which copies references to the parent's physical fields. It then assigns the
class name, size and abstract flag, invokes the class-specific defaults callback,
and computes field ordinals/alignment through `80065A58`.

The concrete metadata constructor `80065218` establishes major version one,
minor version zero, a zero abstract flag and initial alignment four. Every
field in these twenty object layouts has alignment one, two or four, so the
final maximum remains four. Registration sizes remain distinct from slot counts
and from the last occupied field byte.

| Class | Registration | Runtime slots | Native size |
|---|---|---:|---:|
| igObject | 80066B08 | 2 | 8 |
| igNamedObject | 80029D58 | 3 | 12 |
| igDirEntry | 80032D80 | 7 | 28 |
| igObjectDirEntry | 80029394 | 13 | 52 |
| igMemoryDirEntry | 8002C918 | 14 | 56 |
| igExternalDirEntry | 80030A3C | 11 | 44 |
| igExternalImageEntry | 800D4AB0 | 11 | 44 |
| igExternalIndexedEntry | 80030704 | 13 | 52 |
| igExternalInfoEntry | 80030434 | 10 | 40 |
| igDataList | 80033A14 | 5 | 20 |
| igObjectList | 8002907C | 5 | 20 |
| igAdxAfsFileList | 802AAAB8 | 5 | 20 |
| igNamedObjectList | 80029B6C | 5 | 20 |
| igFileName | 802AB0B0 | 4 | 16 |
| igAdxAfsFile | 802AACB8 | 6 | 20 |
| beNDMWAfsSetupData | 803453DC | 4 | 16 |
| igInfo | 8002EABC | 5 | 20 |
| beNDMWAfsSetupInfo | 80344FA8 | 7 | 28 |
| igInfoList | 8002E934 | 5 | 20 |
| beNDMWAfsSetupDataList | 80345238 | 5 | 20 |

Counts follow the original inherited fields and local append operations; no
serialized count initializes a runtime registry. Offsets, widths and byte
`+26` flags are recovered from the concrete field-factory/default tables.
Inherited fields share physical owners. The object-directory and external-
directory classes replace `_ref` with separate object-reference fields, while
`igObjectList` clones and replaces its inherited `_data` field without growing
the list. Other inherited fields keep their parent identities.

The two root fields `_refCount` and `_memPoolIndex` are distinct descriptors
covering the same four-byte offset. Directory field offsets are not necessarily
increasing in physical slot order. The AFS file fields at offsets sixteen and
eighteen are two-byte shorts. These distinctions are retained explicitly.

## Field metadata bindings and limits

Each represented field-class registration supplies an initializer extension
that constructs its singleton and inserts it into the field registry. For
example, `8002E3B8` provides the int-field initializer `8002E440`; its concrete
virtual `+58` leaf `80094020` reads the registered metadata cache. Equivalent
registration/cache/vtable relationships were independently checked for all
forty-seven classes in the native subset.

`field_metadata(field)` is bound to the exact represented field wrapper and
owner. An unrelated object with the same class name, a copied wrapper, or an
owner replacement cannot inherit that virtual binding. Returned metadata is
the same canonical identity exposed through runtime type lookup.

The supplied bootstrap directory requests an additional field class named
`igVertexBlendShaderCapsInfoMetaField`. No registration name for that class was
found in this USA executable. This subset does not invent one; normal lookup
returns null. It also does not infer singleton registry ordinal or order from
the serialized field table. Singleton field widths/defaults beyond the bound
metadata callback are outside this profile.

Field-class metadata schemas remain unrepresented (`fields == nullptr`), so a
reached schema/count request stops explicitly. This prevents a known field
class name from being promoted to a completed reflective class definition.
The recovered twenty object layouts are complete for the represented metadata
fields, but semantic default setters and instance factories remain separate
work. Reference words count the owners created by this native subset, not every
unrecovered engine-global reference.

The existing `ModelSceneObjects` readers construct application objects from an
already decoded document. They remain useful downstream but are not used to
manufacture these runtime registrations from serialized data.

## Validation

Permanent tests exercise parent identity sharing, the three replacement cases,
overlapping root fields, nonmonotonic directory offsets, primitive widths,
registration counts and sizes, metadata identity, independent bootstrap owners,
unknown-class lookup, and exact-instance callback boundaries. The ignored
`work/native-igb-registration-*` research records preserve registration arguments,
default-table captures, physical field records and vtable/cache evidence.
Original instruction words and game assets are not part of the public module.

The permanent suite passes 276 checks. A separate driver linked only against the
final production core matches all 114 independently recovered fact records with
zero differences: twenty object layouts, forty-seven field-class metadata
records and forty-seven singleton metadata bindings. The object layouts contain
forty-seven distinct physical field descriptors after inheritance and the three
replacement operations. Expected records come from the executable registration
and defaults captures; they do not read the native descriptor tables or derive
runtime counts from serialized data. This validates the represented registration
facts, rather than claiming a complete execution of the original initialization
or allocation paths.

The supplied-data decoder-prefix test also uses this metadata set with explicit
native stream, scratch-pool, list-constructor and timer bindings. It resolves the
twenty bootstrap object types, preserves the one unmatched field-class entry,
and reaches the next unrecovered type-preparation operation at `800505E0` without
claiming file-load completion.
