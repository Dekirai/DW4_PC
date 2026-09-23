# Native field prototype callbacks

`igb_field_prototype` reconstructs reference-field callback `80027454`,
object-field callback `80028B64`, and lazy field-registry getter `80063B5C`.
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

The object prototype aliases `IgbFieldValuesServices::object_prototype`.
Reference prototype `80561688` and registry `8056229C` are shared cells that
can alias the bootstrap's canonical members. The registry contains field
prototypes, not type metadata. The later bootstrap store at `8003D46C` also
targets `80561688`; its `80027824` getter returns pointer-field prototype
`80561698`. That later assignment must share the descriptor cell.

Both callbacks first test only whether their prototype pointer is nonnull.
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

For concrete object metadata, the same reflected invocation preserves redirect
handling, pool selection, allocation prefix arithmetic and actual `800667F4`
initialization. The current object initializer requires a typed file view;
an allocation bound only to a field reaches that explicit typed initialization
boundary. It is not converted into a file or initialized with the cold direct
constructor. Generalized field initialization and its reached virtuals remain
required before the native concrete prototype can complete.

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
the concrete typed-initializer boundary, native registry construction and raw
publication, physical append, live global replacement, low-23-bit destruction,
wrapped ordinals, finalization, retained callbacks, malformed bindings and
service lifetime. Nonnull reflected descriptor results and finalization
virtuals are explicitly prepared in the isolated caller fixtures. Local build,
cold-chain and bounded original-instruction evidence is recorded separately in
[VALIDATION.md](VALIDATION.md). Completed abstract callback behavior does not
establish completed native startup or a new gameplay preview.
