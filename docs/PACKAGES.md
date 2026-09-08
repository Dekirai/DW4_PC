# Named package descriptors

The portable package reader connects an exact catalog name such as
`dng101b1.igb` to its original ordered list of archive members. A selected member
can then be read as an owned `IgbDocument`, and its explicit info exports can be
listed. This is package inspection; the game's asynchronous loading,
initialization, sound, scripts and dependency activation are not implemented.

## Schema and provenance

The descriptor must export exactly one exact `beFileListInfo` through its
explicit `igInfoList`. Finding a similarly named object elsewhere in the
document does not select a descriptor. The verified inheritance is
`beFileListInfo -> igInfo -> igNamedObject -> igObject`.

| Serialized field | Original field | Interpretation |
| --- | --- | --- |
| Slot 2 | `igNamedObject` name | Descriptor label |
| Slot 4 | `igInfo::_resolveState` | Stored boolean metadata; not an enable flag |
| Slot 5 | `_section` | Original section string, independently retained |
| Slot 6 | `_fnames` | Local `igNamedObjectList` reference |
| Filename object's slot 2 | `igNamedObject` name | Exact member specifier |

Each filename-list item must be an exact `igNamedObject`. The original list
ordinal and object reference are retained, including repeated entries. Section
names and filenames are not combined into guessed paths. Slot 3 is the inherited
`igInfo::_directory` field; it is not substituted for `_section` or `_fnames`.

These slots were checked against the USA executable's `beFileListInfo`
registration at `0x802DAB5C`. Its member table at `0x804D22A0` names `_section`
and `_fnames` and gives native object offsets `0x14` and `0x18`. The local USA
`main.dol` SHA-1 is `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original asset
bytes, disassembly and research output remain outside tracked source files.

## Member addresses and `/s`

The verified syntax is `[A.E]`, optionally followed by the exact suffix `/s`.
`A` is one decimal archive digit, and `E` is a nonnegative decimal entry index
that fits a signed 16-bit field. The native reader checks both indices against
the validated catalog and mounted archive bounds. It never derives an archive
filename from the number: `A` selects `ResourceCatalog::archives[A]` in its
original order. Matching logical aliases are retained in catalog order; a
packed entry can legitimately have no catalog alias.

The original resolver at `0x802A9828` checks the opening bracket and locates the
closing bracket. It copies one archive digit from offset 1 and the entry digits
from offset 3, then writes the two `igAdxAfsFile` fields at offsets `0x10` and
`0x12`. The opening wrapper at `0x802A8994` calls this resolver and sign-extends
both fields with `lha` at `0x802A89F8` and `0x802A89FC` before calling the AFS
opening routine. Suffix bytes lie outside the parsed entry number. The native
parser checks syntax and overflow explicitly; it does not reproduce the
original temporary-buffer copy or integer truncation.

`/s` denotes a preliminary loading stage in the original file-list manager.
The state machine at `0x802F8A64` scans for `/` at `0x802F91D0` and tests the
following `s` at `0x802F920C`. It moves these members into its filename queue,
loads that queue through `0x802F9E98` in state 100, and processes remaining
members through the same routine in state 2. The `/s` marker changes this
staging; it does not select another AFS payload.

Consequently, exact `/s` members support bounded byte inspection, with the
suffix and a stage diagnostic retained. This does not claim that the native
workbench executes those loading stages. Other suffixes and filename forms
remain visible as unsupported rows and cannot be opened by the package reader.
No suffix is silently stripped to turn an unsupported member into a supported
one.

For the original `dng101b1.igb`, the verified order is:

| Member index | Filename reference | Exact specifier | Physical location | Explicit info exports |
| --- | --- | --- | --- | --- |
| 0 | 4 | `[0.178]/s` | `ndmw.afs`, entry 178 | 4; first is `beStaticInfo` reference 499 |
| 1 | 0 | `[0.179]` | `ndmw.afs`, entry 179 | 419; first is `igSceneInfo` reference 1729 |

The descriptor itself is catalog archive 0, entry 80. Member 1 contains several
scene exports; no first scene or root is automatically chosen. Descriptor
references and member-document references belong to different documents.

## API and bounds

`load_package_descriptor(resources, logical_path, max_bytes)` reads one exact
available catalog entry. Its default read limit is 64 MiB, with an independent
4 MiB descriptor ceiling. A lower caller limit takes effect before archive
payload allocation. `read_package_descriptor(document, catalog)` provides the
same validation for an already owned document and explicitly validates the
caller-supplied catalog.

`read_package_member(resources, member, max_bytes)` parses the member specifier
again, validates the catalog range, and requires its stored location, suffix
and supported status to agree. Caller limits must be between 1 byte and
256 MiB; the default is 64 MiB. Diagnostics, labels and alias strings never
become host filesystem paths or determine the bytes being read.

`list_package_exports(document)` follows the explicit `igInfoList` only. It
preserves each original reference, exact object type, name and list order.
Every list element must be a non-null `igInfo`. This operation does not scan
all objects, infer scene ownership, or choose a root. An explicit empty info
list is valid; a missing list is an error.

Additional limits are 4,096 members/exports and list capacity, 4,096 bytes per
decoded metadata string, 4 MiB combined export strings, and 65,536 copied aliases
with an 8 MiB combined alias-string budget. Before trimming trailing NULs, each
serialized string field is limited to 4,104 bytes, including its length word and
terminator padding. A separate 4 MiB serialized text scan budget applies to each
descriptor read and each export-list operation. The descriptor budget is shared
by its name, section and every filename occurrence; repeated references consume
the budget again. Thus a small shared object with a padded label cannot multiply
the permitted string-scanning work. Valid 4,096-byte decoded labels remain
supported. Metadata control characters, wrong
field/list types, null required references, malformed list storage, duplicate
exported descriptors and unsupported descriptor subclasses are rejected.
The original index-copy profile also limits entry digits to 15 characters.
Unsupported address forms are retained as diagnostic rows rather than being
resolved through a fallback lookup. Underlying IGB and AFS validation remains
in effect.

## Validation

The asset-free tests generate their own IGB graph records, catalog and AFS
payloads. They check explicit list order, hidden-object exclusion, section/name
distinction, duplicate filename preservation, aliases, `/s` byte equivalence,
malformed syntax and references, field types, count/capacity limits, byte limits
and forged member metadata. Padded labels and repeated filename/export references
check serialized scan limits independently of decoded length and document size;
valid maximum-length labels check boundary compatibility. Temporary test archives
contain only synthetic data.

The optional local-data test checks the descriptor at entry 80 and exact byte
equivalence for members 178 and 179. It also checks 97 descriptor documents in
the inspected `ndmw.afs` entry range 0–176, containing 195 members: 97 bare
addresses and 98 `/s` addresses. All 195 resolve within the original catalog
bounds. This census establishes metadata/address coverage; it is not a claim
that every package's scenes, gameplay or initialization work natively.
