# Native IGB runtime type construction caller

`IgbTypeMappingInvocation` reconstructs original USA caller `8004FC94` over
canonical stream, table and runtime metatype owners. It reconciles registered
types and builds the original signed field-slot mappings. It also implements
the dynamic-type caller's ordering and arithmetic, while retaining explicit
boundaries for its engine constructors, field virtuals, registry operations
and final destructors.

Evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original data and disassembly remain
outside the public repository. [IGB_RUNTIME_TYPES.md](IGB_RUNTIME_TYPES.md)
documents the concrete registry lookup and runtime field-count helpers;
[IGB_DECODE_TABLES.md](IGB_DECODE_TABLES.md) documents the preceding table reads.
Parsing serialized type names alone does not satisfy this caller.

## Entry and registered types

The caller creates a runtime `igMetaObjectList` through `80034380` using the
captured file pool, then publishes it at file `+C0`. It reserves space when the
live serialized type count is at least the list's logical count, including
zero/zero. The comparison is against logical count rather than capacity.

The signed file version is then captured. Versions below four use three-word
descriptors and do not create slot maps. Versions at least four use six-word
descriptors, an `igShortListList` at file `+3C`, and a temporary short adjustment
list. Later version changes do not replace this captured profile. Descriptor
conversion uses the reached integer-field width; the descriptor allocation
and initial name pointer are captured after that conversion. Each outer
termination test still reads the live serialized type count.

Each name is passed to the actual runtime lookup without copying or validating
its bytes beforehand. `IgbRuntimeNameSource` retains the original scratch
allocation and offset. Bytes are consumed only by a reached C-string comparison
or by the explicit dynamic naming endpoint. A disabled or empty registry can
therefore return no match without touching unavailable name bytes; a paused
lookup observes byte changes made before its eventual comparison.

For a found canonical runtime type, serialized major version zero is normalized
to one. Major and minor must match the runtime metadata. A mismatch publishes
the live failure word and preserves the already constructed prefix. Compatible
types are appended to the live `+C0` list.

The version-four-and-later path creates and appends a short map for each type.
For every serialized field it reads the signed 16-bit slot, walks serialized
parent descriptors while that slot is less than the current descriptor's total
slots, and adds the selected ancestor's signed adjustment. The result narrows
to 16 bits when stored in the map. The next adjustment is the actual runtime
slot count from `80065D88` minus the serialized total, also narrowed to 16 bits.
Runtime counts are never inferred from serialized totals or native C++ sizes.

## Dynamic-type caller

An unknown type fails for the older descriptor profile. The newer profile
reads the actual dynamic-registration flag `8056213D`; zero fails. When enabled,
the caller selects the serialized parent from the already published type list
and rejects any nonzero parent byte `+1A`.

The native `8006603C` helper counts parent fields whose byte `+25` is zero.
The caller separately reloads the total runtime field count and captures their
difference. Default-pool lookup, metatype creation, name assignment and parent
assignment are explicit reached operations. No caller assignment fabricates
major/minor defaults for the newly constructed type.

Fields below the parent's serialized total are skipped. Other fields derive a
target slot from their signed serialized slot, the parent's adjustment and the
captured parent-count difference. Missing intermediate slots invoke the actual
byte-field factory and initialize fields named `fakeField`; real fields select
their existing field-instance metadata and invoke the generic constructor,
then initialize fields named `Generated`. Initialization remains a distinct
`80063BA8` boundary with the original zero, minus-one and new-type arguments.

The caller writes field ordinal/size and invokes alignment virtual `+6C`.
It preserves the original signed comparison for the class alignment, 16-bit
narrowing, and wrapping mask arithmetic for offsets and class size. This also
preserves unusual zero, non-power-of-two and high-word alignment behavior;
the caller does not replace it with a host alignment utility.

Field registration `80065844` remains explicit because its field virtuals can
change layout. The caller reloads the field's offset and unsigned size after
registration before publishing the new class size. It then rounds the final
class size, sets class flag bit four, and reaches both registry operations
`8003E450` and `8003E574`, reloading the registry singleton separately for each.
Only afterward does it create the dynamic type's slot map. Unlike the known
type path, this map has no initial reserve call.

## Ownership, lists and continuations

The mapping lists represent the original `igDataList` layout: logical count
`+08`, capacity `+0C`, backing `+10`, and an independently mutable packed
reference word `+04`. These are distinct from registry lookup containers.
Original full-word retains/decrements are explicit. A decrement whose low
23 bits become zero selects the final `80066E1C` destructor operation and can
pause there; host `shared_ptr` counts do not decide whether it is final.

Publication retains the incoming factory owner before releasing the old field,
stores only after the reached old destructor, and releases the list creator
temporary afterward. The per-record map likewise retains a list reference
separately from its creator reference. The original caller does not release
new dynamic metatype or field creator references; native state retains those
owners beyond invocation completion.

Object appends retain their incoming owner before the first list access.
The fast path publishes count before reading backing and storing. The slow
path reproduces `80041970`/`80041660`: capture the requested count, compute the
original growth capacity, call the selected allocator, then publish the
captured count over any allocator mutation and reload backing for the store.
Short appends preserve their earlier captured index. Raw stores do not release
an overwritten object slot; native state retains that otherwise lost owner.

Every pending operation retains its selected factory, callback and arguments.
Later original read sites continue to observe live file fields, descriptor
bytes, output lists, pools and globals. Completed callbacks are released before
the next original read, including the second registry-singleton load.
Missing status words pause at their reached reads without replaying completed
allocation, publication or callback prefixes. Malformed state, recursion or
abandonment faults the shared construction state rather than permitting a
silent restart with duplicated side effects.

Normal completion clears the live object-type buffer alias first, captures the
success word, then releases the temporary adjustment list. Failure captures
the failure word first, preserves that buffer alias and existing output lists,
then releases the temporary. Final destruction can remain pending after the
status has already been captured.

## Scope and bounds

Named factory, allocator, field and registry services are genuine remaining
engine boundaries. An unavailable binding stays pending; no generic
parse-success endpoint completes them. Original-data qualification must state
which registration facts and operation implementations were supplied and where
the decoder next stops. [VALIDATION.md](VALIDATION.md) records current test and
original-data results.

The native profile bounds physical list slots, descriptor visits, parent-walk
steps and dynamic field creations to 262,144, and reached C strings to 4,096
bytes. Initialized scratch bytes and released allocations are validated when
read. Invalid parent indices, cycles, null reached owners and missing physical
slots fail explicitly. These host limits do not claim malformed original
memory accesses were safe, nor do completed type mappings establish object
deserialization, loader completion or playable startup.

Final validation passed 107 permanent mapping checks and all 139 core CTest
suites. An independent final-core comparison found zero differences in 1,926
original-instruction cases, including 207 matching endpoint-failure prefixes,
across 2,216,149 executed instructions and 751 addresses. The corpus executes
the actual caller, registry lookup, field-count helpers, append/growth bodies
and byte swapping. Constructors, reserve allocation, final destruction,
default-pool/converter access, metatype naming/parent setup, field operations
and registry publication remain explicitly prepared endpoints in that
comparison; it does not prove those endpoint bodies or complete startup.
