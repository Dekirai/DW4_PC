# Local validation

## Native enum and raw-reference classes complete memory registration

Validated locally on 2026-09-23. The retained cold-bootstrap invocation now
executes enum descriptor factory `80031378`, registration `80031584`, actual
probe `800314D4` and defaults `80031614`. Its single pointer field requires
actual `igRawRefMetaField` registration `80027704` and probe `80027648`.
Neither class is supplied ready. The fixture supplies raw allocations and
the additionally reached child-list reallocation; native constructors, schema,
layout and prototype callers establish the contents and ownership.

Both prototypes complete before the memory defaults caller publishes its
replacement reset descriptor: four-byte value three, member offset `24`,
getter function `8002A5EC` at descriptor `+34`, and canonical cell `80561834`.
Memory metadata and its reflected prototype then complete. The same DataList
caller constructs its memory descriptor and reaches **unsigned-byte class
registration `80023234`**. That body is the next unimplemented operation.

The cold composition now contains **72 descriptors, 77 default buffers,
fifteen published types, eight prototypes and 48 enabled allocations**.
Enum, raw-reference and memory metadata have flags `7` and two instances each.
Their prototypes occupy indices 6, 5 and 7 respectively. DataList remains at
flags `9` with five fields, and its unsigned-byte prototype remains null.
The separate `igMetaEnum` metadata object also remains null. Repeated resumes
preserve completed work, the single deferred bootstrap callback and original
core snapshots `+04 = 7`, `+08 = 1`.

The tests also preserve a subtle original difference between construction paths:
the raw enum reset leaves `+34` untouched; reflected pointer-member initialization
zeroes it. The memory class's four-byte enum default overlaps three subsequent
byte fields. Their ordered writes produce `00000102` at `+24`; forcing the
reflected instance to the raw memory reset's byte-three value would be incorrect.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 195/195 local CTest suites passed with four build jobs; no compiler warnings |
| Cold composition | 2,060 checks, including actual schemas, raw allocations, physical publications, prototype lifecycle, overlapping defaults and stable next boundary |
| Enum defaults | 120 checks covering captured metadata/count, live registry replacement, wrapped indices, flag stores, pending table owners, faults and recursive callbacks |
| Descriptor lifecycle | Existing constructor/value/initializer tests extended to enum and raw-reference profiles; 437 registration and 160 prototype checks |
| Original decoder prefix | 400 original-inclusive /107 synthetic checks; separate enabled `8002A0A8` boundary unchanged |
| Player regression | 447 original-inclusive /62 synthetic checks; 48 held/released and 28 prepared scheduled player frames |
| Python asset verifier | Six synthetic tests passed |

Eight independently sealed original-code groups cover **10,137 observations /
935,846 instructions**: 3,996 completed original caller cases, 6,139 explicit
fault or endpoint-prefix cases and two registration-argument observations.
**10,128 observations match exactly; nine differ in state and endpoint trace.**
There are no native pending cases in this comparison. Native pending, fault and
completion statuses are recorded separately rather than normalized together.

The nine differences deliberately replace the live enum field registry with
more entries than the declared one-element schema table. Native schema processing
faults at the unavailable next table entry. The original continues into adjacent
mapped DOL words and executes seven additional endpoint events before a word
store at `80063F90` faults at address `00000034`. The first eleven events agree;
nine observed state words differ. These are explicit bounded-table differences,
not equivalent execution or successful native completion. Inputs and original
expectations were not changed or excluded to remove them.

Factory and probe groups compare whole represented storage, including enum `+34`
and padding. Defaults and prototype groups compare selected members, ordered
endpoint traces and represented list backing, not every descriptor byte. Actual
registration, allocation, interning and field operations are prepared where
declared by each corpus. The separate cold composition and focused physical
tests cover their native connections. This evidence does not establish complete
startup, enum conversions, all pointer/member operations or playable gameplay.

Final `dw4_core.lib` SHA-256:
`f4941d9dccd6cb8fbb6fd6184434d4f840fec279dd3906dec9e1277d1972618d`.
Ignored evidence includes `work/native-enum-class-complete-build.log`,
`work/native-enum-class-{cold,prefix,player,verifier}.log`,
`work/native-enum-class-original-manifest.json`,
`work/native-enum-class-registration-original-manifest.json`,
`work/native-enum-class-original-boundaries.json` and
`work/native-enum-class-milestone-audit.json`. Original binaries, corpora and
generated observations remain local and ignored. No application preview was
built or launched and no GitHub Actions were used. See
[IGB_ENUM_FIELD_DEFAULTS.md](IGB_ENUM_FIELD_DEFAULTS.md) and
[PORTING_STRATEGY.md](PORTING_STRATEGY.md).

## Native memory-member copying and completed string registration (earlier checkpoint)

Validated locally on 2026-09-23. The same retained cold-bootstrap invocation
now executes memory-member copy `80062824` with its actual null-element descriptor,
null destination and one-byte source default. Native `8005647C` allocates and
copies distinct backing through three owner queries and two size queries. The
remaining reflected member pass then finishes both inherited string replacements.
Their setters publish defaults two and one without modifying the inherited
source. String schema, registration, reflected prototype `80025628` and tail
`8007144C` all execute in this composition. Only raw backing allocation is supplied.

Memory defaults then construct their eighth local descriptor and perform their
own inherited shallow-copy replacement. The next required body is **reset-method
factory `80031378`**, observed with its captured pool in the actual schema factory.
The final composition has **69 descriptors, 69 default buffers, twelve published
types, five prototypes and 38 enabled allocations**, including three native
buffer clones. String metadata has flags `7` and two reflected instances; memory
metadata remains at flags `9`, with thirty fields and no prototype. Enum metadata
remains null. Repeated resumes preserve these owners, completed writes, allocator
calls and original core snapshots `+04 = 7`, `+08 = 1`.

The new memory caller also reconstructs `800626DC` object-element release,
post-copy object cloning and nested-buffer cloning. Full-word reference decrements,
low-23-bit destruction tests, captured counts/backings, live element reloads and
pointer publication retain their original order. Concrete object clone/destructor,
metadata virtuals and pool operations remain explicit endpoints. Canonical pointer
publication updates overlapping typed views, including unaligned partial members,
without reading stale physical neighbors; adjacent allocations remain distinct.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 194/194 local CTest suites passed with four build jobs; no compiler warnings |
| Cold composition | 1,812 checks, including string completion, actual prototype construction, memory replacement and pending `80031378` stability |
| Buffer helpers | 100 checks: exact queries/arguments, unequal sizes, live bytes, overlap, faults, teardown, pending bindings and retained owners |
| Memory-member caller | 51 checks: physical bounds, null gates, alias publication, live offsets, release/destructor continuation, object and nested-memory loops |
| Existing raw/string/object members | 71 checks retained after sharing the canonical byte-merge helper |
| Original decoder prefix | 400 original-inclusive /107 synthetic checks; separate enabled `8002A0A8` boundary remains unchanged |
| Player regression | 447 original-inclusive /62 synthetic checks; 48 held/released and 28 prepared scheduled player frames |
| Python asset verifier | Six synthetic tests passed |

Independent original-instruction evidence covers **3,174 cases / 841,156
instructions**: 732 buffer-helper cases and 2,442 whole memory-caller cases,
including actual ancestry traversal and both element loops. The original inputs,
expected outputs, oracles and imported helpers were sealed before native comparison.
**3,103 cases match exactly.** The remaining **71 differ in terminal status**:
the bounded original interpreter faults on unmapped storage, while the native
invocation suspends for an explicit missing buffer/object binding. Every ordered
endpoint trace and observed state word matches in those cases. They are recorded
as differences, not silently normalized or counted as completed native execution.
The original stops comprise 22 raw source-byte accesses, 31 object-vtable accesses
and 18 virtual-slot accesses. No other comparison difference was observed.

These corpora prepare owner lookup `800590A0`, pool virtuals `+150/+D8/+108`,
element metadata `+58`, object cloning `+54` and destruction `80066E1C`.
They do not qualify those endpoint internals, arbitrary address-space coverage,
console exception delivery or complete startup. Native canonical alias handling
and delayed host bindings have separate focused tests.

Final `dw4_core.lib` SHA-256:
`dde82a8f76e24c5021bda4008ac3bea5114dfd7228edeb627b29795cf7ada133`.
Ignored evidence: `work/native-memory-copy-verified-build.log`,
`work/native-memory-copy-{cold,prefix,player,verifier}.log`,
`work/native-memory-copy-original-manifest.json`,
`work/native-memory-copy-milestone-audit.json` and
`work/native-memory-copy-root-audit.json`. The audit retains the initial comparison
and all original-boundary status differences. Original data and instruction corpora
remain local and ignored. No application preview was rebuilt or launched;
no GitHub Actions were used. See [IGB_BUFFER_COPY.md](IGB_BUFFER_COPY.md),
[IGB_MEMORY_MEMBER_COPY.md](IGB_MEMORY_MEMBER_COPY.md) and
[PORTING_STRATEGY.md](PORTING_STRATEGY.md) for the next original bodies.

## Native reflected cloning and base-field member copies (earlier checkpoint)

Validated locally on 2026-09-23. The same retained cold-bootstrap invocation
now crosses enabled clone `800680B4`: actual source metadata lookup and tagged
pool selection feed the existing reflected factory `8006546C` and initializer
`800667F4`. The resulting signed-byte instance enters actual `80068060` mode
one and the `80067B60` member pass. No completed clone is supplied by the fixture.
Existing string-arena and metadata-allocation identities are bound explicitly;
they do not initialize or replace a class, schema or copied descriptor.

The native pass copies its first seven members: offset, owned string name,
two halfword indices, halfword size, declaring metadata and global-cell pointer.
The next reached body is **`80062824`, memory-reference member copying** of
`_default`. Its invocation remains pending. String metadata still has flags `9`
and 23 fields, memory metadata has flags `9` and 29 fields, their prototypes
remain null, and no inherited replacement is published prematurely.

This composition now has 66 descriptors, 61 default buffers, eleven published
types, four prototypes and 34 enabled allocations. Signed-byte metadata has two
reflected instances: its previously completed prototype and the newly initialized
copy destination. Repeated resumes preserve the allocation, completed member
writes, string reference count, type/prototype registries and original core
snapshots `+04 = 7`, `+08 = 1`. Full native startup remains incomplete.

The new caller also reconstructs modes zero and two with their distinct policy
bytes, hooks and tails. Signed field policies select only the original virtual
slots; unsupported values skip their unused accesses. The member module reads
live canonical base-field values where earlier source reconstruction already
published them, preserving physical padding and generic bytes. Each partial
store updates affected canonical fields, including overlapping typed views.
This prevents stale constructor images from replacing current member values.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 192/192 CTest suites passed locally with four jobs; no compiler warnings |
| Cold composition | 1,722 checks; actual reflected allocation and first seven member copies reach `80062824` without a fabricated clone result |
| Reflected callers/adapters | 1,718 checks; all 256 signed policy bytes at three offsets, modes, capture/reload order, null results, physical faults, suspension and strict typed result identity |
| Member copies | 71 checks; canonical/physical consistency, exact widths, padding, overlap, partial/pending stores, wrapped addresses, string/object ownership and retained missing operations |
| Original decoder prefix | 400 original-inclusive /107 synthetic checks; unchanged separate enabled `8002A0A8` boundary, twenty types and sixty field mappings |
| Player regression | 447 original-inclusive /62 synthetic checks; 48 held/released and 28 prepared application/system/serial player frames |
| Python asset verifier | Six synthetic tests passed |

Four independent comparisons execute **6,110 cases / 465,792 original
instructions with zero differences**: 4,241 completed callers and 1,869 fault
or bounded prefixes. Every original input, expected output, oracle and imported
interpreter helper was sealed before successful native compilation/comparison.
All four final drivers linked against the same core and passed on their first
native comparison. No original case or expected result was changed to pass.

| Original-instruction scope | Cases | Instructions |
| --- | ---: | ---: |
| Reflected allocation caller, three copy passes and signed member dispatch | 2,886 | 289,369 |
| Raw member wrappers and original bytewise copy | 1,312 | 73,282 |
| String member wrappers and actual assignment | 1,288 | 74,437 |
| Object member wrapper and actual assignment | 624 | 28,704 |

The reflected-caller corpus prepares the reflected result, object/member
virtuals and tagged-pool endpoint. It independently checks caller control flow,
captured versus live state, wrapping indices, aliasing, callbacks and faults;
it does not establish reflected initialization or every virtual body. The cold
composition above separately executes the actual reflected initializer and
reached member operations. A focused zero-field test also composes the factory
and initializer with explicit virtual/allocator leaves.

Raw comparisons execute `800642E4`/`80064318`/`8006434C` and `800031E8` without
prepared function leaves, observing bytes and initialized state. They include
zero/null inputs, halfword truncation, overlap, padding and partial failures.
String comparisons execute `80071B58`/`80071BC4` and `800715E8`, preparing only
string destruction `8007381C`. Object comparisons execute `8006A81C` and
`8006A47C`, preparing only object destruction `80066E1C`. Their complete counts,
mutation/failure traces and physical observations qualify those functions;
destruction internals and recursive memory copying are outside these corpora.
Bounds failures model represented storage access, not console exception delivery.
Canonical alias bookkeeping and delayed host bindings have separate native tests.

Final `dw4_core.lib` SHA-256:
`e79e8859bfe464f446d0b9980c46fe9cde4ef481bf4a65fe9b4ebfd703b63caf`.
Original USA DOL SHA-1:
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.
Ignored final build log: `work/native-reflected-copy-verified-build.log`;
regression logs: `work/native-reflected-copy-{cold,prefix,player,verifier}.log`.
The sealed originals are in `work/native-reflected-copy-original-manifest.json`.
Comparison provenance and hashes are pinned in
`work/native-reflected-copy-milestone-audit.json`; final local consistency is
recorded in `work/native-reflected-copy-root-audit.json`. Original data and
instruction corpora remain local and ignored. No application preview was rebuilt
or launched, and no hosted CI was used.

Continue through `80062824` and its buffer free/copy helpers, preserving repeated
size queries and original ownership order. Then complete the string replacements,
schema and prototype, followed by memory-class method dependencies and the later
DataList/list/thread/bootstrap chain. This milestone does not complete native
startup, gameplay, audio, rendering or any full-game acceptance gate. See
[IGB_REFLECTED_CLONE.md](IGB_REFLECTED_CLONE.md),
[IGB_FIELD_MEMBER_COPY.md](IGB_FIELD_MEMBER_COPY.md) and
[PORTING_STRATEGY.md](PORTING_STRATEGY.md).

## Native string registration, signed-byte prototype and scalar clones (earlier checkpoint)

Validated locally on 2026-09-23. The retained cold-bootstrap invocation now
executes string registration `80025404`, exact probe `80025348` and defaults
caller `8002549C`. The latter constructs its Boolean local through enabled
reflection, then selects the inherited `_shallowCopyMethod` descriptor through
the actual name/index lookup and its published canonical global. Its signed-byte
clone enters registration `80037D28`, probe `80037C78` and reflected prototype
callback `80037DB0`. No ready class or completed clone is supplied by the fixture.

Signed-byte metadata finishes with flags `7`, twenty inherited fields and one
reflected instance, becoming type eleven. Its canonical prototype `80561E20`
has ordinal/type ordinal three, one-byte default storage and one retained
reference. The next reached boundary is **`800680B4`, enabled reflected cloning**,
with the captured inherited source and original argument one. The bound endpoint
returns no operation; the invocation remains pending without a result or cold-copy
fallback. Repeated resumes preserve completed registrations and allocations.

At this boundary there are 65 schema descriptors, 61 default buffers, eleven
registered types and four prototypes. String metadata remains at flags `9`
with 23 fields; memory metadata remains at flags `9` with 29 fields. Neither
prototype is published, DataList still has four fields, and enum metadata remains
null. The fixture has 33 enabled allocations, nine field-list backings and ten
metadata/internal-list instances each. Original core snapshots stay `+04 = 7`
and `+08 = 1`. Allocation and platform leaves remain explicit prepared services.

The full string-default caller separately covers both inherited replacements,
byte defaults and final schema publication with prepared clone endpoints.
Signed-byte and Boolean clone wrappers now implement their actual cold allocation
and `8003FFCC`/`8003EC04` copy constructors through shared `800639E4`; enabled
wrappers preserve registration and the separate required `800680B4` call.
Focused tests also cover the string prototype caller and its real `8007144C`
default-buffer tail, which cold startup has not yet reached.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 190/190 CTest suites passed locally with four jobs; no compiler warnings |
| Cold composition | 1,713 checks; real string prerequisite, signed-byte registration/prototype and pending enabled clone with canonical source and argument one |
| String defaults | 172 checks; both inherited replacements, fresh index registries, physical header faults, retained callbacks and final one-field schema |
| Clone wrappers/copy constructors | 224 checks; memory and scalar profiles, exact cold allocations, independent defaults, ownership, padding, suspension and explicit enabled boundary |
| Field registration/probe/prototype/init | 357 /398 /123 /99 checks across represented profiles |
| Original decoder prefix | 400 original-inclusive /107 synthetic checks; preserves the separate enabled `8002A0A8` boundary, twenty types and sixty field mappings |
| Player regression | 447 original-inclusive /62 synthetic checks; 48 held/released and 28 prepared application/system/serial player frames |
| Python asset verifier | Six synthetic tests passed |

Ten independent comparisons execute **10,285 cases / 1,215,758 original
instructions with zero differences**: 4,085 completed callers, 6,198 fault or
explicit-boundary prefixes, and two registration ABI observations. All ten
drivers linked against the same final core, exited zero and produced their exact
expected row counts with empty stderr. The original inputs, expected outputs
and oracle files remained identical to the sealed pre-comparison manifest.
No production correction or case replacement was required by these comparisons.

| Original-instruction scope | Cases | Instructions |
| --- | ---: | ---: |
| String registration arguments, wrapper and getters | 577 | 14,572 |
| String probe, reset and name cleanup | 1,476 | 185,002 |
| String prototype, append, finalization and bounded reserve prefix | 2,098 | 158,730 |
| Signed-byte registration arguments, wrapper and getters | 577 | 14,570 |
| Signed-byte probe, reset and name cleanup | 1,308 | 140,526 |
| Signed-byte prototype, append, finalization and bounded reserve prefix | 2,098 | 158,730 |
| Signed-byte clone wrapper and copy constructor | 858 | 116,526 |
| Boolean clone wrapper and copy constructor | 858 | 123,992 |
| Complete string-defaults caller | 327 | 301,814 |
| String default-buffer tail | 108 | 1,296 |

Registration comparisons prepare the bodies called by wrappers/getters; the
separate ABI observations execute real `80025404`/`80037D28` to the generic
registrar entry and capture arguments/extras. They do not independently execute
that entire registrar. Signed-byte `80037C00` is part of its clone's cold branch,
not an allocation-only metadata getter. Probe comparisons execute actual resets
and cleanup across every physical local word, with separately labeled post-reset
mutations and prepared string-pool/release endpoints.

Prototype comparisons prepare the `8006546C` result and use an existing registry.
They execute actual append and finalization with prepared field virtuals, pool
queries and destruction. Capacity cases execute the `8005383C` prefix through
the explicit `800682A4` boundary; they do not establish complete reserve growth
or positive reflected construction. The cold test separately composes the actual
signed-byte reflected prototype. String prototype completion remains future work
in that same cold invocation.

Scalar-clone comparisons execute the cold wrappers, original raw allocation
dispatch, scalar/base constructors and directional default-buffer copying.
They cover captured allocation/source state, self-aliasing, name ownership,
overlap, changed byte lengths and partial failures, including all 28 bytes after
each `0x34` object in its `0x50` scratch allocation. Enabled cases prepare their
registration and whole `800680B4` endpoints; they do not verify its internal copy.
The string-defaults comparison executes the complete batch/search/replacement/
schema caller with explicit descriptor constructors, clone results, scalar
virtuals and pool/string/destructor services. The tail comparison executes
`8007144C`/`800667D0` with two prepared `+8C` targets; the virtual's buffer body
is outside that corpus. Its additional pending-poll checks qualify native
continuation capture and idempotence, not original guest timing.

Final `dw4_core.lib` SHA-256:
`ce15e8604c48bb3c3a5599b0458e0afe8493b51473e5007bd90324c359676f2e`.
Original USA DOL SHA-1:
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.
Ignored build log: `work/native-string-class-final-build.log`; regression logs:
`work/native-string-class-{cold,prefix,player,verifier}.log`.
The sealed originals are recorded in `work/native-string-class-original-manifest.json`.
Comparison inputs, outputs, source/driver/core/oracle hashes and build provenance
are pinned in `work/native-string-class-milestone-audit.json`; the final consistency
audit is `work/native-string-class-root-audit.json`. Original binaries and generated
instruction corpora remain local and ignored. No application preview was rebuilt
or launched, and no hosted CI was used.

Continue with `800680B4` metadata/pool lookup and reflected allocation, followed
by actual `80068060` dispatch. The reached mode one enters `80067B60`; modes zero
and two select `80067C28` and `80067ADC`, while other signed values select no copy.
These bodies and their reached member operations remain unrecovered. Complete
the string replacements/schema/prototype, then memory method dependencies and
later DataList/list/thread/bootstrap work. This milestone does not complete
native startup, audio, rendering, gameplay or any full-game acceptance gate.
See [IGB_STRING_FIELD_REGISTRATION.md](IGB_STRING_FIELD_REGISTRATION.md),
[IGB_STRING_FIELD_DEFAULTS.md](IGB_STRING_FIELD_DEFAULTS.md),
[IGB_FIELD_CLONE.md](IGB_FIELD_CLONE.md) and
[PORTING_STRATEGY.md](PORTING_STRATEGY.md) for the continuation.

## Native memory-reference defaults and Boolean registration (earlier checkpoint)

Validated locally on 2026-09-23. The retained cold-bootstrap invocation now
executes `igMemoryRefMetaField` registration `8002AEAC`, its exact layout probe
`8002AD90`, and the reached `8002AF40` defaults batch. Metadata and prototype
cells alias the existing descriptor factories and value operations. Cleanup
preserves full-word string references and packed object references separately.

The batch's Boolean prerequisite now completes actual registration `80038828`,
probe `80038778`, reflected initialization and prototype callback `800388B0`.
It becomes type ten, with flags `7`, twenty inherited fields and four reflected
instances: its prototype and three descriptors. Canonical prototype `80561E4C`
occupies ordinal two and receives its one-byte default through the real finalizer.

The next reached cold boundary is **`80025404`, igStringMetaField registration**,
needed by the eighth memory-class descriptor. Seven locals have been constructed
through enabled reflection; their names and final schema remain pending.
The composition has 64 schema descriptors, 60 default buffers, ten registered
types and three prototypes. Memory metadata stays at flags `9` with 29 fields
and no constructed instance/prototype. DataList still has its four fields;
its children and enum registration are not completed. Enabled allocations total
27, with seven backings and eight metadata/internal-list instances each.
The historical core count snapshots remain `+04 = 7`, `+08 = 1`.

The full memory defaults caller separately implements all eight locals and
both inherited method replacements using the existing native batch, schema,
search, scalar-value and replacement operations. Its focused fixtures prepare
constructor, clone and method endpoints; they do not establish cold completion
of those endpoints. A physical-access regression now ensures unreadable vtable
bytes fault before clone/scalar callback dispatch, preserving completed earlier
work and retained calls across suspension.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 189/189 CTest suites passed locally with four jobs; no compiler warnings |
| Cold composition | 1,560 checks; memory registration/probe and seven enabled local factories, including the complete Boolean class and reflected prototype |
| Memory defaults | 551 checks; eight fields, both method replacements, physical stores, captured lookup receivers, retained callbacks and malformed-header access order |
| Field registration/probe/prototype/init | 283 /289 /103 /76 checks across the represented profiles |
| Original decoder prefix | 400 original-inclusive /107 synthetic checks; preserves the separate enabled `8002A0A8` boundary, twenty types and sixty field mappings |
| Player regression | 447 original-inclusive /62 synthetic checks; 48 held/released and 28 prepared application/system/serial player frames |
| Python asset verifier | Six synthetic tests passed |

Six independent comparisons execute **10,726 cases / 4,034,801 original
instructions with zero differences**. They include 4,742 completed calls,
5,982 fault or bounded failure prefixes and two registration ABI observations.
Those observations remain separate from execution of the generic registrar.

| Original-instruction scope | Cases | Instructions |
| --- | ---: | ---: |
| Memory registration arguments, wrapper and getters | 577 | 14,573 |
| Memory probe, reset and member cleanup | 5,074 | 729,362 |
| Memory prototype, append, finalization and bounded reserve prefix | 2,098 | 158,730 |
| Boolean registration arguments, wrapper and getters | 577 | 14,570 |
| Boolean probe, reset and name cleanup | 1,308 | 140,526 |
| Memory defaults, complete batch/search/replacement/schema callers | 1,092 | 2,977,040 |

The memory-prototype comparison uses a prepared reflected result and an existing
registry; it executes actual append/finalization and the reserve prefix through
its capacity queries to `800682A4` entry. It does not cover reflected construction,
null-registry creation or complete reserve growth. The first comparison stopped
the original reserve path earlier than the native unbound allocator endpoint,
producing 59 trace differences. Both sides now stop at the same operation;
all 2,098 inputs remain unchanged and the original executes 4,665 additional
instructions. Before/after evidence is retained locally. No production code or
expected gameplay behavior was changed to remove these differences.

Probe comparisons cover complete represented locals, with explicit mutations
after reset to exercise cleanup branches that normal reset leaves null.
String/object destructor and selected allocation endpoints remain prepared.
The defaults comparison executes the entire original caller and surrounding
batch/search/replacement/schema operations, with explicit field constructors,
clone results, scalar virtuals, pool/string/buffer and destruction endpoints.
Registration comparisons expose the original ABI/extras and wrapper/getter
behavior; they do not independently execute the entire generic registrar.
The composed cold test above supplies separate integration evidence.

Final `dw4_core.lib` SHA-256:
`40d320bd12f1ca811cadca283264302a13a72f17cfc596d8996b57ceb1c3a3b3`.
Ignored local build log: `work/native-memory-class-final-build.log`; regression
logs: `work/native-memory-class-{cold,prefix,player,verifier}.log`.
The pinned comparisons and source/driver/corpus/oracle hashes are in
`work/native-memory-class-milestone-audit.json`; the final consistency audit
is `work/native-memory-class-root-audit.json`. Original assets and instruction
corpora remain local and ignored. No application preview was rebuilt or launched,
and no hosted CI was used.

Continue through actual string-class registration, its defaults and reached
signed-byte clone, then the memory schema's method dependencies, byte-element
prototype and later list/thread/bootstrap work. This milestone does not complete
native startup, audio, rendering, gameplay or any full-game acceptance gate.
See [IGB_MEMORY_FIELD_REGISTRATION.md](IGB_MEMORY_FIELD_REGISTRATION.md),
[IGB_MEMORY_FIELD_DEFAULTS.md](IGB_MEMORY_FIELD_DEFAULTS.md) and
[PORTING_STRATEGY.md](PORTING_STRATEGY.md) for the exact continuation.

## Native DataList hierarchy and enabled integer fields (earlier checkpoint)

Validated locally on 2026-09-23. The same cold-bootstrap invocation now crosses
deferred `80023F28` into the actual non-reference-counted object-list and DataList
parents. Their registrars preserve prerequisite guards, canonical metadata,
parent inheritance, and layout-probe capture/store order. The fifth `igObject`
child exercises native backing growth and zero fill with an explicit allocator
reallocation endpoint.

DataList's first factory enters actual `igIntMetaField` registration `8002E3B8`,
probe `8002E308`, reflected initialization and prototype callback `8002E440`.
The integer class finishes with flags `7` as type nine, inheriting twenty fields.
Its prototype uses canonical `805619E4`, occupies registry ordinal one, and
receives its four-byte default buffer through real finalization. The two DataList
integer descriptors are then reflected instances of that class. Their names,
schema configuration and default buffers remain pending until the third factory
completes; the batch does not finalize them early.

The next reached original body is **`8002AEAC`, igMemoryRefMetaField registration**.
There are 57 schema descriptors, 57 default buffers, nine registered types and
two prototypes. DataList remains at flags `9`; its non-reference-counted object
and thread-list children remain at flags `8`. Enum metadata `80561808` is still
null. The earlier bootstrap snapshots remain `+04 = 7`, `+08 = 1`.
The actual byte-element registration, enabled inherited-field clone, thread
class, semaphore and startup callbacks still need completion. This is a longer
verified startup prefix, not completed native startup or playable gameplay.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 188/188 CTest suites passed locally with four jobs; no compiler warnings |
| Cold composition | 1,360 checks; preserves the earlier 1,230-check checkpoint and composes actual enabled integer registration, prototype and descriptor construction without ready-class seeds |
| Data-list registration | 506 checks; three profiles, canonical getters, prerequisite/parent ordering, exact probes, suspension and allocation ownership |
| Data-list defaults | 292 checks; actual batch/schema and replacement callers with prepared leaves, captured receivers, ordered physical stores, prototype effects before destination faults and wrapped physical indices |
| Field registration/probe/prototype/init | 198 /106 /83 /62 checks, including the integer profile, shared prototype cell, scalar cleanup and verified initialization virtuals |
| Original decoder prefix | 400 original-inclusive /107 synthetic checks; unchanged separate enabled `8002A0A8` boundary, twenty types and sixty field mappings |
| Player regression | 447 original-inclusive /62 synthetic checks; 48 held/released and 28 prepared application/system/serial player frames |
| Python asset verifier | Six synthetic tests passed |

The malformed-destination regression preserves prototype registration effects
before the first `+3C` store faults. The wrapped-index regression follows
`800658E8`'s 32-bit shift before checking the represented physical slot: index
`0x80000002` addresses slot two. The resulting slot remains bounded by the
fixed profile and actual backing size.

Six independent local comparisons execute **10,957 cases / 2,578,396 original
instructions with zero differences**. The unchanged corpora include 7,380
fault or bounded failure prefixes, 3,573 completed instruction-executed calls,
and four observations of the original registration argument lists.

| Original-instruction scope | Cases | Instructions |
| --- | ---: | ---: |
| Three data-list registrations, wrappers, getters and probes | 3,477 | 101,621 |
| DataList defaults, actual batch/schema callers and byte-prototype getter | 3,870 | 1,947,710 |
| Non-reference-counted object specialization, lookup/index, prototype getter and replacement | 1,437 | 369,271 |
| Integer registration arguments, wrapper and getters | 577 | 14,570 |
| Integer layout probe, reset and name cleanup | 1,308 | 140,526 |
| Integer prototype guard/null-publication paths | 288 | 4,698 |

These comparisons preserve explicit prepared endpoints: field constructors,
registration bodies selected by getters, clone results, field virtuals, string
interning and allocator/pool callbacks. Registration-argument observations do
not execute the generic registrar, and lazy getter comparisons cover existing
nonnull cells rather than their allocation branch. The integer-prototype
corpus deliberately uses abstract metadata to qualify the original factory's
null return; it does not establish positive registry append or finalization.
The separate cold composition above executes the actual positive reflected
integer prototype. The specialized object-list comparison excludes reserve
growth. Each report records its exact prepared leaves and failure scope.

Final `dw4_core.lib` SHA-256:
`64be6f5ebf01a5f86c37a2b25a4e0e56536a2d048006fa25a0f8b1d961e9864e`.
Ignored local build log: `work/native-data-list-wrap-build.log`; cold/original
regression logs: `work/native-data-list-{cold,prefix,player,verifier}.log`.
Independent comparison artifacts, source/driver/corpus hashes and transitive
oracle-helper hashes are recorded in `work/native-data-list-milestone-audit.json`.
The final core/test/regression consistency audit is
`work/native-data-list-root-audit.json`. Original instructions, assets and
generated comparison corpora remain local and ignored.
See [IGB_DATA_LIST_REGISTRATION.md](IGB_DATA_LIST_REGISTRATION.md),
[IGB_DATA_LIST_DEFAULTS.md](IGB_DATA_LIST_DEFAULTS.md), and
[IGB_INT_FIELD_REGISTRATION.md](IGB_INT_FIELD_REGISTRATION.md).

## Native ThreadManager fields and lazy dependencies

Validated locally on 2026-09-23. The retained cold-bootstrap integration now
executes actual ThreadManager defaults `80023940`, including its two object
field factories through enabled reflection. Each dependency getter (`80023E40`
and `80026020`) calls the real metadata factory, reflected initializer, positive
object-member construction and nested internal-list memory initialization.
The two dependency metadata objects own distinct 64-byte zeroed backings; their
class flags remain zero because these getters allocate without registering.

Native schema publication assigns `_threadList` / `_sema`, offsets `8` / `12`,
their original global cells and the type/construct stores. The caller publishes
redirect `80023A10` and appends `80023A50` to ArkCore's `+28` bootstrap list.
ThreadManager then completes registration with flags `7`, as the eighth type.
There are 55 schema descriptors, 56 default buffers and one prototype. The
earlier bootstrap snapshots remain `+04 = 7`, `+08 = 1`.

The next required operation is **deferred thread-list registration `80023F28`**.
The retained class callback list is `[80023F28, 80026098, 80023A30]`; this is
separate from the bootstrap queue, and neither startup nor teardown is executed
early. Enum metadata `80561808` remains null. This is additional original
startup reconstruction, not complete initialization or playable gameplay.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 186/186 CTest suites passed locally, four jobs, no compiler warnings |
| Cold composition | 1,230 checks; preserves the earlier 1,045-check checkpoint, actual reflected descriptor and nested metadata/list construction, retained allocator pause, physical stores, callback order and exact next boundary |
| Thread defaults | 214 checks; original schema tables, dynamic inherited indices, captured fields and fresh redirect owner, physical stores and untouched adjacent flags, schema/append suspension and failure prefixes |
| Dependency getters/bootstrap append | 60 checks; both null-only getters, retained canonical cell/factory, unknown/null distinctions, raw overwritten ownership, physical identity conflicts and captured outer core versus fresh inner pool |
| Descriptor factories | 309 checks; existing constructor families plus actual reflected adapter, adjusted typed storage, retained allocation and abstract/null result |
| Original decoder prefix | 400 original-inclusive /107 synthetic checks; unchanged enabled `8002A0A8` boundary, 20 types, 60 field mappings, 47 matched classes |
| Player regression | 447 original-inclusive /62 synthetic checks; 48 held/released and 28 prepared application/system/serial player frames |
| Python asset verifier | 6 synthetic tests passed |

An independent local comparison executes **11,556 cases / 4,765,922 original
instructions across 489 instruction addresses, with zero differences**.
There are 360 completed cases and 11,196 callback/null-allocation failure
prefixes. It executes `23940`, actual batch/schema callers and their reached
helpers, both lazy getters, enabled metadata factory `29E64`, and bootstrap
append `65D94 -> 3E4D8 -> 3E8B8`.

The comparison varies inherited first indices, existing/null dependency cells,
metadata/global/field-backing replacement during callbacks, adjacent descriptor
flags, callback list capacity and null allocation. It compares two metadata
allocations, field state and physical type/construct stores, pointer-list
contents, canonical cells, string references, callback buffers and call order.
Field factories, metadata registration/reflected results, string interning,
field virtuals and pool/allocator leaves are explicit prepared endpoints in
this comparison. Their internals are not established by this corpus; the cold
integration separately composes actual reflected construction. Thread startup,
semaphore behavior and complete game startup remain outside the measured scope.

Final `dw4_core.lib` SHA-256:
`5c08b8478ac86361b6434c9e72e876a9bb35d852dabb7fbcd90952c98a12436e`.
Ignored local logs: `work/native-thread-defaults-final-build.log` and
`work/native-thread-defaults-{cold,prefix,player,verifier}.log`.
Original comparison and provenance: `work/native-thread-defaults-result.json`
and `work/native-thread-defaults-audit.json`.
See [IGB_THREAD_DEFAULTS.md](IGB_THREAD_DEFAULTS.md) and
[IGB_THREAD_DEPENDENCIES.md](IGB_THREAD_DEPENDENCIES.md) for recovered behavior
and the explicitly prepared allocator/platform boundaries.

## Native enabled metadata and recursive list construction

Validated locally on 2026-09-23. The next registration body, `8002AAFC`
(`igMetaEnum`), now runs its actual prerequisite guard and enters the existing
native ThreadManager registrar. The enabled `80029E64` metadata factory
composes `8006546C` and `800667F4` with a distinct typed metadata allocation.
Its positive `_metaFields` descriptor calls actual `8006A618`, recursively
initializes the internal field list, and assigns it through `8006A47C` with
the original packed references and temporary release.

The list's positive memory member executes `80062524 -> 80062BB8`: separate
element size reads, alignment, pool selection, aligned allocation, live element
metadata ancestry, conditional zero fill and raw pointer publication. The
reached list requests 64 bytes aligned to four, zeroes 16 reference slots and
initializes its count through the separate int descriptor. No ready list or
completed child object replaces these operations.

The same retained cold integration now completes both reflected allocations,
publishes the metadata's actual list, balances its reference word back to one,
and increments both class instance counts once. ThreadManager registration
inherits the two base descriptors and interns its name before the next required
body, **defaults `80023940`**. Its canonical metadata `805614E8` is busy with
flags 9; enum metadata `80561808` remains null. There are still seven completed
registered types, 53 schema descriptors, 54 default buffers and one prototype
entry. The earlier enable transition and snapshots `+04 = 7`, `+08 = 1` remain
covered. This is additional startup reconstruction, not a playable game.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 184/184 CTest suites passed locally, four jobs, no compiler warnings |
| Cold composition | 1,045 checks; actual prerequisite recursion, retained aligned-allocation pause, physical backing and member publication, balanced ownership, inherited fields and exact next callback |
| Generic object initializer | 215 checks, including removal/redirection of a pending field's physical binding without further writes |
| Metadata/list initializer | 46 checks; exact getters, typed results, null state, physical/canonical synchronization, nonzero backing offsets, padding, abandonment and binding changes |
| Enum registration/defaults/probe | 198 checks; exact registration arguments, prerequisite order, getters, physical local probe, three-field batch/schema and captured type stores |
| Object member construction/assignment | 201 checks; actual reflected child caller, live pool/offset/ownership reads, full-word reference arithmetic, pending destruction and publication |
| Memory member construction | 176 checks; separate size/alignment reads, wrapping arithmetic, ancestry, partial zero-fill faults, late mappings, allocation failure and status order |
| Original decoder prefix | 400 original-inclusive /107 synthetic checks; unchanged enabled `8002A0A8` boundary, 20 types, 60 field mappings, 47 matched classes |
| Player regression | 447 original-inclusive /62 synthetic checks; 48 held/released and 28 prepared application/system/serial player frames |
| Python asset verifier | 6 synthetic tests passed |

Independent local instruction comparisons against the same final production
core cover **8,904 cases and 6,968,248 original instructions, zero differences**:

| Comparison | Cases | Original instructions | Completion/failure scope |
| --- | --- | --- | --- |
| Metadata and nested construction | 5,040 | 6,755,910 | 840 completions, 4,060 injected callback-failure prefixes, 140 deliberately bounded positive-entry prefixes |
| Object member assignment | 624 | 22,720 | 560 completions, 64 destructor-failure prefixes; aliases, packed-word wrapping and changed destination after destruction |
| Memory member construction | 3,240 | 189,618 | 576 completions, 2,664 callback-failure prefixes; separate widths, alignment, allocation results, ancestry and live publication |

The metadata comparison retains separate control profiles with a disabled
construct flag or explicitly synthetic empty child schema. Its additional
2,870-case profile executes the real internal-list virtuals, registered
four-descriptor layout, `62524/62BB8` and element ancestry. It compares all
100 parent bytes, the 16-byte child plus a 16-byte guard, 64 backing bytes,
callback order, instance counts and canonical ownership. The memory comparison
uses prepared element virtuals and allocator results to vary those boundaries.
Schema/default setup, raw allocators, pool lookup and destructor endpoints are
explicit preparation in the respective comparisons; their internals and
complete original startup are not established by these results.

Final `dw4_core.lib` SHA-256:
`79fe17371d3c6d8f929d9cdab8acb042ba88874202fa56f14de19a2cfc88a89b`.
Ignored evidence includes `work/native-metadata-members-final-build.log`,
`work/native-metadata-members-{cold-final,prefix,player}.log`,
`work/native-metadata-init-verifier.log`,
`work/native-metadata-milestone-audit.json` and the three comparison reports
`native-metadata-init-all-result.json`, `native-object-member-assign-result.json`
and `native-memory-member-init-result.json` under `work/`.
The audit records source, driver, corpus and core hashes. Original data,
generated disassembly and comparison executables remain outside source control.

Continue at the reached ThreadManager defaults, then enum registration and
its further dependencies. The separate enabled decoder fixture remains
separate; it does not borrow this prepared cold fixture's registry. Pool and
platform internals, later classes, outer startup, rendering, audio and complete
gameplay remain required. See [IGB_METADATA_INIT.md](IGB_METADATA_INIT.md),
[IGB_META_ENUM.md](IGB_META_ENUM.md),
[IGB_OBJECT_FIELD_CONSTRUCT.md](IGB_OBJECT_FIELD_CONSTRUCT.md) and
[IGB_MEMORY_FIELD_CONSTRUCT.md](IGB_MEMORY_FIELD_CONSTRUCT.md).

## Native memory-field clone and fundamental bootstrap completion

Validated locally on 2026-09-23. The inherited memory-reference clone now
executes actual `8002ACD8 -> 8006331C -> 8006CC88 -> 800639E4`, including the
object header copy, full name references and separate default-buffer allocation
through `800681C4`. Original `800031E8` copy direction, live source reads,
self-aliasing and partial failure stores are retained. List setup/replacement
now publishes its reached field members and reference words to represented
physical storage as well as canonical owners.

The same suspended cold composition now finishes the child-list defaults,
replacement, layout, publication and deferred callbacks. It holds 53 schema
field descriptors plus one reflected object-field prototype, 54 default buffers,
seven registered types and one prototype registry entry. Parent list metadata
`805619D4` and child list metadata `805619CC` both have flags 7. The original
outer caller enables ArkCore and writes its count snapshots: `+04 = 7`,
`+08 = 1`. It then stops at the next required registration body `8002AAFC`,
`igMetaEnum`, canonical metadata global `80561808`.

This proves completion of the fundamental registration phase within the
explicitly prepared pool/platform, physical allocation and initial-global
fixture. It does not establish complete original startup. Enabled reflected
clone `800680B4`, remaining class registration, complete allocator bodies and
later scene/gameplay loading remain required. The separate original decoder
fixture still stops at its own enabled `8002A0A8` boundary; it does not silently
consume the prepared cold fixture's completed registry.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 180/180 CTest suites passed locally, four jobs, no compiler warnings |
| Cold metadata composition | 953 checks; captured default-allocation suspension, distinct source/copy buffers, original child replacement, physical references, seven type publications, enable transition, count snapshots and retained next registration |
| Base field copy | 24 checks; enabled classification endpoint, captured pool/size, late destination/source binding, live size reload, padding, partial backward-copy faults, recursion and changed physical identity |
| Memory-reference clone | 103 checks; complete cold chain, gate capture, null allocation, late source reads, missing pointer identity, pending registration/reflected boundaries, construction lifetime and changed binding/storage/offset rejection |
| List defaults/replacement | 116 checks; physical source configuration, late element address, original store order, partial-write faults, clone/global publication, aliasing and reference stores |
| Original decoder prefix | 400 original-inclusive /107 synthetic checks; 20 types, 60 inherited field mappings, 47 matched classes, cursor 5,381 and dependency body 2,453 bytes |
| Player regression | 447 original-inclusive /62 synthetic checks; 48 held/released frames and 28 prepared application/system/serial frames |
| Python asset verifier | 6 synthetic tests passed |

Review found and corrected a destination-pointer recapture across a missing
source binding and insufficient destination identity checks after suspension.
Permanent tests verify both fixes. The cold test's original expected disabled
state and zero array flags were corrected against the actual outer enable
transition and original list setup; final assertions now check the reached
state and inherited construct/destruct flags.

An independent local comparison executes the supplied USA DOL instructions
(SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`): 858 cases, 372 completed
returns, 486 fault prefixes and 139,591 original instructions, with zero
differences. It compares the whole 0x50-byte memory descriptor, untouched
padding, source/destination default buffers, name reference counts and ordered
calls. Cases cover aliases, overlap, zero/null allocations, null core/pools,
endpoint failure and allocator mutations of source/default size and later
derived members. Source object storage is represented and initialized in this
corpus; it does not claim equivalence for unrepresented source bytes.

The corpus executes actual cold wrapper/allocation routing, all three copy
constructors, header reset, default-allocation routing and directional byte
copy. Handle lookup `800607F4`, heap allocation `8009281C`, pool `+D8` and
`8005929C` are prepared endpoints. Enabled-wrapper cases prepare registration
`8002AE84` and the whole reflected `800680B4` boundary. Enabled base-copy pool
classification and deep reflected copying are not covered by this original-code
corpus; their exact reached endpoints remain explicit in the implementation.

Final core SHA256:
`D761855733D056D0B9F1F2C911F364C254AFDB374025A18FB629EE8B6D294EC6`.
Ignored evidence is `work/native-field-clone-final-build.log`,
`work/native-field-clone-{prefix,player,verifier}.log`, and
`work/native-field-clone-result.json`. Original data, reference execution and
comparison binaries remain local. See [IGB_FIELD_COPY.md](IGB_FIELD_COPY.md),
[IGB_FIELD_CLONE.md](IGB_FIELD_CLONE.md) and
[IGB_LIST_DEFAULTS.md](IGB_LIST_DEFAULTS.md).


## Earlier native typed field initialization and concrete prototype

Validated locally on 2026-09-23. The shared reflected factory and `800667F4`
initializer now return distinct file/field instances. The cold composition
constructs the concrete object-field prototype through its real 23-descriptor
initialization, all three field passes, string assignment and default-buffer
tail. It publishes canonical `805616EC`, creates and appends to prototype
registry `8056229C`, assigns ordinal zero and finalizes type ordinal zero.
The abstract reference callback still returns null through the original branch.

With explicitly represented callback allocation/deallocation endpoints, the
wrapper now returns to the original parent-list defaults, layout and type
publication. Canonical name-descriptor `805617C4` is the same cell written by
the actual field schema and read by the internal-list lookup. That lookup and
index search reach the inherited memory-reference field (`80476088`) clone
virtual `+54`, argument one. Its actual clone body remains unimplemented.

The verified boundary contains 52 schema descriptors plus one reflected field
prototype, 53 default buffers, six published types and one prototype registry
entry. Parent list `805619D4` has flags 7; child list `805619CC` has flags 9.
ArkCore remains disabled and its later count snapshots remain unwritten.
Repeated suspension does not replay allocation, prototype creation, parent
registration or completed callback cleanup. Physical allocators, pool/platform
state and original BSS bindings remain explicit prerequisites.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 178/178 CTest suites passed locally with four jobs; no compiler warnings |
| Cold metadata integration | 931 checks; concrete initialization/publication, actual parent-list completion, canonical name lookup and retained memory-field clone boundary |
| Generic object initialization | 202 checks; separate typed fields/files, adjusted offsets, conflicting bindings, physical headers, suspension and construction lifetime |
| Reflected/file factories | 107 checks including strict file adapters and field instance results |
| Prototype callbacks | 73 checks including reflected typed publication, adjusted addresses, physical ownership/ordinal stores and retained allocation lifetime |
| Scalar defaults | 136 checks across six scalar profiles, shared pointer slots and canonical member publication |
| Field operations | 66 checks including physical finalization type ordinal |
| Reference/object initialization | 55 checks including captured size callbacks, low16 truncation, null/unknown tables, header aliases, positive-branch suspension and live object virtuals |
| String initialization/assignment | 53 checks including native cached interning, raw pointer reloads, full-word references, pending release, lazy pools, aliases, faults and lifetime |
| Original decoder prefix | 400 original-inclusive /107 synthetic checks; separate enabled fixture still reaches `8002A0A8`, cursor 5,381 and dependency body 2,453 bytes |
| Player regression | 447 original-inclusive /62 synthetic checks; 48 held/released frames and 28 prepared application/system/serial frames |
| Python asset verifier | 6 synthetic tests passed |

Review corrected synchronization of the memory-reference element pointer,
fresh physical vtable selection in the tail, aliased file reference headers,
and the represented-null vtable failure. New permanent checks cover these
cases. Test fixture corrections supplied the previously missing canonical
metadata cell and recorded calls through the later-installed clone dispatcher;
the final complete suite passed after those corrections.

Independent comparisons execute instructions from the supplied USA DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`:

| Corpus | Cases | Faults | Pending boundaries | Original instructions | Differences |
| --- | ---: | ---: | ---: | ---: | ---: |
| Six scalar profiles and verified pointer subset | 5,402 | 1,120 | 0 | 686,368 | 0 |
| Reference initialization virtuals | 1,364 | 640 | 8 | 45,910 | 0 |
| Complete 0x44-byte object-field initialization | 630 | 490 | 0 | 1,174,460 | 0 |
| String assignment and initialization | 1,236 | 276 | 0 | 62,887 | 0 |

Total: 8,632 cases, 2,526 faults, eight explicit pending boundaries and 1,969,625
original instructions, with zero differences. The scalar corpus uses actual
copy functions and leaves, without prepared callees; partial malformed copies
remain outside its complete-range profile. The reference corpus uses actual
clearing and gate bodies, with real width leaves or explicitly prepared width
callback results/faults. Its eight nonnull object-default cases stop at required
`80067098`; positive recursive member-construction branches are not proved.

The whole-object corpus uses the original factory/offset tables and prepared
registered metadata/default buffers. It executes actual `800667F4`, all three
passes, scalar/reference/string-null bodies and `8006A438` through the real
`80063D04/80063C74/80068254/80068430` tail. Physical size/default-pointer stores,
all 0x44 bytes including padding, instance counts and ordered calls are compared.
Allocation-owner lookup, pool tags, `8005929C` and default allocation remain
prepared endpoints. This corpus does not prove class registration or a complete
allocator; the cold composition has its separately stated scope above.

The string corpus executes actual `800715E8/80071A9C` with prepared string-pool
allocation, initialization, interning and release endpoints. It compares all
target bytes, three header reference counts, pool/global mutations, captured
offsets and source rereads. A permanent test separately composes the native
cached interner; neither result establishes complete string-pool allocation.

Final core SHA256:
`EAAB382DAF57A827E354FEC6477AEAFB4EC30BC5513B352E692D49C80086379A`.
Ignored logs are `work/native-field-initialization-final-build.log` and
`work/native-field-initialization-{prefix,player,verifier}.log`. Reports are
`work/native-scalar-field-defaults-result.json`,
`work/native-reference-field-init-result.json`,
`work/native-object-field-init-result.json` and
`work/native-field-string-init-result.json`. All recorded source, executable
and library hashes match the final files; the last rebuild changed only the
cold test and retained the same library hash.

See [IGB_FIELD_INIT.md](IGB_FIELD_INIT.md),
[IGB_FIELD_STRING_INIT.md](IGB_FIELD_STRING_INIT.md) and
[IGB_FIELD_PROTOTYPE.md](IGB_FIELD_PROTOTYPE.md). Continue through the actual
memory-reference clone and remaining deferred registrations. No interactive
preview or GitHub Actions run was launched, and native gameplay remains unproven.

## Native field classes and prototype startup

Built and validated locally on 2026-09-20; report, source, executable and library
hashes rechecked on 2026-09-23. The same cold-bootstrap invocation now completes
the registrations of `igMetaField`, `igRefMetaField` and `igObjectRefMetaField`
through their actual wrappers, parent calls, probes and defaults. Their 23 local
descriptors bring the composition to 52 constructed descriptors, 50 default
buffers and five published types. All ten reached field constructor profiles
are native, including the signed-short and signed-byte profiles.

The abstract reference-prototype callback `80027454` completes through the
actual reflected factory's null-return branch. Concrete object-prototype
callback `80028B64` enters the actual reflected factory, allocates its 0x44-byte
storage through the explicit physical allocator, and reaches `800667F4`.
That initializer still requires typed field-object support. The allocation
remains uninitialized, prototype global `805616EC` remains null, the two
internal-list metadata objects retain flags 8 and 9, and ArkCore remains
disabled. Repeating the pending call does not allocate or construct again.

The bootstrap's canonical `8056229C` global is now correctly represented as
a field-prototype registry, and `80561688` as a reference-field prototype.
These replace earlier incorrect metadata-list/metadata host interpretations;
the original control flow, raw publication and count order are preserved.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 176/176 CTest suites passed with four jobs; no compiler warnings |
| Cold metadata integration | 887 checks including actual registration/default composition, all 23 descriptor names/offsets/globals, five published types, real abstract null return and retained concrete initializer boundary |
| Field-type registration | 162 checks for three profiles, canonical cells, exact arguments, callbacks and getters |
| Field-type defaults | 706 checks for all three schemas, customization order, live field identities, scalar packing, schema composition and suspension |
| Object-field layout probe | 87 checks for actual reset, physical result selection, string cleanup, live reloads and failure prefixes |
| Field prototypes | 50 checks including actual abstract reflection, concrete typed-initializer suspension, native lazy registry creation and separately prepared positive prototype callers |
| Specialized field factories and values | 301 factory checks and 130 value checks across ten profiles |
| ArkCore bootstrap | 70 checks with corrected canonical registry/prototype types |
| Original decoder prefix | 400 original-inclusive /107 synthetic checks; separate enabled fixture still reaches `8002A0A8`, cursor 5,381 and dependency body 2,453 bytes |
| Player regression | 447 original-inclusive /62 synthetic checks; 48 held/released frames and 28 prepared application/system/serial frames |
| Python asset verifier | 6 synthetic tests passed |

The first build exposed one registration-test fixture that incorrectly expected
absent parent/type-word state after construction. Correcting that fixture to
the constructor's represented null parent and zero type word required no
production change. The final full build passed all 176 suites.

Independent bounded comparisons used the supplied USA DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, with unchanged instruction semantics:

| Profile | Cases | Failure prefixes | Original instructions | Addresses | Differences |
| --- | ---: | ---: | ---: | ---: | ---: |
| Ten field factories, constructors, resets and tails | 5,258 | 3,121 | 611,895 | 1,133 | 0 |
| Object-field layout probe and cleanup | 1,763 | 973 | 232,215 | 189 | 0 |
| Field registration arguments, wrappers and getters | 1,782 | 540 | 45,539 | 234 | 0 |
| Three complete field-default callers | 708 | 675 | 1,298,477 | 576 | 0 |
| Abstract prototype callbacks and reflected null return | 576 | 204 | 9,396 | 61 | 0 |

Total: 10,087 cases, 5,513 failure prefixes and 2,197,522 original instructions,
with zero differences. Address counts are per corpus, not a union count.
The factory corpus compares complete initialized storage and padding while
retaining explicit pool, allocator, enabled registration/reflection and
string-pool endpoints. The probe corpus includes both uninterrupted resets
and explicitly injected post-reset string owners for cleanup mutation cases;
it does not claim the constructor naturally produces those injected owners.

The registration corpus captures all three argument lists at `80066204` and
checks all 23 original default-table rows. It excludes complete generic
registration and allocating lazy getters. The defaults corpus executes the
full three callers and reached schema helpers; prepared factories cyclically
return three field identities to exercise aliasing. Physical allocation,
string services and selected field virtuals remain explicit endpoints.
The prototype corpus proves the abstract null branch only. Positive typed
initialization, registry append and finalization are outside that comparison;
the separately prepared positive native tests do not prove cold construction.

Final core SHA256:
`FCECD0CBCF737CF426FD528A2CAA1114D3EEBF7E3793FC1FE8A04DF81D18E7FA`.
Ignored build/regression logs are `work/native-field-prototype-build.log` and
`work/native-field-type-{prefix,player,verifier}.log`. The five reports are
`work/native-object-field-family-result.json`, `work/native-field-probe-result.json`,
`work/native-field-type-registration-result.json`,
`work/native-field-type-defaults-result.json` and
`work/native-field-prototype-null-result.json`. Their final source, executable
and shared-library hashes all match the local files.

See [IGB_FIELD_TYPE_REGISTRATION.md](IGB_FIELD_TYPE_REGISTRATION.md),
[IGB_FIELD_TYPE_DEFAULTS.md](IGB_FIELD_TYPE_DEFAULTS.md),
[IGB_FIELD_PROBE.md](IGB_FIELD_PROBE.md) and
[IGB_FIELD_PROTOTYPE.md](IGB_FIELD_PROTOTYPE.md).
No interactive preview or GitHub Actions run was launched. Continue with
typed field initialization at `800667F4`, then prototype publication and the
remaining list defaults. This evidence does not establish a native game boot.

## Native deferred list registration and memory-reference fields

Validated locally on 2026-09-20. The same cold-bootstrap composition now enters
actual deferred wrapper `8002DD18`, body `8002DD40` and its parent registration
`8002E07C`. The parent `__internalNonRefCountedObjectList` inherits both
`igObject` fields, interns its original name and executes stack probe
`8002E020` with the actual `800536F8` reset. Defaults `8002E108` then construct
memory-reference `_data` and signed-integer `_count` descriptors through the
original two-entry batch. Their names/configuration have not yet been assigned:
getter `80028BFC` first requires object-field prototype registration `800289F8`
while the canonical prototype cell `805616EC` is still the original cold null.

At this boundary there are 29 constructed descriptors: the completed 27-field
metadata schema plus these two unconfigured list fields. `igMetaObject` remains
ready with flags 7 and registry index 1. Internal-list metadata `805619CC` has
flags 8; its parent `805619D4` has flags 9, four fields and type word `80475C54`.
Neither list type is published yet. The type registry still contains two types,
ArkCore remains disabled, and the earlier deferred callback order is retained.
Repeated suspension does not replay inheritance, probes or field construction.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 172/172 CTest suites passed locally with four jobs; no compiler warnings |
| List registration | 150 checks for exact registration arguments/canonical aliases, gates, wrappers, lazy/full/plain getters, reset probes, physical reads, suspension and lifetime |
| List defaults and replacement | 77 checks for two-field schema, prototype registration, captured receiver, original name/index searches, clone boundary, reference ordering, live backing, reserve growth and unchanged count |
| Cold metadata integration | 504 checks, including actual deferred list parent registration and two additional original field constructors before the required prototype registration |
| Specialized field factories | 263 checks across eight profiles, including complete memory-reference bytes/padding, three resets, pre-reset ownership stores and physical failure prefixes |
| Field values | 117 checks across eight prototype/size/alignment/default-copy profiles and existing ownership/copy cases |
| Typed list constructors | 12,420 metadata-element /47 field-element checks, including actual derived reset `80053CF4` without backing allocation |
| Original decoder prefix | 400 original-inclusive /107 synthetic checks; separate enabled fixture still reaches `8002A0A8`, cursor 5,381 and dependency body 2,453 bytes |
| Player regression | 447 original-inclusive /62 synthetic checks; 48 held/released frames and 28 prepared application/system/serial frames |
| Python asset verifier | 6 synthetic tests passed |

Physical allocators, pool/platform prerequisites and explicit cold global cells
retain the previous integration's declared scope. The new probes execute the
actual reset constructors in represented stack storage. No ready list metadata
or object-field prototype replaces the missing original registration. The
memory-reference descriptor's complete 0x50-byte construction is native, including
its intermediate ownership-word stores and actual return-only factory tails.

Independent bounded comparisons used the supplied USA DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, with unchanged instruction semantics:

| Profile | Cases | Failure prefixes | Original instructions | Addresses | Differences |
| --- | ---: | ---: | ---: | ---: | ---: |
| Eight field factories, constructors, resets and tails | 4,296 | 2,543 | 515,154 | 983 | 0 |
| List registration arguments, wrappers, getters and actual probes | 2,202 | 818 | 77,080 | 242 | 0 |
| Internal-list defaults, field search and replacement | 2,020 | 1,697 | 311,331 | 290 | 0 |

The factory corpus retains explicit physical allocator, enabled registration,
reflection and string-pool endpoints. It compares complete descriptor storage,
including padding. The registration corpus executes both real probes and
resets, wrappers/callback runner, full/alias/plain getters and the nonnull lazy
branches; generic registration arguments are captured at `80066204`. Wrapper
bodies return explicitly prepared null callback lists in this corpus. It does
not independently prove full generic registration or allocating lazy getters.

The defaults corpus executes `8002DDD4`, lookup `800658F8/80053910`, original
string comparison, index search `80053998`, finalization and replacement
`8006588C/80053EA0/80053C44`. Clone, field virtuals, destruction and physical
allocation queries are prepared endpoints. This corpus excludes reserve growth
and the full `8002E108` body; permanent tests cover their scoped composition.
Independent instruction review checked those callers and the new constructor
stores. An initial probe-driver fixture bound two incompatible list views to
one local; correcting that fixture produced matching results without production
changes. No comparison establishes a complete original clone, allocator or boot.

Final core SHA256:
`7861877F2960F908117499AAB4B0C615A524090D9F2AC3ABCABAB325E722B241`.
Ignored logs are `work/native-list-registration-build.log`,
`work/native-list-registration-prefix.log`, `work/native-list-registration-player.log`
and `work/native-list-registration-verifier.log`. Reports
`work/native-list-field-family-result.json`, `work/native-list-registration-result.json`
and `work/native-list-defaults-result.json` record matching source/driver hashes
and their comparison scopes. See [IGB_LIST_REGISTRATION.md](IGB_LIST_REGISTRATION.md),
[IGB_LIST_DEFAULTS.md](IGB_LIST_DEFAULTS.md) and
[IGB_META_FIELD_FACTORY.md](IGB_META_FIELD_FACTORY.md).
No interactive preview or GitHub Actions run was launched. Continue at actual
object-field registration `800289F8`, then finish the list defaults/clone and
remaining deferred registrations before connecting initialized ArkCore to the
original-data decoder. Complete native startup and gameplay remain required.

## Native specialized fields and completed metadata registration

Validated locally on 2026-09-20. The cold-bootstrap composition now constructs
all 25 local `igMetaObject` descriptors through their actual specialized
factories, alongside the two inherited `igObject` fields. Actual default-value
copying, string customization, field layout and type publication finish this
type's registration with flags 7, registry index 1, size 0x64 and alignment 4.
The registry contains two types. The actual deferred callback list contains
`8002DD18`, `8002E054` and `80066AE0` in that order; the runner stops at missing
internal field-list registration `8002DD18`. ArkCore is still disabled and has
not published its final type snapshot. This does not establish complete startup.

Seven constructor profiles cover unsigned/signed integers, booleans, strings,
object references, pointers and unsigned shorts. String/object post-constructor
tails execute actual default-buffer preparation. Physical size/default-pointer
stores remain coherent with an optional weak constructor-storage binding;
other field state continues to use its canonical typed representation.
Two lazy metadata objects and their empty field lists are constructed through
the actual cold factories, rather than supplied as completed types.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 170/170 CTest suites passed locally with four jobs; no compiler warnings |
| Specialized field factories | 236 checks for all seven profiles, common/reference constructor chains, physical bytes and padding, allocation gates, exact tail dispatch and retained continuations |
| Field values | 108 checks for prototype cells, sizes/alignment, source capture, live destination/size, overlapping byte copies, directional failure prefixes and string-default ownership |
| Field operations | 65 checks, including physical size/default-pointer stores, conflicting typed identities and expired weak storage |
| Cold metadata integration | 484 checks through all 27 fields, 25 local names/offsets/globals, lazy metadata construction, actual defaults/layout/publication and ordered deferred callbacks |
| Original decoder prefix | 400 original-inclusive /107 synthetic checks; separate enabled fixture still reaches `8002A0A8`, cursor 5,381 and dependency body 2,453 bytes |
| Player regression | 447 original-inclusive /62 synthetic checks; 48 held/released frames and 28 prepared application/system/serial frames |
| Python asset verifier | 6 synthetic tests passed |

The integration still prepares physical allocators, pool/platform prerequisites
and original cold null global/prototype cells explicitly. Its default buffers
have declared zero-initialized bytes. The string arena exposes four initialized
padding bytes after each allocation for the original comparison's aligned word
reads; the actual interner must still copy and initialize the character payload.
These allocator preconditions do not prove a complete allocator or universal
zero initialization. Pointer-list growth, string interning and table insertion,
descriptor construction and metadata registration run their native bodies.
Repeated suspension at `8002DD18` does not replay completed schema or publication.

Bounded comparisons used the supplied USA DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, and the unchanged reviewed decoder:

| Profile | Cases | Failure prefixes | Original instructions | Addresses | Differences |
| --- | ---: | ---: | ---: | ---: | ---: |
| Seven field factories, constructors, resets and actual tails | 3,715 | 2,214 | 439,588 | 890 | 0 |
| Field default copying, string setter and known virtuals | 825 | 326 | 57,036 | 265 | 0 |

The family comparison checks every physical byte, including padding, plus
ordered callbacks and failures. It executes the string/object `+2C` tails and
their buffer callers; registration wrappers, reflected allocation, physical
pool operations and string-pool construction/release remain prepared endpoints.
The values comparison executes `80063D04`, directional copy `800031E8`, string
setter `80071694`, finalization and the known prototype/size virtuals. It covers
scratch aliases, changed live destinations/sizes, captured string buffers,
reference words and partial failures. Nonnull interning and physical allocator
endpoints are prepared; null interning runs the original leaf. Alignment leaves
received independent instruction review, not a separate differential corpus.
Neither profile establishes whole registration, allocator behavior or gameplay.

Final core SHA256:
`8250D2CE126F92EB31E923EF8179B3EBF5A07F81B8C5E9A2FD8D4FD94E157211`.
Ignored logs are `work/native-field-family-build.log`,
`work/native-field-family-prefix.log`, `work/native-field-family-player.log`
and `work/native-field-family-verifier.log`. Reports
`work/native-field-family-result.json` and `work/native-field-values-result.json`
record the matching source/driver hashes and bounded comparison scope.
See [IGB_META_FIELD_FACTORY.md](IGB_META_FIELD_FACTORY.md),
[IGB_FIELD_VALUES.md](IGB_FIELD_VALUES.md) and
[IGB_METADATA_REGISTRATION.md](IGB_METADATA_REGISTRATION.md).
No interactive preview or GitHub Actions run was launched. Continue through
the reached deferred class registrations before connecting initialized ArkCore
state to the original-data decoder.

## Native metadata probe and field schema

Validated locally on 2026-09-20. The same cold-bootstrap composition now runs
actual metadata probe `80029FFC` and reset `80065218`, publishes type word
`804761D8`, and enters defaults `8002A164`. Its actual field batch `80065924`
reaches the first specialized signed-integer factory `8002E1AC`. That body is
still an explicit boundary. `igMetaObject` retains its two inherited fields,
flags 9 and size 0x64; `igObject` remains the sole published type with flags 7,
and ArkCore remains disabled. Reset-only construction creates no field list.

The 25 constructor/name/global/offset table entries, all 21 customization steps,
lazy metadata getters, scalar temporary packing and generic schema callers are
reconstructed separately. Prepared specialized constructors, scalar virtuals and
string-default setter outputs make their full caller tests possible; they do
not turn the incomplete cold prefix into completed metadata registration.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 169/169 CTest suites passed locally with four jobs; no compiler warnings |
| Metadata probe | 77 checks covering actual reset, captured core/word, ordered cleanup, suspension, wrapping references, physical bindings and malformed state |
| Generic field schema | 51 checks covering factory batches, raw append, packed creator release, name ownership, live tables/backing, captured count and finalization |
| Metadata defaults | 1,477 checks covering all 25 table entries, every customization, scalar byte widths, actual lazy-getter callers, suspension and explicit missing bodies |
| Cold metadata integration | 204 checks through the actual probe/defaults/batch prefix, with no manufactured field from missing `8002E1AC` |
| Original decoder prefix | 400 original-inclusive /107 synthetic checks; separate enabled fixture still reaches `8002A0A8`, cursor 5,381 and dependency body 2,453 bytes |
| Player regression | 447 original-inclusive /62 synthetic checks; 48 held/released frames and 28 prepared application/system/serial frames |
| Python asset verifier | 6 synthetic tests passed |

Bounded differential comparisons used the supplied USA DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, with the unchanged reviewed decoder:

| Profile | Cases | Failure prefixes | Original instructions | Addresses | Differences |
| --- | ---: | ---: | ---: | ---: | ---: |
| Field batch, configuration and schema callers | 617 | 396 | 112,444 | 339 | 0 |
| Metadata probe and actual reset constructor | 422 | 129 | 54,352 | 145 | 0 |

The field comparison prepares specialized constructors, string-pool/interner/
release bodies, unknown virtuals, allocator operations and destruction. Actual
helper bodies and raw append run with spare represented backing. The probe
comparison executes `80029FFC`, `80065218`, `8006665C` and the null string leaf;
string-pool construction and release are prepared endpoints with mutations.
It does not substitute a prepared constructor. Ordinary reset always clears
`+60`, so adversarial nonnull probe cleanup at that slot belongs to the separate
synthetic tests. Independent review checked the original tables, customization
ordering, helper read order and explicit proof boundaries.

Core SHA256:
`0C4BBDE82A87F8F6EE7A45D4C5F11AE7BC09291808BC3AE1F6D4C1BA3095FC89`.
Ignored logs are `work/native-metadata-schema-build.log`,
`work/native-metadata-schema-prefix.log`, `work/native-metadata-schema-player.log`
and `work/native-metadata-schema-verifier.log`. Differential reports are
`work/native-field-schema-result.json` and `work/native-metadata-probe-result.json`.
See [IGB_METADATA_PROBE.md](IGB_METADATA_PROBE.md),
[IGB_FIELD_SCHEMA.md](IGB_FIELD_SCHEMA.md) and
[IGB_METADATA_DEFAULTS.md](IGB_METADATA_DEFAULTS.md).
No interactive preview or GitHub Actions run was launched. Complete native
startup and gameplay remain required; continue at actual `8002E1AC` and the
other reached specialized field bodies.

## Native type publication and metadata layout

Validated locally on 2026-09-19. Actual type publication `800660AC` and field
layout `80065A58` now finish `igObject` registration in the cold-bootstrap
composition. Original initial type-list constructors and reserve are connected
in that same test, replacing its earlier prepared completed lists. Actual
child-list construction and inherited field append then reach igMetaObject's
local constructor probe `80029FFC`.

At the new boundary, igObject has flags 7, registry index 0, reference word 1,
alignment 4, allocation prefix 0 and unchanged native size 8. Its two fields
have ordinals 0/1, nonowning declaring-type bindings and reference words 2 after
inheritance. igMetaObject has flags 9, the same inherited fields and native
size 0x64. The registry contains exactly one type; ArkCore remains disabled.
No ready class seed or successful registration callback replaces those bodies.
Platform/manager work and physical allocation remain explicitly prepared.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 166/166 CTest suites passed locally with four jobs; no compiler warnings |
| Type publication | 72 checks for identity search, signed counts, missing bindings, full-word wrapping retain, actual append/growth, suspended allocators, fresh registry reload, partial failure and adapter lifetime |
| Metadata layout | 47 checks for known unsigned-int alignment, special/inherited fields, exact rounding, 16-bit ordinals, captured field count, live parent/list reads, ready-flag publication, callbacks and nonowning declaring-type lifetime |
| Cold metadata integration | 198 checks including actual initial lists/reserve, complete base registration, child-list construction and inherited physical field stores before pending metadata probe |
| Specialized field reset regression | 65 checks including expired nonnull declaring-type state and raw null reset without an owning cycle |
| Original decoder prefix | 400 original-inclusive /107 synthetic checks; separate enabled-runtime fixture still reaches `8002A0A8`, cursor 5,381 and dependency body 2,453 bytes |
| Player regression | 447 original-inclusive /62 synthetic checks; 48 held/released frames and 28 prepared application/system/serial frames |
| Python asset verifier | 6 synthetic tests passed |

Layout deliberately does not write native size from its computed field extent:
the original body only publishes alignment, conditional allocation prefix,
field ordinal/declaring metadata and flag4 before registered callbacks. The
callback dispatcher captures its initial signed count but reloads the captured
core's list/backing for each function. Type publication searches by canonical
pointer identity, stores -1 before retained append when absent, then searches
the freshly reloaded registry again. Neither operation enables reflection.
See [IGB_TYPE_PUBLICATION.md](IGB_TYPE_PUBLICATION.md),
[IGB_METADATA_LAYOUT.md](IGB_METADATA_LAYOUT.md) and
[IGB_METADATA_REGISTRATION.md](IGB_METADATA_REGISTRATION.md).

Bounded comparisons with the supplied USA DOL (SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`) used the final native core:

| Profile | Cases | Failure prefixes | Original instructions | Addresses | Differences |
| --- | ---: | ---: | ---: | ---: | ---: |
| Type publication, identity search and retained append | 1,022 | 875 | 51,074 | 253 | 0 |
| Metadata layout and registered callback dispatch | 1,332 | 1,114 | 110,800 | 159 | 0 |

The publication profile executes actual search, publication, retained/raw
append, reserve/growth and zeroing; only underlying pool lookup/allocation
endpoints are prepared. It compares core list pointers, list reference/backing/
count, backing words and metadata reference/index fields. The layout profile
executes actual slot helpers, field layout, registered-callback traversal and
153 calls to the exact unsigned-int alignment leaf. Other alignment virtuals
and registered callback bodies remain prepared. Both profiles provide explicit
matching physical pointer-owner mappings and use the unchanged reviewed
instruction decoder. They do not establish arbitrary allocator/MMU behavior,
unknown virtual bodies or complete engine startup. Sources and proof scopes
received independent review; all recorded source hashes match the final tree.

Final core SHA256:
`933392B281D25E8CB8A7CCC7E8C6781ADF363B54D80DD1C65526358BA6C66931`.
Ignored local build/integration evidence: `work/native-publication-layout-build.log`,
`work/native-publication-layout-prefix.log`, `work/native-publication-layout-player.log`
and `work/native-publication-layout-verifier.log`. Differential reports are
`work/native-type-publication-result.json` and
`work/native-metadata-layout-result.json`. No interactive preview or
GitHub Actions run was launched. The full native game remains incomplete;
continue through the metadata constructor probe, its field schema and remaining
runtime registration before claiming complete startup or decoded gameplay.

## Native igObject specialized fields and finalization

Validated locally on 2026-09-19. The same cold metadata chain now executes
`80066B7C`: both `_refCount` and `_memPoolIndex` use actual `80022A90`
construction, native string interning and mutable name assignment, unsigned-int
default-buffer preparation, field finalization and raw pointer-list append.
The next boundary is runtime type publication `800660AC`. `igObject` has flags
11, `igMetaObject` remains busy with 8, and ArkCore remains disabled. Physical
allocators and initial platform/manager prerequisites remain prepared; the
unsigned-int prototype is explicitly represented as its original cold BSS null.
This is not a completed cold startup or a new gameplay preview.

| Check | Evidence |
| --- | --- |
| Final Release CoreOnly build | 164/164 CTest suites passed locally with four jobs; no compiler warnings |
| Specialized field factory | 63 checks for all four constructor/reset entries, complete 0x34-byte results and untouched padding, exact double reset, physical typed identity, enabled registration/reflection boundaries and retained callbacks |
| Field operations | 58 checks for mutable source cells, wrapping references, callback replacement, acquired/displaced name lifetime, fresh virtual/global reads, signed capacity comparison, live free/allocate arguments and actual raw append |
| igObject defaults | 126 checks for two actual descriptors, names/default storage, late canonical cells, fresh pool/metadata reads, full-word string counts, packed creator release and retained failure prefixes |
| Cold metadata integration | 164 metadata-registration checks, including both real field constructors/finalizers and their physical list stores before pending type publication |
| Global-cell lifetime | Nonowning weak bindings preserve unknown, known-null and expired nonnull states; publication does not introduce a field/cell ownership cycle |
| Original decoder prefix | 400 original-inclusive /107 synthetic checks; separate enabled-runtime fixture still reaches `8002A0A8`, cursor 5,381, dependency body 2,453 bytes |
| Player regression | 447 original-inclusive /62 synthetic checks; 48 held/released frames and 28 prepared application/system/serial frames |
| Python asset verifier | 6 synthetic tests passed |

Two bounded comparisons execute original instructions against the final native
core using the supplied USA DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`:

| Profile | Cases | Failure prefixes | Original instructions | Addresses | Differences |
| --- | ---: | ---: | ---: | ---: | ---: |
| Specialized field factory and constructors | 1,453 | 657 | 138,761 | 284 | 0 |
| Field naming, finalization, buffer preparation and append | 1,360 | 924 | 54,916 | 262 | 0 |

The factory profile includes actual direct allocation/constructor callers,
both resets, null-name interning and verified return-only virtuals. Pool
allocation/tag services, enabled registration/reflected construction, string
pool creation and release remain prepared endpoints. The field-operation
profile includes concrete unsigned-int virtuals, default-buffer allocation
wrappers and actual retained/raw pointer-list append; string release, unknown
virtuals and underlying pool operations remain prepared. Its append cases stay
inside available physical backing; separate pointer-list evidence covers growth.
Neither profile claims arbitrary memory-fault equivalence or whole startup.
The `80066B7C` caller is covered by permanent composition tests and independent
instruction review, not a new whole-body differential comparison. See
[IGB_META_FIELD_FACTORY.md](IGB_META_FIELD_FACTORY.md),
[IGB_FIELD_OPERATIONS.md](IGB_FIELD_OPERATIONS.md) and
[IGB_OBJECT_DEFAULTS.md](IGB_OBJECT_DEFAULTS.md).

The first build exposed a test-only trace retaining a descriptor after the test
released its other owners. Clearing that trace corrected the weak-owner fixture;
explicit byte-width construction also removed a narrowing warning. The final
build and comparisons use the corrected source.

Final core SHA256:
`47535AEED92FC884FD5A9BCE4940F14C270154DAE59E13418AED1A4AE4583A77`.
Ignored local evidence: `work/native-object-defaults-final-build.log`,
`work/native-object-defaults-prefix.log`, `work/native-object-defaults-player.log`,
`work/native-object-defaults-verifier.log`,
`work/native-meta-field-factory-result.json` and
`work/native-field-operations-result.json`. No interactive preview or GitHub
Actions run was launched.

## Native igObject registration and typed list construction/growth

Validated locally on 2026-09-19. The cold metadata chain now enters actual
`igObject` parent registration, root callback-list construction, native name
interning and its local layout probe before reaching specialized field defaults
`80066B7C`. Typed metadata-list factories and raw pointer-list append/reserve
also replace their prior caller endpoints. See
[IGB_OBJECT_REGISTRATION.md](IGB_OBJECT_REGISTRATION.md),
[IGB_TYPE_LIST_FACTORY.md](IGB_TYPE_LIST_FACTORY.md) and
[IGB_POINTER_LIST.md](IGB_POINTER_LIST.md).

| Check | Evidence |
| --- | --- |
| Final Release CoreOnly build | 161/161 CTest suites passed locally with four jobs; no compiler warnings |
| igObject registration | 75 checks for canonical argument/getter identities, actual root-parent allocation/constructor ordering, retained calls, local probe, pending defaults, weak adapters and recursive failure |
| Cold metadata integration | 140 metadata-registration checks; distinct constructed `igMetaObject`/`igObject` owners remain busy with flags 8/9, actual interning publishes one string header, and core stays disabled at `80066B7C` |
| Raw pointer lists | 48 checks for nonlinear capacity growth, fresh size/live count reads, captured store across missing mappings, wrapped negative indices, null publication, byte/word clear failure prefixes, retained owners and actual parent field-retain/append composition |
| Typed list factories and bootstrap integration | 12,417 checks including represented allocation bytes: two factories/three constructor entries, typed alias conflicts, physical suballocations, actual bootstrap list construction and reserve to 1,536 slots, byte-only backing and pending registration |
| Original decoder prefix | 400 original-inclusive /107 synthetic checks; separate prepared enabled-runtime fixture still reaches `8002A0A8`, cursor 5,381, dependency body 2,453 bytes, loading=1 and succeeded=0 |
| Player regression | 447 original-inclusive /62 synthetic checks; 48 held/released frames and 28 prepared application/system/serial frames |
| Python asset verifier | 6 synthetic tests passed |
| Pointer-list instruction comparison | 1,679 cases, 162,732 original instructions at 189 addresses, zero differences, including 863 bounded failure prefixes |
| Typed factory instruction comparison | 1,393 cases, 79,230 original instructions at 250 addresses, zero differences, including 788 bounded failure prefixes |
| igObject caller/probe comparison | 467 cases, 8,423 original instructions at 83 addresses, zero differences |

The two cold integrations are complementary. Metadata integration prepares
initial core-list owners/reserve and physical/string-arena endpoints, then
executes actual metadata/object registration and construction. List integration
executes actual initial list constructors, three allocation-size calls and
reserve/zeroing before missing metadata registration, with platform/manager,
pool/allocation and callback-allocation endpoints prepared. Neither fixture
seeds ready metadata or enables reflection before required registration returns.
They do not establish full cold startup or completion of the decoder fixture.

Pointer-list comparisons execute `80053BC0`, `8005383C`, `8005641C`, `800682A4`,
`80068430` and original memset. Allocation-owner/tagged-pool lookup and pool
size/reallocation virtuals remain prepared. The corpus compares represented
backing bytes, pointer/count/reference state and ordered calls; physical bounds
exclude unrepresented wrapped addresses. It does not qualify a general MMU or
arbitrary malformed allocation contents. Permanent tests separately cover
partial byte/word clear prefixes and late typed binding without store redirection.
Typed-factory comparisons execute both factories, all direct constructor/reset
entries, raw/base allocation helpers and verified no-op virtuals. Registration,
reflected construction, allocator lookup/allocation/tag services remain prepared.
The igObject comparison executes the local probe/base reset for aligned offsets
zero/four and null core, raw getters, and root-parent callback-list constructor.
Pool/allocation endpoints are prepared. Its `80066B08` case compares exact
arguments captured at `80066204` against the native argument helper; the full
generic-registration composition is covered by permanent tests, not claimed
as a new full-body differential proof. Sources and proof scopes received
independent review.

Final core SHA256:
`933F6F5349751611AA86CB73DEA028096B84EB91E39246E0E4A4E53BC0C7EEAA`.
Ignored local evidence: `work/native-object-lists-final-build.log`,
`work/native-object-lists-prefix.log`, `work/native-object-lists-player.log`,
`work/native-object-lists-verifier.log`,
`work/native-typed-pointer-list-result.json`,
`work/native-type-list-factory-result.json` and
`work/native-object-registration-result.json`. The reports record matching
library/driver identities and covered source hashes. No GitHub Actions or
interactive preview ran. The walking preview is unchanged; complete field
descriptors, registry/layout, allocator services and game startup remain required.

## Native cold ArkCore startup and metadata registration

Validated locally on 2026-09-19. The native ArkCore constructor and bootstrap
caller now preserve the original disabled initialization phase. Canonical
`igMetaObject` registration/getters compose with the existing registrar and
direct metadata/internal-list constructors. Parent-field inheritance and name
assignment also replace their original registrar endpoints. See
[IGB_ARK_CORE_BOOTSTRAP.md](IGB_ARK_CORE_BOOTSTRAP.md),
[IGB_METADATA_REGISTRATION.md](IGB_METADATA_REGISTRATION.md) and
[IGB_METADATA_OPERATIONS.md](IGB_METADATA_OPERATIONS.md).

| Check | Evidence |
| --- | --- |
| Final Release CoreOnly build | 158/158 CTest suites passed locally with four jobs; no compiler warnings |
| Parent and name operations | 42 checks for live field/layout reloads, captured counts and owners, wrapping references, retained failure/suspension, raw overwrite lifetime, actual native interning and registrar adapters |
| Canonical metadata registration | 124 checks for exact arguments, shared cache, ready/busy gates, both getters, retained calls, recursive invalid-startup failure and actual cold constructor integration |
| ArkCore constructor/bootstrap | 70 checks for exact physical stores and untouched bytes, enable ordering, 23 registration calls, capacity equality, fresh pool/count reads, missing/null prefixes and retained continuations |
| Cold integration | Actual constructor/bootstrap, callback runner, metadata registration, generic registrar and direct metadata/internal-list constructors reach missing parent registration `80066B08`; canonical metadata is published with busy flag 8, enabled remains 0 and later counts remain unwritten |
| Original decoder prefix | 400 original-inclusive / 107 synthetic checks; the separate prepared enabled-runtime fixture still reaches `8002A0A8` with cursor 5,381, dependency body 2,453 bytes, loading=1 and succeeded=0 |
| Player regression | 447 original-inclusive / 62 synthetic checks; 48 held/released frames and 28 prepared application/system/serial frames |
| Python asset verifier | 6 synthetic tests passed |
| Parent/name instruction comparison | 906 cases, 29,675 original instructions at 124 addresses, zero differences, including 369 bounded failure prefixes |
| ArkCore instruction comparison | 759 cases, 262,345 original instructions at 447 addresses, zero differences, including 586 bounded failure prefixes |

The cold integration prepares platform/manager work, initial core-list owners
and reserve, physical allocation and pool endpoints. It first suspends inside
field-list backing allocation, resumes without replay, and reaches the actual
missing parent registration. It does not seed ready metadata or enable reflection
before registration returns. The outer ArkCore allocation/global-publication
wrapper, complete classes/fields/layout and remaining services are required.
The existing enabled decoder fixture has not become an authentic cold start:
enabled reflection with a null fundamental metadata cache recursively requests
itself before publication. The new adapter detects that invalid active-call
cycle and reports failure without modifying flags or manufacturing metadata.

The parent/name corpus executes actual `80065640`, `80065788`, `800658E4`,
`80065820` and `80053E6C`. Pool lookup, child-list construction, raw `80053BC0`
append/growth and external string services are prepared boundaries. The permanent
native tests separately compose the recovered string-table interner.
The ArkCore corpus executes `8003D234`, `8003D2F8`, the pool selector,
callback-list header constructor, registration wrappers and callback runner.
Platform/manager calls, pool virtuals, initial list construction/capacity/reserve,
callback allocation, registration bodies and final metadata are prepared.
Registration body results are null in that corpus; it does not qualify field
schema construction or nonempty deferred callbacks. Physical core bytes,
represented global changes, callback-header projections and ordered endpoint
arguments are compared, including bounded malformed-storage failure prefixes.
Both production and comparison boundaries received independent review.

Final core SHA256:
`04DC34F5EDA5C4CBEB15D78C5CDE5F2A89A4943D1333867CB1EE662BFAC11E43`.
Ignored local evidence: `work/native-cold-registration-final-build.log`,
`work/native-cold-registration-prefix.log`,
`work/native-cold-registration-player.log`,
`work/native-cold-registration-verifier.log`,
`work/native-metadata-parent-name-result.json` and
`work/native-arkcore-bootstrap-result.json`. The comparison reports record
library, driver and covered-source identities matching the final build.
No full dependency load or native game boot is claimed. No GitHub Actions or
interactive preview ran; the walking preview is unchanged.

## Native bootstrap metadata and internal-list construction

Validated locally on 2026-09-19. The original ThreadManager registration now
enters the generic registrar using the actual canonical metadata cell and
handle-based memory-manager lookup. Metadata factory `80029E64`, direct
constructors `800651B4`/`80065218`, internal-list factory `8002DB4C` and its
three direct entries compose native raw allocation and base-header bodies.
See [IGB_THREAD_REGISTRATION.md](IGB_THREAD_REGISTRATION.md),
[IGB_BOOTSTRAP_OBJECT.md](IGB_BOOTSTRAP_OBJECT.md),
[IGB_METADATA_FACTORY.md](IGB_METADATA_FACTORY.md) and
[IGB_FIELD_REGISTRY_FACTORY.md](IGB_FIELD_REGISTRY_FACTORY.md).

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 155/155 CTest suites passed locally with four jobs; no compiler warnings |
| Bootstrap allocation and header | 70 checks for heap/pool prefix differences, wrapping arithmetic, physical failure prefixes, retained operations, live metadata, pool tags, teardown order and construction ownership |
| ThreadManager registration | 59 checks for exact arguments, canonical globals, native handle lookup, recursive prerequisites, lazy metadata reload and adapter lifetime |
| Internal-list construction | 44 checks for actual object/backing requests, fresh core gate, two base resets, physical backing, unknown pointer entries, null/failure paths and constructor lifetime |
| Metadata construction | 62 checks for exact stores and preserved padding, lazy strings and live release, retained overwritten string owners, actual internal-list composition, missing/null boundaries, stable results and construction leases |
| Original decoder prefix | 400 original-inclusive / 107 synthetic checks; native ThreadManager registration, handle 5 selection and metadata factory now reach `igMetaObject` registration `8002A0A8`, retaining cursor 5,381, dependency body 2,453 bytes, loading=1 and succeeded=0 |
| Player regression | 447 original-inclusive / 62 synthetic checks; 48 held/released frames and 28 prepared application/system/serial frames |
| Python asset verifier | 6 synthetic tests passed |
| Metadata instruction comparison | 1,326 cases, 103,331 original instructions at 261 addresses, zero differences, including 671 bounded failure prefixes |
| Internal-list instruction comparison | 1,312 cases, 72,954 original instructions at 209 addresses, zero differences, including 754 bounded failure prefixes |

The supplied-data prefix preserves its prior prepared loader, string-pool,
runtime-type and list endpoints. The represented core remains enabled. It does
not switch to the disabled bootstrap constructor merely to manufacture metadata
or move the decoder forward. The explicit handle publication represents only
the verified metadata-handle store, not the full static initialization sequence.

The direct constructor fixtures preserve actual physical headers and typed
identities. They do not infer initialized field-pointer entries from allocation,
or complete class registration from a constructed empty internal list. Original
metadata registration, reflected construction for these typed owners, pool and
string construction internals, field/default registration, layout and the rest
of the loader lifecycle remain required. No full dependency load or game boot
is claimed.

The metadata comparison executes original factory, direct constructor/reset,
raw allocation and base-header bodies, null-name interning and reached no-op
virtuals. Registration, reflected allocation, pool lookup/allocation/tag,
string-pool allocation and construction, internal-list factory and final string
release are explicitly prepared boundaries in that corpus. The separate
internal-list comparison executes its factory and all three direct entries,
including raw allocation and base construction; it prepares registration,
reflected construction, pool resolution/allocation and selected virtuals.
Comparisons include complete represented object bytes and ordered callbacks.
Malformed physical ranges qualify the reached bounded failure prefix, not
unrepresented heap contents or hardware exception behavior. The permanent
metadata test separately composes the actual native internal-list factory.

Core SHA256:
`48F489A9336AE42C8F387617A6394C0510589962AA3C6B8C1BCBF8C556BE0BCA`.
Ignored local evidence: `work/native-bootstrap-final-build.log`,
`work/native-bootstrap-prefix.log`, `work/native-bootstrap-player.log` and
`work/native-bootstrap-verifier.log`. Original comparison reports with library,
driver and covered-source identities are stored in
`work/native-metadata-construction-result.json` and
`work/native-internal-list-result.json`. No GitHub Actions or interactive preview
ran; the walking preview is unchanged.

## Native type registration, callbacks and scalar defaults

Validated locally on 2026-09-19. Native `80066204` type registration now
composes the actual deferred callback-list constructor and append. The file
registration entry uses its original argument identities and canonical metadata
cell, actual callback runner and prerequisite once guard. Exact int/bool field
virtuals now perform original default-byte copying inside the object initializer.
See [IGB_TYPE_REGISTRATION.md](IGB_TYPE_REGISTRATION.md),
[IGB_REGISTRATION_CALLBACKS.md](IGB_REGISTRATION_CALLBACKS.md) and
[IGB_FIELD_DEFAULTS.md](IGB_FIELD_DEFAULTS.md).

| Check | Evidence |
| --- | --- |
| Final Release CoreOnly build | 151/151 CTest suites passed locally with four jobs; no compiler warnings |
| Generic type registration | 132 checks covering all low flag combinations, initial pool/cache order, metadata publication, abstract-byte truncation, full-word release, fresh filter/layout state, native callback-list construction/append, suspension, nested registration and retained failures |
| Callback lists and prerequisites | 338 checks for constructor/append, captured count/live backing, nonlinear capacity growth, physical failure prefixes, callback cleanup, all nonzero once-byte values, exact file arguments, ownership and native adapters |
| Scalar field defaults | 96 checks for exact int/bool bindings, physical byte copies, overlapping ranges, captured fields, missing/null/released state, header aliases, and actual initializer composition writing cookie `0xFADA` and a Boolean default |
| Original decoder prefix | 392 original-inclusive / 107 synthetic checks; actual file registration/runner/once guard reaches missing thread-manager registration `800238A8`, retaining cursor 5,381, dependency body 2,453 bytes, loading=1 and succeeded=0 |
| Player regression | 447 original-inclusive / 62 synthetic checks; 48 held/released frames and 28 prepared application/system/serial frames |
| Python asset verifier | 6 synthetic tests passed |
| Registrar instruction comparison | 1,662 cases, 226,700 original instructions at 285 addresses, zero differences, including 501 bounded failure prefixes; separate 31-instruction `8002F1A0` argument/metadata-cell alias proof matched |
| Scalar instruction comparison | 1,544 cases, 196,106 original instructions at 51 addresses, zero differences, including 320 initial null-read failures |
| Guard/runner instruction comparison | 677 cases, 15,794 original instructions at 93 addresses, zero differences, including 467 callback/null failure prefixes |

The registrar corpus executes actual `80066204`, its stack string preparation,
callback-list constructor and append. Parent linkage, names, metadata allocation,
registry publication, layout, filter operations and allocator callbacks remain
explicit prepared endpoints. Callback backing allocations have explicit physical
ranges: malformed writes beyond them compare the reached count-publication and
bounded failure prefix, not hypothetical original heap contents or hardware
exception behavior. The direct file argument proof isolates its prerequisite
and generic-registration call boundaries.

The scalar corpus executes original copy wrappers, byte-copy body and verified
leaves without prepared internal callees. Whole-range validation is the native
profile; it does not qualify partial malformed copies across missing storage.
The guard/runner corpus executes actual `80066188`, `8003E85C`, `80021B94` and
`8003E848`, with target callbacks and physical free operations prepared. Both
oracles and production ordering received independent source review.

The original-data prefix keeps its earlier prepared metadata/list, loader,
timer and string-pool allocation state. It additionally supplies explicit cold
prerequisite bytes after verifying that missing state remains pending. Native
code sets the once flags before entering the first thread-manager registration.
It does not provide a successful prerequisite body or manufacture file metadata.
This is not completed dependency decoding, full registration or original boot.
Actual prerequisite and parent metadata construction, field registration/layout,
reference handlers, automatically constructed directory lists and complete
loader/decoder lifecycle remain required.

Final core SHA256:
`B545577505570763D8F046AF9646536D51A1E7633999591894DF66646C5C146D`.
Ignored build/integration evidence: `work/native-registration-final-build.log`,
`work/native-registration-prefix.log`, `work/native-registration-player.log`
and `work/native-registration-verifier.log`. Original comparison reports retain
their final driver, production-library and covered-source hashes in
`work/native-type-registration-result.json`,
`work/native-scalar-field-defaults-result.json` and
`work/native-registration-callbacks-result.json`. No interactive preview or
GitHub Actions ran; the walking preview is unchanged.

## Native reflected file construction and post-default pools

Validated locally on 2026-09-19. Native file factory `8002EBEC` now enters the
reflected allocation caller `8006546C` and object initializer `800667F4`, with
the actual allocation-base helper, three field-pass callers and default-mode
dispatcher. Verified file virtuals connect the current metadata getter,
return-only leaves and post-default pool assignment `80051698`. Current-pool
lookup uses the canonical memory-manager globals. See
[IGB_FILE_FACTORY.md](IGB_FILE_FACTORY.md),
[IGB_OBJECT_INIT.md](IGB_OBJECT_INIT.md) and [IGB_FILE_POOLS.md](IGB_FILE_POOLS.md).

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 148/148 CTest suites passed locally with four jobs; no compiler warnings |
| Object initialization | 143 checks for header publication, physical vtable reads, first-pass gates, signed modes, F0 low-byte results, captured counts/live backing, fresh metadata, packed-header mutations, pending instance append, callback teardown, physical limits and retained failures |
| File factory | 85 checks for registration/metadata order, redirects, pool selection, wrapping allocation, pending construction, missing state, ownership and ordinary/dependency adapters; composed native file virtuals and current-pool lookup |
| File post-default pools | 86 checks for selector/fallback state, lazy preparation, fresh assignments, full-word references, low23 destruction, callback teardown, pending owners, alias cycles and abandonment |
| Original decoder prefix | 389 original-inclusive / 107 synthetic checks; supplied `ndmw.igb` enters the actual file wrapper and stops at required registration `8002F178`, retaining cursor 5,381, dependency body 2,453 bytes, loading=1 and succeeded=0 |
| Player regression | 447 original-inclusive / 62 synthetic checks; 48 held/released frames and 28 prepared application/system/serial frames |
| Python asset verifier | 6 synthetic tests passed |
| Factory/initializer instruction comparison | 1,761 cases, 348,209 original instructions at 330 addresses, zero differences; 1,153 ordinary returns and 608 matching reached failure prefixes |
| Pool instruction comparison | 2,092 finite cases, 294,427 original instructions at 177 addresses, zero differences, including 725 reached failure prefixes; four nonterminating original alias cycles excluded explicitly |

The combined factory/initializer comparison executes the original wrapper,
allocation caller, header initialization, allocation-base helper, three field
loops and signed default-mode dispatch. Registration, redirects, pool selection
and allocation, virtual implementations, allocation-owner lookup and instance
append use explicit prepared endpoints. The separate pool comparison executes
the actual post-default callback, current-pool wrappers, concrete manager getter
and fallback traversal; pool preparation, unknown manager virtuals and final
destructors remain prepared endpoints. Original nonterminating alias cycles
are not counted as equivalent to the native bounded failure.

Permanent tests additionally cover native callback lifetimes across suspension
and teardown, and the paired physical/typed reference-word mutation contract.
Prepared zero-field metadata proves factory/initializer composition only.
It does not supply or prove the original file's complete registered fields,
field defaults, decoder state or parent-class initialization.

The supplied-data prefix retains its previously documented prepared list,
loader, timer and string-pool allocation state. It now binds the real file
wrapper, which reaches missing `8002F178` registration. No synthetic registration
success, empty metadata or constructed dependency file is supplied. All 223
dependencies, graph construction, complete startup and gameplay remain unproved.
Next work is actual file/parent class registration, reached field-default
implementations and pool construction/allocation, alongside remaining loader
and decoder lifecycle work.

Final core SHA256:
`3D13C184E7C3FD4CEC3982E86851ECB4BFF97CFEFBB9DC19CEA553E3FC92A07A`.
Ignored evidence: `work/native-file-init-build.log`,
`work/native-file-init-prefix.log`, `work/native-file-init-player.log`,
`work/native-file-init-verifier.log`, `work/native-igb-factory-init-result.json`,
`work/native-igb-factory-init-review.md` and
`work/native-igb-file-pools-result.json`. Both comparison reports record the
production library, driver and covered source hashes. No GitHub Actions or
interactive preview ran; the walking preview is unchanged.

## Native loader access, synchronous dependencies and string interning

Validated locally on 2026-09-19. The dependency-table caller now composes original
loader getter `8007042C`, interning `80054094` with concrete ordered string-table
operations, and synchronous dependency request `8006FA6C`. The getter reads the
same canonical ArkCore owner used by runtime metadata; its `+40` loader remains
distinct from direct global `80562104`. See [LOADER_GLOBALS.md](LOADER_GLOBALS.md),
[LOADER_DEPENDENCY.md](LOADER_DEPENDENCY.md) and
[IGB_STRING_POOL.md](IGB_STRING_POOL.md).

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 145/145 CTest suites passed locally with four jobs; no compiler warnings |
| Canonical loader getter | 21 permanent checks for shared ArkCore identity, separate direct global, missing/null state, captured owners and adapter composition |
| Synchronous dependency request | 136 checks for independent pool fallbacks, packed references, cache claims, full-word string ownership, live names, nested loads, retries, suspension, destruction and abandoned-owner retention |
| String pool | 145 checks for sorted insertion, cache identity, empty/case-sensitive names, reference wrapping, aligned physical reads, mutable names, mutex captures, duplicate second searches, nested allocation, copy aliasing and failure prefixes |
| Original decoder prefix | 386 original-inclusive / 107 synthetic type-mapping checks; `ndmw.igb` resolves 20 types and 60 field maps, reads its 2,453-byte dependency body, resolves ArkCore's loader and interns the base path before the first synchronous request reaches file factory `8002EBEC`; cursor 5,381, loading=1, succeeded=0 |
| Player regression | 447 original-inclusive / 62 synthetic checks; 48 held/released player frames and 28 prepared application/system/serial frames |
| Python asset verifier | 6 synthetic tests passed |
| Loader getter instruction comparison | 128 cases, 374 original instructions at 3 addresses, zero differences, including 10 reached null-core faults |
| Synchronous loader instruction comparison | 2,225 cases, 785,153 original instructions at 512 addresses, zero differences; 2,011 ordinary returns and 214 matching reached faults |
| String interner instruction comparison | 1,104 cases, 375,246 original instructions at 334 addresses, zero differences |

The getter evidence separately executes four original constructor profiles that
leave `+40` unchanged, and four bounded startup publication profiles that write
the core field before the distinct direct global. These do not establish the
resource factory or complete ArkCore startup.

The synchronous-loader corpus executes actual request, directory lookup,
`strcmp`, object-pool byte selection, loading claim, append/growth and prefix
retry bodies. Numeric pool resolution, reflected file construction, destructors,
string operations, mutexes, list reserve and decoder endpoints are prepared.
The string corpus separately executes actual interning, lookup and insertion
wrappers, both searches, insertion, `strcmp`, `strlen`, byte copy and bounded
`memmove`. Arena allocation, realloc and mutex endpoints are prepared. It does
not qualify pool construction, final release or partial malformed copies across
distinct host allocations; the latter use an explicit complete-range profile.

The original-data prefix retains native stream reads, recovered metadata and
existing prepared list/timer endpoints. It additionally supplies an explicitly
prepared `igResource` owner, string-pool allocation/initialization and arena
storage. Native code owns actual getter reads, path interning and the first
synchronous request; no dependency file factory or decoder success is supplied.
The body remains retained at cursor 5,381. This is not evidence that all 223
dependencies loaded or that native boot/gameplay is complete. Concrete file and
pool factories, arena/reallocation/release services, graph construction and the
remaining startup path are still required.

Final core SHA256:
`5C672E23F1BDDA08E6CD86281B6D0C41D2AB7C6892AE578F4D70A40BDD7147C7`.
Ignored evidence: `work/native-loader-intern-build.log`,
`work/native-loader-intern-prefix.log`, `work/native-loader-intern-player.log`,
`work/native-loader-intern-verifier.log`, `work/native-loader-globals-result.json`,
`work/native-loader-sync-result.json` and `work/native-string-intern-result.json`.
No interactive preview or GitHub Actions ran. The walking preview is unchanged.

## Native loading paths, dependency tables and pool names

Validated locally on 2026-09-19. Native decoder stages now implement directory
path `800505E0`, dependency-table caller `800521EC` and pool-name caller
`80052554`. The concrete memory-manager lookup and its 46-name initialization
are also reconstructed. The earlier provisional label of `800505E0` as
type/field preparation was corrected after inspecting its actual body: it
duplicates the stream filename and truncates the final separator. See
[IGB_LOADING_PATHS.md](IGB_LOADING_PATHS.md),
[IGB_DEPENDENCIES.md](IGB_DEPENDENCIES.md) and
[IGB_MEMORY_POOLS.md](IGB_MEMORY_POOLS.md).

| Check | Evidence |
| --- | --- |
| Final Release CoreOnly build | 142/142 CTest suites passed locally with four build jobs; no compiler warnings |
| Directory paths / pool-name caller | 62 synthetic / 116 original-inclusive checks; separator handling, retained pool/name inputs, raw path publication, both byte orders, numeric handles, padding, growth mutations and short-read/fault prefixes |
| Concrete memory-manager lookup | 123 permanent checks for original names, selector/manager captures, exact case/first match, null/empty requests, aligned string reads and reached physical boundaries |
| Dependency caller | 179 checks for header/body reads, signed counts, four path-string transactions, full-word references, loader/list/name captures, restoration/free order, suspension and faults |
| Original decoder prefix | 380 original-inclusive / 107 synthetic type-mapping checks; `ndmw.igb` resolves 20 types and 60 field maps, processes its directory path, reads the 2,453-byte dependency body and reaches loader `8007042C` at cursor 5,381; loading=1, succeeded=0 |
| Separate original pool-name table | Original table at offset 5,381 maps all 46 names to handles 0..45 and frees its temporary buffer; next cursor 5,717 |
| Player regression | 447 original-inclusive / 62 synthetic checks, including 48 held/released player frames and 28 frames across seven prepared application/system/serial batches |
| Python asset verifier | 6 synthetic tests passed locally |
| Path/pool caller instruction comparison | 1,392 cases, 570,062 original instructions at 261 addresses; zero differences, including 78 matching reached faults |
| Dependency caller instruction comparison | 1,103 cases, 154,934 original instructions at 311 addresses; zero differences, including 104 matching reached faults |
| Memory lookup instruction comparison | 3,307 cases, 1,209,746 original instructions at 137 addresses; zero differences, including 1,340 matching null/physical-read failures |
| Memory-manager name initializer | Actual initializer/strncpy execution: 19,464 instructions at 61 addresses; native initialized name bytes match |

The path/pool corpus executes the two actual callers, string length, swaps and
list growth. The dependency corpus executes its actual caller, swap, pointer
append and growth. Their stream operations, allocation/free, list constructors,
string interning/release and dependency loader endpoints remain explicitly
prepared, including callback mutations and injected failure prefixes. The
pool-name caller's prepared lookup domain is checked separately from the real
memory-manager getter/selector/lookup/strcmp corpus. Constructor name-table
verification does not qualify manager allocation or global activation.

The supplied decoder prefix uses native byte reads, explicitly bound native
scratch storage and recovered runtime metadata; list allocation and timing
remain fixture services. It stops before the missing loader singleton operation
and retains its dependency buffer. The pool-name integration starts separately
at its independently located table offset; it does not assert the preceding
223 dependency requests have completed. Native dependency loading, interning,
object factories, graph population, post-load work, cleanup and complete startup
remain required. No native boot/gameplay completion follows from these tests.

The final core SHA256 is
`1DF92583C1BC341DAE9935F7CD15F3CB34076E8B84AB85A3191E719ECC0AEB8E`.
Ignored evidence includes `work/native-igb-loading-final-build.log`,
`work/native-igb-loading-prefix.log`, `work/native-igb-loading-pools-original.log`,
`work/native-igb-loading-player.log`, `work/native-igb-loading-verifier.log`,
`work/native-igb-loading-paths-result.json`,
`work/native-igb-loading-dependencies-result.json` and
`work/native-igb-memory-pools-result.json`.
The walking preview is unchanged. No interactive preview or GitHub Actions ran.

## Native runtime types and inherited field mapping

Validated locally on 2026-09-19. The decoder now binds original `8004FC94`
runtime type reconciliation to authored native C++. Canonical metadata serves
both the existing field-table callback and actual registry lookup, physical
field access and runtime counts. The bounded bootstrap profile contains twenty
object layouts and forty-seven field-class identities recovered independently
from original registration/default routines. See
[IGB_RUNTIME_TYPES.md](IGB_RUNTIME_TYPES.md),
[IGB_TYPE_MAPPING.md](IGB_TYPE_MAPPING.md) and
[IGB_RUNTIME_BOOTSTRAP.md](IGB_RUNTIME_BOOTSTRAP.md).

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 139/139 CTest suites passed locally with four build jobs; no compiler warnings |
| Runtime metadata helpers | 335 permanent checks, including all enabled/field-flag byte values, lazy name consumption, pending descriptor globals, retained list identity and reached null/physical bounds |
| Type mapping | 107 synthetic / 376 original-inclusive checks; legacy/current formats, both byte orders, signed inherited slots, status/reference publication, pending final destruction, dynamic gaps/layout, live registration reload, recursion and abandonment |
| Recovered bootstrap | 276 checks for inherited field sharing, replacements, registered layouts and widths, exact metadata identity and unsupported schema/owner boundaries |
| Original loading prefix | `ndmw.igb`: 20 runtime types, 60 inherited field mappings, 47 matched field classes and one unmatched class; cursor 2,916, pending `800505E0`, loading=1 and succeeded=0 |
| Original player regression | 447 original-inclusive / 62 synthetic checks; 48 held/released player frames and 28 frames across seven prepared application/system/serial batches remain successful |
| Python asset verifier | 6 synthetic tests passed locally |
| Runtime helper instruction comparison | 4,096 cases, 193,584 original instructions at 108 addresses; zero differences |
| Type caller instruction comparison | 1,926 cases, 2,216,149 original instructions at 751 addresses; zero mismatches, including 1,719 ordinary returns and 207 matching reached endpoint-failure prefixes |
| Bootstrap registration facts | 114 records compared against independent original registration/default/vtable evidence; zero differences across 20 object layouts, 47 field-class metadata identities and 47 singleton bindings |

The runtime helper corpus executes the original registry lookup, slot count,
physical getter and local-field count bodies; only `strcmp` is a prepared C-string
contract. Its static physical input profile is supplemented by permanent tests
for missing globals, lazy scratch-name reads, partial strings and ownership.

The type caller corpus executes `8004FC94`, the actual runtime helpers, pointer
list append/short-list growth and byte swap. Constructors, reserve allocation,
final list destruction, pool/converter getters, field operations and class
registration are explicitly prepared endpoints. This includes the full dynamic
caller sequence and observable reference/count/layout effects, not the original
bodies of those prepared endpoints. Callback teardown ordering and lazy name
consumption were corrected during independent review before the final build.

The supplied-data decoder prefix uses recovered registration metadata instead
of constructing registries from serialized names/counts. List constructors and
allocators, status words, platform timer and exact-name byte-source binding are
explicit fixture services. An empty alignment registry still selects default16.
The unsupported `igVertexBlendShaderCapsInfoMetaField` remains null through the
ordinary unmatched lookup; no synthetic class or dynamic fallback fills it.
The runtime registration subset does not establish original global ordering,
complete singleton initialization, field-class schemas or object factories.
Its separate registration-facts comparison also verifies the shared identities
of 47 distinct physical object fields. This is a comparison with independently
recovered facts, not execution of complete original engine startup.

At this checkpoint, remaining work included directory-path preparation
`800505E0` (then provisionally mislabeled type/field preparation), concrete factories,
object construction/population, original post-load work, cleanup/close, complete
runtime registration, loader queue execution and startup. The native walking
preview is unchanged; no interactive preview or GitHub Actions was launched.

Final comparison drivers link production `dw4_core.lib`, SHA256:
`D36C1EB0444C69D72B90FE4E40BEDD99ADC352CFABD461BD24C297E41FAC5E30`.
Ignored evidence includes `work/native-igb-mapping-build.log`,
`work/native-igb-mapping-original.log`, `work/native-igb-mapping-player.log`,
`work/native-igb-mapping-verifier.log`,
`work/native-igb-runtime-types-result.json`,
`work/native-igb-types-result.json` and
`work/native-igb-registration-result.json`.

## Native IGB stream, early tables and decoder orchestration

Validated locally on 2026-09-19. The directory loader now has a native adapter
for original decoder `800528DC`, its failure tail, concrete stream/header
processing and three early table bodies. Original `ndmw.igb` reaches runtime
metatype construction with explicitly prepared field registry/list endpoints.
It remains loading at that boundary; complete native boot is not established.
See [IGB_DECODE.md](IGB_DECODE.md), [IGB_STREAM.md](IGB_STREAM.md) and
[IGB_DECODE_TABLES.md](IGB_DECODE_TABLES.md).

| Check | Evidence |
| --- | --- |
| Final Release CoreOnly build | 136/136 CTest suites passed locally with four build jobs; no compiler warnings in the final build |
| Stream/header suite | 258 synthetic / 271 original-inclusive checks; byte order, status/version flags, short and stale reads, pool bindings, retained callbacks and reached ownership failures |
| Early tables | 57 synthetic / 116 original-inclusive checks; field metadata matching, duplicate calls, null unmatched fields, alignment defaults, physical slots, shared cursor and failure prefixes |
| Decoder orchestration | 97 checks for original helper order, checked failures, live status/version gates, warning/log rules, timer ordering, suspension, recursion and actual loader/header/table composition |
| Clock | 104 permanent checks, including binary64 seconds, original rounding, stopped/live behavior and one-sample ownership |
| Original loading prefix | `ndmw.igb`: 48 prepared runtime field mappings, four default-16 alignments and raw descriptions of 20 types; exact cursor 2,916, pending `8004FC94`, loading=1 and succeeded=0 |
| Original header integration | Actual `ndmw.igb`, `bectext.igb`, `dng101b1.igb` and `dng101b2.igb` headers agree with the independent metadata reader |
| Original player regression | 447 original-inclusive / 62 synthetic checks; existing four-player prepared frame coverage remains intact |
| Python asset verifier | 6 synthetic tests passed locally |
| Stream/header instruction comparison | 5,120 cases, 1,539,152 original instructions at 370 addresses; zero mismatches against the final core |
| Decoder caller instruction comparison | 5,457 cases, 4,927,279 instructions at 605 addresses; zero mismatches, including 4,530 normal returns and 927 matching reached endpoint failures |
| Clock seconds instruction comparison | 20,032 cases, 16,977,350 instructions at 241 addresses; zero mismatches |

The stream corpus executes the actual header, scratch-buffer caller, byte swap,
opening caller, basename helper and mode setter. Stream reads, pool allocation
and free, singleton getters, underlying filename dispatch and interning are
prepared endpoints. It uses bounded valid buffers, full header reads and
nonfinal guest references. Permanent tests cover additional missing/short data,
unknown scratch bytes, pending native pool bindings and explicit final-destructor
boundaries. Neither corpus claims complete original allocator or file-manager
internals.

The decoder comparison executes original main/failure orchestration and the
actual timer seconds/getter/signed arithmetic bodies. Every format/table helper,
activation, cleanup, conditional close, warning and log body is an explicitly
prepared endpoint in that caller comparison. It compares callback-visible
prefixes, live status/version/clock changes, exact elapsed binary64 bits and
failure boundaries. This qualifies the caller, not those prepared helper bodies.
The separate supplied-data integration does execute concrete open/header and
all three early table helpers. Its field registry is deliberately prepared from
independently decoded names, and its empty alignment registry selects default
16. Those fixtures do not qualify original runtime registration or constructors.

The clock corpus executes `800936A0`, `80093630` and the actual signed
remainder/division/conversion routines, using independent integer-based binary64
rounding for division/addition. Only the running platform nanosecond source is
prepared. It includes signed limits and cases where converting the entire
nanosecond word before division differs by one binary64 step. Matching supplied
samples does not claim identical host and console clock sampling.

Independent source review corrected scratch publication after free, explicit
pool selection, final old-field destructor boundaries and persistent ownership
of the field-list factory reference. The field table keeps that creator
reference separately from its published `+BC` reference; this is required by
the original cleanup's two live releases. The alignment list instead drops its
temporary creator reference at its actual release site. Cleanup itself remains
a named implementation boundary. A fresh-file original test also clears the
direct-table fixture's prepared endian field before running actual header setup.

Remaining work includes concrete file/registry factories, `8004FC94` metatype
and slot reconciliation, object construction/population, post-load effects,
cleanup/closing, queue execution and original startup registration. The native
walking preview is unchanged. No interactive preview or GitHub Actions ran.

All three final comparison drivers link production `dw4_core.lib`, SHA256:
`09A477BD9A315D82FE9CA4A7CA77ECDCBD7073822F5E2C717DAE25536ECC716A`.
Ignored evidence includes `work/native-igb-final-build.log`,
`work/native-igb-stream-original.log`, `work/native-igb-tables-original.log`,
`work/native-igb-player-original.log`, `work/native-igb-python-verifier.log`,
`work/native-igb-stream-result.json`, `work/native-igb-decode-main-result.json`
and `work/native-igb-decode-clock-result.json`.

## Native directory requests, loading claims and context info registration

Validated locally on 2026-09-19. The stage factory now enters actual native
directory request, loading-claim release and application info-registration
bodies. The connected cache fixture follows request, suspended info delivery,
remaining export occurrences and zero-claim removal. See
[NATIVE_STAGE_SERVICES.md](NATIVE_STAGE_SERVICES.md),
[LOADER_DIRECTORY.md](LOADER_DIRECTORY.md), [LOADER_REQUEST.md](LOADER_REQUEST.md)
and [CONTEXT_INFO.md](CONTEXT_INFO.md).

| Check | Evidence |
| --- | --- |
| Final Release CoreOnly build | 133/133 CTest suites passed locally with four build jobs; no compiler warnings |
| Context registration | 77 checks covering live unsigned registry bounds, low-byte acceptance, captured callbacks, nested calls, duplicate/null infos, append growth and retained failure references |
| Directory lookup and claims | 142 checks for filename metadata, signed claim wrap, refreshed mutex/parent owners, first-pointer removal, root replacement, suspension and abandoned-reference preservation |
| Directory request and path fallback | 128 checks for cache reuse, new-file publication, exact async-one versus nonzero decisions, captured decode paths, live next attempts, pending operations and failure prefixes |
| Native stage composition | 10 checks for actual cache request/claim, suspended context registration, duplicate export order, claim-zero removal, missing bindings and absence of a globals/service ownership cycle |
| Original player regression | 447 original-inclusive / 62 synthetic checks passed; 48 per-RAM held/released calls and 28 calls over seven application/system/serial batches remain qualified |
| Context instruction comparison | 2,552 cases, 255,829 original instructions at 187 addresses; zero mismatches against the final core, including 1,845 completed cases and 707 matching reached boundaries |
| Directory instruction comparison | 4,096 cases, 392,355 original instructions at 328 addresses; zero mismatches against the final core |
| Request/path instruction comparison | 3,072 cases, 408,082 original instructions at 340 addresses; zero mismatches against the final core |

The context corpus executes actual registration, append and capacity-growth
instructions, including 127 nested registrations. It compares manager traces,
selected identities/counts, physical pointer-slot contents, incoming retained
references after invocation destruction and the returned backing identity.
Manager bodies and reflected type/allocation/copy/free endpoints are explicit
fixtures. Native retained backing aliases do not prove freed guest-memory reuse
or the original memcpy body's partial-fault behavior.

The directory corpus executes original lookup/named-list, claim, list-erase and
filename-release bodies. String comparison, mutex virtuals, memmove and the
special root-directory accessor are prepared endpoints. It uses valid physical
ranges and nonfinal reference counts, and compares return words, owner/list
state and call order. It does not qualify unknown destructors or asynchronous
loading merely because claim management succeeds.

The request corpus executes the actual directory-request/path bodies, claim
acquisition, semaphore preparation and fast-append callers. Lookup, factories,
semaphore virtuals, interning, C formatting, queue execution and the concrete
decoder are prepared endpoints; the corpus uses existing string-pool state,
valid backing and nonfinal references. It compares selected published fields,
path order and endpoint identities. An independent review checked the signed
positive-result calculation, exact-one/nonzero async decisions and endpoint
scope; this does not prove decoder or worker internals.

Independent review corrected two request ownership prefixes: an incoming pool
reference now survives a reached old-field destructor boundary, and a mutex
published during its factory callback survives the subsequent original raw
overwrite. A further regression clears all external/cache references while a
filename-release wrapper is pending; abandoning that wrapper now retains its
original temporary in loader state instead of releasing it prematurely.

The stage integration explicitly prepares its cached directory, exported
owners, pool/lifecycle operations and manager acceptance callbacks. Its actual
native request and claim operations are not a substitute for new-file decoding.
Concrete igIGBFile factory/decoder `800528DC`, queue execution, loader lifecycle,
manager construction and remaining handlers still require implementation.
Complete startup, active camera, rendering, audio, saves and full gameplay
remain unqualified. No interactive preview or GitHub Actions was launched.

All final comparison drivers link only production `dw4_core.lib`, SHA256:
`65C56346284A5DAC62019929C029F91B0BEF6FEE011E9F811E1ECF8F0D1585B9`.
Ignored local evidence includes `work/native-loader-context-final-build.log`,
`work/native-loader-context-player-original.log`,
`work/native-context-registration-result.json`,
`work/native-loader-directory-result.json` and
`work/native-loader-request-result.json`.

## Native scheduled system, stage processing and storage-error phases

Validated locally on 2026-09-19. The scheduled `beSystem` update now executes
its recovered reset prefix, time publication, stage continuation, storage-error
continuation and first-time tail. The supplied-data player fixture enters
through the actual application, serial scheduler, system and player-manager
bodies. See [SYSTEM_FRAME.md](SYSTEM_FRAME.md), [SYSTEM_STAGE.md](SYSTEM_STAGE.md)
and [SYSTEM_STORAGE.md](SYSTEM_STORAGE.md).

| Check | Evidence |
| --- | --- |
| Final Release CoreOnly build | 129/129 CTest suites passed locally with four build jobs; no compiler warnings in the final build |
| Scheduled system update | 109 checks for reset byte/full-word semantics, callback order, time publication, child suspension, first-time gating, ownership and application scheduling |
| Stage continuation | 376 checks for live pool selection, retained arguments/results, request-versus-completion ordering, signed info traversal, reference publication and reached failures |
| Storage-error continuation | 195 checks for status classification, native message delivery, pause ownership, phases, captured contexts, suspended operations and reached failures |
| Original player integration | 447 original-inclusive / 62 synthetic checks; 48 held/released per-RAM calls plus 28 calls through seven application/system/serial/manager batches |
| Composed system instruction comparison | 9,656 cases, 858,656 original instructions at 174 addresses; zero mismatches against the final production core |
| Stage instruction comparison | 4,096 cases, 307,993 original instructions at 231 addresses; zero mismatches against the final production core |
| Storage-error instruction comparison | 1,816 cases, 156,054 original instructions at 392 addresses; zero mismatches against the final production core |

The composed system corpus includes 4,452 reset exits and 5,204 executions of
the actual timing conversion and storage helper's short paths. It executes
the original null-stage branch and prepares only reached platform SDK calls.
The stage corpus separately executes the original stage caller, directory
readiness and null-string leaf; pool, load, registration, allocation, string
comparison and cleanup endpoints are explicit prepared services. It compares
service traces and selected published state with nonfinal reference counts,
not every memory access or unknown destructor behavior.

The storage corpus executes the actual helper, pause setter and send wrapper.
It compares selected state, pause anchor, mask choice and ordered message/factor
effects. SDK status, lookups, message replies, scene changes, mask helpers and
factor setting are prepared endpoints; the native side uses the production
messenger with explicitly authored recipients. This qualifies the control flow
for those fixtures, not the missing error-scene or platform implementations.

The original player integration explicitly supplies clock/reset/storage
samples, the dispatcher endpoint, registry order, gate activation, initial
viewer state and device input. A suspended second player preserves the first
player's completed work, the application sample and the completed system
prefix. All seven batches perform exactly one reset/status sample, dispatcher
call and system time publication apiece. Application callback teardown releases
the fixture's prepared context/registry ownership cycle.

Independent review found and corrected a retained-argument bug: a missing
operation provider could allow a replacement system context to redirect an
already selected world-factor call. Factor, messenger and viewer requests now
capture the context at the original read boundary. Regression checks cover
replacement while waiting, and captured viewer handles cannot be redirected
by later changes to their provider wrapper.

The comparison drivers link only the final production core, SHA256:
`132DA6EB586E321BE5DC20A613373A9625BB55B6BB06B1F78FEC414A3B2610F8`.
Local ignored evidence includes `work/native-system-final-build.log`,
`work/native-system-player-original.log`,
`work/native-system-frame-composed-result.json`,
`work/native-system-stage-final-report.json` and
`work/native-system-storage-result.json`.

Actual initialization must still request and load `ndmw.igb` and the error
scene through the concrete loader, registration and scene operations.
Factory-default null requests and prepared platform samples do not complete
startup. Remaining managers, active camera publication, device polling,
rendering, audio, saves and complete gameplay remain required. The walking
preview is unchanged; no interactive preview or GitHub Actions was launched.

## Original manager traversal, scheduled players and camera inverse

Validated locally on 2026-09-19. The recovered serial scheduler now reaches
the common manager `+74` traversal and actual player `+84` operation through
canonical info/RAM lists. The specialized camera view inverse is separately
implemented with original overlapping-buffer and arithmetic behavior. See
[MODEL_MANAGER_FRAME.md](MODEL_MANAGER_FRAME.md),
[MODEL_FRAME.md](MODEL_FRAME.md) and [CAMERA_MATH.md](CAMERA_MATH.md).

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 126/126 CTest suites passed locally with four build jobs; no compiler warnings in the final build |
| Common manager traversal | 315 checks for exact default/gate bytes, signed captured counts, live list/backing/virtual reloads, pending targets, reached failures, ownership and serial-scheduler binding |
| Actual player frame and adapter | 50 checks, including retained runtime binding, duplicate RAM occurrences, new batch operations, invalid owner/type and destroyed-facade boundaries |
| Original player integration | 445 original-inclusive / 62 synthetic checks; existing 48 held/released per-RAM calls plus 28 calls through seven actual serial-scheduler/manager batches |
| Manager instruction comparison | 4,096 fixtures, 448,733 original instructions at 58 addresses, 18,045 prepared RAM calls; zero mismatches against the final production core |
| Camera numerical helper | 20 permanent checks for overlap, store order, original rounding, signed zero and partial publication on reached arithmetic failure |
| Camera instruction comparison | 15,474 cases, 730,725 original instructions across all 51 addresses; zero mismatches against the final production core |

The original integration prepares one player-manager registry entry and its
canonical four-RAM info list. In its first serial batch, RAM zero completes
before RAM one suspends at the unrepresented viewer owner. Retrying does not
repeat either the completed player or the reached input prefix. Supplying the
viewer resumes the same scheduler, manager and player operations, finishes
the remaining authored RAMs and clears the actual scheduler cache. Six more
batches cover three held and three released samples for each player. They
preserve SQ_WALK/RUNND1 and SQ_WAIT/WAITD1 transitions, per-player delayed
number effects, original collision and completed-batch nonreplay.

The manager comparison executes original caller/gate instructions with
explicit prepared RAM endpoint mutations; the actual player body is exercised
separately by the original-data integration. The camera corpus compares
11,646 completed calculations and 3,828 reached finite-profile boundaries,
including every whole-float overlap from -16 through +16. Its matrix-only
API excludes the original metadata-wrapper store and all camera mode/graph
and viewer publication operations. Both comparison drivers link only the
final production core, SHA256:
`9E14F061A9039399B6C215A2F446773E8BA495E7759DE45ECA0CEC544542D838`.

Independent review caught an initial test-only vector/span comparison that
did not compile. The expected authored order is now an owned vector snapshot.
New queue assertions guard size before inspecting the first entry so a
lifecycle regression produces a test failure instead of an invalid access.
No original engine behavior was altered to accommodate those test fixes.

Complete startup/registration, remaining scheduled managers, device polling,
active camera, rendering, audio and full gameplay remain required. The test
still supplies context samples, gate activation and initial viewer state.
The walking preview is unchanged; no interactive preview or GitHub Actions
was launched. Local ignored evidence is in `work/native-manager-camera-*`,
`work/native-manager-player-original.log`, `work/native-manager-frame-*` and
`work/native-camera-math-*`.

## Native viewer ownership, repeated player updates and serial scheduling

Validated locally on 2026-09-19. The original player input now resolves its
native viewer parameters directly and retains their inline matrix through
suspension. Binding an explicitly prepared viewer resumes the pending
directional frame to SQ_WALK/RUNND1, real motion and collision. Another 48
per-RAM calls complete six directional and six released samples for each
of four original Agumon slots. See [VIEWER.md](VIEWER.md),
[PLAYER_INPUT.md](PLAYER_INPUT.md) and [MODEL_FRAME.md](MODEL_FRAME.md).

| Check | Evidence |
| --- | --- |
| Final Release CoreOnly build | 124/124 CTest suites passed locally with four build jobs; no compiler warnings |
| Viewer ownership | 119 checks for exact zero defaults, separate allocations, inline aliases, raw matrix words, null owners, live updates, replacement and final alias lifetime |
| Player input | 101 checks, including native viewer selection, missing versus represented null setup, retained parameters during inverse suspension, later live buttons and override precedence |
| Original player integration | 379 original-inclusive / 62 synthetic checks; four neutral frames, one resumed directional frame and 48 further per-RAM frames complete with all five original stage collision infos registered |
| Serial scheduler | 51 checks for copied-count batching, duplicate order, live source/cache replacement, pending callbacks, physical storage, reference/failure prefixes and application binding |
| Scheduler instruction comparison | 1,200 cases, 426,861 original instructions and 140 instruction addresses; zero mismatches against a driver linked only to the final production core |

The repeated player fixture advances its prepared shared context by16,666,667
nanoseconds per sample and calls the actual system timing leaf. Each per-RAM
update changes only that slot's node and command state. Held samples retain
SQ_WALK/RUNND1 with positive local movement. Released samples return to
SQ_WAIT/WAITD1, enqueue the original delayed player-number effect and advance
its elapsed time from1 through6. Re-entering each completed frame preserves
the node and complete command list. Traversal stacks remain balanced.

An initial test assumed the startup number-effect entry survived SQ_WALK and
accessed an empty list. The actual ordinary sequence entry correctly cleans
that queue; returning to SQ_WAIT correctly creates a new one. The corrected
test checks those original lifecycle effects and compares complete list
snapshots rather than dereferencing a permanently assumed first entry.

Factory review also corrected a research assumption: the identity matrices
in the parameters layout probe are not copied into runtime instances. The
actual reflected-field factory publishes zero matrices. Native preparation
uses those zero words. Input's original inverse caller initializes its own
identity destination and ignores singular status. This qualifies the initial
viewer owner and that original branch, not active gameplay camera selection.
The separate igViewModel, camera mode, graph path and later parameters
publication remain required.

[SERIAL_SCHEDULER.md](SERIAL_SCHEDULER.md) records the native batch and its
supported physical/reference profile. Independent review found a late-copy
failure path that released an already acquired incoming owner during native
exception unwinding. It now preserves that original retained-reference prefix,
with a permanent regression. The original-body comparison executes scheduler
and resize instructions with explicitly prepared manager and memset endpoints
in fixed-capacity16 fixtures. It does not compare allocation growth, packed
reference counts, destructors or the missing game manager bodies.
Final core SHA256:
`14AB7E4C79DD264C610E0260AEC7AA92D8AFCC1D1E040FDFF6E64EB85209686E`.

The fixture supplies device samples, initial viewer state and per-RAM order.
It is not the game's registered manager scheduler or connected controllers.
The serial batch is separately tested through the native application entry;
the original dispatcher, registration, manager bodies, camera, rendering/audio
and complete playable game remain unfinished. The visible walking preview is
unchanged. No interactive preview or GitHub Actions was launched.
Ignored evidence is in `work/native-viewer-*`,
`work/native-serial-scheduler-*` and `work/native-camera-mode-boundary.md`.

## Native application timing and directional player input

Validated locally on 2026-09-19. Player input now calls the recovered native
matrix inverse with its original identity-initialized destination and ignored
singular status. Its heading calculation reconstructs the reached finite MSL
operations and original single-precision rounding. The new application entry
publishes its time prefix before explicit dispatcher and scheduler boundaries;
the recovered system timing leaf consumes the same shared context owner.
See [APPLICATION_FRAME.md](APPLICATION_FRAME.md),
[PLAYER_INPUT_MATH.md](PLAYER_INPUT_MATH.md) and [PLAYER_INPUT.md](PLAYER_INPUT.md).

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 122/122 CTest suites passed locally with four build jobs; no compiler warnings |
| Application timing | 45 checks covering fixed-point wrap, original defaults, publication order, current context, live fields, retained callbacks, recursion, competing frames and abandonment |
| Heading math | 37 numerical checks covering original instruction goldens, quadrants, reduction regions, tiny results, signed zeros and reached/unused nonfinite fields |
| Player input | 88 checks, including native camera inversion, singular destination behavior, source identity and retained override suspension |
| Original player integration | 169 original-inclusive / 62 synthetic checks; four prepared neutral frames still complete; the next directional frame retains its selected pad and turn-history prefix at the required original viewer-matrix owner |
| Application instruction comparison | 11,296 cases, 587,392 original instructions and 52 instruction addresses; zero mismatches against the final production-core test executable |
| Heading instruction comparison | 71,162 cases: 70,934 exact numerical results and 228 checked finite-profile boundaries; 17,011,398 original instructions and 283 instruction addresses; zero mismatches against the final production core |

The original directional frame reaches its camera boundary in `SQ_WAIT`, mode2,
after its selected turn-history counter becomes29. Retrying the same pending
frame leaves that value unchanged. Its pad sample is explicitly prepared;
device polling and active camera selection are not implemented by this test.
The camera owner chain is the original `igViewerSceneInfoManager` parameters,
not the walking preview's camera. No replacement camera is supplied here.

Application comparisons execute the original producer instructions with
prepared timer-return, dispatcher and scheduler endpoints. They validate the
producer and callback ordering, not the bodies of those endpoints. The
application timer is distinct from the global collision timer. The system
timing helper represents only its reached time-publication leaf, not the
surrounding complete system update or application startup.

The heading corpus includes every signed-byte stick pair after division by74,
578 camera/stick boundary cases, 5,000 arbitrary word profiles and 48
reached/unused nonfinite placements. Its expected values come from decoded
original instructions with integer-controlled rounding, not host trig or the
native helper. The final driver links the production core without substitute
objects. These finite profiles do not claim hardware FPSCR exception behavior.
Final core SHA256:
`01175E8AE07403326A4C7F22F63F41B49C2D672B171E738694E417E4F958F2C8`.

The visible preview is unchanged. Complete startup, manager scheduling, camera
publication, repeated original gameplay and connected controllers remain open.
No interactive preview or GitHub Actions was started. Ignored local logs are
`work/native-player-timing-*`, `work/native-application-frame-*` and
`work/native-player-input-math-*`.

## Native game clock and completed prepared player frames

Validated locally on 2026-09-19. The native global long timer and verified
`igTransform`/`beCopyTransform` updates now connect the retained collision
traversal to completed prepared neutral player frames. This supersedes the
earlier missing-clock boundary below. See [GAME_CLOCK.md](GAME_CLOCK.md),
[ACTOR_TRANSFORM.md](ACTOR_TRANSFORM.md) and [MODEL_FRAME.md](MODEL_FRAME.md).

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 120/120 CTest suites passed locally with four build jobs |
| Clock lifecycle | 60 checks for start, pause, reset, stop, restart, raw stopped bytes, wrapped time, live field reads, failure prefixes and monotonic PC sampling |
| Native collision dispatch | 61 checks, including clock selection, bound-time bypass, native null-input update and retained nonnull-input suspension without a second clock read |
| Original player integration | 167 original-inclusive / 62 synthetic checks; four original GENERATE calls followed by one completed neutral per-RAM frame for each slot; all five stage collision infos registered; actual wall/floor responses and balanced traversal stacks |
| Original transform updates | 1,151 original-inclusive / 82 synthetic checks; 56 stage-179 copy-transform occurrences preserve every matrix word; real nonnull sequence input 1874 on node 1873 remains at its required virtual +5C boundary |
| Independent native comparisons | 10,291 cases: 4,264 timer lifecycle, 4,001 hardware conversions, 12 timebase rollovers and 2,014 null-input transform calls; zero mismatches |
| Bound visitor time | Separate raw instruction proof for 1,035 stored-time cases; no global clock access |

The first player frame is deliberately paused at missing HITLANDMODEL, then at
the missing clock. Retrying preserves its work, prepared rays and earlier frame
effects. Installing a started `NativeGameClock` with its real PC monotonic
source resumes that same invocation to completion. Each remaining slot retains
independent work and advances only its own delayed subcommand. A completed-frame
retry leaves the matrix and subcommand time unchanged. Pad data, system delta,
manager registration and per-RAM call order remain explicit test preparation;
these results do not establish scheduled gameplay or connected controllers.

The instruction comparison executes 3,316,670 original instructions across its
measured profiles. Its native drivers link the production core without
replacement source objects. Core SHA256:
`BE2EE3DF844F84A332D9BDB70897DEE9B2C3C195DCBAA7E393DE0E268B8CD364`.
Equal-input arithmetic comparisons do not establish identical host/console
sample streams, complete animation inputs or a game scheduler.

A malformed transform-input chain exposed a parser stack overflow. The original
field's base class is now checked before recursively loading an incompatible
owner. Original-data validation also caught an incorrect inference from the
field name `_transformInput`: the actual base is `igTransformSource`, proven by
the registration string and all 472 nonnull stage inputs. The corrected check
accepts those original hierarchies and rejects unrelated MODEL ONE inputs.
An older rendering fixture was changed to supply an actual source object at
its intended nonnull-input boundary. Synthetic tests cover rejection, retry,
canonical ownership and preservation of valid unknown source subclasses.

Original startup, nonneutral/repeated gameplay updates, complete animation
inputs, remaining collision operations and the outer scheduler remain required.
No interactive preview was launched and GitHub Actions remained disabled.
Ignored logs and independent evidence are in `work/native-dynamic-clock-*`.

## Native CHECKWALL and original player-frame integration

Validated locally on 2026-09-19. Native CHECKWALL now executes the original
three-pass wall query and correction, with retained messenger delivery and
original wall-enabled state. Static geometry responses reach HITWALL and update
its shared node. The original player-frame integration now reaches the dynamic
transform's required global clock instead of stopping at missing HITLANDMODEL
registration. See [HITLAND_WALL.md](HITLAND_WALL.md).

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 118/118 CTest suites passed; four build jobs; no compiler warnings in the final build |
| Wall invocation tests | 131 synthetic checks: three passes, exact prepared endpoints, first-minimum ties, per-pass query retention, packet changes during suspension, metadata prefixes, disabled-wall gates and numerical instruction goldens |
| Real synthetic wall | Actual mapped vertical triangle, original default rotation, native query receiver and existing HITWALL node adjustment; balanced vertex map/unmap |
| Supplied-data wall integration | 163 original-inclusive checks: 15 stage-179 wall-center queries produce 14 corrections; a registered HITWALL call recomputes and applies a selected response |
| Original player lifecycle | 159 original-inclusive / 62 synthetic checks: four prepared original GENERATE calls, all five stage collision info occurrences, actual HITWALL/CHECKWALL entry and dynamic global-clock boundary; retry preserves query, traversal and earlier player-frame effects |
| Independent helper comparison | 5,089 cases: 4,604 exact outputs and 485 checked arithmetic boundaries; 272,900 original instruction steps, 530 addresses; zero mismatches |
| Original MSL and reset matrix | Actual acos/cos/sin and their reached kernels, 17 ratio inputs, both narrowings and all 16 reset-matrix words independently checked; no host trig reference hooks |
| Positive-length and ratio verification | 33,554,432 representative sqrt comparisons with zero mismatches, covering both exponent parities and extremal exponents; 8,196 independent integer-instruction crosschecks |

The final numerical driver links only the final production core, with no
replacement source objects. Core SHA256:
`95FF5788F3BAAAD27849ED8C1489E4D28825D827933B498D040AF7957597C853`.
The numerical profile and exponent-scaling justification are documented in
HITLAND_WALL.md; it does not establish physical Gekko/FPSCR behavior or arbitrary
nonfinite recovery.

The original frame stops at `global clock virtual+68` before the current dynamic
node's path/matrix push. The fixture initially expected an already-pushed path;
that assertion was corrected to match the original dispatch order. No fake clock
or update provider was installed. A separate compile correction made the test's
recursive registration accept const child owners. Dynamic services, missing
bounds, CHECKWALLFULL, startup and complete scheduling remain unfinished.

Ignored evidence: `work/native-checkwall-verified-build.log`,
`work/native-checkwall-original.log`, `work/native-checkwall-player-frame.log`,
`work/native-checkwall-oracle-report.md` and
`work/native-checkwall-oracle-final-report.json`. No preview was launched,
original data was unchanged and GitHub Actions remained disabled.

## Native CHECKLAND floor delivery

Validated locally on 2026-09-19. Native CHECKLAND now prepares the original four
vertical rays, executes actual triangle traversal, selects the floor candidate
and publishes its response through a retained messenger receiver. The existing
HITFLOOR consumer applies that response to a prepared shared node. Original
manager startup, full wall responses and the complete player frame remain
unfinished. See [HITLAND_FLOOR.md](HITLAND_FLOOR.md).

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 117/117 CTest suites passed; four build jobs; no compiler warnings in the final build |
| Permanent floor tests | 79 synthetic checks: all three selection passes, rounded ties, strict endpoints, physical storage, exact rays, negative width, suspension, packet ownership, nullable metadata and partial publication on capacity failure |
| Real synthetic geometry | Four distinct corner rays hit an authored mapped triangle at Y 8; registered CHECKLAND delivery reaches HITFLOOR and updates the shared recipient node, with balanced vertex map/unmap and preserved candidate aliases |
| Supplied-data floor integration | 103 original-inclusive checks: 12 stage-179 floor-center queries produce 7 floor responses within CHECKLAND's fixed Y range; an additional registered HITFLOOR query recomputes and applies one response to a prepared synthetic recipient node |
| Independent reduction comparison | 7,689 cases; 1,533,032 actual original instruction steps; all 114 addresses in the bounded reduction/empty branch reached; 7,652 exact output comparisons and 37 expected arithmetic-overflow boundaries; zero mismatches |
| Final production linkage | The final comparison driver links only the freshly built dw4_core.lib, without replacement selector, messenger or candidate-access objects |

The original-map checks use width zero, so their four rays coincide at each
center. Distinct-corner coverage comes from the synthetic fixture. These are
complete native floor query/response calls with explicit manager and recipient
preparation, not original startup or a scheduled gameplay session. The numeric
comparison covers finite NI=1/RN=00 arithmetic and explicit overflow rejection;
it does not establish full nonfinite behavior, FPSCR exceptions or physical
console execution.

The first test build exposed an invalid fixture constructor, and its first run
exposed missing messenger selection setup. Both were corrected in tests; no
production behavior was changed to satisfy those failures. The final source
comparison used core SHA256
`B067D50668A2A162A269A679730578C199E9F407B1D7BF6C3FEE8224138F9700`.
Ignored evidence: `work/native-checkland-verified-build.log`,
`work/native-checkland-original.log`, `work/native-checkland-oracle-result.json`
and `work/native-checkland-oracle-report.md`. No preview was launched, original
assets were unchanged and GitHub Actions remained disabled.

## Native source-port resumption and transformed collision

Validated locally on 2026-09-19. The authored native engine is the active path;
DolRecomp/ModernGekko is set aside with its packages, source patches and saves
preserved. Native geometry now calls the recovered matrix inverse and canonical
AABox scratch transform. Full collision responses and the original game loop
remain unfinished. See [MATRIX_INVERSE.md](MATRIX_INVERSE.md).

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 116/116 CTest suites passed, including 90 permanent inverse checks |
| Raw original inverse comparison | 7,798 cases; 3,080,642 instructions; 3,899 exact aliases; zero status, output-word or destination-prefix differences in the documented finite NI=1/RN=00 profile |
| Native transformed geometry | 49 synthetic / 76 original-inclusive checks; rotated/scaled/translated synthetic triangle plus actual stage-179 geometry with a prepared translated matrix |
| Original common traversal regression | 54 synthetic / 85 original-inclusive checks, retaining twelve floor and fifteen wall node visits |
| Original-data identity | Supplied DOL SHA256 remains EC61C010477C844781763A3041922E38719B3CED3B74BE5CE71D778CE458E471 |

The first integration test incorrectly expected a normalized transformed
normal. The recovered original candidate append performs direct multiplication,
so scale three produces a normal of length three. The test was corrected from
the existing original instruction evidence; production behavior was retained.
No preview or interactive game was started and no hosted workflow was used.

## Historical recompilation integration

The set-aside DolRecomp/ModernGekko path has its own evidence and acceptance
scope in [RECOMPILATION.md](RECOMPILATION.md). The manual-core results below
do not establish that the recompiled game boots or plays correctly.

## Version 0.45 static collision candidates

Validated locally on 2026-09-13. Canonical geometry owners, typed node dispatch
and the original mapped primitive scan now connect common static traversal to
actual query candidate storage. Dynamic bound/inverse operations, complete
CHECKLAND/CHECKWALL responses and original player scheduling remain required
in this manual engine. It remains available as reference work while the
selected recompilation path is integrated.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 115/115 CTest suites passed; no compiler warnings |
| Static geometry producer | 37 synthetic / 62 original-inclusive checks; 12 original floor queries produce 10 triangle candidates through typed dispatch and actual query storage |
| Canonical geometry owners | 47 synthetic / 242 original-inclusive checks; 27 original collision attributes and vertex arrays, plus indexed stage-mesh coverage |
| Primitive enumeration | 83 synthetic / 198 original-inclusive checks; all 102 original stage-179 floor triangles reached with their original cursors |
| Node dispatch and retained traversal | 51 node-dispatch, 44 intersection and 54 synthetic / 85 original-inclusive common-traversal checks |
| Model owner rollback | 84 synthetic / 1,652 original-inclusive checks; completed reentrant loads retain canonical attributes, vertices and backing across repeated outer-load failures |
| Existing original integrations | 198 actor-scene, 156 player-construction/frame, 43 HITLANDDELIVER-scene, 62 scene-traversal and 413 weapon-preview checks |
| Point, distance and segment arithmetic | 2,628 cases, 253,216 original instructions, 1,675 square-root comparisons and 52 checked numeric boundaries; zero differences |
| Matrix composition and paired-single copy | 7,743 cases, 1,461,363 original instructions and 1,093 checked numeric boundaries; zero mismatches |
| Primitive instruction comparison | 861 cases, 302,123 original instructions and 460 checked prefixes; zero mismatches |
| Python and command-line tools | Six verifier, eight inspect and five catalog tests passed; boot suite ran nine tests with one skipped because no application executable was supplied |

All three numerical drivers were freshly compiled with current headers and
linked only against the final production core, without replacement module
objects. The core hash remained unchanged before and after the replays:
`C60609B12E5CBBC81A0BD48EC56FECEAF86ED8EAB807EFB8A26A9EC1E5A38CA1`.
These comparisons cover represented original instructions and reached prefixes;
they are not a physical Gekko/FPSCR oracle or full-game execution. Explicit
numeric boundaries do not claim the original nonfinite fallback behavior.

The original-data player acceptance still completes four prepared GENERATE
calls and reaches HITLANDMODEL in the first player-frame prefix. That frame
does not yet consume a complete collision response. The separate candidate
integration explicitly prepares class registration and a static query profile.
No v0.45 walking-preview application was built or launched; the manual preview
remains v0.42. GitHub Actions remained disabled throughout this validation.

## Previous v0.44 checkpoint

### HITLAND query infrastructure validation

Version 0.44.0 was validated locally on 2026-09-12 and 2026-09-13. Canonical HITLANDMODEL
geometry, reusable query pools, common switch traversal, retained visitor
entry and bounded segment/triangle arithmetic now have native implementations.
The actual player frame still reaches the HITLANDMODEL service boundary:
complete node/attribute dispatch, candidate production and wall/land response
handlers remain required. This checkpoint does not complete that backend.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 111/111 CTest suites passed; no compiler warnings |
| Canonical geometry owners | 37 synthetic / 56 original-inclusive checks; shared masks/backing, nullable physical slots, raw kind bits, dynamic source links and canonical bounds |
| Common HITLAND traversal | 54 synthetic / 85 original-inclusive checks; live lists/masks, captured loop state, name-before-kind publication, dynamic matrix prefixes and retained visitor calls |
| Query storage and publication | 60 checks; independent query/list counts, reusable segments/candidates, physical backing, aliases and committed failure prefixes |
| Intersection primitives | 43 checks; visitor reset, allocation prefixes, retained dispatch and bounded triangle effects |
| Loaded actor regression | 94 synthetic / 198 original-inclusive checks passed |
| Original model and recipient graphs | 75 synthetic / 1,643 original-inclusive model checks; 43 original-inclusive HITLANDDELIVER checks |
| Original player construction/frame | 62 synthetic / 156 original-inclusive checks; four complete GENERATE returns and the first actual player frame reaching the unchanged HITLANDMODEL boundary |
| Existing original integration | 160 motion, 202 control, 554 control/timer, 1,324 prepared sequence, 620 player-start, 672 prepared spawn and 413 weapon-preview checks passed |
| Segment/normal instruction comparison | 10,826 cases, 359,388 original instructions, 2,840 square-root comparisons and 4,850 checked numeric-boundary cases; zero differences |
| Triangle instruction comparison | 2,902 cases, 605,246 original instructions, 240 output-alias cases and 89 checked numeric-boundary cases; zero mismatches |
| Python and command-line tools | Six verifier, eight inspect and five catalog tests passed; boot suite ran nine tests with one skipped because no application argument was supplied |

Original geometry validation loads `ndmw.afs` entry 179, including floor info
1768, wall infos 192/1953 and dynamic info 24723. Its 56 dynamic children retain
actual source-transform links; loading does not copy their matrices. The common
traversal acceptance uses an explicitly prepared info order and a test-only node
observer. It visits 12 canonical floor and 15 wall nodes through their inner
masks while leaving candidate count zero. These visits establish owner and
traversal behavior, not original triangle production or hit/miss responses.

The numerical comparisons execute original segment setup `801251D0`, normal
transform `8012D434`, triangle leaf `801238E8` and its cross-product helper
`8012CB74` with independently rounded instruction models. They compare published
outputs and reached failure prefixes, including aliases. Explicit numeric
boundaries do not claim the original nonfinite fallback or FPSCR exception
behavior. They do not execute complete node dispatch, attribute/vertex mapping,
CHECKWALL/CHECKLAND handlers, physical Gekko hardware or the full game loop.

Both numerical comparison drivers were freshly compiled with current headers
and linked only against the final production core, with no replacement module
objects; both builds were clean at `/W4`. Original geometry and actor regressions
used the final unified Release executables. The core was unchanged before and
after both numerical replays; the geometry and actor executables also retained
their hashes across their original-data runs. Frozen core SHA-256:
`21335E3DC72823505D68DB9DA4256642BCB9E3CEAB47529DE83F274B920D710D`.

No v0.44 application build, launch or package was produced. The manual preview
remains v0.42 with unchanged provisional walking and camera behavior. Original
startup/scheduling, the remaining script-helper and ride providers, full
collision responses and the rest of the original gameplay remain outstanding.

All validation for this checkpoint was local. GitHub Actions was disabled at
the user's request, the workflow was removed, and no jobs were active when it
was disabled. The hosted CI entries below are preserved historical results;
they do not describe v0.44 validation.

## Previous v0.43 checkpoint

### Actual movement-collision validation

Version 0.43.0 was validated locally on 2026-09-12. The actual player frame now
sends movement-collision requests through registered HITLANDDELIVER owners and
the retained native recipient. The first original-data player frame reaches a
real HITLANDMODEL query. The query backend, remaining script-helper and ride
providers, and the full original game loop are still required.

| Check | Evidence |
| --- | --- |
| Release CoreOnly build | 107/107 CTest suites passed; no compiler warnings |
| Actual movement-collision caller | 51 checks; native delivery, reply gates, retained continuations and explicit remaining service boundaries |
| Actual HITLANDDELIVER recipient | 49 checks; five command branches, real nested queries, captured manager ownership and committed prefixes |
| HITLAND arithmetic | 44 native checks; bounded horizontal normalization and X/Z overlap helpers |
| Canonical HITLANDDELIVER graph | 28 synthetic / 43 original-inclusive checks; authored physical order, shared MODELCTRL nodes, nullable fields and real registration |
| Native messenger | 173 checks passed |
| Original player construction/frame | 62 synthetic / 156 original-inclusive checks; four complete registered GENERATE returns, actual HITLANDDELIVER registration and the first player frame reaching HITLANDMODEL |
| Canonical model graph / actual sequence | 75 synthetic / 1,643 original-inclusive graph checks; 68 original-inclusive actual sequence checks |
| Existing original integration | 160 motion, 202 control, 554 control/timer, 1,324 prepared sequence, 620 player-start, 672 prepared spawn and 413 weapon-preview checks passed |
| Arithmetic instruction comparison | 8,080 cases, 201,953 original instructions, zero native comparison mismatches |
| Python and command-line tools | Six verifier, eight inspect and five catalog tests passed; boot suite ran nine tests with one skipped because no application argument was supplied |

The original-data acceptance retains explicitly prepared startup databases,
font, map-name, clock and ACTION services. HITLANDDELIVER shares the exact
canonical node allocations used by MODELCTRL ONE. Registration preserves the
authored zero previous positions; it does not replace them with current node
positions. The first changed-position request therefore reaches the real
HITLANDMODEL query boundary. A wall/land query result is not fabricated to
complete the frame. This remains a bounded startup and first-frame acceptance,
not boot-to-game or four-player gameplay validation.

The arithmetic comparison executes the represented instruction ranges at
`802FDAF0..802FDC0C` and the complete `80300974` X/Z overlap helper. Its 7,072
normalization cases include 3,906 supported finite positive-length cases and
3,166 explicit checked numeric boundaries; the latter do not claim equivalence
to the original nonfinite fallback. The remaining 1,008 cases cover overlap.
This is bounded arithmetic coverage, not a full HITLANDDELIVER handler,
CHECKWALL/CHECKLAND traversal, response or physical Gekko/FPSCR oracle.

The final arithmetic driver was rebuilt with current headers and linked only
against the frozen production core. The core SHA-256 was unchanged before and
after replay:
`96BB4E9B53DF7F631B9B9C93076B1F5CBE7D80E3DDF5332AE70C225F8D5C135A`.

No v0.43 application build, launch or package was produced. The manual preview
remains v0.42 with the same provisional walking and camera implementation.
The tool checks above do not claim new application or visual acceptance.

Hosted CI for v0.42, run `34718661426`, was verified during this checkpoint:
all three jobs succeeded (Windows app, Ubuntu core and Windows core). This is
separate from local v0.43 validation and does not report v0.43 hosted CI.

## Previous v0.42 checkpoint

### Actual player frame validation

Version 0.42.0 was validated locally on 2026-09-12. One actual player frame
now connects input/control, status, movement, wait, invulnerability and delayed
subcommands through retained original owners. The visible walking preview is
unchanged; the package is refreshed for manual launch. Original outer
scheduling, camera and movement collision response remain required.

| Check | Evidence |
| --- | --- |
| Release build/package | 103/103 CTest suites passed; no compiler warnings |
| Actual input | 83 checks; original pad selection, captured stick/later buttons, panel messages, attack requests, live system reads and unused-owner gates |
| Actual status update | 80 checks; poison, timed resets, METAL, icon ownership, real messenger delivery and retained nested SLEEP/STUN entry |
| Actual common/player wait | 72 checks; narrow movement activity reads, qualified ACTION queries, captured old/new work and real direct command-loop continuation |
| Actual movement | 59 checks; independent captured/current channel lists, per-axis timer sites, fresh ONE/node matrix writes, aliases and committed failure prefixes |
| Actual frame integration | 35 checks; early-gate read order, empty subcommands, effect suspension, replaced work/wrapper, direct wait return, exclusivity, abandonment and rejection of unrelated guest execution while paused |
| Actual subcommand update | 66 checks; per-entry system reads, independent helper captures, retained effects, cleanup with absent versus invoked HITBOX providers and cycle-free abandonment |
| Direct command continuation | 32 entry checks; actual C3AC resume keeps captured work and skips ordinary resets/extra player callback |
| Raw actual control and timer expiry | 64 control / 62 timer checks; reached physical fields, ordered publications, live expiry lookups and signed end comparison |
| Original player construction/frame | 62 synthetic / 151 original-inclusive checks; eight `+98` returns, four complete registered GENERATE returns, then the first neutral actual frame reaches the movement-collision boundary |
| Actual movement instruction comparison | 216 cases across 18 scenarios, 49,596 original instructions, zero differences including 84 reached null-read failures and their committed prefixes |
| Timer-expiry instruction comparison | 12,144 cases, 1,767,498 original instructions, 18,868 lookups, 6,724 clock reads and 4,000 mutation cases, zero differences |
| Existing original integration | 160 motion, 202 control, 554 control/timer, 1,324 prepared sequence, 620 player-start, 672 prepared spawn and 413 weapon-preview checks passed |
| Application/verifier | 18 app CLI, 10 play CLI and six Python verifier checks passed |

The original-data test retains v0.41's explicitly prepared databases, font,
map-name, clock and actor-aware ACTION source, including 58 original exported
event tables. Four Agumon GENERATE calls complete. It then supplies neutral
physical pad owners and a system delta of one for the first player only.
That frame completes real input, raw control, status and movement callbacks,
preserves SQ_WAIT and advances the delayed player-number effect to elapsed one
without starting it before its delay of 480. The translated gate then reaches
the required original movement-collision operation. No collision success is
invented to make the test return a complete frame. Other players' frames,
device polling and the original scheduler are not supplied by this acceptance.

The movement comparison executes actual `8030A448`, `8030C85C`, `8030A2D8`,
reached scalar setup/stop, Euler/copy/local-translation helpers and the concrete
node2 setter. It compares both node matrices, old/new channel allocations,
angle fields, translated flag and current list identity. Deterministic timer
results with per-call mutation, named ONE lookup and host trig/FMA remain
explicit supplied services. Saved-box transformation has source/regression
coverage rather than this instruction comparison. The corpus does not claim
arbitrary FPSCR, nonfinite math, malformed spare capacity or full game parity.

The expiry comparison executes original `8031E238` and its fresh nested
`8031E36C`/`800695CC` helpers, with allocator/name/string/clock service boundaries.
It compares strict signed expiry, independently captured outer end fields,
fresh nested lookup/clock and committed failures. It does not establish the
whole status callback, guest allocator ABI or a complete frame by instruction
comparison. Other new input/status/wait/frame/subcommand paths have focused
native tests and source review; older prepared instruction suites do not
stand for new original-instruction coverage of those actual paths.

No interactive preview or new visual replay was launched for this core-only
integration. The previous v0.41 hidden replay remains historical graphics
evidence below. The current package uses the same provisional walking and
camera implementation.

Both new comparison drivers were freshly linked with current headers against
only the frozen production core, SHA-256
`679043B7FA06BE66795E43B8BCDBF4D16D4DCA0B8442ECCC8386DA42D8FD75E3`.
The packaged application SHA-256 is
`3A293E924A058BEF4D2F5B3C294922EF41C594E44F089050D1580405C228C36B`.

Hosted CI for v0.41 commit `095c0c9`, run `34716079313`, completed successfully
in all three jobs: Windows app, Ubuntu core and Windows core. This is separate
from local v0.42 validation; v0.42 hosted CI follows publication.

## Previous v0.41 checkpoint

Version 0.41.0 was validated locally on 2026-09-12. Ordinary MODELCTRL entry now
consumes the canonical mutable SEQ graph, executes through native ACTION
delivery, invokes player `+B8` and returns through the outer GENERATE tail.
Actual named timers read the current RAM manager/context. The visible walking
preview remains unchanged and is available for manual launch; original frame
scheduling and complete gameplay remain required.

| Check | Evidence |
| --- | --- |
| Final Release build/package | 97/97 CTest suites passed; no compiler warnings |
| Canonical model graph | 75 synthetic / 1,643 original-inclusive checks; real sequence/command/list owners, aliases, nullable names and physical backing |
| Actual sequence runner | 52 synthetic / 68 original-inclusive checks; publication order, retained data/local cursor, mutable lists, nested JUMP and real-return versus host-suspension distinction |
| Ordinary entry integration | 26 checks; fields-only entry, exact retained cleanup wrapper, paused ACTION/work replacement, both player callbacks after JUMP, current context reads and facade lifetime |
| Player sequence callback | 59 checks; real idle effect send, panel dictionary, fresh reconciliation work, status-icon removal, SLEEP/STUN and nested direct entry |
| Native ACTION adapter | 35 checks; real messenger rows, query priority, definition execution, retained broadcasts/time/services, freeze ordering and recursive-fault boundaries |
| Raw actual control | 53 checks; MOVE/MUTEKI timer and heading prefixes before current parameter reads, movement allocation, CTRL/PDMOVE field order and committed failures |
| Original player construction | 62 synthetic / 149 original-inclusive checks; eight real `+98` returns across four slots and two point groups, then four complete registered GENERATE returns with actual ACTION and 58 original exported event tables |
| Actual original-instruction comparison | 264 cases, zero differences: 144 entry/label prefixes and 120 mutable command-loop cases, freshly linked against the frozen production core |
| Existing original integration | 160 motion, 202 control, 554 control/timer, 1,324 prepared sequence, 620 player-start, 672 prepared spawn and 413 weapon-preview checks passed |
| Packaged application | 18 app / 10 play CLI checks and six Python verifier tests passed |
| Hidden walking replay | 240 frames, 438 simulation steps, 101.333 source-unit travel, 286 idle / 152 run steps, 241 pose uploads and 240 attached-weapon frames; pause/restart/focus-loss checks passed; final frame visually inspected |

All four original Agumon GENERATE calls return with canonical SQ_WAIT data
reference 1534, cursor 4, LABEL count 1, null wait name, actual WAITD1 action and
KEIKATIMER. This sequence contains no WCHECK: its next LABEL returns from the
command loop. Each player has exactly one untouched `i_pcnumic/SQ_OPENP{slot}`
effect with delay 480, offset `(0,10,0)` and both original flags enabled.
Each original physical first child has a null box; the saved and final root boxes
correctly remain null. Separate synthetic generation covers non-null box copy
and transform. The first original call also pauses for missing ACTION and then
resumes without repeating creation. Startup uses explicitly prepared original
name/default/map tables, font, clock and map-name services; the ACTION source is
explicitly prepared from original clips and definitions. This is not an
original boot-to-game or four-controller gameplay acceptance result.

The instruction comparison interprets E944 through its cleanup call and the
actual D084 label helper, with named lookup/string/reference services supplied.
The C3AC cases execute the original loop and represented CTRL/ACTION/WCHECK/JUMP
helpers with explicit ACTION and direct-JUMP service boundaries. They compare
captured work data, current lists/backing, cursor publication, identity reads and
retained completed prefixes. The 264 cases do not execute full cleanup, player
`+B8`, arbitrary recursive entry, HITBOX/Lua or the original frame scheduler.
Post-cleanup, player callback and raw actual control ordering have source-review
and native regression evidence; older prepared instruction suites are not
presented as new original-instruction coverage for those actual paths.

The final current-header comparison driver linked only against the frozen
production core, SHA-256
`1B56D7C1A104DBCFB8074CDA4B046ED9DD313181B59D8AC7E6AC761FD5AD2E46`.
The core hash was unchanged after replay. The packaged application SHA-256 is
`27F83D48C894D42E53FCA192EBAC2494A098A3789917E5C9D3CCA57A74E2F8B5`.

Hosted CI for the previous v0.40 commit `1342710` completed successfully in all
three jobs: Windows app, Ubuntu core and Windows core, run `34713726238`.
This is distinct from local v0.41 validation; v0.41 hosted CI follows publication.

## Previous v0.40 checkpoint

The following records describe the previous build and its own hashes. They do
not claim the complete v0.40 timer instruction suite was rerun against v0.41.

Version 0.40.0 was validated locally on 2026-09-12. Actual RAM named timers now
serve MOVE/MUTEKI, movement, per-axis turning and the recovered positive
invulnerability countdown. Control/sequence staging does not copy or publish
timer fields. Full direct player sequence entry, player `+B8`, actual context
resolution and the original frame loop remain required. The visible walking
preview remains unchanged and is available for manual launch.

| Check | Evidence |
| --- | --- |
| Release build/package | 92/92 CTest suites passed; no compiler warnings |
| Actual named timer endpoint | 47 checks; live RAM data, untyped presence, captured records, separate W1/W2, field-specific reads, committed failure prefixes and permanent fault state |
| Actual movement/turn consumers | 40 checks; per-axis clock reads, empty-list gates, live timer lookup and completed movement writes retained after later checked failures |
| Control/sequence timer integration | 26 synthetic / 554 original-inclusive checks; work-only staging, fresh ONE heading, retained work generations, countdown NaN/zero branches and four-slot/four-weapon-label original Agumon WAIT/WALK commands |
| Original timer instruction comparison | 12,576 cases / 1,313,041 original instructions / 13,422 named lookups / 8,505 clock reads / 4,389 checked boundaries, zero differences |
| Existing original integration | 160 model-motion, 202 control, 1,324 sequence, 129 construction, 620 player-start, 672 prepared spawn and 413 weapon-preview checks passed |
| Packaged application | 18 app / 10 play CLI checks and six Python verifier tests passed |
| Hidden walking replay | 240 frames, 438 simulation steps, 101.333 source-unit travel, 286 idle / 152 run steps, 241 pose uploads and 240 attached-weapon frames; pause/restart/focus-loss checks passed; final frame visually inspected |

The final timer driver was freshly compiled against current headers and linked
only against the frozen production core, SHA-256
`41D4BB01C42E7099715A81186C16331A0148C0ED49D2214842E275F50DF38B90`.
The core hash was unchanged after replay. The packaged application SHA-256 is
`CD9302084F913323528DF74E07EDFD723E737AFF2A86F8AF87D0E76697ED3D0D`.

The comparison executes original `8031E138`, `8031DE44`, `8031E36C`,
`8031E4C4` and named-lookup instructions with explicit context-clock,
allocation/name and string-service boundaries. It compares current data/list
identities, retained timer fields, pre-clock mutation prefixes and independent
outer/inner captures. It does not prove the guest allocator ABI, original list
capacity growth or a complete manager/frame. Unsafe consumed pointers and
nonfinite scales are compared at their reached checked boundaries; the native
endpoint deliberately rejects those cases. The original integration's ACTION
callback records actual requests, without claiming complete animation dispatch.

Hosted CI for the previous v0.39 commit `ca2884c` completed successfully in all
three jobs: Windows app, Ubuntu core and Windows core. This is distinct from
the local v0.40 validation above; v0.40 hosted CI follows publication.

## Previous v0.39 checkpoint

The following records describe the previous build and its own hashes. They do
not claim the complete v0.39 lookup comparison was rerun against v0.40.

Version 0.39.0 was validated locally on 2026-09-12. Canonical loaded RAM/ONE
owners now participate in actual player construction and retained MODELCTRL
dispatch. The normal GENERATE path reaches its explicit ordinary sequence-entry
boundary; complete sequence entry, player `+B8`, original frame scheduling and
gameplay remain unfinished. The visible walking demo is unchanged.

| Check | Evidence |
| --- | --- |
| Final Release build/package | 89/89 CTest suites passed; no compiler warnings |
| Canonical model graph | 61 synthetic / 75 original-inclusive checks; four actual Agumon RAMs, aliases, retained documents, null/name lookup, shared HIT boxes and RAM timers |
| Mutable scene nodes | 94 synthetic / 198 original-inclusive checks; shared matrices, nullable box/list fields, physical child backing, independent count/capacity, alias writes, growth and cycle/depth guards |
| Common scene traversal | 47 synthetic / 62 original-inclusive checks; live matrix reads and retained child backing/end through mutation and pauses |
| Player construction | 62 synthetic / 129 original-inclusive checks; eight real `+98` returns across four Agumon owners and two original point groups, actual GETPLAYER/GAMESTART/RECALC and number/meter startup, then registered GENERATE retained at SQ_WAIT entry |
| Base dispatch | 30 synthetic checks; separate base/status/player/outer captures, null work, live registration fields, manager backlinks, paused status, captured queue writes, recursive faults, count-zero first-child access and retained outer prefixes |
| Final box tail | Synthetic registered GENERATE completes with a valid direct GETHP override and preserves source/previous box owners; normal ordinary-sequence completion remains unimplemented |
| Original named-list lookup comparison | 10,000 cases / 264,516 original instructions / 4,321 explicit string comparisons; 5,604 checked null/addressability boundaries, zero differences |
| Existing original integration | 620 player-start, 672 prepared spawn and 413 weapon-preview checks passed |
| Packaged application | 18 app / 10 play CLI checks and six Python verifier tests passed |
| Hidden walking replay | 240 frames, 438 simulation steps, 101.333 source-unit travel, 286 idle / 152 run steps, 241 pose uploads and 240 attached-weapon frames; pause/restart/focus-loss checks passed; final frame visually inspected |

The final lookup driver was freshly compiled against current headers and linked
only against the final production core, SHA-256
`A5166A136162025DB0F1ED04464D4FE759E759FB07390B5D557790B55106F210`.
Its hash was unchanged after verification. The packaged application SHA-256 is
`E9A25D8F3114DD33810D950E325C506E0EE46845DBDD879B04BA2CC0DD6747F3`.

The lookup comparison executes the retained original instructions with explicit
memory/string-service boundaries. It does not execute the guest allocator or a
complete original manager/frame. Other construction ordering was checked against
the supplied DOL and owner-replacement regressions, rather than claimed as a new
whole-function CPU comparison. The earlier v0.38 Ubuntu build failure was traced
to a missing `<utility>` include in a test and corrected here; final hosted CI
results are reported separately from local Windows validation.

## Previous v0.38 checkpoint

The following records describe the previous build and its own hashes. They do
not claim those complete instruction suites were rerun against v0.39.

Version 0.38.0 was validated locally on 2026-09-12. One `ModelPlayerWork`
allocation now supplies the recovered 68 field locations to existing
motion/control, sequence/wait, script, status and subcommand consumers. The
checked MODELCTRL runtime consumes this work directly. Node matrices and RAM
timers remain separate owners. See [MODEL_PLAYER_WORK.md](MODEL_PLAYER_WORK.md).

| Check | Evidence |
| --- | --- |
| Final Release build/package | 86/86 CTest suites passed; no compiler warnings |
| Canonical factory/work | 46 checks covering the complete recovered default table, six distinct guest referenced allocations, prepared compatibility, live field aliases, detached publication, generation retention and no ownership cycle |
| Motion/control | 305 scalar motion checks; 138/160 model-motion and 148/202 control synthetic/original-inclusive checks; live list-slot replacement, raw bytes and independent node/timer owners |
| Sequence/wait | 440 synthetic / 1,324 original-inclusive checks; actual command-data provenance, stable wait address, signed cursor/count, one executor per generation, abandoned/copy-prefix rejection and pre-JUMP cleanup validation |
| Script/status | 93 script-context and 95 status checks; canonical HP/list replacement, sibling status anchor, const views and retained invocations |
| Event integration | 288 event-runtime and 115 script-work checks; actual factory registration, shared fields, rejected cleanup overlaps, post-ACTION JUMP recovery and live effect attachment |
| Subcommand/HITBOX | 157 subcommand and 65 HITBOX checks; live wrapper slot, retained pending pass, cleanup preflight and post-FIND_SPACE payload capture |
| Original scalar motion comparison | 18,000 setups / 54,000 ticks, zero differences; NaN classification compared, payloads excluded |
| Original control/PDMOVE comparison | 10,000 control cases including raw 0/1/2/255 flags; 1,800 PDMOVE cases (1,445 original comparisons, 355 checked rejections), 100,648 instructions and 280 supplied backing-growth services; zero differences |
| Original labels/sequences | 3,000 label programs / 24,539 update-lookup pairs and 6,000 command programs; zero differences |
| Original status comparison | 594 cases / 90,899 instructions / 312 queries, both normal and paused native delivery; zero differences |
| Original queued lifecycle comparison | 1,005 sound/effect cases / 2,018,236 instructions and 2,106 HITBOX cases / 1,690,336 instructions; zero differences |
| Original dynamic effect attachment | 120 cases / 46,525 instructions; earlier PLAY/FIND_SPACE mutations, null/empty names, signed RAM extremes and retained paused GENERATE payload; zero differences |
| Existing original integration | 672 prepared spawn, 613 player-start and 413 weapon-preview checks passed |
| Packaged application | 18 app / 10 play CLI checks and six Python verifier tests passed |
| Hidden walking replay | 240 frames, 438 simulation steps, 101.333 source-unit travel, 286 idle / 152 run steps, 241 pose uploads and 240 attached-weapon frames; pause/restart/focus-loss checks passed; final frame visually inspected |

Final comparison drivers were freshly compiled against current headers and
linked only against the frozen production core, SHA-256
`2FC2FDE64888D010A6124F4C2E697785BE4F76B7E87531E5A6787F54A62A2A8E`.
The packaged application SHA-256 is
`B1DFBCDB7EBF65D440553CA95C1412839FD6C601D8DF7437696F0823F45210CF`.
The recovered factory/default table was independently checked against the
supplied DOL and its metadata. This is not execution of the guest allocator
or runtime type registry. All instruction comparisons retain explicit prepared
objects, string/list/factory, messenger, ACTION and external-effect services.
They do not execute complete original player creation, sound, collision or
gameplay. Full mutable RAM/ONE routing and the retained GENERATE continuation
remain required.

Review found and corrected three integration hazards: a second or copied
sequence executor could bypass a pending prefix; rejected wrapper cleanup
could leave half-published selection state; and effect attachment was read
before the original post-FIND_SPACE phase. Regression tests cover these cases,
including a slot change by an earlier ACTION in the same sequence. Initial
full-build failures were missed pointer-style test accessors and one synthetic
fixture requesting an absent sequence. The corrected final full suite passes.

No interactive preview was automatically launched. The packaged preview is
available for manual launch; the isolated Preview-0.36.1 is preserved.
The preceding commit `2601e6ce35f076a94b241491f050f14d71c5cb72` passed all three
GitHub jobs in run `34690070862`; that result belongs to the preceding commit.
Original assets, instruction dumps, drivers, logs and captures remain ignored.

## Retained work lists (0.37)

Version 0.37.0 was validated locally on 2026-09-12. Existing motion, PDMOVE,
subcommand and label consumers now use actual shared list allocations and
ordered entry owners. Detached arithmetic/selection candidates publish through
those same live allocations. No separate duplicate cache represents the lists.

| Check | Evidence |
| --- | --- |
| Final Release build/package | 85/85 CTest suites passed; no compiler warnings |
| Movement ownership | 305 synthetic checks; allocated-empty list, first MOVE allocation, shared entries, detached candidates, publication, malformed profiles and matrix integration |
| PDMOVE/control | 128 synthetic / 182 original-inclusive checks; retained backing after count reset, independent guarded stores, backing reads without count, malformed owners and moved-from recovery |
| Sequence ownership | 277 synthetic / 1,161 original-inclusive checks; three nullable label lists, first-key replacement, live offset cache, detached snapshots, phased selection and supplied matrix/PDMOVE/movement owners |
| Event integration | 278 synthetic checks; paused cleanup observes the retained model and subcommand lists, then stops actual movement entries after completion |
| Subcommand/HITBOX | 95 subcommand and 65 HITBOX checks; actual wrapper/list/entry lifetimes, shared continuation state, ordered cleanup and retained removed entries |
| Original scalar motion comparison | 18,000 setups / 54,000 ticks, zero differences; NaN classification compared, payloads excluded |
| Original PDMOVE comparison | 1,800 cases: 1,445 original comparisons and 355 separately asserted checked rejections; 100,648 instructions and 280 explicit backing-growth services, zero differences |
| Original control comparison | 10,000 prepared cases, zero differences |
| Original labels/sequences | 3,000 label programs / 24,539 update-lookup pairs and 6,000 command programs, zero differences |
| Original queued lifecycle comparisons | 1,005 sound/effect cases / 2,018,236 instructions and 2,106 HITBOX cases / 1,690,336 instructions, zero differences |
| Existing original integration | 146 model-motion, 672 prepared spawn, 613 prepared startup and 413 weapon-preview checks passed |
| Packaged application | 18 app / 10 play CLI checks and six Python verifier tests passed |
| Hidden walking replay | 240 frames, 438 simulation steps, 101.333 source-unit travel, 286 idle / 152 run steps, 241 pose uploads and 240 attached-weapon frames; pause/restart/focus-loss checks passed; actual frame visually inspected |

All comparison drivers were freshly compiled against current headers and linked
only against the final production core, SHA-256
`C081B014CCB47381C4415F925AC6D23E3FF4A03EE978EBBB8A94DCF6C7B7B3D6`.
The hash remained unchanged during their runs. These comparisons retain explicit
prepared list, interned-string, factory, messenger, ACTION and external-effect
service boundaries. They do not execute complete original allocation, audio,
collision, player creation or the whole game. The label comparison covers the
original handler on prepared lists; null-to-allocated native ownership is
separately checked against the recovered allocation order and synthetic tests.

The first integrated test run exposed a moved-from PDMOVE facade being mistaken
for an explicit null original list. A host transfer marker now permits restoring
that detached facade without fabricating a missing original reference. The
regression and final full suite pass. Independent reviews found no blocking
issues in label-list selection or PDMOVE publication.

No interactive preview was automatically launched. The existing isolated
Preview-0.36.1 remains available for the owner to start manually. Full canonical
68-field player work, RAM/ONE registration and the retained GENERATE continuation
remain the next requirements.

The preceding source commit `353955a70d5497bd0fdcf9580b72a52719e5f4c6`
passed all three GitHub jobs in run `34688760568`; that CI result belongs to the
preceding commit, not this release. Original assets, instruction dumps, drivers,
logs and captures remain ignored local files.

## Record-query gates (0.36.1)

Version 0.36.1 corrects the original GETRECNO activity and captured-work gates.
The original branch at `8030E544..E564` leaves the packet unchanged when the RAM
is inactive or the captured work is null; otherwise it appends the signed record.
The preceding code and an older test incorrectly allowed inactive reads.
Missing active native work still requires its real owner, rather than becoming
an invented null-work result. The prepared status facade is unnecessary for
this known status decline; complete mutable RAM/ONE construction must still
perform the original separate status lookup.

- Fresh Release build/package: 85/85 CTest suites passed without compiler warnings.
- Script/controller integration: 96 checks, including raw nonzero activity,
  null/expired work, packet preservation and an actual overlapping collision
  whose inactive source must not invoke the target hit script.
- Fresh production-core GETRECNO comparison: 1,968 rows, comprising 1,608
  comparisons against 45,888 original instructions and 360 separate native
  missing-owner checks; zero differences. The original comparisons include
  480 appended records and 1,128 unchanged packets, signed extremes and exact
  preservation of typed packet contents. This scope uses the original player's
  known status-decline result, not a fabricated generic status override.
- Original spawn, prepared startup and weapon regression: 672, 613 and 413
  checks passed respectively. The packaged 240-frame walking replay also passed.

A separate read-only 960-case capture audit established the surrounding
pre-status work capture and the exact low-byte-equals-one return gate. That
broader reference audit does not prove complete native mutable-work dispatch;
the full lifecycle remains the next implementation requirement.

## Spawn and node ownership (0.36.0)

Version 0.36.0 was validated locally on 2026-09-12 with MSVC 19.51. Original
PLAYERPOINT messages now feed the recovered placement helper in a prepared
integration with all four original Agumon node matrices. The node allocation
has an independent lifetime; motion candidates remain detached until commit.
This does not establish full fresh-work construction, GENERATE or multiplayer.

| Check | Evidence |
| --- | --- |
| Release build and package | 85/85 CTest suites passed; no compiler warnings |
| PLAYERPOINT | 141 synthetic / 179 original-inclusive checks; ordered registration, physical aliases, live saved group, point matrices, null-group identity and retained polygon service |
| Original point handler comparison | 980 cases / 58,760 original instructions; zero differences; matrix append/copy are explicit prepared helpers with original signed-subnormal semantics |
| Model placement | 42 synthetic checks; vector/matrix precedence, exact two-vector rotation, bottom-row preservation, source/target node reloads, retained setters, reentrancy and partial failures |
| Original placement comparison | 3,638 cases per run, including 144 checked rejections / 518,913 instructions / 8,668 explicit host trig calls; zero differences in normal and paused publication |
| Prepared spawn integration | 233 synthetic / 672 original-inclusive checks; two original point groups and positive fallback for all four Agumon nodes, original initial matrices, retained packet and setter, independent work fields sharing actual node storage |
| Motion ownership and regression | 124 synthetic / 146 original-inclusive checks; shared-node construction, detached candidates, stable publication, moved-state checks, atomic failure, original turning and local movement |
| Script/controller ownership | 78 synthetic checks; shared matrix survives the registration move chain, state publication and controller destruction; existing HP/status and paused callback checks remain intact |
| Existing original control/sequence regression | 140 control and 1,122 sequence checks passed against local original resources |
| Existing original startup/weapon regression | 613 prepared startup and 413 weapon-preview checks passed |
| Packaged walking preview | 240 frames with 8x MSAA; 438 steps, 101.333 source units, 286 idle / 152 run steps, 240 attached-weapon frames; pause/restart/focus checks passed; actual frame visually inspected |
| Python and packaged CLI | Six verifier tests; 18 app and ten play argument checks passed |

The placement comparison executes the represented original branch, argument
readers, Euler arithmetic, paired-single matrix copies and concrete node2
setter. Host double sine/cosine remain explicit shared service boundaries;
non-finite copied inputs use checked native rejections. It does not execute the
earlier RAM/ONE lookup, factory, original MSL or complete game loop. Both normal
and paused placement drivers were freshly linked against the final core only.

The original factory/default investigation also recovered all 68 inherited and
player fields, real auto-created list owners and the later GENERATE tail. That
evidence guides the next implementation; it does not imply those remaining
owners and callbacks are already integrated. See
[PLAYER_CONSTRUCTION.md](PLAYER_CONSTRUCTION.md).

The preceding commit `1c54709cc576497d9f9daf480a931b0bbf6694af` passed all three
GitHub jobs in run `34686844672`. Its Windows artifact `10295703788` has digest
`4dfed35bae75775c0fd8df213e00934a7fda797e9069788300d1135b07bf6630`.
That CI result belongs to the preceding commit. Original assets, executable
probes, complete logs and screenshots remain ignored local files.

See [PLAYER_POINT.md](PLAYER_POINT.md), [MODEL_PLACEMENT.md](MODEL_PLACEMENT.md),
[PLAYER_SPAWN.md](PLAYER_SPAWN.md) and [MODEL_MOTION.md](MODEL_MOTION.md).

## Native original player-start validation (0.35)

Version 0.35.0 was validated locally on 2026-09-12 with MSVC 19.51. The prepared
post-GETPLAYER caller now reaches its real number/meter/status recipients and
returns through the original map tail. Four original Agumon slots share the
actual HP wrappers with their meters while retaining separate work, number UVs
and deferred weapon requests. This does not establish complete player creation
or connected multiplayer.

| Check | Evidence |
| --- | --- |
| Release build and package | 82/82 CTest suites passed; no compiler warnings |
| Prepared startup caller | 126 synthetic checks; ordered paused sends, live RAM/scalars, old-row/map/ONE retention, numeric and malformed-DOL boundaries |
| Original four-player integration | 95 synthetic prefix / 613 original-inclusive checks; actual GAMESTART, twelve number slots, eight meter values, four shared HP bindings, metal/main-lobby branches, four deferred weapon generations and eight replacements |
| MODELCTRL work/status integration | 71 synthetic checks; GETHP append order and exact wrappers, null/expired owners, original virtual ordering, nested same-model SETMETAL, packet restoration and abandoned-continuation lifetime |
| NUMBERCTRL | 116 synthetic / 1,472 original-inclusive checks; all seven original infos and 43 RAM occurrences, actual digit and shared UV mutations |
| METERCTRL | 46 synthetic / 73 original-inclusive checks; eight commands, exact HP wrappers, partial-result faults, smoothing and actual node sample-list updates |
| Status handler | 77 synthetic checks; classification, ordered list initialization, SETMETAL slot writes, live nested GETPLAYER and nullable DOWNDATA payload |
| Original caller instructions | 469 cases / 128,563 instructions / 2,389 packets / 24 checked boundaries; zero differences, also with each native recipient paused after its effects |
| Original HP/MP arithmetic | 2,290 cases / 30,838 instructions; zero differences, including classified exceptional division and nonfinite results |
| Original NUMBERCTRL instructions | 1,457 cases / 266,733 instructions; zero differences in work, digit/UV words and publication order |
| Original meter instructions | 2,326 cases / 797,659 instructions, including 256 original-owner cases; zero differences; 41 checked numeric boundaries |
| Original status instructions | 594 cases / 90,899 instructions / 312 GETPLAYER requests; zero differences, also with paused native recipients |
| Existing ACTION and weapon regression | 6,034,137 original ACTION checks and 413 original-inclusive weapon-preview checks passed |
| Packaged walking preview | 240 frames, 8x MSAA; 438 steps, 101.333 source units, 286 idle / 152 run steps, 240 attached-weapon frames; pause/restart/focus checks passed; actual frame visually inspected |
| Python and CLI | Six verifier tests; 18 app, ten play and nine boot argument checks passed |

The instruction comparisons execute bounded original routines with documented
prepared services and compare them to the production core. Each component's
document identifies those service boundaries. A caller packet comparison is
not a recipient implementation test; the separate number/meter/status oracles
provide that additional evidence for their represented branches. These checks
do not establish whole-game behavior or physical-console frame parity.

The earlier player construction, base GENERATE continuation after the player
virtual returns, original outer scheduling, HUD drawing, input/locomotion,
camera, combat, progression, audio, saves and connected local multiplayer remain
required. The walking preview retains its existing host movement/camera profile.

The previous version 0.34 commit `dafa903c21dcf94ab069b5b43f4de0d8722e364a`
passed all three GitHub jobs in run `34684932520`. Windows artifact `10295611121`
has digest `b62093cd0c00be61d3330e23fb940c7976d41fa48698e4994ba2ec41e4d3442d`.
That is evidence for the previous commit; current local verification is listed
above. Original data, executable probes and captures remain ignored under `work/`.

See [MODEL_PLAYER_STARTUP.md](MODEL_PLAYER_STARTUP.md),
[NUMBER_CONTROL.md](NUMBER_CONTROL.md), [METER_CONTROL.md](METER_CONTROL.md)
and [MODEL_STATUS.md](MODEL_STATUS.md).

## Previous version 0.34 validation

Version 0.34.0 was validated locally on 2026-09-12 with MSVC 19.51 and an
NVIDIA GeForce RTX 5070 Ti. The same prepared ACTION runtime now supplies body
animation, weapon queries and captured draw matrices. Common child traversal
feeds a shared native depth/MSAA pass. The refreshed walking preview includes
the original common05 weapon and its slash stand/run actions.

| Check | Evidence |
| --- | --- |
| Release build | 78/78 CTest suites passed; no compiler warnings |
| Common scene traversal | 41 synthetic / 56 original-inclusive checks; exact paths, retained services and callbacks, scope restoration, original flags, left transforms and temporary root observers |
| Original graphics instructions | Fresh final-core comparison: 459 cases / 61,389 original scalar instructions / 306 submitted matrices and 4,896 exact words; 135 scoped-attribute cases and 72 explicit matrix-list service boundaries; zero differences |
| Prepared weapon draw ownership | 12 synthetic / 413 original-inclusive checks; three weapons across all four RAMs, actual appearance selector identity, separate slot caches, forward ACTION transitions, placement, same-time recapture and stable body/weapon owners |
| Original prepared player starts | 577 checks; four deferred generations, eight replacements, twelve appearance branches and 24 selector visits |
| Original ACTION regression | 6,034,137 checks, including four-instance sequence execution and skin poses |
| GPU renderer | Shared prepared depth/order/projection, raw pose updates, neutral textured materials, invalid-pass preservation, MSAA, sampler ownership, normalization export and the existing occluded-body visibility aid passed |
| Gameplay preview | Packaged 240-frame input replay with 8x MSAA; 438 simulation steps, 101.333 source units travelled, 286 idle / 152 run steps, 240 body/weapon frames; pause, restart and focus-loss checks all passed |
| Two-hand GPU inspection | Original common04, RAM 3, actor 1 and WAITG1 rendered 120 frames with three batches, two shared images and 8x MSAA |
| Python and CLI | Six verifier tests; 18 app, ten play and nine boot argument tests passed |

Both gameplay and two-hand captures were visually inspected. The gameplay
preview uses actual original assets and prepared runtime work with host movement,
collision response and camera. It does not establish original combat, full
player construction, connected multiplayer, progression, audio or saves.
The reset graphics baseline and neutral material profile are explicit; complete
original GX materials and recorded original-frame pixel parity remain pending.

The prior version 0.33 commit `9e59bef1ae02b19c5935d7b9d8f2b0a110eec5c0`
completed all three GitHub build jobs in run `34453525005`. Its Windows artifact
`10143040844` has digest
`c74fe1446c62c28a41b3a49ce531c8b29ba5ec11bc4855d61c4849ee989e152a`.
That CI evidence applies to the previous commit; the current local checks are
listed above. Original resources, instruction probes and screenshots remain
ignored under `work/`.

See [WEAPON_PREVIEW.md](WEAPON_PREVIEW.md), [SCENE_TRAVERSAL.md](SCENE_TRAVERSAL.md),
[PREPARED_RENDERING.md](PREPARED_RENDERING.md) and [PLAY_PREVIEW.md](PLAY_PREVIEW.md).

## Previous version 0.33 validation

Version 0.33.0 was validated locally on 2026-09-10 with MSVC 19.51. Prepared
weapon GENERATE/REMOVE now uses actual loaded node, appearance-list and skeleton
owners through PLAYER, ACTION and MODELCTRL messages. Four original Agumon
player-start prefixes deliver their deferred common05 requests, replace weapons
across twelve appearance branches and exercise the common bone-selector visitor.
Generic child rendering, later player construction and complete gameplay remain
required.

| Check | Evidence |
| --- | --- |
| Release build and package | 76/76 CTest suites passed; local MSVC build without compiler warnings |
| Loaded weapon resources | 61 synthetic / 150 original-inclusive checks; three infos, twelve RAM occurrences, five node/bone pairs and 63 sequence rows |
| Prepared weapon runtime | 105 synthetic checks; canonical RAM aliases, manager-wide tracking, old list retention, nullable labels, live ordinals, nested generation, interrupted sends and faults |
| Loaded scene owners | 43 synthetic / 63 original-inclusive checks; exact appearance order, canonical aliases, lifetime, cached DAG depth and aggregate storage bounds |
| ACTION scene queries | 75 synthetic / 91 original-inclusive checks; actual typed owners, active gate, missing-owner resume, bound delayed preparation and runtime replacement rejection |
| PLAYER and MODELCTRL queries | 54 synthetic / 70 original-inclusive checks; raw live activity, shared parent names, nullable labels, valid nesting and conflicting-work rejection |
| Original connected starts | 95 synthetic / 577 original-inclusive checks; four deferred generations, eight two-hand replacements, twelve appearance branches, 24 common selector visits and cross-info removal |
| Common bone-selector native tests | 123 synthetic checks; matrix source capture, pool/stack order, retained child services, nested continuations and reset/fault ownership |
| Original common-selector instructions | 144 cases / 8,480 scalar plus 3,584 matrix instructions / 1,024 computed matrix words; zero differences against the final core |
| Original animation regression | 1,122 model-sequence checks; 6,034,137 ACTION checks including four-instance animation execution |
| Original data/script regression | Game data 8,269; GAMERAM 421; skills 499; database 275; database runtime 182; GAMESTART 217; items 157; stats 143; names 461; RECALCPLAYER 283; scripts 104; registry 51; HIT bodies 109; collision 80; events 108; waits 107 |
| Fresh final-core instruction regressions | 39 magic gates, 43 wrappers plus four checked prefixes, 101 stack cases, 84 ordinary deliveries and 61 deferred cases; 5,580,272 original instructions, zero differences; twelve retained-packet guard checks |
| Python and packaged app | 6 verifier tests; 16 app, 10 walking and 9 boot argument tests; GPU renderer suite; actor and walking previews each rendered 120 frames with visually inspected captures |

The original-data integration prepares actual MODELCTRL/PLAYER/ACTION owners;
it does not execute the complete player constructor. The first deferred request
pauses until its actual ACTION scene is prepared, then all four requests finish
through the real weapon manager. Common05, common04 and common03 replacements
retain their original node graphs and sequence overrides. The common visitor
receives each actor's chosen bone array and reaches an inspection child callback;
that callback is not evidence of completed original geometry rendering.

The independent selector comparison executes bounded original callback and
matrix instructions with explicit pool/attribute/child services. It compares
return values, pool cursor, callback count, stack depth and matrix words. The
generic node dispatcher and compiled-transform path remain separate. The full
weapon handler was reviewed against original instructions, but was not subjected
to an independent end-to-end instruction oracle.

Original bytes, probes, logs and captures remain ignored under `work/`. See
[WEAPON_RUNTIME.md](WEAPON_RUNTIME.md), [ACTOR_SCENE.md](ACTOR_SCENE.md),
[BONE_SELECT.md](BONE_SELECT.md) and [MODEL_PLAYER_START.md](MODEL_PLAYER_START.md).

## Previous version 0.32 validation

Version 0.32.0 was validated locally on 2026-09-10 with MSVC 19.51. Original
deferred message packets now retain their owners, signed deadlines, pause shifts
and explicit update continuation. The prepared post-GETPLAYER caller connects
HP binding, exact modifier resets, GAMESTART, BASE_ISUSE, a deferred PLAYERARMS
request and immediate RECALCPLAYER. Four original Agumon slots reach the explicit
later-UI boundary. Actual weapon construction and complete gameplay remain open.

| Check | Evidence |
| --- | --- |
| Release build and package | 71/71 CTest suites passed; local MSVC build without compiler warnings |
| Deferred kernel | 87 synthetic checks: packet identity, typed payloads, repeated preparation, signed/wrapped time, FIFO, growth, callback budget, pause, suspended writes, stable fault progress and cleanup/destructor reentry |
| Deferred runtime | 151 synthetic checks: four actual provider identities/indices, missing third provider, nested retained sends, same-ID foreign handles, immediate ownership reservation, frozen clock/count and no replay |
| Original deferred instructions | 61 cases / 1,388 operations / 2,110 exact state and callback rows / 729,885 original instructions; zero differences against the final core |
| Additional instruction evidence | 521 reference-only preparation/pause cases / 50,410 instructions, including all initialized and active byte values; 12 native borrowed-packet guard checks |
| Prepared MODELCTRL caller | 95 synthetic / 127 original-inclusive checks; old rows, exact reset prefixes, live routing/name/clock reads, four original player slots and four queued common05 requests |
| RECALCPLAYER | 259 synthetic / 283 original-inclusive checks; all four RAMs, missing/default/raw modifier bytes, nested row reset, retained stat prefixes, final packet and fault handling |
| Original game/data regression | Game data 8,269; GAMERAM 421; skill selection 499; database 275; database runtime 182; GAMESTART 217; items 157; stats 143; name conversion 461 |
| Original action/script regression | ACTION 6,034,137 checks; target scripts 104; registry 51; HIT bodies 109; collision 80; player events 108; waits 107 |
| Refreshed instruction regressions | 39 magic gates / 2,952 instructions; 43 wrappers plus 4 checked prefixes / 225,904 instructions; 101 stack cases / 7,525 rows / 2,232,011 instructions; 84 ordinary-delivery cases / 2,028 rows / 2,389,520 instructions; zero differences |
| Python and packaged app | 6 verifier tests; 16 app, 10 walking and 9 boot argument checks; actor and walking previews each rendered 120 frames and their captures were visually inspected |

The deferred comparison executes bounded original prepare, schedule, update,
pause, registration, selection, send and buffer-stack instructions. Allocation,
typed-list/string operations, controller hooks and initial pool preparation are
explicit services. It compares all eight represented typed lists, packet identity,
retained object counts, active record order, deadlines and callback traces. It
does not execute the complete original constructor or outer frame loop.

The four-player caller integration consumes actual GETPLAYER rows and original
database exports, DOL names and a prepared font converter. The queued common05
requests use distinct original PLAYERARMS RAM indices. At the exact deadline,
delivery stops at the real missing weapon-manager service. No weapon attachment,
full UI/constructor completion or connected multiplayer success is fabricated.
This caller integration is separate from an independent instruction execution
of the full MODELCTRL constructor, which has not been performed.

Original bytes, probes, logs and captures remain ignored under `work/`. See
[DEFERRED_MESSENGER.md](DEFERRED_MESSENGER.md),
[MODEL_PLAYER_START.md](MODEL_PLAYER_START.md) and [GAME_RAM.md](GAME_RAM.md).

## Previous version 0.31 validation

Version 0.31.0 was validated locally on 2026-09-10 with MSVC 19.51. Native
GAMESTART now connects the original species/base/equipment/stat/common/name
sequence through retained shared database delivery. All four original Agumon
data initializations complete with explicitly prepared database/font services.
Full player construction, other item providers, skill growth and gameplay remain
required.

| Check | Evidence |
| --- | --- |
| Release build and package | 67/67 CTest suites passed without compiler warnings; 6 Python verifier tests passed |
| GAMESTART integration | 183 synthetic / 217 original-inclusive checks; packet precedence, sixteen-row lookup, base-array aliases, nested resets, retained name routes and four actual player starts |
| Initial weapons and queries | 121 synthetic / 157 original-inclusive checks; fresh queries, post-callback records, distinct folder items, shared sockets and append/name/equipped publication order |
| Status and techniques | 127 synthetic / 143 original-inclusive checks; signed wrapping, health refill gates, retained modifiers, six technique groups, experience and full represented equipment tail |
| Name conversion | 35 synthetic / 461 original-inclusive checks; actual 424-pair table, two-pass conversion, shared scratch and distinct nine-unit player name |
| Original equipment/item instructions | 54 cases / 12,228 instructions, zero differences; explicit prepared typed lookup, allocation, list, string and database hooks |
| Original numeric stat instructions | 884 cases / 333,378 instructions, zero differences; includes 56 checked stops above the bounded level table |
| Original outer stat routine | 21 cases / 434,115 instructions / 516 item queries; 31 published fields, ordered techniques, names and query counts match |
| Original name instructions | 516 cases / 2,519,526 instructions, zero differences; all original mapping inputs and four actual default display-name strings included |
| Original selection/base instructions | 311 cases / 183,706 instructions, zero differences; 192 base-array cases, 79 species cases and 40 packet-cardinality cases |
| Existing game/script integration | Game data 8,269 checks, GAMERAM 421, skill selection 499, databases 275, DB runtime 182, target scripts 104, registry 51, HIT bodies 109, collision 80, events 108 and waits 107 |
| Original ACTION regression | 6,034,137 checks across 183 actions, 369 commands, 4,157 poses and 240 four-instance sequence poses |
| Original messenger/wrapper regression | 101 stack cases / 7,525 rows, 84 delivery cases / 2,028 rows, 43 wrappers plus 4 checked prefixes and 39 magic gates; zero differences against the refreshed core |
| Packaged app regression | 16 app, 10 walking and 9 boot argument tests; actor and walking previews each render 120 frames, with both captures visually inspected |

The original-data GAMESTART cases register all twelve DB infos from the actual
selected source, then initialize four separate player rows. Each obtains weapon
records 266/275/274, the actual initial technique and a name converted using the
supplied executable's table. P2ESKILLUP then uses the populated Waza and reaches
its remaining probability/growth boundary. The separate existing Hit tests retain
their fresh factory-row setup; no skill increment or combat success is invented.

Instruction comparisons execute bounded original routines with explicitly
prepared context, allocation, typed-list, string, database and item services.
The stat comparison executes the represented outer routine; font construction,
whole MODELCTRL creation, original scheduling and a physical-console playthrough
are not established. Original bytes, probes, traces and captures remain ignored
under `work/`. See [GAME_START.md](GAME_START.md), [GAME_ITEMS.md](GAME_ITEMS.md),
[GAME_STATS.md](GAME_STATS.md) and [GAME_NAME.md](GAME_NAME.md).

## Previous version 0.30 validation

Version 0.30.0 was validated locally on 2026-09-10 with MSVC 19.51. Original
database graphs now retain typed ownership through actual DATABASE info/RAM
registration. GAMERAM selects a weapon or technique skill before its original
ceiling comparison, retaining pcTech delivery through nested callbacks. Full
selected-player initialization, growth and gameplay remain required.

| Check | Evidence |
| --- | --- |
| Release build and package | 63/63 CTest suites passed without compiler warnings; 6 Python verifier tests passed |
| Database decoding | 157 synthetic / 275 original-inclusive checks; six groups, signed widths, first names, physical aliases, row bounds and actual pcTech/shinka data |
| DATABASE runtime | 126 synthetic / 182 original-inclusive checks; all 12 original DB infos/RAMs, final alias parents, null-to-empty registration names, captured fallback, raw activity and atomic failure |
| GAMERAM skill phase | 467 synthetic / 499 original-inclusive checks; signed ceilings, four source rows, first Waza record, actual pcTech SKILL lookup, retained cache/query and nested player reset |
| Shared ownership regression | 4,151 identity/provider checks; 8,181 synthetic / 8,269 original-inclusive game-data checks; unnamed Waza items remain distinct from disks and named wrappers |
| Original database helpers | 107 native comparisons, including 49 checked host stops, plus 15 guest-only raw-index cases; 8,529 original instructions, zero differences |
| Original database registration/delivery | 48 cases, 787,966 original instructions; final alias parents, ordinal rows, null/empty names, gates and captured GETDATABASE result identity match |
| Original skill selector/ceiling | 63 cases, including 3 explicit missing-Waza boundaries, match across 6,406 original instructions |
| Existing game/script integration | GAMERAM 421 checks, target scripts 104, registry 51, HIT bodies 109, collision 80, player events 108 and player waits 107 |
| Original ACTION regression | 6,034,137 checks across 183 actions, 369 commands, 4,157 poses and 240 four-instance sequence poses |
| Original messenger/wrapper regression | 101 stack cases / 7,525 rows, 84 delivery cases / 2,028 rows, 43 wrapper cases plus 4 checked prefixes and 39 magic-gate cases; zero differences |
| Packaged app regression | App, walking and boot argument checks passed; actor and walking previews each rendered 120 frames and their captures were visually inspected |

The original-data skill cases explicitly prepare Waza items and skill values
to exercise recovered helpers against pcTech reference 1592. They do not
execute GAMESTART or its equipment/stat calculation. The unchanged original
Hit integration still reaches PC 63 after 48 instructions and five logical
calls; its fresh generated WAZALIST is empty, so technique selection remains
an explicit prerequisite. No starting HP or growth effect is fabricated.

Instruction probes execute bounded original routines with explicit prepared
allocation, list, type, context and recipient services. Checked schema/index
stops and missing native services are reported separately from original
unchecked memory behavior. No complete console execution, player constructor
or playable combat claim follows from these comparisons. Original bytes,
probes, traces and screenshots remain ignored under `work/`.

See [DATABASE.md](DATABASE.md), [DATABASE_RUNTIME.md](DATABASE_RUNTIME.md) and
[GAME_RAM.md](GAME_RAM.md) for implemented behavior and remaining prerequisites.

## Previous version 0.29 validation

Version 0.29.0 was validated locally on 2026-09-10 with MSVC 19.51. Generated
GAMERAM player rows now retain their shared HP objects through actual GETPLAYER
delivery and prepared model binding. Original Hit scripts can advance through
the recovered magic return gates into the actual skill-growth service boundary.
Selected-player initialization, damage/status effects and gameplay remain required.

| Check | Evidence |
| --- | --- |
| Release build and package | 60/60 CTest suites passed without compiler warnings; 6 Python verifier tests passed |
| Native identities/providers | 4,143 checks cover separate native/serialized domains, retained callbacks, aggregate owner bounds, atomic registration failure, typed data-list lifetime and integer-only buffer reset |
| Generated game data | 8,178 synthetic / 8,266 original-inclusive checks; all 77 schema records, eight kinds, four rows, 48 distinct disks, exact defaults and retained row/wrapper identities |
| Model data binding | 59 checks cover shared HP/MHP, exact-zero HP and MP refill, negative values, captured pre-reset rows, typed failures and live script HP merge |
| Lua messenger wrappers | 156 checks cover actual packet order, current-context fields, signed results, Number-to-byte conversion, captured send ownership, nested pauses and terminal missing-result handling |
| GAMERAM integration | 188 synthetic / 421 original-inclusive checks; exact generated registration, GETPLAYER variants, resets, retained owners, signed magic gates and eight original Hit scenarios |
| Original wrapper comparison | 43 scenarios plus four separately labeled checked service prefixes match 47 native rows across 225,904 guest instructions |
| Original magic gate comparison | 39 cases match across 2,952 kernel instructions, covering lookup-before-unequal, signed remainder branches and integer-only return clearing |
| Messenger regression comparisons | 101 stack scenarios / 7,525 exact rows / 2,232,011 instructions and 84 delivery scenarios / 2,028 rows / 2,389,520 instructions; zero mismatches against the refreshed core |
| Original script/collision/event regressions | Target scripts 104 checks; registry 51; HIT bodies 109; collision integration 80; player events 108; player waits 107 |
| Original ACTION regression | 6,034,137 checks across 183 actions, 369 commands, 4,157 poses and 240 four-instance sequence poses |
| Packaged application | 16 app, 10 walking and 9 boot CLI checks passed; actor inspection renders 120 frames / 121 uploads; walking renders 120 frames, 198 steps, 101.333 travel and 102 uploads |
| Visual inspection | Version 0.29 actor and walking captures inspected; pause/restart/focus replay and the walking 1236x600 render target remain intact |

The unchanged entry179 `hit_p_agumon.lua` now reaches BLT at PC 63 after 48
instructions and five logical calls when supplied with the owned GAMERAM and
bridge. Eight cases exercise every actual Agumon RAM with equal and unequal
source RAMs. The pending packet is `P2ESKILLUP`; retries preserve the completed
BLP call and damage-turn clear. Initial generated HP remains zero throughout.
This does not establish healing, skill growth or a playable character.

Instruction comparisons execute bounded original kernels with explicit prepared
list/allocation/controller hooks. Missing native services are checked host stops,
not claims of original asynchronous behavior. The empty BLP result fault stops
before an original unchecked read. Schema tests compare actual records/defaults;
they do not execute the entire original constructor. All original bytes, probes,
traces and captures remain ignored under `work/`.

See [GAME_DATA.md](GAME_DATA.md), [GAME_RAM.md](GAME_RAM.md) and
[MODEL_SCRIPT_MESSENGER.md](MODEL_SCRIPT_MESSENGER.md).

## Prepared target Lua and HP context validation

Version 0.28.0 was validated locally on 2026-09-10 with MSVC 19.51. Selected
original target-Hit chunks now execute with shared model/HP context and retained
Lua continuations. Actual damage/status services, player construction and the
original game loop remain required before playable combat.

| Check | Evidence |
| --- | --- |
| Release build and package | 55/55 CTest suites passed without compiler warnings; 6 Python verifier tests passed |
| Lua interpreter | 416 synthetic / 426 original-inclusive checks; source-linked MSVC AddressSanitizer also passes; retained CALL/TAILCALL, captured values, nested/sibling frames, shared budgets, abandonment and uncertain effects covered |
| Original func/boot comparison | Strict and retained engines match official Lua 4.0.1 in all 3 explicit profiles: 188 ordered host calls and 3 final resource-queue rows; retained execution pauses once before each effect, with zero differences |
| Shared target context | 81 checks cover qualified work, distinct default HP wrappers, direct signed record, live matrix alias, conditional current-wrapper HP merge, nested overwrite and strict continuation ownership |
| Ordered script registry | 44 synthetic / 51 original-inclusive checks; exact export/registration occurrences, first-name barriers, owned original Hit bytes and aggregate bounds |
| Target script execution | 88 synthetic / 104 original-inclusive checks; repeated chunks, native status versus Lua returns, signed getters, finite setter conversion, retained native/filesystem services, nested shared VM/context, four actual Agumon RAM early returns and explicit ordinary combat boundary |
| Original Hit comparison | 28 cases match official Lua 4.0.1: 24 RAM/kind early returns and four kind-80000 boundaries, 64 ordered calls, 92 exact rows and 736 executed instructions; 12 unavailable-service retries preserve the prefix |
| Collision/script adapter | 45 checks cover live GETRECNO/HP-presence work, owner expiry and matrix lifetime; real CHKBULLET/record/context continuation completes through an explicit synthetic filesystem effect, with no repeated record lookup or committed matrix write |
| Original collision/events/waits | HIT body 109 checks; collision integration 80; player events 108; waits 107, retaining all four Set129/TALKWF/WCHECK paths and the missing HITPLAYERTALK boundary |
| Original ACTION regression | 6,034,137 checks across 183 actions, 369 commands, 4,157 poses and 240 four-instance sequence poses |
| Packaged application | 16 app, 10 walking and 9 boot CLI checks passed; actor inspection renders 120 frames / 121 uploads; walking replay renders 120 frames, 198 steps, 101.333 travel and 102 uploads |
| Visual inspection | Version 0.28 actor and walking captures inspected; walking retains pause/restart/focus-loss replay and the 1236x600 render target |

The original Hit chunk is loaded unchanged from map AFS entry 179, LuaInfo
2783/export 237/data index 26. Four canonical original MODELCTRL RAM bindings
qualify the integration cases. Ordinary kind 80000 executes the verified
damage-turn clear and stops at `be.BLPcalcKoukaMagic`, PC 58, after 43
instructions and four logical calls. No magic, damage or status result is
supplied. The independent reference uses only opcode-count/PC instrumentation;
its deliberate test error at that unavailable method corresponds to the native
retained service boundary, not a claimed identical error mechanism.

Explicit fixture HP values and callbacks are not original player construction.
Lua allocation limits remain cumulative without an original garbage collector.
These checks establish the prepared component and bounded script paths, not a
complete combat or connected multiplayer session. Original bytes, reference
drivers, traces and app captures remain ignored under `work/`. See
[MODEL_SCRIPT_RUNTIME.md](MODEL_SCRIPT_RUNTIME.md),
[MODEL_SCRIPT_CONTEXT.md](MODEL_SCRIPT_CONTEXT.md), [LUA4.md](LUA4.md) and
[SCRIPT_REGISTRY.md](SCRIPT_REGISTRY.md).

## Native target collision validation

Version 0.27.0 was validated locally on 2026-09-10 with MSVC 19.51. Prepared
HITBOX queries now reach target body caches, first overlap and retained nested
record/hit-script operations. Actual target Lua/HP/global context, damage and
original player construction remain required before playable combat.

| Check | Evidence |
| --- | --- |
| Release build and package | 51/51 CTest suites passed without compiler warnings; 6 Python verifier tests passed |
| Authored HIT body/cache | 88 synthetic / 109 original-inclusive checks; four Agumon RAMs, ordered boxes/aliases, raw category bytes, zero-time factory bounds, timestamp reuse and independent mutable caches |
| Target collision kernel | 162 checks cover lazy gates, first/equality overlap, deferred argument reads, qualified endpoints, record counts, nested buffer/script suspension, captured positions, live pose changes and uncertain effects |
| Prepared collision integration | 67 synthetic / 80 original-inclusive checks; source HITBOX to target to BHit, nested target cleanup/LUACALL retry, canonical RAM rejection and retained GETRECNO; four original Agumon HITENEMYBULLET recipients produce four checked misses |
| Target box arithmetic | 231 public math checks; 10,048 original-instruction cases, 44,137 exact output words and 2,544,172 instructions, zero mismatches against the final production core |
| Source box regression | 4,599 original-instruction cases, 22,728 exact finite output words and 2,222,379 instructions, zero mismatches; includes 693 original-authored vector cases |
| Euler numeric correction | 111 synthetic / 133 original-inclusive motion checks; forced hit heading uses NI flushing for trig conversion and Euler arithmetic while preserving live scalar position words |
| Euler instruction comparison | Final-core runner matches 4,932 cases, 78,624 exact output words and 647,129 instructions with a shared host-trig boundary; the existing 9,000 turn/tick cases also retain zero mismatches |
| Messenger stack | 161 checks; final-core comparison matches 101 scenarios, 7,525 exact buffer/selection/owner rows and 2,232,011 instructions |
| Messenger delivery regression | Final-core comparison matches 84 scenarios, 2,028 callback/result rows and 2,389,520 instructions |
| Original ACTION/events/waits | 6,034,137 ACTION checks across 183 actions and 4,157 poses; player events 108 checks; player waits 107 checks, preserving all four Set129/TALKWF/WCHECK paths and the missing HITPLAYERTALK boundary |
| Packaged application | 16 app and 10 walking CLI suites pass; actor inspection renders 120 frames / 121 uploads; walking replay renders 120 frames, 198 steps, 101.333 travel and 102 uploads |
| Visual inspection | Version 0.27 actor and walking captures inspected; walking retains pause/restart/focus-loss replay and the 1236x600 render target |

The original-data checks prepare explicit live owners and matrices; they do not
initialize a complete game session or connected multiplayer. Synthetic scripts
exercise continuation contracts, not original damage behavior. Instruction
comparisons have bounded prepared helper/trig profiles and do not establish
physical-console execution. Original assets, probes and captures stay ignored.
See [MODEL_COLLISION.md](MODEL_COLLISION.md), [MODEL_HIT_BODY.md](MODEL_HIT_BODY.md),
[COLLISION_MATH.md](COLLISION_MATH.md) and [MESSENGER.md](MESSENGER.md).

## Native HITBOX and collision dispatch validation

Version 0.26.0 was validated locally on 2026-09-10 with MSVC 19.51. Timed
HITBOX work now reaches real collision/script service boundaries through the
prepared player runtime. Target body collision, damage, full Lua context and
original player construction/entry remain required for playable combat.

| Check | Evidence |
| --- | --- |
| Release build and package | 48/48 CTest suites passed; no compiler warnings; 6 Python verifier tests passed |
| HITBOX lifecycle | 65 synthetic checks; 2,106 original-instruction cases match across 1,690,336 instructions at 577 addresses; existing sound/effect suite retains 67 passing checks |
| Query-box arithmetic | 92 synthetic checks; 4,599 instruction cases, 22,728 exact finite output words and 2,222,379 instructions, zero mismatches; includes 693 cases using all 231 original Set vectors under three explicit matrices |
| Prepared HITBOX integration | 216 checks cover typed retained boxes, three recipient categories, attachment/source identities, live post-debug matrix and post-query script reads, registration/manager/Lua pauses and callback ownership |
| Sequence and control | 230/1,114 synthetic/original-inclusive sequence checks; 86/140 control checks; all 352 Agumon HITBOX and 62 MUTEKI occurrences decoded; LUACALL pending/fault and post-cleanup field resets covered |
| Full archive SEQ scan | All 9,060 SEQ programs, 80,621 sequence occurrences and 447,274 commands across 2,273 IGB files decode with zero rejections |
| Wrapper Lua continuation | Prepared event runtime passes 275 checks, including incomplete/uncertain immediate Lua, nested-call preflight and retained messenger delivery without recipient replay |
| Messenger regression | 109 synthetic checks; rebuilt original comparison matches 84 scenarios, 2,722 operations, 2,389,520 instructions and 2,028 callback/result rows |
| Original four-instance boundary | Player waits pass 107 checks; all four actual SQ_ATCK1 labels append Set129, start TALKWF and reach WCHECK516. An eligible check then retains the missing HITPLAYERTALK registration boundary |
| ACTION/event regressions | 6,034,137 ACTION checks across 183 actions, 369 commands, 4,157 poses and 240 sequence poses; original player events pass 108 checks |
| Packaged app | 16 app and 10 walking argument suites pass; actor inspection renders 120 frames/121 pose uploads; walking replay renders 120 frames, 198 steps, 101.333 travel and 102 pose uploads |
| Visual inspection | Version 0.26.0 actor and walking captures inspected on an RTX 5070 Ti; walking retains pause/restart/focus-loss replay and its 1236x600 target |

The first Set129 is a talk-category query, not evidence of attack damage.
Synthetic managers exercise dispatch and continuation contracts; they do not
implement the game's target collision or Lua behavior. Original-instruction
comparisons use explicit bounded helper profiles and do not establish physical
Gekko execution. The app captures show the prepared viewer and provisional
walking path. Original assets, probes and captures remain ignored. See
[MODEL_HITBOX.md](MODEL_HITBOX.md), [COLLISION_MATH.md](COLLISION_MATH.md) and
[ACTION_EVENT_RUNTIME.md](ACTION_EVENT_RUNTIME.md).

## Native model wait and live ACTION query validation

Version 0.25.0 was validated locally on 2026-09-10 with MSVC 19.51. Prepared
model waits now use registered live ACTION work and separately selected original
pad slots. Full original player creation, HITBOX/collision dispatch and outer
sequence-entry/status effects remain required for gameplay.

| Check | Evidence |
| --- | --- |
| Release build and package | 45/45 CTest suites passed; no compiler warnings; 6 Python verifier tests passed |
| Model wait kernel | 73 synthetic checks cover common channels, null/empty names, per-pass suspension, live callback mutations, signed attack latches, reentrancy and uncertain effects |
| Original wait comparison | 1,835 cases match the original instruction model: 165,190 instructions at 416 unique addresses; prepared ONE/pad/messenger/string/command/sequence helper boundaries are explicit |
| WCHECK decoder and runner | 150 synthetic checks; 610 with original data, including all 259 occurrences and seven shared records; null-to-empty assignment, retained cursor, scalar preservation and resumable ACTION dispatch covered |
| Live ACTION query and state identity | 62 synthetic checks; 78 with original data across four actual RAMs; live results, missing selected work, ownership rejection, pending events, all mutation stamps and stable read-only getters covered |
| Original query comparison | 56 scenarios, 2,336 instructions, zero mismatches; 48 defined results and 8 explicit missing-work boundaries; actual boolean append and retained-work decrement execute with prepared capacity/reference ownership |
| Prepared player integration | 79 synthetic checks; 99 original-inclusive; separate pad slots, first-result query semantics, retained time/shared random state, same-owner retry, callback-owned message suspension, stale runtime rejection and Atck02 live destination/cursor covered |
| Original attack boundary | All four weapon-selected SQ_ATCK1 instances stop at the required unsupported HITBOX command; four explicitly prepared real KATCA3 actions then reach their own motion waits and return live incomplete ACTION results |
| ACTION/event regressions | 6,034,137 ACTION checks pass across 183 actions, 369 commands, 4,157 poses and 240 sequence poses; original four-player event integration passes 108 checks |
| Packaged app | 16 app and 10 walking argument suites pass; actor inspection renders 120 frames/121 pose uploads; walking replay renders 120 frames with 198 steps, 101.333 travel and 102 pose uploads |
| Visual inspection | Version 0.25.0 actor and walking captures inspected on an RTX 5070 Ti; walking retains pause/restart/focus-loss replay and its 1236x600 target |

The original-data test deliberately retains the HITBOX boundary instead of
skipping it to claim a complete attack sequence. Its separate KATCA3 setup does
not initialize original player work or collision services. Synthetic outer
sequence callbacks exercise continuation ordering only. Instruction comparisons
do not establish physical Gekko execution or complete external-manager behavior;
the app captures show the existing prepared viewer and provisional walking path.
Original assets, probes and captures remain ignored. See
[MODEL_WAIT.md](MODEL_WAIT.md) and [ACTION_EVENT_RUNTIME.md](ACTION_EVENT_RUNTIME.md).

## Native player event routing validation

Version 0.24.1 was validated locally on 2026-09-10 with MSVC 19.51. Explicit
MODELCTRL/ACTION graph bindings now connect native event consumers to owned
sound/effect queues. This is prepared component integration; complete original
player construction, audio/effect managers and outer gameplay scheduling remain
required.

| Check | Evidence |
| --- | --- |
| Release build and package | 42/42 CTest suites passed, no production compiler warnings; 6 Python verifier tests passed |
| Prepared event runtime | 243 synthetic checks cover cross-source routing, aliases, repeated parent assignment, ordered queues, nested suspension, protected shared arguments, externally visible pre/post-cleanup selection phases, local-jump continuation, source collisions and atomic registration bounds |
| Prepared player binding | 57 synthetic checks; 105 with original data, covering all four RAMs and three explicit LOD branches |
| Consumer tables | 68 synthetic checks; 5,960 with original Se and all 58 map Starter exports: 78 Se model rows/2,173 actions, 958 Starter model occurrences/5,887 actions, 1,338 exact Data2 occurrences |
| Messenger | 102 synthetic checks; independent original-instruction comparison covers 84 scenarios, 2,722 operations, 2,389,520 instructions and 2,028 exact callback/result rows, with zero mismatches |
| Subcommand lifetime | 67 synthetic checks; independent original-instruction comparison covers 1,005 lifecycle cases and 2,018,236 instructions, with zero mismatches |
| Subcommand/system timing | 3,008 scalar-addition cases and 3,171 system-time cases cover signed tiny results, two-stage conversion, timestamp wrap and uncapped frame deltas; 621,329 instructions, zero mismatches, 2 expected finite-input overflow rejections |
| Four original player instances | 63 actual model/consumer registrations; original weapon RUN/WAIT sequences route four independent footstep queues through delay, retained start and cleanup; 24 actor-cache poses and 32,904 deformed vertices |
| Original attack event | KATCA3 dispatch reaches its motion wait and emits actual PC_SUBURI02 sound plus x_100armk01/SQ_ATCK01 effect; missing find/generate/erase services resume without repeating completed work |
| ACTION/sequence regressions | All 182 ordinary Agumon actions plus NDSP, 369 commands, 4,157 poses/5,699,247 vertices and 240 four-instance sequence poses/329,040 vertices pass; original model sequence suite passes 258 checks |
| CLI and packaged app | 16 app and 10 walking argument suites pass; 120 actor frames/121 pose uploads and the 120-frame walking replay pass |
| Visual inspection | Version 0.24.1 actor and version 0.24 walking screenshots inspected; walking retains 198 steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads, pause/restart/focus-loss replay and its 1236x600 target |

Instruction comparisons run independent models of the inspected original
instructions with explicitly supplied allocation/list/string/controller service
boundaries. They do not validate those hooked original helper bodies or physical
Gekko execution. Original-data integration runs production native decoders and
owners; test-manager sound/effect handles do not establish audible output or
rendered effect actors. The screenshots show the existing separate inspection
and provisional walking paths. Original assets, instruction probes and captures
remain ignored. See [ACTION_EVENT_RUNTIME.md](ACTION_EVENT_RUNTIME.md),
[MESSENGER.md](MESSENGER.md) and [MODEL_SUBCOMMANDS.md](MODEL_SUBCOMMANDS.md).

## Native actor cache rendering validation

Version 0.23 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. Original actor cache capture and single-influence CPU
deformation now feed an explicit prepared draw. Its camera and fit remain host
inspection state; complete appearance traversal and gameplay remain pending.

| Check | Evidence |
| --- | --- |
| Release build and package | 36/36 CTest suites passed; no production compiler warnings; 6 Python verifier tests passed |
| CLI validation | 16 app option suites and 10 walking option suites passed without opening a graphics context |
| New core coverage | 41 actor skin checks and 463 animation math checks passed; source/RAM/actor/binding identity, permuted and repeated palette entries, private/shared caches, retained frames, malformed graphs and finite-result rejection covered |
| Original actor skin integration | 36 captures across four RAMs and all three appearances, 24,024 deformed vertices, 24,989 checks passed |
| Quaternion instruction comparison | 16,578 cases, 239,440 exact finite words, 1,553 finite-input overflow and 60 invalid-input rejections; all 10,250 supplied-data rotations unchanged; zero mismatches |
| CPU skin-loop instruction comparison | 1,612 cases, 7,327 positions and 226,991 executed guest instructions; 18,894 exact finite XYZ words and 1,029 finite-input overflow/NaN rejections; zero mismatches |
| Prepared GPU rendering | Exact image comparison against separately specified clip-space geometry verifies raw coordinates, noncommuting root/projection, position updates/reset, API separation, invalid-input rejection and GL state preservation |
| Packaged actor inspection | High-detail body in RAM 0, low-detail body in RAM 1 and solid shadow geometry in RAM 2 each render 8 frames; RAM 3 high-detail run playback renders 120 frames with 121 pose uploads |
| Original traversal and ACTION regression | 720 traversals and 18,000 cached bone queries pass; all 182 ordinary actions plus NDSP, 369 commands, 4,157 poses and 240 four-instance sequence poses pass |
| Packaged walking regression | 120 frames, 198 steps, 101.333 travel, 46 idle/152 run steps and 102 pose uploads; pause/restart/focus-loss replay passed |
| Visual inspection | Actor body/shadow/playback, overview and walking screenshots inspected; version 0.23 and 8x MSAA visible; body targets 1290x554 and walking target 1236x600 |

Math comparisons use the independently implemented original instruction model
and startup NI=1/RN=00 profile, including the documented inferred extreme fused
tininess boundary. They were rerun against helpers linked to the final core
implementation. They do not establish physical Gekko/GX parity. Original-data
integration exercises production decoding and caches; synthetic GPU checks
establish transform order, not original lighting, material or raster output.
The shadow geometry is inspected in Solid mode because its full original
effective material is unsupported. Original data, probes and images stay ignored.
See [ACTOR_SKIN.md](ACTOR_SKIN.md) for exact source and execution boundaries.

## Native actor hierarchy and traversal validation

Version 0.22 was validated locally on 2026-09-09 with MSVC 19.51. The portable
core now owns shared bone/joint caches, computes the original ordinary actor
hierarchy and exposes cached named bone queries. Complete renderer traversal,
event consumers and original gameplay integration remain pending.

| Check | Evidence |
| --- | --- |
| Release build and package | 35/35 CTest suites and 6 Python verifier tests passed; no production compiler warnings |
| New synthetic coverage | 248 animation math checks, 46 hierarchy checks and 50 traversal checks passed; actor lifecycle coverage now contains 61 checks |
| Original actor traversal | Four Agumon instances, three appearances, 720 traversals and 18,000 cached bone queries; freeze, interrupted actions, direct-pose invalidation and NDSP/reactivation included |
| Original graph validation | 484 hierarchy checks and 548,694 traversal checks passed using original Agumon data |
| Matrix instruction comparison | 7,415 cases, 101,168 exact finite output words, 996 finite-input overflow rejections and 96 invalid-input rejections; zero mismatches |
| Hierarchy instruction comparison | 64 bounded cases match all 12,976 output float words; helpers, root exclusion, joint-index permutation and retained root are covered |
| Original ACTION regression | All 182 ordinary actions complete, deactivate and reactivate; 369 commands, 4,157 poses / 5,699,247 finite vertices, plus 240 sequence poses / 329,040 finite vertices |
| Packaged walking regression | 120 frames, 198 steps, 101.333 travel, 46 idle/152 run steps and 102 pose uploads; pause/restart/focus-loss replay passed |
| Packaged visual inspection | Overview and walking captures inspected; version 0.22 visible, original Agumon/map render at 1236x600 with 8x MSAA on an RTX 5070 Ti |

The matrix comparison decodes original paired-single words with exact integer
binary32 arithmetic and the startup NI=1/RN=00 profile. Its 1,271 finite
subnormal/underflow cases include an inferred extreme fused tininess boundary;
they are not physical-console measurements or a claim of universal Dolphin
equality. See [ANIMATION_HIERARCHY.md](ANIMATION_HIERARCHY.md) for primary sources,
the inference and the unchanged numeric scope of older math helpers.

The hierarchy probe executes original integer control flow with explicit
bone-count, copy and matrix services. Original-data tests exercise production
decoding and runtime state; the broadcast sink records events. The packaged
preview retains its prior pose/controller path. These checks do not establish
original rendered-frame parity, event playback, connected multiplayer or the
complete game. Original data, probes and captures remain ignored.

## Native ACTION actor lifetime validation

Version 0.21 was validated locally on 2026-09-09 with MSVC 19.51. The portable
core now owns prepared ACTION actor buffers and executes activation, NDSP and
reactivation. Original actor traversal and messenger consumers remain pending.

| Check | Evidence |
| --- | --- |
| Release build and package | 33/33 CTest suites and 6 Python verifier tests passed; no production compiler warnings |
| Asset-free lifecycle coverage | 124 ACTION checks, 60 actor graph/lifetime checks and 86 combiner checks passed |
| Original actor graph | All four Agumon RAMs decode three actors each; each actor allocates 25 bone and 20 joint matrices, releases them and reactivates |
| Original ACTION regression | All 182 ordinary actions complete their command/effect phases, then deactivate and reactivate; NDSP bypasses its null list |
| Original pose regression | 369 commands, 4,157 action poses / 5,699,247 finite vertices, plus four weapon sequences producing 240 poses / 329,040 finite vertices |
| Original lifecycle instruction probe | 14 bounded activation/NDSP scenarios pass, including the first/middle allocated-actor barrier, attribute cleanup loops and frozen-node retention |
| Native buffer comparison | 13 original/native actor buffer transitions match all 208 compared allocation-prefix, link-selection, storage-presence and identity fields |
| Packaged walking regression | 120 frames, 198 simulation steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads; pause/restart/focus-loss replay passed |
| Packaged visual inspection | Overview and walking captures inspected; version 0.21 visible, original Agumon/map render at 1236x600 with 8x MSAA on an RTX 5070 Ti |

The lifecycle instruction probe executes original activation/cleanup branches
and identity initialization with prepared objects. Allocation, destruction,
metadata lookup, combiner operations, timer deletion and list-capacity services
are explicit hooks; their bodies are not independently validated by this probe.
The 208-field native comparison covers actor buffers only, excluding guest
pointers/refcounts, node/work/attribute/timer fields and those hook bodies.

The original-data tests exercise production decoding and runtime methods with a
recording event sink. Identity actor buffers are not evaluated bone caches.
These results do not establish event-consumer execution, actor traversal,
rendered fade/flash effects, connected multiplayer or a complete original game.
See [ACTION_ACTORS.md](ACTION_ACTORS.md) for ownership and remaining boundaries.
Generated probes, original data and captures remain ignored.

## Native ACTION2 validation

Version 0.20 was validated locally on 2026-09-09 with MSVC 19.51. Prepared
ACTION2 programs now execute motion, joint, fade, wait and flash effects. Full
actor construction, event consumers and renderer integration remain pending.

| Check | Evidence |
| --- | --- |
| Release build and package | 32/32 CTest suites and 6 Python verifier tests passed; no compiler warnings |
| ACTION fixtures | 91 asset-free checks cover typed/bounded graphs, first/same-action dispatch, cursor and wait behavior, timer phase/freeze, raw/packed color, shared attribute cleanup, required/failed/reentrant services and source/info/RAM ownership |
| Original ACTION integration | 183 Agumon definitions decode; all 182 ordinary actions complete their command and motion/fade/flash phases; NDSP remains an explicit outer actor boundary |
| Original action poses | 369 command occurrences, 4,157 complete poses and 5,699,247 finite vertex positions |
| Original sequence-to-action integration | Four independent weapon sequence instances apply their real ACTION lists into 240 poses / 329,040 finite vertices, including interrupted transitions |
| Independent instruction comparison | 20,000 node, timer-speed, joint, random-wait, fade and flash operations match all compared numeric/state fields at explicit prepared-object and service boundaries |
| Packaged walking regression | 120-frame replay passes with 198 steps, 101.333 travel, 46 idle/152 run steps and 102 pose uploads; pause/restart/focus-loss checks pass |
| Packaged visual inspection | Overview and walking captures inspected; version 0.20 visible, original map/Agumon render at 1236x600 with 8x MSAA on an RTX 5070 Ti |

The instruction probe executes original arithmetic and state branches, including
color conversion with original initialized bounds. It supplies prepared objects,
list removal and timer/RNG service results; those external bodies, the complete
dispatcher and actor allocation are outside the independent comparison. The
original-data tests use production codec/pose evaluation and a recording
broadcast sink. They establish neither event-consumer execution nor rendered
fade/flash behavior, physical multiplayer or original gameplay-frame parity.
See [ACTIONS.md](ACTIONS.md) for exact ownership, numerical and integration limits.
Original bytes, probes and captures stay ignored; distributed fixtures are synthetic.

## Native animation combiner validation

Version 0.19 was validated locally on 2026-09-09 with MSVC 19.51. The portable
core now retains prepared combiner states and cached-pose snapshots and applies
explicit MOTIONSET effects. Full ACTION/actor integration remains pending.

| Check | Evidence |
| --- | --- |
| Release build and package | 31/31 CTest suites and 6 Python verifier tests passed |
| Combiner fixtures | 73 asset-free checks cover priority/retirement, interrupted snapshots, new flags, same-time cache, motion phases, system/node timestamp separation, four owned clocks, source sharing and malformed prepared graphs |
| Matrix and animation graph fixtures | 151 math checks and 69 graph checks passed, including independent synthetic snapshot expectations and exact duration/priority metadata |
| Original clip regression | All 82 Agumon clips pass the existing sample, five-mode timing, complete pose and layer checks; 5,728 total checks including the asset-free fixtures |
| Original combiner integration | All four prepared Agumon combiners load; eight original idle/run MOTIONSET records drive 240 complete poses with 329,040 finite vertex positions and four interrupted transitions |
| Independent state-update instruction oracle | 10,000 updates in 500 programs / 17,692 commands match at explicit prepared-object and list/pool service boundaries; NaN payload excluded |
| Independent matrix instruction oracle | 30,556 conversions / 305,224 float fields match; nonfinite guest outputs require native rejection |
| Original square-root rounding probe | 25,166,839 positive float cases match the original estimate plus three double Newton steps; exhaustive mantissas for both exponent parities, all float subnormals and normal exponent boundaries |
| Packaged walking regression | 120-frame replay passes with 198 steps, 101.333 travel and 102 pose uploads; pause, restart and focus-loss checks pass |
| Packaged visual inspection | Overview and walking captures inspected on an RTX 5070 Ti; version 0.19 visible, original map/Agumon render at 1236x600 with 8x MSAA |

The state probe executes the original update/retirement-marking instructions.
It supplies prepared creation fields, empty named-transition lookup, signed
division, replace-index rebuilding and list/pool removal services. Those service
bodies and the complete actor are outside that comparison. Original combined
poses use production channel/skin evaluation; they are not independent codec
validation or original gameplay-frame comparisons. See
[ANIMATION_COMBINER.md](ANIMATION_COMBINER.md) for the implementation profile,
numeric evidence and remaining boundaries. Original files and local probes stay
ignored; published tests contain synthetic data only.

## Native animation timing and layer validation

Version 0.18 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. The portable core implements recovered numeric animation
clocks, source wrapping and ordered layer arithmetic. Complete ACTION/combiner
state management remains pending; the preview retains its provisional controller.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed without compiler warnings; 30/30 CTest suites and 6 Python verifier tests passed |
| Animation timing fixtures | 62 asset-free checks cover signed/unsigned coarse clocks, original metadata cache defaults, shared caches, speed-origin continuity, saturation, blend rounding, exceptional durations, endpoint branches and runtime modes |
| Animation graph and layer fixtures | 66 asset-free checks, including timed sampling, cache publication on success, layer order, original quaternion behavior, fused cancellation and invalid inputs |
| Original Agumon integration | 5,659 additional checks pass for all 82 clips; five modes at six local-time fractions add 2,460 complete timed poses and finite layer evaluations |
| Independent timing instruction oracle | 30,000 operations in 1,000 shared-cache programs match at prepared-object and duration-service boundaries; NaN payloads excluded |
| Independent layer instruction oracle | 10,000 tuples / 70,000 float32 quaternion and translation components match; half use decoded original rotations, half synthetic rotations |
| Packaged walking regression | 120-frame SDL replay passes with 198 steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads and final (-292,40,431); pause/restart/focus-loss checks pass |
| Packaged visual inspection | 1280x720 overview and walking captures inspected; version 0.18 visible, original Agumon/map render in the 1236x600 walking target with 8x MSAA |

The instruction probes cover numeric operations and bound channel mixing, not
the complete ACTION state machine, source construction, snapshots, events or
actor lifecycle. Timed original-data checks use the production codec and pose
path; they are not independent codec or original gameplay-frame comparisons.
Original bytes, local probes and captures remain ignored. See
[ANIMATION_TIMING.md](ANIMATION_TIMING.md) for ownership and exact boundaries.

## Native model sequence validation

Version 0.17 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. The portable command runner retains original model sequence
state, weapon sections, supported control/motion effects and required ACTION
requests. Complete outer player callbacks and ACTION execution remain pending;
the walking preview continues using its provisional host controller.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed without compiler warnings; 29/29 CTest suites and 6 Python verifier tests passed |
| Sequence fixtures | 65 asset-free checks cover original list slots, aliases, retained state, label caches/barriers, command-name precedence, nested jumps, movement stop rules, failed/reentrant services and malformed/bounded input |
| Original Agumon sequence integration | 193 additional checks pass across both entry groups, all four slots and all four weapon sections; original lists request the matching WAIT/RUN/WAIT actions and travel approximately 8.275 local units at explicit cached delta 1 |
| Complete SEQ corpus decode | 9,060 programs, 80,621 sequence occurrences and 447,274 command occurrences in 2,273 archive IGBs; zero rejections; unsupported runtime commands are retained, not executed |
| Independent command instruction oracle | 6,000 bounded command programs match at the supplied list/string/channel allocation and ACTION service boundaries; NaN sign/payload excluded |
| Independent label instruction oracle | 3,000 programs with 24,539 set/lookup pairs match original label tables and cached offsets; missing original labels produce an explicit native guard |
| Packaged walking regression | 120-frame SDL replay passes with 198 steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads and final (-292,40,431); pause/restart/focus-loss checks pass |
| Packaged visual inspection | 1280x720 overview and walking captures inspected; version 0.17 visible, Agumon and original map render in the 1236x600 walking target with 8x MSAA |

The original-data fixture supplies a recording ACTION service to verify requests;
it does not execute the animation manager, full sequence entry or player creation.
The instruction probes use synthetic prepared work and supplied external service
boundaries. They do not establish allocator behavior, full callbacks, original
gameplay or physical multiplayer. Original bytes/probes/captures remain ignored.
See [MODEL_SEQUENCE.md](MODEL_SEQUENCE.md) for exact scope and source addresses.

## Native model control and preparation validation

Version 0.16 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. Original CTRL/PDMOVE effects, movement preparation, player
turn rates and post-input sequence requests now run in the portable core.
Full original player construction, input and sequence/action execution remain
pending. The walking preview retains its provisional host controller.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed without compiler warnings; 28/28 CTest suites and 6 Python verifier tests passed; installed executable matches the build by SHA-256 |
| Model control fixtures | 64 asset-free checks: original rates, FMA signed-zero heading, timer/position-lock preparation, exact CTRL/PDMOVE fields and names, byte/phase branches, request priority, independent state, dependency guards and atomic failures |
| Complete CTRL/PDMOVE scan | All 668 records in 2,273 recognized archive IGBs decode: 526 CTRL and 142 PDMOVE; zero rejections |
| Independent control instruction oracle | 10,000 cases, 2,500 each for CTRL, PDMOVE, post-input control and MOVE; zero mismatches at the supported service boundaries |
| Original command/placement integration | 49 additional checks pass using Agumon Move 130 / Stop 1540, CTRL 6499 / 622 and all eight PLAYERPOINT 115 matrices; explicit command effects request WAIT/WALK and travel approximately 8.275 local units over 18 Move / six Stop updates at cached delta 1 |
| Packaged walking regression | 120-frame SDL replay passed: unchanged 198 host steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads and final (-292,40,431); pause/restart/focus-loss checks passed |
| Packaged visual inspection | 1280x720 overview and walking captures inspected; version 0.16 visible; Agumon and original map render in the 1236x600 walking target with 8x MSAA |

The oracle executes a bounded instruction profile with synthetic named objects
and preallocated channel/list capacity. Host atan2, timer creation and sequence
dispatch are supplied boundaries. It does not establish original libm parity,
allocator behavior, full sequence execution or gameplay equivalence. The optional
data checks explicitly apply commands at stored matrices; they are not original
player creation, a scheduler run or physical multiplayer. Original bytes,
instruction probes and captures remain ignored. See [MODEL_CONTROL.md](MODEL_CONTROL.md).

## Native model turning and movement validation

Version 0.15 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. It adds the recovered model turn interpolation and combined
rotation/translation order to the portable core. The walking preview still uses
its provisional host controller; original player preparation and dispatch remain
required before that core can drive the original game.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed; 27/27 CTest suites and 6 Python verifier tests passed |
| Model motion fixtures | 65 asset-free checks: shortest-arc ties, signed steps, exact-zero/force branches, timer side effects, Euler basis and position, negative zero, update order, independent ownership and atomic failure |
| Accumulated angles | Accelerated wrapping matches a separate literal repeated-float loop across both signs and exponent boundaries through magnitude 2^27; stagnant original loops stop explicitly |
| Independent instruction oracle | 9,000 cases: 6,000 standalone turns and 3,000 combined ticks; all 8,434 published states/matrices match by bits, with 566 original non-finite poses rejected at the native boundary and zero mismatches |
| Original Agumon commands and placements | 22 additional checks pass using Move 130 / Stop 1540 and all four PLAYERPOINT 115/group-0 matrices; 18 Move and six Stop updates at explicit cached delta 1 travel approximately 8.275 units along each source basis and stop |
| Packaged walking regression | 120-frame SDL replay passed: unchanged 198 host steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads and final (-292,40,431); pause/restart/focus-loss checks passed |
| Packaged visual inspection | 1280x720 overview and walking frames inspected; version 0.15 is visible, and the walking target is 1236x600 with driver-supplied 8x MSAA |

The instruction oracle shares host double sine/cosine and IEEE float FMA with
the native boundary. Its bitwise matches establish surrounding arithmetic and
control flow, not equivalence to the original PPC math library. NaN payloads,
FPSCR exception flags and guest subnormal modes remain outside this profile.
The original-data fixture exercises four stored placements, not original spawn
initialization, physical multiplayer or the game's scheduler. Original data,
instruction probes and captures remain ignored. See [MODEL_MOTION.md](MODEL_MOTION.md)
for source addresses, supported effects and remaining integration work.

## Native motion and timer validation

Version 0.14 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. The original motion arithmetic and timer are portable core
implementations; the walking preview still uses its provisional host controller.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed; 26/26 CTest suites, 6 verifier tests, 9 boot CLI, 11 app CLI and 10 play CLI tests passed |
| Motion fixtures | 168 checks: six modes, preserved/impulse speed, strict crossings, expiry before clamp, exact-zero sentinel, signed deltas, valid zero-frame infinity/NaN, malformed schema and atomic input failure |
| Channels, matrix and timer | Independent owned channels, shared cached delta, initial cached frame 1, coarse clock/float rounding/wrap, signed scale and elapsed time, saturation, strict expiry, preserved restart fields, local scale/rotation and explicit fused translation |
| Complete original command scan | All 3,796 MOVE/MOVEEX records in 2,273 recognized archive IGBs decoded; 197 MOVEEX and 131 zero-frame records; zero rejections |
| Independent scalar instruction oracle | 18,000 setups and 54,000 ticks agree with a bounded interpreter of the supplied PPC routines, including expiry/stop, overflow and unordered comparisons; finite/infinite results checked by bits, NaN payloads excluded |
| Agumon original commands | All 16 MOVE records decode; original Move 130 reaches speed 0.7 and travels approximately 6.65 over 18 delta-1 updates; Stop 1540 travels approximately 1.625 over six updates and becomes inactive |
| Packaged walking regression | 120-frame SDL replay passed: unchanged 198 steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads and final (-292,40,431); pause/restart/focus-loss checks passed |
| Packaged visual inspection | 1280x720 overview and walking frames inspected, driver supplied 8x MSAA; version 0.14 and existing preview scope remain visible |

Original instructions/data and local oracle probes remain ignored. These checks
establish the supported arithmetic/data profile, not a full original gameplay
comparison. NaN payloads, FP exception flags and guest subnormal modes are not
modeled. Original actor/sequence state, turn interpolation, collision response,
timer ownership and scheduling are required before integrating this core into
the original game. See [MOTION.md](MOTION.md).

## Native initialization and pad validation

Version 0.13 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. This increment reconstructs preliminary loading, supported
native callback effects and original physical-pad state. It does not complete
the original startup, audio backend or multiplayer game.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed; 25/25 CTest suites, 6 verifier tests, 9 boot CLI and 11 app CLI tests passed |
| Native controller fixtures | 50 checks: exact exports/schema, owned state, source-qualified aliases, captured recipient keys, preserved RAM fields, ordered unknown-handler stop, atomic malformed steps and budgets |
| Startup loading fixtures | 81 checks including the prior aggregate path and new preliminary-only reader order, descriptor retention, empty `/s` subset, repeated loads, partial ownership, malformed routes and byte caps |
| Original preliminary trace | Only `ndmw.afs[80]` descriptor and `[178]` member requested; one retained file, 222,408 bytes and four exports; no ordinary/common read |
| Original callback effects | Preliminary Static info 499 / RAM 496 registers `STATICINFO / dng101b1 / 0`, then Sound info 3 stops the prefix; separate explicit Switch inspection initializes info 3471 / RAM 3443 without changing its target |
| Packaged preliminary CLI | Reports preserved native RAM/recipient state and returns the expected exit 3 at `beSoundInfo`; func/boot do not execute |
| Original pad fixtures | 2,130 checks: radius and angular boundaries, strict/inclusive comparisons, chord edges, independent stick/button repeat timers, four physical slots, freeze/disconnect/reconnect, invalid-input atomicity and raw GC conversion |
| Independent pad conversion oracle | Every 65,536 raw button mask agrees with interpreted original conversion instructions; all 131,072 signed-byte stick pairs across main/C-stick agree with the SDK branch formulas |
| Aggregate inspection regression | One-player profile retains nine files, 4,006,731 bytes, 454 exports, one Lua info, 17 held scenes and 436 aggregate deferred infos; actual Lua state remains alive |
| Startup UI | Finite packaged 1280x720 run passed; inspected screenshot shows explicit preliminary/audio bypass scope and readable owned-file results |
| Walking regression | Packaged 120-frame replay passed with unchanged 198 simulation steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads, final (-292,40,431), and pause/restart/focus-loss checks |

The native pad library has no device enumeration or original player-join state;
four independent synthetic slots do not establish physical multiplayer gameplay.
Host libm results at angular boundaries are not claimed bitwise identical to the
original PPC math library. Preliminary callback dependencies are explicit and do
not establish the original complete manager registry or sound-transfer services.
The existing aggregate inspection queue spans phases and must not be replayed as
the original later registration queue. See [CONTROLLERS.md](CONTROLLERS.md),
[PLAYER_MOVEMENT_RESEARCH.md](PLAYER_MOVEMENT_RESEARCH.md) and
[AUDIO_STARTUP_RESEARCH.md](AUDIO_STARTUP_RESEARCH.md).

## Native walking preview validation

Version 0.12 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. This establishes an interactive native original-map walking
state, not completion of the original game or its startup sequence.

| Check | Evidence |
| --- | --- |
| Release build | Native executable, portable core and package built successfully |
| Asset-free tests | 23/23 CTest suites; 6 verifier, 8 inspect CLI, 5 catalog CLI, 8 boot CLI, 11 app CLI and 10 play CLI tests passed |
| Walking solver | 683 synthetic checks: slopes, shared edges, tiny gaps, stepped/stacked floors, bounded hierarchy, swept wall radius, sliding, overhead walls, malformed input and equal travel at 30/60/144 Hz |
| Collision reader | 40 synthetic checks plus 5 original checks; 102 original floor triangles and 228 original wall/door triangles preserve their source coordinates and stored inner switch selection |
| Original assets | Map package dng101b1 member 1 / BG02, player model 1386, original entry 115/group 0/player 0, waitS/runD clips; 236 textured GPU batches and six images |
| SDL keyboard replay | 120-frame run: 198 host simulation steps, 101.333 units of total travel, 46 idle and 152 running steps, 102 pose uploads; final position (-292,40,431) after restart and further movement |
| Pause, restart, focus loss | Production replay asserts frozen position/animation, cleared held inputs and exact grounded spawn/facing/idle restoration; all three checks passed |
| Original-data lifecycle | 37 local harness checks passed: movement in all four directions, stable map batches/textures, pause, held-key clearing, restart, return to workbench, fresh start, remount, missing-resource failure and recovery |
| Rendering | 1280x720, 1920x1080 and 3840x2160 windows passed the same replay with identical simulation results; scene targets 1236x600, 1876x960 and 3796x2040; driver supplied 0/8/8 MSAA samples |
| Visibility and GPU state | Original GPU suite plus partial-occlusion silhouette, unchanged visible texture, no self-highlighting, invalid batch rejection, resize, MSAA and state restoration passed |
| Visual inspection | App-rendered walking, spawn, pause and overview frames inspected; original Agumon is visible in the corridor, with gold hidden-surface indication when needed |

The 120 Hz solver, speed, body dimensions and follow camera are provisional
native behavior. These finite runs are not a sustained FPS benchmark. Physical
Xbox/PlayStation/GameCube device compatibility, original collision parity,
enemies, combat, doors, audio, progression, saves and multiplayer remain open.
Original-data captures and harnesses are ignored local files; CI needs no game
assets. [PLAY_PREVIEW.md](PLAY_PREVIEW.md) describes launch and controls.

## Earlier viewer and startup validation

Local validation completed 2026-09-08 on Windows x64, MSVC 19.51, NVIDIA
GeForce RTX 5070 Ti. This records version 0.11 owned startup loading and routing,
not gameplay compatibility or a finished port. Earlier asset, animation,
placement and renderer measurements are retained from their respective
milestones. Retained Lua runtime ownership, ordered common-file loading,
source-qualified info routing and startup UI/CLI lifecycle are the new
acceptance work.

| Check | Result |
| --- | --- |
| Release core and native app build | Passed |
| CTest: existing asset/viewer suites plus scripts, Lua 4, startup and startup loading | 21/21 suites passed in Release |
| Embedded scripts and scheduling | 67 synthetic checks plus 66 original checks passed in Release and fully instrumented AddressSanitizer; exported identity, shared records, first-match shadowing, independent native barriers and aggregate budgets |
| Original embedded scripts | dng101b1 member 1 exports 55 supported script records; func/boot/start/first references and byte sizes match the recovered chains |
| Native Lua 4 | 286 synthetic checks plus 10 original checks passed in Release and fully instrumented AddressSanitizer; typed stack/call/closure/table/vararg behavior, malformed chunks, invalid operands, poison-on-failure and cumulative limits |
| Boot host/resolver/runtime fixtures | 69 checks passed in Release and fully instrumented AddressSanitizer; prior host/resolver checks plus actual string/table/function/number globals after source destruction, move construction/assignment, stable report identity, failed-state rejection, one-attempt execution and resolution after moves |
| Startup loading/routing fixtures | 59 checks passed in Release and fully instrumented AddressSanitizer; bounded callback reads, stage/FIFO order, source-qualified aliases, common Lua chunks not rerun, exact scene rules, valid reference zero, malformed/null branches, cumulative camera/name/export limits, failed-prefix counters and independent loading/routing success flags |
| Original owned startup resources | Zero/one/three profiles retain 96/9/11 files and 14,535,397 / 4,006,731 / 4,747,791 serialized bytes; every document equals its direct AFS payload, including repeated occurrences across profiles |
| Independent original info routing | All 1,617 qualified routing tuples, including the separate late-scene list, match an independent raw-IGB oracle across three profiles; final deferred counts 583/436/446 and held-scene counts 104/17/19; every ordered source/stage/ordinal/location/size also matches |
| Retained original boot runtime | All 17 original helper globals survive source/chunk destruction and runtime moves; the actual production Host path still matches official Lua 4.0.1 for all 188 calls/arguments and 110 queue entries, with unchanged instruction counts |
| Packaged startup CLI | All original ordered queue and qualified Lua/held/deferred entries match the independent oracle for zero/one/three profiles; actual retained Lua state reported after loading |
| Version 0.11 app lifecycle | 32 isolated native checks passed: successful owned-session counts, invalid input/package/unresolved-resource replacement, clear/remount, fresh-run recovery and byte-identical original 236-batch animated-player framebuffer before/after success, failure and result-view rendering |
| Version 0.11 startup UI | Packaged finite 1280×720, 1920×1080 and 3840×2160 runs passed; Resources, Loaded files, Deferred and Late views inspected at 720p; difficulty/camera variants, missing/unresolved resources, slot-1-only state, empty startup and all nine pages return expected results |
| Independent original execution | Native VM and unmodified official Lua 4.0.1 match all 188 ordered host calls/arguments and all 110 total queue entries across zero/one/three-player input profiles |
| Original zero-player boot | func: 35 instructions / 0 calls; boot: 505 instructions / 105 calls; 94 distinct requests, all catalog-resolved |
| Original one-player boot | Slot 0 Agumon with sword01/gun01/nil: boot 310 instructions / 34 calls / 7 requests, all catalog-resolved |
| Original three-player boot | Agumon/Vmon/Dorumon with shared weapons: boot 432 instructions / 49 calls / 9 requests, including original q_ substitution and exact dedup; all resolved |
| Boot CLI syntax | 8 table-driven test methods cover explicit state, malformed/range/duplicate selectors, nil weapon configuration, fixed USA getters, missing values and mixed app sources; no graphics/data required |
| Version 0.10 boot app lifecycle | 26 ignored native checks passed: mixed-source guards, input order, zero/one queues, success replaced by failed rerun, recovery, remount/missing mount and exact existing animated-scene pixels before/after boot success/failure; 9 additional tool CLI cases passed |
| Version 0.10 packaged startup UI | Original zero/one/three-player queues match all reference entries; finite 1280×720, 1920×1080 and 3840×2160 runs passed and screenshots were inspected; difficulty/camera variants, empty startup without data, missing/unresolved resources and slot-1-only state return the expected results |
| Version 0.10 viewer regression | All nine pages and the existing textured, animated player scene passed finite runs with the final packaged executable |
| Python ISO verifier tests | 6 synthetic tests passed |
| Native CLI end-to-end tests | 8 synthetic tests passed |
| Catalog/package CLI arguments | 5 tests passed, including missing/duplicate/conflicting selectors, empty paths, signed/partial/overflow indices and index-zero handling |
| Native app CLI without graphics | 11 test methods with table-driven subcases passed; explicit player selectors, duplicates, ranges, source conflicts, finite clip time and playback prerequisites; runs in Windows app CI |
| Player placement fixtures | 66 synthetic checks passed in Release and fully instrumented AddressSanitizer; exported identity, exact groups/slots, affine rotation/scale/reflection, immutable source, invalid links, overflow and aggregate serialized-name budgets |
| Original entry matrices | Both groups and all four slots in ndmw entry 179 decoded; all 128 matrix components compare bit-for-bit with eight original stored matrices |
| Player visual fixtures | 118 synthetic checks passed in Release and fully instrumented AddressSanitizer; model instance/ONE/LOD/actor/appearance/skin provenance, malformed or ambiguous ownership, attachments, modifiers, visibility and allocation guards |
| Original player visual | 12 additional checks passed in Release and AddressSanitizer: Agumon's three explicit branches and fourth model instance select the expected original geometry |
| Dynamic scene GPU and focus | Release and AddressSanitizer passed isolated actor-batch updates, every static pixel unchanged, UV/sampler retention, occlusion, exact reset, MSAA, stable normalization, source-bounds focus and invalid-input state preservation |
| App lifecycle integration | 37 ignored local harness checks passed: original shadow Solid to rejected Textured to Solid restores exact pixels; failed clip index/time/post-decode placement retains prior clip/time/vertices/pixels; member changes remove player; invalid resource leaves 235 static batches |
| Combined original map and player | BG02 plus high Agumon: 236 textured batches, six images, 31,886 static vertices plus 1,371 player vertices; full entry matrix replaces authored model-node transform |
| Original player native variants | Package and direct-AFS sources; high/low appearance, Solid shadow, both entry groups, slots 0 and 3, waitS/runD and finite playback; unsupported shadow material and invalid resource/model/point/group/branch/clip/time fail explicitly |
| Compact playback and 4K | 1280×720 and 3840×2160 host windows render combined scenes; actual targets 650×243 at MSAA 0 and 3210×1683 at MSAA 8; elevated inspection camera shows group 0, while nearby geometry can obscure group 1 |
| Version 0.9 startup research | Original file-list manager dispatch order func/boot, then start, then first traced; start calls funcPlayerSet; embedded filename lookup and outer ONE-model matrix replacement documented; script execution was added in version 0.10 as recorded above |
| Package synthetic fixtures | 109 checks passed in Release and AddressSanitizer: export/member order, catalog identity, exact `/s` handling, malformed input, forged metadata, NUL-padded repeated names, boundary compatibility and allocation limits |
| Original package integration | 8 checks passed in Release and AddressSanitizer; 97 descriptors / 195 members, including 98 preliminary `/s` members; both dng101b1 members match their AFS bytes exactly |
| Named map export identity | `dng101b1.igb` -> ndmw entry 80 -> member 1 `[0.179]` -> explicit export 7 / scene index 1 / info 3372 / BG02 root 2877 |
| Original switch command evidence | `first.lua` submits `SWITCHCTRL / ALLON`; original dispatcher, controller registration callback and target-mask effect traced; controller 3471 is explicit export 9 and targets root 2877 |
| Switch-controller and scene fixtures | 127 combined scene checks pass in Release and AddressSanitizer, including all-on/off, cross-word and nested masks, unchanged bytes, wrong/exported targets, malformed state and cumulative child-validation budget |
| Original native command replay | ALLON produces 235 instances / 26,352 triangles; ALLOF is complete and empty; every instance path, geometry reference and world transform agrees with explicit child-zero inspection |
| Package/control UI regression | 36 finite native checks rerun on version 0.9 passed: descriptor-only inspection, original commands, source/selector guards, invalid controller/target, empty/no-scene results, all eight pages, font, raw scene, Agumon animation and manual rig |
| Final compact scene controls | 1280×720 screenshot inspected after tab-fitting adjustment: Instances, Details, State and Status visible; ALLON, ALLOF and Stored selection fit |
| Named package at 1080p and 4K | All 235 textured batches / 5 images retained; 1290×603 and 3210×1683 scene targets inside 1920×1080 and 3840×2160 host windows, actual 8-sample MSAA |
| USA DOL identity | SHA-1 matches `e409a88a7379ed1a536f93b0a303a0ce7cd5d877` |
| ISO versus extracted data | Boot header, DOL, filesystem table and all four filesystem files match |
| Native boot/DOL inspection | `GDJEB2`, revision byte 0, 10 loaded sections, entry `0x80005240` |
| Native archive index | 1,382 `area.afs` entries; 973 `ndmw.afs` entries |
| Native IGB metadata integration | 2,274 inputs, zero failures (details in IGB_FORMAT.md) |
| Native IGB object integration | All 2,274 inputs and their object lists passed (details in IGB_OBJECTS.md) |
| Logical resource catalog | 2,080 paths, 2,079 available; archive counts match; 20 logical/direct reads identical |
| Native texture integration | 25,906 base images decoded, zero failures: 22,254 indexed8/RGBA8 and 3,652 BC1 |
| Independent BC1 sample | Original card payload decoded by Pillow's DDS reader matches native RGBA byte-for-byte |
| Texture viewer | Original font atlas and card rendered on the native GPU; screenshots inspected |
| Logical texture loading | `--resource bectext.igb` resolves and displays `ndmw.afs[79]` |
| Invalid explicit texture source | Missing resource reports an error and exits with code 3 in a finite smoke run |
| Native mesh corpus integration | 198,068 geometry instances decoded; 3,793,429 emitted triangles; zero unsupported profiles or failures |
| Original UV corpus | All 198,068 geometries checked: 46,988 valid UV streams, 151,080 absent; zero size or non-finite failures |
| UV wrapping evidence | 48,360 UV pairs on 3,825 geometries lie outside [0,1]; source coordinates are preserved |
| Stored base-texture binding corpus | 3,208 supported bindings; all have valid UVs and decodable bound images; remaining material profiles explicitly unsupported |
| Mesh synthetic fixtures | Triangle lists, grouped strips, winding, degenerates, counts, references, finite values and allocation bounds passed |
| Original topology identification | DOL enum table confirms triangle list 3 and triangle strip 4; all source index/length constraints checked |
| Portable orbit camera | Projection/frustum, orbit, pan, zoom bounds, reset and invalid-input tests passed |
| Native GPU renderer test | Pixel coverage, orbit, wireframe, resize, MSAA resolve, textured colors/UVs/wrapping/filtering/alpha, state restoration and cleanup passed |
| GPU pose updates | Position changes retain texture, UVs, indices, normalization and frame counter; reset reproduces pixels, invalid updates preserve the prior pose |
| Joint overlay projection | Uses the same fixed normalization/camera; fitted center, clipping and unchanged reference projection passed |
| Original skin reference mapping | Agumon geometry 5665 -> selector 5481 -> skin 404 -> database 810 -> skeleton 701; 25 bones, 20 joints, 24 palette entries |
| Original CPU manual pose | All three Agumon geometries reset with zero measured position error; bone 8 Z+30 degrees changes only vertices mapped to its joint descendants |
| Original rig corpus | All 894 `ndmw.afs` IGBs screened: 476 geometries with skin buffers, 290 supported with 222,694 vertices; no supported reset/manual-pose failures |
| Rig reset accuracy | Maximum coordinate error 1.97209e-15 across supported corpus; remaining 186 skinned profiles explicitly unsupported |
| Skinning synthetic fixtures | 153 checks passed, including full helper ancestry, original quaternion signs/norm, noncommuting child rotation, reset and malformed graph/matrix/palette/cache rejection |
| Native manual Agumon pose | Textured high geometry, bone 8 (`LArm01`) Z+45 degrees: 252 changed vertices, maximum displacement 4.47705; inverse-bind reset changes zero vertices |
| Original animation graph and poses | All 82 Agumon clips decoded and evaluated at zero, quarter, three-quarter and endpoint times: 8,200 bone transforms with complete mesh/joint outputs |
| Independent pose composition | 328 high-detail Agumon poses / 449,688 vertices compared with separate float32 composition using the same resolved channels; maximum vertex component difference 2.38419e-6, below 1e-5 |
| Reference codec compatibility | 55,950 quaternion/translation tuples over every half-sample interval and endpoint match the pinned enbrip implementation at nine significant digits; separate original engine interpolation is documented in ENBAYA.md |
| Original interpolation math | 49 synthetic checks; 83,400 quaternion comparisons against a bounded scalar interpreter of the original DOL routine match exactly on MSVC; the comparison does not emulate floating exception/status flags |
| Animation graph fixtures | 50 synthetic checks cover binding identity/ambiguity, masks including bits 31/32, duration, unsupported channels, owned data and original pose guards |
| Bounded Enbaya fixtures | 201 checks passed in Release and AddressSanitizer, covering packed widths, state changes, malformed/truncated streams, allocation limits and both sampling paths |
| Native clip inspection | Original textured waitS/runD at zero and quarter time produce distinct inspected frames; 720p controls fit and 1080p uses actual 8-sample MSAA |
| Native live playback | 32 frames upload 33 evaluated poses and cross the runD endpoint with Loop enabled while retaining texture and UVs |
| Animation error handling | Missing/case-mismatched names, out-of-duration time, unsupported rig and missing source exit 3; malformed, non-finite, negative or mixed manual/animation CLI inputs exit 1 |
| Original 3D sample | `ndmw.afs[179]`, reference 3: 24 vertices and 8 triangles rendered |
| Agumon stored-space preview | `_common/p_agumon.igb`, mesh 2, reference 5665: 1,371 vertices and 1,966 triangles; solid and wireframe screenshots inspected |
| Agumon bound-image verification | Image 5609, `P_agumon01.dds`, 256×256 BC1: all 65,536 texels opaque; independent Pillow DDS decode matches native RGBA exactly |
| Agumon textured GPU preview | Original high-detail mesh, 1,371 UV pairs and bound image rendered; yellow skin, green eye, mouth/teeth and white claws visually inspected |
| Low Agumon textured preview | Mesh 0, image 5597 (`P_agumon02.dds`, 64×64), 570 UV pairs rendered |
| Unsupported original material | Shadow mesh 1 in Textured mode reports its incomplete/ambiguous scope and exits 3; its Solid preview still succeeds |
| Mesh MSAA target | 1920×1080 window, 1290×554 textured offscreen mesh target with rig inspection and actual 8 samples |
| Compact rig layout | 1280×720 window: scrollable left Rig tab, visible manual controls and complete posed model in a 636×211 viewport |
| Manual rig source routing | Logical resource and direct AFS entry both work; low-detail default, high-detail textured/wireframe and combined head-axis deltas render successfully |
| Explicit rig failures | Helper bone 0, out-of-range bone 25, unsupported box and missing resource exit 3; malformed angles and CLI combinations exit 1 |
| Seven UI pages | Finite native smoke runs passed, including the existing texture CLI |
| Textured source routing | Both logical resource and direct AFS entry select the verified original image; missing source exits 3 |
| Invalid explicit mesh source | Missing resource exits 3; `--mesh` without a source exits 1 |
| Native BNR decode | 96×32 RGB5A3 banner displayed correctly |
| Initial five UI pages | Version 0.1 hidden rendering tests passed; overview, graphics and archive screenshots inspected |
| 4K/MSAA context | 3840×2160 framebuffer, actual 8 samples, VSync Off reported by native graphics page |
| Start without game data | Passed; data can be selected later |
| Static scene fixtures | 80 active checks passed in Release and AddressSanitizer: transforms, DAG instances, ordered geometry emission, masks, explicit inspection, malformed data, incomplete diagnostics and limits |
| Instance-path materials | 96 synthetic checks passed in Release and AddressSanitizer, including independently inherited bind/enable scopes and full-path texture-modifier rejection |
| Original BG02 stored state | `ndmw.afs[179]`, scene index 6 / info 3372 / root 2877: stored root mask is zero, so default traversal remains empty |
| Original BG02 explicit inspection | Root child 0: 462 node visits, 219 transforms, 235 instances from 29 geometry attributes; 31,886 placed vertices and 26,352 triangles; complete static traversal |
| Independent scene placement | Separate raw-graph traversal and original instruction arithmetic match all 235 owner/geometry/path tuples; matrix component error 0 and maximum world-position component error 9.5367431640625e-7 |
| Original scene texture coverage | All 235 instances have verified path bindings and correctly sized UVs; 5 unique images decode to 528,384 RGBA bytes |
| Native scene GPU | Shared placement, differently textured batches, opaque occlusion independent of draw order, texture sharing, one MSAA target, state restoration and malformed/budget rejection passed in Release and AddressSanitizer |
| Native BG02 scene | Solid, Wireframe and Textured finite renders passed; 720p controls fit and the 1080p scene target uses actual 8-sample MSAA |
| Native scene at 4K | 3840×2160 host window, 3210×1683 textured scene target, actual 8-sample MSAA, all 235 batches and 5 images retained |
| Eight-page and viewer regression | All eight pages, original font, manual Agumon pose and textured runD inspection passed finite native runs |
| Scene error handling | Empty stored selection, invalid root/child, non-switch override, missing source and unsupported dynamic branch exit 3; invalid/mixed CLI options exit 1 |

Run `scripts/build.ps1 -Package` to reproduce the Windows build and CTest.
The additional synthetic checks are:

```powershell
python -m unittest discover -s tests -p 'test_*.py' -v
python tests/cli_tests.py build/Release/dw4_inspect.exe
python tests/catalog_cli_tests.py build/Release/dw4_catalog.exe
python tests/app_cli_tests.py build/Release/dw4_pc.exe
python tests/boot_cli_tests.py build/Release/dw4_boot.exe build/Release/dw4_pc.exe
```

Example finite rendering check (output is local and ignored by Git):

```powershell
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 3 --page graphics --screenshot work/graphics.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource bectext.igb --screenshot work/font.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --archive area.afs --entry 621 --screenshot work/card.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource _common/p_agumon.igb --mesh 2 --screenshot work/agumon.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --screenshot work/agumon-textured.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --rig --bone 8 --rotate-z 45 --screenshot work/agumon-manual-pose.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --animation runD --animation-time 0.2 --screenshot work/agumon-animation.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --archive ndmw.afs --entry 179 --scene-root 6 --scene-inspect-child 0 --scene-mode textured --screenshot work/bg02-scene.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --package dng101b1.igb --package-member 1 --scene-root 1 --scene-switch-control 3471 --scene-switch-command all-on --scene-mode textured --screenshot work/bg02-package.bmp
.\build\Release\dw4_renderer_tests.exe
```

`--smoke-test` deliberately renders finite hidden frames and skips normal frame
limiting. It is not a benchmark or proof of 144 FPS game simulation. The 4K
check used a local settings file requesting 3840×2160 and MSAA 8x. Borderless
desktop mode was implemented but not exercised in the hidden-window test.

No physical controller was connected during validation; Xbox, PlayStation and
GameCube hardware behavior remains unverified. Gameplay scene activation, actor and
animation-combiner behavior, gameplay, audio, saves, online co-op and modding remain
unimplemented. Original clip inspection includes helper transforms; skinning
still requires single influences and identity selector transforms. See
[SKINNING.md](SKINNING.md) and [ANIMATIONS.md](ANIMATIONS.md).
Static scene inspection now reconstructs a bounded export/subtree profile with
original placements and explicit root selection. The inspected BG02 switch is
stored disabled. An explicit original ALLON command can now enable it through
its exported controller, alongside the separate manual-child inspection option.
The original func/boot script order now executes in a bounded native VM and
builds a resolved resource queue. Version 0.11 retains that live VM, loads the
common payloads and reconstructs the original info-routing order. The original
loading-screen gate, active manager callbacks and later layer/text initialization
remain unimplemented. Nested masks and source data remain unchanged. See
[STARTUP_LOADING.md](STARTUP_LOADING.md) and [STARTUP_VIEWER.md](STARTUP_VIEWER.md).
Explicit player visual placement now replaces the verified outer model matrix
and joins original single-clip playback to the scene. It does not execute player
generation, choose startup state, compose weapons/shadows or simulate movement.
The host inspection camera can be occluded by map geometry and does not reproduce
the original camera. See [PLAYER_VIEWER.md](PLAYER_VIEWER.md),
[PLAYER_PLACEMENT_RESEARCH.md](PLAYER_PLACEMENT_RESEARCH.md) and
[STARTUP_RESEARCH.md](STARTUP_RESEARCH.md).
See [PACKAGES.md](PACKAGES.md) and [SCENE_ACTIVATION.md](SCENE_ACTIVATION.md).
The scene renderer applies base
images with inspection shading; original lighting, render passes, culling and
transparent-surface ordering are still outstanding. See [SCENES.md](SCENES.md),
[SCENE_RESEARCH.md](SCENE_RESEARCH.md) and [SCENE_RENDERER.md](SCENE_RENDERER.md).
The mesh viewer displays stored or posed positions with inspection shading or a
verified base-texture binding. Full original materials and comparison against
recorded gameplay frames remain outstanding. The 3,208 supported bindings are a limited profile, not a claim that all
198,068 meshes have fully supported materials. See [MATERIALS.md](MATERIALS.md)
and the corpus diagnostics in [MATERIAL_RESEARCH.md](MATERIAL_RESEARCH.md).
Base texture decoding does not establish final appearance with the original renderer.
No exhaustive visual comparison of the 25,906 image instances was performed.
See [TEXTURES.md](TEXTURES.md) for layout constraints and sample observations.
Big-endian IGB metadata and documents were tested synthetically; all recognized
local game IGB metadata was little-endian. The original data remains outside
the repository.

Historically, GitHub Actions built/tested the portable core on Windows/Linux
and built/packaged the Windows app without game files. Hosted workflows are now
disabled at the owner's request. Current builds, tests and packaging run locally;
renderer GPU tests require an explicit local run with an OpenGL driver.
