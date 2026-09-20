# IGB field operations

The native field-operation module recovers descriptor naming, finalization,
default-buffer preparation and metadata-list insertion from the supplied USA
executable. It shares canonical `IgbDecodeRuntimeField` owners with the concrete
field factory and registration code. It does not derive runtime descriptors
from serialized IGB field names or counts.

## Recovered operations

| Native entry | Original entry | Behavior |
| --- | --- | --- |
| `IgbFieldNameInvocation` | `80063D54` | Assign a name through a mutable string-pointer cell. |
| `IgbFieldFinalizeInvocation` | `80063F14` | Resolve type ordinal, size and defaults, then publish a descriptor global. |
| `IgbFieldAppendInvocation` | `80065844` | Finalize, retain and append through the existing native pointer-list implementation. |
| `IgbFieldBufferInvocation` | `80063C74` | Query the required size and conditionally replace the default-data allocation. |

`IgbFieldNameSlot` and `IgbRuntimeFieldSlot` distinguish unknown state from a
represented null pointer. Invocations retain their selected operations across a
pause. Missing services remain explicit boundaries, and a failed or recursive
invocation cannot silently replay an already executed prefix.

## Name assignment

`80063D54` receives the address of a string-pointer cell. It first reads and
retains that cell's current header, then reads and releases the field's live old
name. After a release callback, it reads the source cell again for the final raw
publication. The initially retained and finally published names can differ.
The source cell may alias the field's own name slot.

Reference increments and decrements use the complete wrapping 32-bit word. A
decrement reaching zero selects the shared string-release service with the
captured header and container. A later failure does not undo the initial retain.
Host ownership preserves displaced and acquired owners without adding guest
reference-count operations that the original did not perform.

## Finalization and append

Finalization reloads the live field vtable separately for each operation:

1. Virtual `+5C` returns a nullable descriptor. Its ordinal at `+12` becomes the
   receiver's type ordinal at `+10`; a null result supplies `FFFF`.
2. Virtual `+64` returns the size. Its low 16 bits are stored at `+14`.
3. Virtual `+8C` receives a literal null default-data source.

After the third operation, finalization reads the field's live global-cell
pointer at `+1C`. A nonnull cell receives the field without an additional guest
retain or release. A callback may replace this cell or the vtable before the
next original read.

`80065844` completes this sequence before reading metadata `+28`. It then
retains the captured field and invokes the recovered raw pointer-list append.
The destination list is captured before the retain. Finalization failure
prevents append; append failure preserves completed publication and retain
effects. Pointer identities must be explicitly bound for the native list
store; no host-pointer cast supplies a guest address.

## Default-data allocation

`80063C74` calls the live size virtual and narrows its result to 16 bits. A null
default pointer selects allocation. A nonnull pointer is captured for the
allocation-owner lookup and size query. The returned allocation-size word is
compared as a signed 32-bit value against the freshly read unsigned field size.

If replacement is required, the original reloads the pointer before free and
the field size again before allocation. The old pointer remains published
between those operations. The wrappers capture the free pointer or allocation
arguments across pool selection, which uses the live reference word's high
byte. The new pointer is published only after allocation returns; a represented
null allocation result is preserved as null.

The reached backing-owner lookup, tagged-pool lookup and pool virtuals remain
explicit services:

- `590A0` and pool `+150` for allocation size;
- `68430` / `5929C` and pool `+108` for free;
- `68254`, `68430` / `5929C` and pool `+E0` for allocation with alignment `1`.

Known physical identities and canonical storage are separate. An unknown
binding pauses at its reached use; it does not substitute a successful
allocation or infer capacity from a host container.

Constructed descriptors can retain an optional weak binding to their physical
allocation. Size stores at `+14` and default-pointer publication at `+20` also
update that storage before updating the canonical typed fields. Expired storage
or a changed typed identity is an explicit failure. The back-reference does not
own the allocation and does not introduce an allocation/descriptor cycle.
This binding does not imply physical mirroring of every other field property.

## Concrete unsigned-int field binding

`make_native_igb_unsigned_int_field_factory` recognizes exact vtable
`80476E6C`:

| Slot | Original target | Native behavior |
| --- | --- | --- |
| `+5C` | `80094140` | Read canonical prototype global `805614C0` without registration. |
| `+64` | `800759D8` | Return size `4`. |
| `+8C`, null source | `80063D04` | Execute the actual default-buffer preparation caller. |

The companion [field-values module](IGB_FIELD_VALUES.md) now supplies all seven
reached prototype/size/default-copy bindings, nonnull byte copying and the
string default setter. The separate [field-schema module](IGB_FIELD_SCHEMA.md)
implements configuration caller `80063BA8`. Unknown field virtuals and complete
engine registration remain explicit boundaries.

## Validation scope

Permanent tests cover source-cell aliasing and replacement, wrapping reference
counts, retained continuations, callback mutation, fresh virtual/global reads,
default-buffer failure prefixes and composition with native pointer-list
insertion. Weak-owner regressions separately check host lifetime; a numeric
fixture that keeps all owners alive cannot establish that property.

The ignored original-instruction corpus executes naming, finalization,
finalize-and-append and buffer preparation, including the concrete unsigned-int
virtual bodies and the actual retained/raw append and allocation wrappers.
Only string release, unknown field virtuals and underlying pool operations are
prepared endpoints. Append fixtures stay within available physical backing;
growth is qualified by the separate pointer-list evidence. There is no claim
of arbitrary memory-fault equivalence, a complete pool implementation or a
completed game startup.

The original unsigned-int milestone's production-core comparison passed **1,360 cases**, including **924
bounded failure prefixes**, with **zero differences** across **54,916 executed
original instructions** and **262 distinct instruction addresses**. The
independent decoder and endpoint scope were reviewed separately. The core
SHA-256 is
`47535aeed92fc884fd5a9bce4940f14c270154dae59e13418aed1a4ae4583a77`.
The ignored report `work/native-field-operations-result.json` records the
matching driver and source hashes. The permanent suite passed **58 checks**.
The current suite passes **65 checks**, adding physical-storage publication
and lifetime cases. The newer seven-profile comparisons and their final core
identity are recorded in [VALIDATION.md](VALIDATION.md).
