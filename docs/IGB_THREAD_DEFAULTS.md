# Native ThreadManager defaults

`IgbThreadDefaultsInvocation` reconstructs defaults callback `80023940` for
`igThreadManager`. Evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, instruction range
`80023940–80023A0C` and the referenced original schema tables. The implementation
composes native field batch/schema construction, the two actual allocation-only
dependency getters, and the actual bootstrap callback append. It does not mark
either dependency class registered or execute the queued callback.

## Recovered stores and calls

The caller first reads canonical ThreadManager metadata `805614E8` and stores
its volatile byte `+0x18 = 1`. It then reloads that global and captures the
receiver for field construction. Helper `65D88` reads the live field-list count;
this first local index is retained across all later callbacks.

`65924` appends two fields using the original factory table. Each `658E4`
selection reloads the captured metadata receiver's live field-list backing.
The first field receives the result of lazy getter `23E40` at `+0x38` and then
construct byte `+0x34 = 1`; the second receives getter `26020` in the same order.
The object-type pointers are nonowning. The caller does not change ownership
bytes `+0x35` or `+0x3C`.

| Index | Factory | Name | Descriptor global | Object offset | Dependency metadata |
| --- | --- | --- | --- | --- | --- |
| 0 | `8002879C` | `_threadList` | `805614EC` | `8` | `80561530`, `igNonRefCountedThreadList` |
| 1 | `8002879C` | `_sema` | `805614F0` | `12` | `805615EC`, `igSemaphore` |

Tables reside at `8055CF98` (factories), `8055CFA8` (names), `8055CFB0`
(descriptor-global pointers), and `8055CFB8` (offsets). Native `659C0` performs
the naming, configuration, default-buffer preparation, and finalization with
the captured receiver and first index. See [IGB_FIELD_SCHEMA.md](IGB_FIELD_SCHEMA.md).

After schema completion, the caller reloads `805614E8` again and stores
`factory_redirect +0x3C = 80023A10` on that fresh owner. It can differ from the
receiver whose fields were configured. Finally, `65D94(80023A50)` reads fresh
ArkCore `80562100` and its bootstrap callback list `+0x28`; native `3E4D8` /
`3E8B8` appends the function there. This is separate from the registrar's
returned callback list and ArkCore's per-type callback list `+0x2C`.

## Physical and canonical views

Known metadata allocations are supplied through `physical_metadata`.
Volatile and redirect stores update physical bytes before canonical values.
Descriptors use their existing `physical_storage` bindings for the reached
type-pointer and construct-byte stores. Type addresses come from the actual
getter allocation or explicit metadata-address mappings; an absent address
pauses before the physical store. No host pointer is cast to an original
address. A canonical-only fixture may omit physical storage, while represented
storage must pass identity and bounds validation.

Suspension preserves the selected field, dependency result, receiver, first
index, and all completed effects. Changing the global during a getter, schema
operation, or callback allocation does not redirect captured earlier work.
Recursive entry faults the invocation; raw overwrites and unfinished work retain
host owners without inventing guest reference releases.

## Scope and validation

Synthetic tests cover exact tables and stores, actual batch/schema/append
composition with explicit allocation and virtual endpoints, canonical-only
fixtures, dynamic inherited counts, metadata-global replacement, late dependency
and address bindings, schema and append suspension, physical fault prefixes,
and adapter selection. Known null dependency cells reach the real metadata
factory boundary rather than becoming manufactured ready children.

ThreadManager registration has abstract flag one and no layout probe. Its
extension callbacks `23F28` and `26098`, first-registration startup callback
`23A30`, redirect `23A10`, and queued teardown `23A50` are separate reached
behaviors. Completing this defaults caller does not establish their completion,
native threads/semaphores, full startup, or gameplay.

The current cold integration runs these defaults with actual reflected field
and metadata/list construction, reaches eight completed types and stops at
the first deferred `23F28` registration. Its 1,230 checks include the preceding
bootstrap checkpoints. A separate original-instruction comparison covers
11,556 cases with zero differences; prepared endpoint scope, instruction counts
and artifact hashes are recorded in [VALIDATION.md](VALIDATION.md).
