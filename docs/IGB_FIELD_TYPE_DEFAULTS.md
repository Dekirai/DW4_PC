# Field type metadata defaults

`igb_field_type_defaults` reconstructs the original USA initialization bodies
`8002A764` (base field), `800273EC` (reference field), and `80028AB4` (object
reference field) as authored native C++. These bodies declare the fields that
describe the field classes themselves. They are part of the original metadata
registration chain reached while initializing the internal object lists.

## Original input tables

| Body | Canonical metadata cell | Local fields | Factories | Names | Global pointers | Offsets |
| --- | --- | ---: | --- | --- | --- | --- |
| `8002A764` | `805617BC` | 18 | `8046478C` | `804648D0` | `80464918` | `80464960` |
| `800273EC` | `80561684` | 2 | `8055D160` | `8055D168` | `8055D170` | `8055D178` |
| `80028AB4` | `805616E8` | 3 | `804640E8` | `8046410C` | `80464118` | `80464124` |

The object metadata cell is the existing object profile slot in
`IgbMetaFieldFactoryServices`. There is no second metadata cache. The original
factory identities, literal names, offsets, and destination global addresses
are represented separately in the default service tables.

The base field declares:

| Name | Member offset | Factory | Descriptor global |
| --- | --- | --- | --- |
| `_offset` | `08` | `8002E1AC` | `805617C0` |
| `_fieldName` | `0C` | `800251EC` | `805617C4` |
| `_typeIndex` | `10` | `800259A8` | `805617C8` |
| `_internalIndex` | `12` | `800259A8` | `805617CC` |
| `_size` | `14` | `80021E10` | `805617D0` |
| `_parentMetaObject` | `18` | `8002879C` | `805617D4` |
| `_externalEnumStorage` | `1C` | `800274EC` | `805617D8` |
| `_default` | `20` | `8002AC34` | `805617DC` |
| `_resetMethod` | `24` | `80037B1C` | `805617E0` |
| `_isRunTime` | `25` | `800385E0` | `805617E4` |
| `_persistent` | `26` | `800385E0` | `805617E8` |
| `_shallowCopyMethod` | `27` | `80037B1C` | `805617EC` |
| `_deepCopyMethod` | `28` | `80037B1C` | `805617F0` |
| `_constructCopyMethod` | `29` | `80037B1C` | `805617F4` |
| `_isAlikeExactlyMethod` | `2A` | `80037B1C` | `805617F8` |
| `_isAlikeDeepMethod` | `2B` | `80037B1C` | `805617FC` |
| `_isAlikeShallowMethod` | `2C` | `80037B1C` | `80561800` |
| `_invarianceFunction` | `30` | `800274EC` | `80561804` |

The reference class adds `_construct` and `_destruct`, two boolean fields at
offsets `34` and `35`, publishing `8056168C` and `80561690`. Its defaults body
performs no extra value or flag customization.

The object class adds `_metaObject`, `_refCounted`, and `_poolName`, using object,
boolean, and string descriptors at offsets `38`, `3C`, and `40`. Their globals
are `805616F0`, `805616F4`, and `805616F8`.

## Preserved behavior

Each body captures its metadata receiver and initial field count through
`80065D88`, executes the original `80065924` batch, applies its indexed
customizations, then calls `800659C0` with its original tables and initial
index. Existing inherited descriptors remain before the local batch. Batch
construction retains the existing per-entry pool lookup, raw append, and
creator release behavior; final schema configuration and finalization use the
existing native field operations.

Base defaults clear persistence on local indices `0`, `2` through `7`, and
`17`. The two short fields receive `FFFF` through the actual two-byte setter
shape of `800711A8`. The byte defaults are applied in original order:
`8=0`, `10=1`, `11=1`, `12=0`, `13=0`, `14=0`, `15=1`, `16=2`.
`80040030` and `8003EC68` both pack one byte before selecting the fresh virtual
`+8C` operation. Scalar storage survives suspension and preserves its original
one-byte or two-byte width.

For base local index `5`, the body calls the actual lazy metadata getter
`80029F84`, stores the returned metadata at object-type `+38`, clears byte
`+3C` and persistence `+26`, then sets `+28` and `+27`. Local index `6` also
sets `+28`. The memory-reference descriptor at index `7` sets `+35`, `+40`,
and `+41` before clearing persistence.

Object defaults use the same lazy metadata getter for their first descriptor,
clear its `+3C`, and set `+28` and `+27`, preserving persistence. Their second
descriptor receives the boolean default `1`.

The lazy getter reads canonical `8056174C` independently of the captured
defaults receiver. Unknown globals remain pending; explicit null receivers
fault when dereferenced. An already-entered lazy allocation cannot be bypassed
by a later global replacement. Indexed field accesses reload the live list
backing. The final schema captures the count at its own entry, after all
customizations. Selected operations and packed scalar inputs are retained
across suspension; recursion poisons the invocation even when an inner error
is swallowed by a dependency callback.

## Validation and limits

The caller test suite composes all three native defaults bodies with actual
batch/schema, field-buffer, field-finalization, size dispatch, and scalar copy
operations. Specialized constructor outputs, string interning results,
allocation capacities, inherited descriptors, and cold-zero prototype cells
are explicit prepared inputs in this suite. Separate constructor and field
value tests validate those native components; this caller suite does not
present its prepared allocations as proof of the full startup chain.

Tests verify all original rows, inherited-field preservation, packed defaults,
guest ownership, metadata identity, exact global publication, pending factory
and scalar prefixes, retained virtual selection, live list/count changes,
unknown/lazy globals, null faults, recursion, adapter dispatch, and abandoned
argument lifetime. The original instructions and literal tables are also
reviewed independently.

This module does not implement prototype callback `80028B64` or fabricate a
ready object-field prototype. The registration and integration layers decide
which subsequent original callback is reached. Missing specialized
constructors, unresolved prototype globals, and other dependency operations
remain explicit boundaries.
