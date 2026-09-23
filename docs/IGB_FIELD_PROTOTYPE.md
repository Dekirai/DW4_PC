# Native field prototype callbacks

`igb_field_prototype` reconstructs reference-field callback `80027454`,
object-field callback `80028B64`, integer-field callback `8002E440`,
memory-reference callback `8002B178`, Boolean callback `800388B0`, string
callback `80025628`, signed-byte callback `80037DB0`, and lazy
field-registry getter `80063B5C`.
The callbacks run after their descriptor-class metadata registration. They
publish canonical descriptor instances; those globals are distinct from the
metadata cells documented in
[IGB_FIELD_TYPE_REGISTRATION.md](IGB_FIELD_TYPE_REGISTRATION.md).

Evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original instructions and data
remain local; committed code and fixtures are authored C++.

## Canonical state and original order

| Callback | Class metadata | Prototype cell |
| --- | --- | --- |
| `80027454` | `80561684`, igRefMetaField | `80561688` |
| `80028B64` | `805616E8`, igObjectRefMetaField | `805616EC` |
| `8002E440` | `805619E0`, igIntMetaField | `805619E4` |
| `8002B178` | `80561818`, igMemoryRefMetaField | `8056181C` |
| `800388B0` | `80561E48`, igBoolMetaField | `80561E4C` |
| `80025628` | `805615B4`, igStringMetaField | `805615B8` |
| `80037DB0` | `80561E1C`, igCharMetaField | `80561E20` |

The object prototype aliases `IgbFieldValuesServices::object_prototype`.
The integer prototype aliases `IgbFieldValuesServices::signed_int_prototype`;
the existing `80094028` descriptor getter therefore observes the same cell.
Memory-reference and Boolean prototypes alias `memory_reference_prototype`
and `boolean_prototype` on those same services. Plain getters `80094070`
and `80093E84` consequently share their publication identities.
String and signed-byte aliases are `string_prototype` and
`signed_byte_prototype`, also shared by plain getters `80094100` and `80093E94`.
Reference prototype `80561688` and registry `8056229C` are shared cells that
can alias the bootstrap's canonical members. The registry contains field
prototypes, not type metadata. The later bootstrap store at `8003D46C` also
targets `80561688`; its `80027824` getter returns pointer-field prototype
`80561698`. That later assignment must share the descriptor cell.

Each callback first tests only whether its prototype pointer is nonnull.
A known null pointer reads metadata pool handle `805621F4`, runs actual
`800607F4`, reloads the class metadata, and calls reflected factory `8006546C`.
The returned pointer is stored directly into the prototype cell before its
null test. No registration-ready seed or direct descriptor constructor replaces
this reflected call. Callback replacement of the cell is overwritten with the
captured result; displaced owners are retained without a guest release.

A nonnull result follows this exact sequence:

1. Obtain the field-prototype registry through `80063B5C`.
2. Retain the captured factory result and append it through actual
   `80053E6C -> 80053BC0`. There is no preceding finalization.
3. Reload the canonical prototype, decrement its full reference word, and
   invoke `80066E1C` if the resulting low 23 bits are zero.
4. Call `80063B5C` again, read its count, reload the prototype and store
   `(count - 1)` truncated to 16 bits into ordinal `+12`.
5. Reload the prototype again and execute actual `80063F14` finalization.

The appended owner can differ from the later released or finalized owner if
a callback changes the global. Pending destruction retains the selected call;
it does not repeat the decrement. The second registry getter reads its live
global, so a replacement registry supplies the later ordinal count. These
callbacks return no descriptor result; publication is through their globals.

## Actual abstract reflected branch

The registered reference-field class is abstract: `flag_1a == 1` and its
ordinary redirect pointer is zero. Existing native
`IgbReflectedFactoryInvocation` therefore executes the real `8006546C` abstract
branch and returns null before allocation. `80027454` publishes that null and
skips registry creation, append, release and finalization. This is the original
callback behavior and does not manufacture a reference-field instance.

For all six represented concrete field classes, the same reflected invocation
preserves redirect handling, pool selection, allocation prefix arithmetic and actual `800667F4`
initialization. The object initializer now accepts a canonical runtime-field
binding at the adjusted allocation offset. Its generic result retains that
allocation and owner; the callback rejects a returned file and uses the original
adjusted numeric address for registry publication. The prototype services retain
the allocation after construction so the field's weak physical binding remains
valid. Reached field/default virtuals still determine construction completion;
the binding does not substitute a completed descriptor or cold direct constructor.

The integer profile uses its inherited `igMetaField` schema and verified scalar
object virtuals. Its `+2C` is the return-only `800667D0`; it does not run the
object-reference field's default-buffer tail. After reflection, `8002E440`
performs the same captured append, live canonical release, ordinal and finalizer
sequence described above. Its separate lazy prototype getter `8002E4D8` remains
outside this callback change. See
[IGB_INT_FIELD_REGISTRATION.md](IGB_INT_FIELD_REGISTRATION.md).

Memory-reference and Boolean callbacks preserve that same publication sequence.
Their reflected `+2C` entries are also `800667D0`; their distinct class schema
and other member operations still execute. Separate prototype getters
`8002B210` and `80038948` are outside these callback adapters. See
[IGB_MEMORY_FIELD_REGISTRATION.md](IGB_MEMORY_FIELD_REGISTRATION.md).

String callback `80025628` and signed-byte callback `80037DB0` also use the
same publication sequence. String construction reaches the real default-buffer
tail `8007144C`; signed byte uses return-only `800667D0`. Their separate lazy
prototype getters `800256C0` and `80037E48` remain outside these callback
adapters. See [IGB_STRING_FIELD_REGISTRATION.md](IGB_STRING_FIELD_REGISTRATION.md).

Prototype retain/release and ordinal stores also update represented physical
field storage. Each store uses its original captured or freshly reloaded owner,
checks that owner's physical identity, and preserves the existing canonical
reference-count and ordinal behavior. Prepared instances without physical
storage retain the same explicit caller-test profile.

When reflected services are absent, a separate explicit typed service endpoint
allows bounded caller fixtures to provide an already initialized descriptor.
Those fixtures exercise subsequent caller order and ownership; they do not
establish the omitted reflected constructor. Nonzero pointer identity without
a matching typed descriptor fails at that boundary.

## Native lazy registry and validation scope

`80063B5C` returns any existing nonnull `8056229C` registry. A known null cell
performs fresh `800607F4` pool lookup and actual field-list factory `8002DB4C`,
then raw-publishes and reloads the result. In the disabled startup profile,
the existing native list constructor performs its physical resets and separate
backing allocation. Missing backing or allocator services preserve the pending
constructor. A callback replacement of the registry does not suppress the
later original raw store.

The permanent suite covers the null-only gates, actual abstract `8006546C`,
the retained concrete initializer, strict field-result typing, adjusted-address
publication, physical ownership stores, native registry construction and raw
publication, physical append, live global replacement, low-23-bit destruction,
wrapped ordinals, finalization, retained callbacks, malformed bindings and
service lifetime. Nonnull reflected descriptor results and finalization
virtuals are explicitly prepared in the isolated caller fixtures. Local build,
cold-chain and bounded original-instruction evidence is recorded separately in
[VALIDATION.md](VALIDATION.md). Completed abstract callback behavior does not
establish completed native startup or a new gameplay preview.

The cold composition finishes the concrete object-field prototype
with its actual 23 reached descriptors, all three field passes, default-buffer
tail, registry append and finalizer. It then completes the parent-list type and
the actual internal-list memory-field clone through
`8002ACD8 -> 8006331C -> 8006CC88 -> 800639E4`. Clone finalization and
replacement finish the child list; both list metadata owners have flags `7`.
The bootstrap caller subsequently performs its original enable store and
records its then-current type and prototype counts at core `+04/+08`.
The earlier ThreadManager/DataList checkpoint reached the integer registration,
its reflected prototype, and the enabled scalar field factories. That extension
did not complete the DataList memory-field class or later thread services.
Current measured totals and the next reached boundary are recorded in
[VALIDATION.md](VALIDATION.md). Enabled reflected clone `800680B4` and full
startup remain separate work. See [IGB_FIELD_INIT.md](IGB_FIELD_INIT.md),
[IGB_FIELD_CLONE.md](IGB_FIELD_CLONE.md) and [IGB_FIELD_COPY.md](IGB_FIELD_COPY.md)
for the composition and its explicit pool/platform and physical allocator
prerequisites.
