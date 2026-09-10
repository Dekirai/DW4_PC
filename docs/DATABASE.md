# Original typed databases

`DatabaseDefinition` decodes an immutable, source-qualified `beDataBase`
graph and implements the original integer, boolean, string and row-count
queries. It preserves all six typed groups, exact column order, nullable
names, signed values and canonical object/memory identities. DATABASE info
registration, GETDATABASE delivery and GAMERAM's lazy cache are separate
owners. Decoding a database does not select a technique, populate WAZALIST or
initialize a ready player.

## API and ownership

```cpp
auto database = decode_database(document, database_reference,
                                source, source_occurrence);
auto skill = database.integer(row, "SKILL");
auto raw_flag = database.boolean(row, "PC");
auto name = database.string(row, "NAME");
```

The source string and loaded occurrence are explicit. `reference()` preserves
the requested serialized database reference; `owner_reference()` identifies
the canonical physical object. The separate info decoder must prove the
exported `beDBManagerInfo` and its database edge. The data decoder does not
infer that attachment from a name or export ordinal.

Copies of `DatabaseDefinition` share immutable decoded ownership. The six
group getters return const data:

- `char_group()` stores signed bytes.
- `short_group()` stores signed 16-bit values.
- `int_group()` stores signed 32-bit values.
- `float_group()` preserves finite 32-bit float values, including signed zero.
- `bool_group()` stores raw bytes without normalizing them to C++ `bool`.
- `string_group()` stores named-object cells with nullable string payloads.

Each group retains column-list and data-list identities, declared count and
capacity, optional raw memory references and canonical memory owners. Each
column and string cell retains both its raw reference and canonical object
owner. Distinct serialized aliases and repeated occurrences remain visible.
Canonical references must resolve to the same physical object or memory
record as their raw reference. The immutable values own their storage and
remain valid after the source document is destroyed. String query views remain
valid while a definition retaining that decoded owner remains alive.

## Exact serialized graph

The original USA DOL reference has SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Registration `802DDD9C` defines
`beDataBase` as an `igObject` of size `0x38`. The field name table at
`804D2518` and offset table at `804D2578` establish these pairs:

| Slots | Guest offsets | Column list / payload fields | Payload type |
| --- | --- | --- | --- |
| 2 / 3 | 08 / 0C | `_charOffsetList` / `_charData` | `igCharList` |
| 4 / 5 | 10 / 14 | `_shortOffsetList` / `_shortData` | `igShortList` |
| 6 / 7 | 18 / 1C | `_intOffsetList` / `_intData` | `igIntList` |
| 8 / 9 | 20 / 24 | `_floatOffsetList` / `_floatData` | `igFloatList` |
| 10 / 11 | 28 / 2C | `_boolOffsetList` / `_boolData` | `igBoolList` |
| 12 / 13 | 30 / 34 | `_stringOffsetList` / `_stringData` | `igNamedObjectList` |

All column lists are exact `igNamedObjectList` objects. Their elements, and
string payload cells, are exact `igNamedObject` objects with a nullable inline
name. Lists have signed count/capacity fields at slots 2/3 and memory at slot4.
Cell widths are respectively 1, 2, 4, 4, 1 and 4 bytes. Every group uses its
own row-major stride: `row * group_column_count + column_ordinal`.

Empty authored groups are valid: both list objects remain present, count and
capacity may be zero, and memory may be null. A non-null empty string differs
from a null name in decoded metadata, even though the string query returns an
empty view for either cell payload. Null column/cell **objects** are outside
the checked profile because the original helpers dereference those objects.

## Query behavior

Index helper `8031F54C` first searches exact names through
`800695CC`, then finds the first matching object pointer in the same ordered
column list. Null names are skipped. A null query has no match; a non-null
empty name can match an empty column name. Comparisons use exact bytes and
are case-sensitive. Duplicate names and aliases use the first occurrence.

`integer(row, column)` follows `8031F608`:

1. Search char columns; a match loads a byte and sign-extends it
   (`8031F644..8031F648`).
2. If absent, search short columns and load a signed halfword
   (`8031F678`).
3. If absent, search int columns and load its signed word
   (`8031F6A8`).
4. Return zero only if no matching column exists in those three groups
   (`8031F6B0`).

A negative char value still takes precedence over a same-name short or int
column. Missing integer columns return zero even when the supplied row is
negative, because the original performs no row addressing without a match.
A found column with an invalid row raises `DatabaseError`; it does not fall
through to another group. Integer queries never search float columns.

`boolean` follows `8031F6D0` and returns the raw byte. `string` follows
`8031F70C`, returning the selected cell object's name or an empty string when
that name is null. These helpers have no original safe missing-column result;
the native implementation rejects absent columns and invalid rows explicitly.

`row_count()` follows `8031F760`: the first group with nonempty columns
determines the result, in **string, char, short, int, float, bool** order.
It divides that group's data count by column count. Nonempty columns with
zero data rows determine a result of zero; lookup does not continue to another
group. The checked decoder permits different row counts in different groups,
and typed queries validate against the selected group's rows.

There is no float query API. Float storage and its row-count priority are
verified, but no float-valued accessor was recovered in the inspected skill
path. No conversion or absent-float default is invented.

## Actual pcTech and shinka assets

The mounted catalog maps `dng101b1/separate/usa_pack.igb` to `area.afs`
entry9. Its exported `beDBManagerInfo` reference1500 is named `pcTech`, with
RAM-list772, RAM1516 and database1592. Its 100-row database contains:

| Group | Column list / data list | Columns | Payload |
| --- | --- | --- | --- |
| Char | 1575 / 1567 | 6 | memory1534, 600 bytes |
| Short | 1576 / 1568 | 11 | memory1535, 2,200 bytes |
| Int | 1580 / 1582 | 0 | Authored empty list |
| Float | 1593 / 1594 | 0 | Authored empty list |
| Bool | 1595 / 1569 | 2 | memory2033, 200 bytes |
| String | 1570 / 1571 | 4 | memory2034, 1,600 bytes |

Char columns are `RANK`, `CHIMEIRITSU`, `KAIHIRITSU`, `MHIRITSU`, `SKILL`,
`DELAY`. Short columns are `MP`, `HP`, `TSUYOSA`, `MAMORI`, `SUBAYASA`,
`MAXHP`, `TIME`, `FIRE`, `ICE`, `THUNDER`, `DARK`. Bool columns are `PC` and
`ENEMY`; string columns are `NAME`, `ZOKUSEI`, `TXT` and `LABEL`.

Actual row0 is `Petty Fire`, with signed-char SKILL6, short MP16 and HP-160.
Row40 has SKILL7 and HP300. These values do not supply a fallback technique
for an empty WAZALIST: the original caller must first identify its actual row.

The same resource exports shinka info1897 and database1879, with 16 rows.
Its row0 name is `Agumon`. The six authored TECH base rows are
0, 80, 40, 30, 35 and 75; their pcTech SKILL values are 6, 8, 7, 6, 6 and 8.
These are verified inputs to later player preparation, not evidence that the
current generated player rows have been selected or recalculated.

## Checked boundaries and validation

The decoder supports the verified little-endian graph profile. It requires
all six exact typed pairs, required field widths, consistent canonical refs,
exact memory capacity/element width, finite floats and complete rows within
each nonempty group. Partial rows are rejected even though the original
unchecked helper can address some cells in such a malformed group. Empty
columns with nonempty data are also rejected.

Limits are 4,096 columns per group, 1,048,576 values/capacity entries per data
list, 2,097,152 aggregate decoded elements, 16 MiB aggregate backing storage,
4 MiB aggregate name data and 4,096 bytes per string. Embedded NUL text,
unknown/extra fields, absent typed list owners and malformed references fail
with `DatabaseError`. These are explicit host validation boundaries, not
claims about the original behavior after invalid memory access or overflowing
row arithmetic.

The isolated `/W4` build is clean. `dw4_database_tests` passes 157 synthetic
checks for all groups, signed values, type precedence, null/empty strings,
duplicate names, canonical object/memory aliases, immutable lifetimes,
row-count priority, differing row counts and malformed graphs. Its optional
original-inclusive run passes 275 checks against the actual pcTech and shinka
assets:

```text
dw4_database_tests [EXTRACTED_ROOT]
```

An independent ignored raw-instruction comparison matches 107 cases against
the original lookup/index/row-count helpers, including 49 explicitly labeled
native checked stops. It executes 8,529 original instructions using prepared
memory/name-service inputs. Fifteen additional raw index arithmetic cases are
guest-only evidence. This validates the represented query operations and their
stated boundaries, not database loading, complete original manager execution,
skill growth or full player preparation.
