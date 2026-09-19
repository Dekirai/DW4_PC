# ThreadManager registration and metadata-pool selection

`igb_thread_registration` reconstructs original `800238A8`, the first class
body reached by the prerequisite sequence used during file registration. It
composes the native prerequisite guard, generic registrar, callback runner,
and general pool-by-handle lookup. It also provides the lazy full metadata
getter `80023844`, reached through `80023920`.

The evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Public source contains authored
logic and synthetic fixtures, not original instructions or assets.

## Exact registration

The canonical optional `IgbThreadRegistrationState::metadata` represents the
`805614E8` cell. Missing storage remains unknown; an engaged null is a known
null metadata pointer. The registration arguments alias this cell rather than
copying a separate cache.

`800238A8` first executes `80021B94`. On the recursive entry from the original
prerequisite sequence, the already-published started byte suppresses another
copy of that sequence. The exact arguments to `80066204` are:

| Argument | Original value |
| --- | --- |
| Name, abstract flag, size | `igThreadManager`, `1`, `0x10` |
| Parent registration/getter | `80066B08` (`igObject`) / `800237D0` |
| Type getter | `80023920` |
| Layout probe | Null, skipped for abstract flag `1` |
| Field/default callback | `80023940` |
| Extension callbacks | `80023F28`, `80026098`, zero terminator |
| Trailing callback | `80023A30` |

The native wrapper executes these real caller bodies. Its remaining field,
parent and activation operations delegate or suspend; they do not fabricate
an empty completed type. In particular, `80023940` installs two ObjectRef
fields, `_threadList` and `_sema`, and a concrete platform factory redirect.
`80023A30` enters `800755F8`, which constructs and activates actual thread
objects. Neither operation is an ignorable registration annotation.

The lazy getter reads the canonical cell. It calls the native
`80023880 -> 80066188(800238A8)` wrapper when the pointer is null or ready
flag `4` is clear. It waits for every returned callback and list cleanup,
then reloads the cell. The returned owner can differ from the initially read
one. A known null final result is preserved; an unknown final cell remains
pending without replaying completed registration.

## Proven handle publication and pool access

`publish_original_igb_metadata_pool_handle` performs only the proven store of
literal `5` to the metadata handle represented by
`IgbTypeRegistrationServices::metadata_pool_handle`. Original instruction
`800947EC` writes that value inside static initializer `800946A0`, whose
pointer is present at `80412E64`. The helper does not claim that the entire
static initializer, memory manager activation, or an allocation pool has run.
Absent optional state remains unknown until explicit publication.

`make_native_igb_registration_pool_factory` routes generic registration's
`pool_for_handle` to the actual `IgbPoolByHandleInvocation`. The bootstrap
adapter routes the same operation kind from `IgbBootstrapServices`; other
operations remain delegated. In particular, object allocation-pool lookup
and object-owned pool selection are different helpers and are not substituted
with the active manager's requested slot.

The general getter preserves the requested handle through manager selection,
unknown virtual dispatch, lazy preparation and previous-manager fallback.
The existing current-pool entry remains fixed at handle `2`. A valid
metadata handle does not prove the selected manager or its physical pool
slot exists. Missing represented storage pauses, while an original reached
null dereference fails.

## Continuations and validation scope

Native adapters hold service bindings weakly. A selected operation retains
its services, canonical metadata owner and nested invocations until its
continuation completes. Changing bindings during suspension does not restart
registration, replace an already-selected operation, or replay earlier pool
reads. Exceptions and recursive entry permanently invalidate the affected
invocation. Metadata publication and original flag mutations are not rolled
back.

Permanent tests cover exact argument values and aliasing, explicit handle
publication, native handle `5` selection, the recursively entered prerequisite
guard, ready and busy metadata paths, the real field callback boundary,
lazy getter capture and reload, null/unknown distinction, adapter lifetime and
recursive faults. The file-pool suite also covers all 46 physical handles,
invalid handles and unknown virtual forwarding. Prepared ready metadata and
name-setter endpoints are labeled as synthetic caller fixtures; they do not
prove original ThreadManager fields, allocation or activation.

The optional supplied-data test preserves the original decoder's position,
loading state, dependencies and completed type mappings while resuming the
first prerequisite into this module. It explicitly supplies the proven
handle publication and a prepared manager/pool identity, then enters the
actual metadata factory `80029E64`. Its canonical runtime remains enabled and
reaches the real `igMetaObject` registration `8002A0A8`; no metadata instance
is returned. The test does not silently select the different disabled
bootstrap constructor path.
