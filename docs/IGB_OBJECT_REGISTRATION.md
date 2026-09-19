# Native igObject registration

`igb_object_registration` implements the USA `80066AE0` wrapper, its concrete
registration body `80066B08`, root-parent callback `80066A60`, plain metadata
getters and layout probe `80066AAC`. The evidence is the supplied `main.dol`,
SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original instructions remain
outside the repository; the implementation and synthetic tests are authored C++.

The canonical metadata cell is `805622A4`. Services share an
`IgbRegistrationMetaSlot` with `IgbBootstrapServices::object_metadata`, or alias
the corresponding member of reflected-initializer services. An absent optional
means an unrepresented read; an engaged null means the original null pointer.
The module never creates a second metadata cache or a ready schema seed.

## Concrete caller

`80066AE0` calls the existing native `80066188(80066B08)` runner. Unlike several
other generated registration bodies, `80066B08` has no prerequisite once-guard.
It passes the following arguments to native generic registrar `80066204`:

| Argument | Value |
| --- | --- |
| Canonical cell | `805622A4` |
| Name, abstract flag, size | `igObject`, `0`, `8` |
| Parent registration/getter | `80066A60` / `80066AA4` |
| Type getter | `80066B74` |
| Layout probe | `80066AAC` |
| Defaults callback | `80066B7C` |
| Extra/trailing callbacks | None |

The root-parent callback resolves the live metadata pool handle `805621F4`
through native `800607F4`, then requests `80056138(12, captured pool)`. Allocation
remains an explicit operation. After the selected allocator returns and its
continuation is released, a nonnull result executes the real callback-list
initializer `8003E848`; null returns directly. Missing allocators do not produce
an empty list. Captured pool arguments and selected continuations survive pauses.

Getters `800237D0`, `80066B74` and `80042DEC` only load the canonical cell. They
do not inspect readiness, allocate metadata or run registration. Getter
`80066AA4` returns literal null and does not require any global state.

## Local layout probe

`80066AAC` constructs an eight-byte local base object through leaf `8006665C`,
captures ArkCore `80562100`, reads its live `+394` offset, then loads the word at
that offset in the local object. The constructor's complete sequence leaves
word zero equal to `804717F0` and word four equal to `1`: its packed reference
operations overwrite all previous bits. There are no intervening callbacks.

The native probe supports aligned offsets zero and four within those fully
written eight bytes. It does not assume the result is always the vtable word.
Malformed, misaligned or out-of-object offsets raise a bounded native error;
unrepresented surrounding original stack memory is not synthesized. If the
captured core's offset is unavailable, resumption reads that same core rather
than redirecting to a replacement singleton.

## Remaining field registration

Defaults callback `80066B7C` remains explicit. It creates two specialized
descriptors through `80022A90`, assigning `_refCount` and `_memPoolIndex` at
object offset four and publishing descriptor globals `805622A8`/`805622AC`.
It performs real string ownership updates, default-mode/flag stores, descriptor
finalization and reference-counted append/release operations. These descriptors
are not replaced with ordinary integer fields or an empty schema.

The composed cold-start test begins with the actual ArkCore constructor and
bootstrap, without either canonical metadata owner. It executes native
`igMetaObject` registration, disabled metadata and field-list construction,
then this native `igObject` parent registration with distinct allocations.
The root parent list is initialized natively; actual metadata naming invokes
native string lookup, copying, header initialization and insertion. The layout
probe runs before the call suspends at `80066B7C`.

At that boundary `igMetaObject` remains busy with flag `8`, `igObject` has flags
`9`, and ArkCore is still disabled. No successful registration or reflective
enable store has occurred. Platform/manager initialization, initial ArkCore list
owners/reserve, physical allocation and string-arena allocation are explicitly
prepared endpoints. This is a composed native prefix, not complete cold startup.

Permanent tests cover exact argument identities, all getter aliases, captured
pool/allocator continuations, null allocation, constructor-after-callback order,
probe offsets and malformed storage, ready/busy registrar gates, unknown defaults,
weak adapter ownership, selected-operation retention and recursive misuse.
The local Release build passes 75 module checks; the separate metadata
registration suite passes 140 checks including the extended cold composition.
Whole-core validation is recorded in [VALIDATION.md](VALIDATION.md).

A bounded independent comparison against the final core passed 467 cases with
zero differences, executing 8,423 original instructions across 83 addresses.
It covers the actual local probe/base constructor with varied prior stack words,
plain/null getters and the root callback plus callback-list constructor. Pool
selection and physical allocation are prepared endpoints, including their
failure prefixes. The exact `80066B08` call ABI is captured at the original
`80066204` entry and compared with the native argument helper; that portion
does not claim differential execution of the entire generic registrar. The
permanent composed tests separately execute the real native registrar chain.
