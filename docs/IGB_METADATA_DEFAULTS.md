# Native metadata defaults

`IgbMetadataDefaultsInvocation` reconstructs original USA function `8002A164`
as authored C++. Its receiver is captured from canonical global `8056174C`,
shared with the existing metadata factory. The generic registration caller's
metadata argument does not replace that global read.

The caller captures the existing field count, invokes actual `80065924` with
25 constructor entries, customizes the resulting descriptors, and invokes
actual `800659C0` starting at the captured first local index. The schema helpers
perform actual name ownership, buffer preparation and descriptor finalization;
see [IGB_FIELD_SCHEMA.md](IGB_FIELD_SCHEMA.md). Existing inherited fields remain
before the local batch.

## Recovered tables

The original literal base is `80463100`. These four independent tables are
represented explicitly; global-cell addresses are not object-member offsets.

| Table | Original address | Entries |
| --- | --- | --- |
| Constructor functions | `80464304` | 25 live function identities |
| Field names | `80464478` | 25 original strings |
| Descriptor global cells | `804644DC` | `80561750` through `805617B0`, step 4 |
| Member offsets | `80464540` | `08,0C,10,14,18,19,1A,1C,20,24,28,2C,30,34,38,3C,40,44,48,4C,50,54,58,5C,60` |

The field names are `_id`, `_index`, `_majorVersion`, `_minorVersion`,
`_volatile`, `_persistent`, `_abstractState`, `_name`, `_runTimeSize`,
`_validity`, `_metaFields`, `_instanceCount`, `_instances`, `_children`,
`_parent`, `_abstractProxy`, `_writeProxy`, `_templateObject`, `_sizeofSize`,
`_setupFunction`, `_cleanupFunction`, `_getMetaObjectFunction`,
`_requiredAlignment`, `_vTablePointer` and `_poolName`.

The first constructor is actual `8002E1AC`. All specialized constructors used by
the table now execute through their original class profiles, with exact sizes,
vtables and reset chains; see [IGB_META_FIELD_FACTORY.md](IGB_META_FIELD_FACTORY.md).
The cold integration constructs all 25 local descriptors, rather than seeding
completed fields. Unknown factory identities still remain reached operations.

## Defaults and type references

The original caller sets signed integer defaults to `-1` for `_id`, `_index`,
`_runTimeSize` and `_sizeofSize`; `_majorVersion` becomes 1. `_persistent`
receives a one-byte 1, and `_requiredAlignment` receives a two-byte 4.
Helpers `80053650`, `8003EC68` and `80075E8C` pack those values as original
big-endian temporary bytes before reading the selected field's fresh virtual
`+8C`. The temporary storage and selected continuation survive suspension.
This nonnull source is distinct from the null-source finalization call.
The string default uses actual setter `80071694(field,0)`, including its
string-pool operations and captured default-buffer ownership. Scalar copies,
null-source preparation, prototype getters and alignment leaves are also native;
see [IGB_FIELD_VALUES.md](IGB_FIELD_VALUES.md).

Type-reference defaults use original lazy getters `8002DC6C` and `8002DFA8`
with globals `805619CC` and `805619D4`. A represented null triggers actual
pool lookup `800607F4` and metadata factory `80029E64`, followed by raw global
publication and reload. An unknown binding suspends. `_parent` performs the
same allocation-if-null behavior against canonical `8056174C` (`80029F84`).
`_templateObject` reads `805622A4` directly and permits a represented null.
These descriptor `+38` type links are nonowning, preserving the distinction
between null and an expired represented identity.

Each customization reloads the current field-list/backing. The receiver and
first local index remain captured even if callbacks change the metadata global.
The selected field survives its setter; later flag stores target that same
descriptor. Type-specific bytes `+34`, `+35`, `+3C` and persistence byte `+26`
are changed only at their original store sites. The final schema call observes
the field count at its own entry, independently of the initial captured count.

## Validation scope

The earlier isolated caller Release suite passed 1,477 checks. `igb_metadata_defaults_tests`
composes the actual batch, pointer-list append,
schema, name ownership, buffer preparation and finalization callers with
explicit prepared specialized field factories and virtual bodies. It checks
all table identities, scalar widths/bytes, every customization, inherited-field
offsets, global publication, weak back-references, retained pending operations,
live backing/count/global changes, null and missing prerequisites, recursive
failure propagation, and interrupted temporary lifetime. Separate cases enter
the actual lazy getter and metadata factory callers with prepared reflected
allocation outputs and verify raw overwrite/publication and cache reuse.

Those isolated tests establish caller behavior with prepared field bodies.
The separate cold registration integration now uses actual constructors for all
seven descriptor profiles reached across the base and local schema, native
default setters and schema/layout/type-publication operations. It constructs
25 local fields after two inherited fields and two lazy metadata/list globals
under explicit allocator, pool and BSS prerequisites. `igMetaObject` finishes
with flags `7` as the second published type. Deferred callbacks are appended
in order `8002DD18`, `8002E054`, `80066AE0`; the next unresolved callable is
internal field-list registration `8002DD18`. ArkCore remains disabled pending
callback completion. This does not establish complete startup or an updated
playable preview. Original
instructions and tables were read from the user-supplied USA `main.dol`
(SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`); original bytes and generated
disassembly remain outside tracked deliverables.
