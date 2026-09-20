# Native field schema construction

`igb_field_schema` reconstructs three callers used by the original class-default
registrations. The evidence is the supplied USA executable, with SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original executable bytes and
disassembly remain outside the public source tree.

| Original entry | Native implementation | Recovered behavior |
| --- | --- | --- |
| `80065924` | `IgbFieldBatchInvocation` | Repeated pool lookup, field factory, raw list append, creator reference release |
| `80063BA8` | `IgbFieldConfigureInvocation` | Name ownership, descriptor-global pointer, offset, declaring metadata, default-buffer preparation |
| `800659C0` | `IgbFieldSchemaInvocation` | Captured field count, live parallel tables and field-list reads, configuration followed by finalization |

## Construction and ownership

The batch compares its index and requested count as signed 32-bit values.
Nonpositive counts return without touching metadata, tables or pools. Each
iteration reads global metadata-pool handle `805621F4`, executes the existing
native `800607F4` lookup and only then reads the live constructor-table entry.
Pending operations retain their selected function and pool.

The factory result enters `80065820`, which reads metadata `+28`, retains a
nonnull field using its entire 32-bit reference word, and calls the existing
raw `80053BC0` pointer-list implementation. This call does **not** finalize the
descriptor. The batch then decrements the creator reference and calls the
explicit `80066E1C` destruction boundary when the low 23 bits are zero. A null
factory result is appended before the later unconditional reference read fails.
Missing metadata fails before the retain; a null list fails after it.

`make_native_igb_field_schema_factory` connects original `80022A90` to the
existing authored unsigned-int descriptor factory. Other specialized constructor
identities remain reached operations. The module does not manufacture a generic
descriptor to complete an unknown class factory. Factory results may supply an
original address for the raw pointer-list mapping; addresses are never inferred
from host pointers.

## Configuration and live reads

Configuration skips interning for a null name. A nonnull name initializes the
original string-pool global lazily, interns the name, calls native `80063D54`,
then reloads and releases the temporary string header with full-word arithmetic.
Only after that release finishes does it write field `+1C`, `+08` and `+18`, in
that order, before calling native `80063C74` to prepare default storage.

Global-cell and declaring-type links remain weak references, matching the
existing canonical field representation. Global-cell arithmetic addresses and
owner bindings must agree when the original pointer store is reached. That
validation occurs after the name operations, without applying subsequent stores
on failure. Global publication itself belongs to the later finalizer.

The schema loop reads its field count once through `80065D88`. It begins the
parallel name/global/offset tables at entry zero, even when its first field index
skips inherited descriptors. Each iteration reloads metadata `+28`, reads the
name, captures the field-list backing, reads the global cell, selects the field,
and reads the offset. Configuration and `80063F14` finalization use that captured
field. Later iterations reload the list and backing while retaining the initial
count. Index comparisons are signed; the original four-byte pointer arithmetic
wraps at 32 bits.

An absent optional table entry pauses at that exact read. A represented null
name is valid; inaccessible physical table/field slots fail when reached.
Pending callbacks are retained and recursive invocations poison their caller.
Interrupted retained fields and temporary string owners remain represented
without invented guest reference releases.

## Verification scope

`igb_field_schema_tests` covers raw append bytes and ownership, signed empty
batches, live pool handles and factory entries, retained pending constructors,
list replacement during append, null factory/metadata/list failures, packed
creator destruction, interrupted ownership, name-release prefixes, raw stores,
captured schema counts, fresh list/backing reads, inherited-field offsets,
wrapped indices, malformed global bindings and recursive invalidation.

Prepared specialized factories, string allocation and allocator operations are
explicit fixtures. Native pool selection, pointer-list append, name assignment,
unsigned-int field virtuals, buffer preparation and finalization execute their
existing implementations. The focused tests do not establish complete class
registration, original game startup or a playable native game.
