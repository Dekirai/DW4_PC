# Original DATABASE manager ownership

`DatabaseRuntime` attaches one generated native `beDBManager` identity to the
existing `ActionEventRuntime` messenger. Its immutable source programs decode
the actual exported `beDBManagerInfo` objects, their ordered RAM occurrences and
their `beDataBase` references. It does not construct a second messenger or use
database names as source identities.

`decode_database_managers(document, source, occurrence)` requires a little-endian
IGB document and its exported `igInfoList`. It accepts only the exact
`beDBManagerInfo` type; a descendant is skipped by the original exact type test.
Unrelated info exports are retained by their own systems. The decoded fields are:

| Object | Serialized slots | Meaning |
| --- | --- | --- |
| `beDBManagerInfo` | 2: string; 4: byte; 5: object ref; 6: object ref; 7: object ref | Nullable name, authored info byte, optional base data, required RAM list, nullable database |
| `beBaseInfoRamList` | 2/3: signed word fields; 4: memory ref | Count, capacity, ordered four-byte object references |
| `beBaseInfoRam` | 2: signed word; 5: byte; 6–9: signed words; 10: object ref | Authored index, active byte, four work words, optional base data |

Names preserve null versus empty. Active values preserve all eight bits; the
messenger's enabled-only test treats any nonzero RAM byte as active. The info
byte is retained as authored metadata and does not override RAM activity.
Referenced base-data lists are type-checked but have no invented DB-specific
initialization effect. A null database remains a null object on delivery.
The typed database graph and lookup behavior are described in
[DATABASE.md](DATABASE.md).

Raw exported info and RAM references remain visible alongside their canonical
physical object references. Repeated database aliases in one loaded document
share the same `shared_ptr<const DatabaseDefinition>`. Repeated loads have
different source occurrence identities and distinct owners, even when their
paths and bytes match.

## Registration and dispatch

The native factory is `802DE3F0`; manager registration `802DE608` supplies type
`beDBManager`, size `0x20`, and vtable `804E0860`. The original runtime manager
owner is allocated at `80318D58` and installed into its existing info context at
`803199B4`. The native facade represents that manager allocation separately from
the serialized info/RAM identities; it does not claim the complete original
controller construction or shutdown sequence.

Generic info initialization `802F251C` compares exact info types, appends the
info, invokes the empty DB info hook `802F2234`, then walks the RAM list in order.
At `802F25B4..25CC` it zeroes all four work words, writes manager and info owners,
and writes the current ordinal before registering that RAM. DB registration
`8031F83C` uses category `DATABASE` and that ordinal. Its helper `80305308`
substitutes the empty literal for a null info name at `8030532C`; the immutable
metadata still preserves the original null versus empty distinction.
The authored RAM active byte survives this loop.

`register_source` prepares every provider and registration row before committing
them through `ActionEventRuntime::register_controllers`. Each physical RAM has
one provider. Repeated info/RAM occurrences produce repeated ordered rows.
Shared RAM state ends with the last assigned info and ordinal; each category row
keeps the index captured at its own registration step. Consequently, an earlier
row can return the database belonging to the RAM's later parent. This is not
correctly represented by a separate immutable database payload per row.

The DB manager's **structured hook** at vtable `+90` is `802F2238`, which returns
zero unconditionally. Its **fallback hook** at `+8C` is `8031F890`. The latter
resolves the send's captured command through `80304FD8` and the one-entry enum
`802DE390`: exact `GETDATABASE` has value zero. At `8031F8D4..E0`, it reads the
RAM's current parent info, appends that info's same database object to the current
shared object list, and returns one. Unknown commands return zero. The fallback
return does not replace the messenger's existing fallback completion semantics.

Using the captured fallback text matters when an earlier recipient changes a
live cached message. DB dispatch must not substitute
`MessengerDelivery.message.command`. It neither selects another category nor
clears arguments, pushes a buffer, copies the database, or performs a query.
Completed delivery prefixes remain owned by the existing messenger continuation.

## Ownership and checked limits

Registered callbacks retain each RAM state and its typed database independently
of the inspection facade's lifetime. The facade is noncopyable and nonmovable;
its `ActionEventRuntime` must remain alive when registering further sources.
`ram(id)` accepts actual raw or canonical RAM IDs from its own registered source.
`set_active(id, byte)` is explicit preparation of live RAM state, not an inferred
original activation or frame scheduler.

A source occurrence can be installed once per DB runtime. Conflicting existing
provider ownership, inconsistent source paths or exhausted registration capacity
reject before local source/RAM publication. Registration preserves the existing
message selection, argument buffers and populated recipient caches. It does not
refresh an old cache to make a newly installed DB visible.

Each program bounds exported rows at 65,536, accepted infos and aggregate RAM
occurrences at 4,096, visited list entries at 131,072, and info/source names at
4,096 bytes each. Info names have an 8 MiB aggregate budget. Unique decoded
database owners share a 128 MiB ownership budget, including vector elements and
names; each individual database also has its decoder's bounds. One bounded
database may be decoded temporarily before the aggregate guard rejects it.
The existing event runtime additionally bounds total prepared RAM providers at
4,096 and messenger registration rows at 65,536. Malformed references, fields,
list storage and duplicate field slots are checked failures.

## Validation boundary

The asset-free `database_runtime_tests` suite passes 126 checks and covers exact versus derived info
types, raw and physical info/RAM aliases, final parent/ordinal writes, repeated
rows, distinct repeated source loads, typed database pointer identity, null and
empty names, null database output, noncanonical active bytes, source/provider
collisions, capacity rollback and retry, facade lifetime, unchanged argument
lists, populated recipient caches, completed-send retry and a preceding recipient
that suspends then changes the live command before DB fallback.

The optional original-data path adds 56 checks, for 182 total. It accepts
`ORIGINAL_ROOT` and checks the supplied
`area.afs` entry 9 USA pack. Its 12 exact DB exports include `pcTech` info 1500,
RAM 1516 and database 1592, and `shinka` info 1897/database 1879. The path sends
through the actual registered info names and RAM indices, checks retained owner
identity, and verifies the delivered pcTech's 100 rows, signed-byte `SKILL` and
negative signed-short `HP` example. It does not supply generated player choices
or claim skill growth, complete combat, database cache acquisition, or full game
startup.

An independent ignored comparison executes 48 synthetic cases through actual
`802F251C` initialization, `8031F83C`/`80305308` registration, messenger
selection/send, the structured-zero hook, and `8031F890`/`80306E2C` fallback
append. The native runner linked only with the final Release core matches all cases with
zero mismatches across 787,966 guest instructions, including 71,912 registration
instructions. It compares final RAM owners/ordinals/active bytes/work words,
registration counts, ordered database outputs, null-name normalization, live
index/active filtering, null database output and retained integer/string/object
prefixes. List allocation/mutation, strings, the info type getter, controller
context callback and exact command-enum lookup are explicit prepared services;
their complete helper bodies and the native constructor are outside the claim.
The fixture data are synthetic, and no original bytes are passed to the native
runner.

The 126 synthetic checks, 182 original-inclusive checks and 48-case comparison
were rerun against the final Release build. The ignored native comparison used
only `dw4_core.lib`, with no source overrides (SHA-256
`0AB04C059583A239C1AFB25AFF57AA585DCDEF30EB994FADE8EC9D665A0D6047`).

The manager control flow above was checked against the supplied USA `main.dol`
(SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`). Original data and disassembly
remain in ignored local evidence. The original-data integration separately checks
serialized ownership; it is not an execution of the original full startup.
