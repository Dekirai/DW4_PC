# Native field clone wrappers

`igb_field_clone` reconstructs USA `8002ACD8`, the clone virtual at `+54`
of memory-reference descriptor vtable `80476088`. The cold path executes
copy constructor `8006331C`, its reference base `8006CC88`, and the shared
`800639E4` field copy through [IGB_FIELD_COPY.md](IGB_FIELD_COPY.md).
The module also reconstructs the signed-byte clone `80037BC0` and Boolean
clone `80038684`, and unsigned-short clone `80021EB4`, including their cold
scalar copy constructors `8003FFCC`, `8003EC04` and `80075F48`. These execute the
same shared `800639E4` body and then publish
only their own derived vtable. The source is the existing canonical descriptor; its physical constructor
image is not substituted for later schema changes.

Evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Instructions, game data and
comparison corpora remain local. The implementation is authored C++.

## Wrapper and allocation

The wrapper captures the source and its original integer argument before
reading the live ArkCore `80562100` enable byte. Unknown global storage pauses;
a represented null core faults. Once selected, the branch is retained across
pending allocation or registration rather than retesting the enable byte.

With reflection disabled, `80066DD8(0x50, 0)` calls `80066D60` with a null
incoming pool. The existing native raw allocator selects the canonical default
pool handle and preserves its pool or heap path. A null result returns null
without calling a constructor or virtual tail. A nonnull result enters the
actual copy constructor. This path ignores the captured clone argument and
does not call the ordinary `8002AC34` field factory or `8006324C` constructor.

These scalar wrappers use `80066DD8(0x34, 0)` with the same null and
allocation semantics. They call the copy constructor for the selected profile,
without running reference-base or memory-member copying. Their original integer
argument is likewise ignored by the cold path.

With reflection enabled, registration `8002AE84` runs before `800680B4` with
the captured source and argument. Existing callback services can execute the
actual registration runner for body `8002AEAC`; otherwise the exact wrapper
is an explicit reached operation. The reflected clone remains a separate
reached operation. It is not replaced with cold copying when registration or
generic reflected copying is missing.

Enabled signed-byte, Boolean and unsigned-short wrappers similarly execute
registrations `80037D00`, `80038800` and `80021FF4`, respectively, before
`800680B4(source, argument)`.
When registration callback services are bound, the native runner executes the
exact corresponding bodies `80037D28`, `80038828` and `8002201C`. The selected branch,
source, argument and registration continuation remain captured across suspension.
An unavailable `800680B4` remains pending for these profiles; completing the
scalar cold constructors does not implement generic reflected copying.

The authored [reflected clone module](IGB_REFLECTED_CLONE.md) now supplies that
operation through `make_native_igb_reflected_field_clone_factory`. It composes
the actual reflected initializer and selected member pass. The retained cold
integration now executes memory member `80062824` and the inherited
unsigned-short `_size` clone, completing struct defaults before the next
prototype callback. See [IGB_UNSIGNED_SHORT_FIELD.md](IGB_UNSIGNED_SHORT_FIELD.md).
The disabled copy constructor is not substituted for reflected copying.

## Copy construction

The outer constructor claims the canonical destination field's construction
token, binds its physical allocation and enters `800639E4`. That shared copy
performs the original object header, name ownership, field scalars and separate
default-buffer allocation/copy. A fresh default buffer uses `800681C4`, whose
pool virtual is `+D8`; it is distinct from the aligned `+E0` field-buffer path.

After the base copy returns, `8006CC88` publishes vtable `80471384`, then
copies source bytes `+34` and `+35` in order. `8006331C` then performs:

1. Publish memory-reference vtable `80476088`.
2. Clear destination words `+48` and `+4C`; these are not copied from source.
3. Copy source byte `+42`.
4. Copy the source element-descriptor pointer at `+3C` without a guest retain.
5. Copy source word `+38`, byte `+24`, byte `+41`, byte `+40`, and word `+44`.

Each source read occurs at its original point, including after a pending base
allocation returns. Source and destination may be the same canonical field:
earlier header/name/default-buffer stores then affect subsequent source reads.
There is no eager source snapshot or assignment of the whole host structure.
Padding and fields not written by the original copy remain untouched.

After the shared base copy, signed-byte constructor `8003FFCC` writes only
`80473F24` at destination `+00`; Boolean constructor `8003EC04` writes only
`80473E30`. Both release the same construction lease after this final store.
Neither reads source reference flags or elements, and neither writes beyond the
original `0x34`-byte scalar object.

The element pointer uses the canonical weak field identity and explicit
numeric bindings in `values->fields->pointer_lists->values`. A known null
pointer stores zero. Missing pointer state or an unavailable nonnull numeric
identity pauses without inventing null. The selected element identity remains
captured if its numeric binding arrives later; subsequent source scalar reads
remain live. Conflicting numeric identities fail.

## Continuations and integration

Selected callbacks, allocation and partially constructed fields remain retained
across suspension. Recursion, callback failure and abandonment invalidate the
destination field rather than exposing a completed clone. Completed allocations
remain owned by clone services, keeping each descriptor's weak physical-storage
binding valid. Typed results are available only after construction returns.

`IgbFieldCloneInvocation(source, argument, kind)` and
`IgbFieldCloneConstructorInvocation(destination, source, kind)` accept
`memory_reference`, `signed_byte`, or `boolean`; omitted kinds preserve the
memory-reference profile. `make_native_igb_list_field_clone_factory` selects
those profiles from captured tables `80476088`, `80473F24`, and `80473E30`,
respectively, for virtual `+54`. Its result
includes the canonical field and adjusted numeric address for later replacement.
Other tables, slots and operations delegate. The subsequent internal-list flag,
global-slot assignment, finalization and replacement remain the original
`igb_list_defaults` operations; clone construction does not seed their result.

Tests distinguish the complete cold copy chain from explicitly prepared enabled
registration/reflected endpoints. Scalar checks additionally cover exact
allocation size, independent one-byte default storage, padding, null allocation,
late source-buffer changes, failure before the derived-table store, abandonment,
and both registration routes stopping at the explicit reflected-copy boundary.
Local build results, bounded original-code
comparisons and the complete cold-start integration are recorded separately in
[VALIDATION.md](VALIDATION.md). Neither clone completion nor registration tests
establish complete game startup or playable native gameplay.
