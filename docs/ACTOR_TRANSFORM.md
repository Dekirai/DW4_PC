# Original actor transform update

`ActorTransformUpdateInvocation` reconstructs the USA `igTransform` update at
`801D73A8`. It is the native fallback for the transform visitor's node virtual
`+9C` call after the visitor has captured its time. This completes the verified
null-input path and exposes the actual input operation when one is present;
it does not supply a general animation or transform-input implementation.

## Original binding and field evidence

The supplied USA `main.dol` has SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Its concrete table entries bind both
reached classes to the same function:

| Exact concrete type | Vtable | Slot `+9C` | Target |
| --- | --- | --- | --- |
| `igTransform` | `804B34FC` | `804B3598` | `801D73A8` |
| `beCopyTransform` | `804D4BA4` | `804D4C40` | `801D73A8` |

`beCopyTransform` registration at `802E0684` supplies constructor/type-size
probe `802E03AC`. Its store at `802E0438` establishes the concrete vtable and
its reported object size is `0x70`. These bindings, rather than an inheritance
assumption, justify handling the two exact types. Other concrete transform
subclasses remain pending until their actual update binding is established or
a whole-node update provider is supplied before entry.

`igTransform` metadata construction `801AC968` uses the field-name table at
`804ABB40` and offset table at `804ABB60`:

| Serialized slot | Original field | Object offset |
| --- | --- | --- |
| 8 | `_m` | `+20` |
| 9 | `_target` | `+60` |
| 10 | `_mvm` | `+64` |
| 11 | `_transformInput` | `+68` |

Slot 11 is an `igObjectRefMetaField`; its metadata class is established through
the metadata getter `801AB2B4`. Its registration at `801AB354` names
`igTransformSource` using the original string at `804AB4FC`; the serialized
field name `_transformInput` is not the name of its permitted base class. The separate
`beCopyTransform` field 12, `_srcTransform`, is registered at `802E0724` with
offset `+6C`. Its destructor `802E0464` also releases `+6C` separately from the
inherited transform fields. The input owner and source-transform owner must
therefore remain distinct even when both are null.

## Recovered operation

The function body is only `0x40` bytes. It reads `node+68` and returns immediately
when that input is null. It does not read the matrix, the incoming time, the
copy-source field, or a clock/cache field on this branch. Matrix values,
including nonfinite values, remain unchanged.

For a nonnull input it loads the input's virtual `+5C`, passes the input as
`r3`, the node's inline matrix at `node+20` as `r4`, and leaves incoming `r5/r6`
unchanged. The native service receives those same logical arguments as retained
input and matrix owners plus the already captured 64-bit time. Any matrix
writes performed by the input operation remain visible. The collision visitor
does not consume the update's return register as a semantic result, so native
completion reports zero.

The caller still obtains the time before this update, including for a null
input. `NativeGameClock` and the visitor's bound-time behavior are documented in
[GAME_CLOCK.md](GAME_CLOCK.md) and [HITLAND_NODE_DISPATCH.md](HITLAND_NODE_DISPATCH.md).
The earlier dynamic-collision source-matrix copy also remains at its original
traversal boundary; it is not repeated inside this update.

## Native ownership and continuation

`ActorSceneNode` represents slot 11 with a separate availability flag and a
nullable canonical `ModelSceneObject` owner. A missing serialized slot remains
unavailable and fails only if an operation reads or writes it. An explicitly
present null slot completes this update. An unrelated node class's slot 11 does
not become a transform input by sharing the same slot number.

Loading uses `ModelSceneObjects::object`, preserving physical reference aliases
and the registry's public nested transaction semantics. A completed input owner
that has escaped into the independently cached node graph remains canonical if
a later outer model load fails. Field type, width and reference validation use
the existing document readers. The field's original `igTransformSource` base
class constraint is checked before loading a nonnull owner and before replacing
the runtime slot. Unknown input subclasses retain canonical generic owners and
remain pending at their unresolved operation. An unrelated model owner is
rejected before its concrete loader could recursively load more nodes. The
owner retains its source document without retaining the resource facade.

A per-resource active node-visit guard also spans nested public loads through
the model registry. It preserves the existing root-at-zero limit of 128 edges;
alternating model and node references cannot reset that active recursion
budget. The guard unwinds on success and failure and does not replace the
registries' existing canonical caches or cycle detection.

The invocation captures its node and time at construction, then reads its input
once at the reached update. A nonnull input also captures the node's inline
matrix allocation. Without a provider, execution remains pending at the input
virtual call. Once a supplied provider is entered, its service owner and
callback are retained across suspension. Changing the node's input slot or the
service's callback does not retarget that entered call; partial matrix writes
are preserved. Switching service owners during that call, recursive entry, or
a thrown error faults the invocation, which cannot be replayed. Completed calls
also do not replay their effects.

`ActorTransformUpdateServices::input_update` is an explicit unresolved-operation
boundary. It must implement the selected input's actual virtual `+5C` behavior;
an arbitrary nonnull input is never treated as a successful no-op. The core has
no SDL, renderer, or host clock dependency here.

## Validation scope

Original-data validation caught an initial incorrect inference from the field
name: a guard against a class named `igTransformInput` passed synthetic fixtures
carrying that same mistaken name but rejected the supplied stage/player files. Rechecking the actual
metadata registration established `igTransformSource` as the permitted base.
The corrected guard retains type validation and accepts its actual derived
classes; it does not make unrelated model objects valid input owners.

`actor_transform_tests` covers canonical and aliased inputs, null and missing
fields, malformed field type/width/reference, exact-type binding, matrix-word
preservation, captured 64-bit time and owners, provider suspension, partial
publication, recursion, replay prevention, canonical input identity across an
outer registry rollback, and malformed cycles crossing the actor and model
registries. Long alternating model/node chains exercise early wrong-type
rejection, failure cleanup and later canonical cache reuse. Existing actor
scene tests retain their valid exact 128-edge node-path acceptance boundary.

Its optional extracted-root argument loads `ndmw.afs[179]`, collision info
24723, and examines the 56 dynamic child occurrences. The integration checks
each authored `beCopyTransform` input field and separate source link, then
compares every matrix word across a native null-input update. It also checks
the independently inspected first child 21503 and source node 24992. Exact
`igTransform` 1873 retains its real `igTransformSequence1_5` input 1874 and
remains pending at that input's virtual `+5C`, preserving every matrix word.
The final local run passed 1,151 checks, including 82 synthetic checks and all
56 original copy-transform updates. These results validate this operation; full game startup,
animation-input coverage and interactive gameplay remain separate work.

Local instruction probes and disassembly remain in ignored `work/`; no game
binary or generated instruction listing is required by the public tests.
