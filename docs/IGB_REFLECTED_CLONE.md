# Reflected object cloning and copy passes

This module reconstructs the original USA DOL callers `800680B4`, `80068060`,
`80067C28`, `80067B60`, `80067ADC` and the per-field dispatchers
`8006426C`/`80064294`/`800642BC` -> `800641C4` as authored C++.
Evidence comes from the supplied DOL with SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`; generated instruction listings and
comparison corpora remain local and ignored.

The enabled branch of [IGB_FIELD_CLONE.md](IGB_FIELD_CLONE.md) can compose this
caller through `make_native_igb_reflected_field_clone_factory`. This is separate
from the disabled branch's scalar or memory copy constructor. The module does
not substitute that cold constructor when enabled reflection is incomplete.

## Allocation and captured state

`IgbReflectedCloneInvocation` implements `800680B4` in this order:

1. Read the source's current physical vtable and call virtual `+58` for metadata.
2. Retain that returned metadata, release the completed callback, and read only
   source byte `+04` for `80068430`'s tagged-pool query. The lower three bytes of
   the packed reference word are not read by this query.
3. Execute actual `IgbReflectedFactoryInvocation` (`8006546C`) with the captured
   metadata and returned pool, including its real initializer. Allocator,
   platform and reached initialization operations remain explicit services.
4. Capture the resulting destination and call `80068060` with the original
   source and mode. Return that same destination after the selected pass.

There is no null-result guard before the copy dispatcher. Recognized modes
dereference the destination even when reflected allocation returns null;
unsupported modes return without accessing it. When the reflected factory
service is absent, an explicitly bound `reflected_factory` operation can prepare
its result for bounded caller research. An unbound operation remains pending;
the caller does not invent a completed object.

## Mode and member dispatch

| Original mode | Pass | Before field iteration | Descriptor policy | After iteration |
| --- | --- | --- | --- | --- |
| `0` | `80067C28` | Destination metadata virtual `+58` | Signed byte `+27` | Fresh destination virtual `+18(destination, source)` |
| `1` | `80067B60` | Destination metadata `+58`, destination `+48`, source `+48` | Signed byte `+28` | Fresh destination virtual `+14(destination, source)` |
| `2` | `80067ADC` | Destination metadata virtual `+58` | Signed byte `+29` | None |
| Other signed 32-bit values | None | No object or service access | None | None |

Mode one retains the metadata returned before both `+48` hooks. Only after the
hooks does it capture that metadata's field registry. Every pass captures the
registry, then the base `igObject` global `805622A4`, then the registry count,
and reads the base registry count through actual `80065D88` semantics. Missing
base-global knowledge suspends before the later count read. A represented null
global or registry faults at its reached dereference.

The base count becomes the signed starting index. The loop compares that index
with its captured signed end count, but reloads the captured registry's backing
on every iteration. Changes to metadata's registry identity or the registry
count inside a member operation do not change the range; replacement backing
does affect the next descriptor. Original `slwi` and subsequent additions wrap
in 32 bits independently of the signed comparison. Each computed physical slot
must be represented, and a host visit bound prevents unbounded traversal of
malformed state. The bound is not an inferred game schema limit.

The selected policy byte is sign-extended. Values `0` through `4` call field
virtuals `+90`, `+94`, `+98`, `+9C` or `+A0`, respectively, with the captured
descriptor, destination and source. Every other value skips the field without
reading its vtable or either object. The dispatcher itself does not dereference
the destination or source merely to forward them to a selected member virtual.
This policy is distinct from descriptor `+24`'s default-initialization mode.

## Virtual adapters and ownership

The object adapter reuses existing metadata `+58` providers. For the verified
signed-byte `80473F24`, Boolean `80473E30` and string `8047693C` tables, `+48`,
`+14` and `+18` are original BLR leaves. Unknown tables or hooks delegate or remain
unbound; the adapter does not assume an arbitrary virtual is empty.

The member adapter maps only verified field tables and slots to their original
member entries. Scalar tables reach `800642E4`/`8006434C`/`80064318` raw copying,
string tables reach `80071B58`/`80071BC4`, and the object shallow-assignment slot
reaches `8006A81C`. Their implementations and limits are described in
[IGB_FIELD_MEMBER_COPY.md](IGB_FIELD_MEMBER_COPY.md). Known `800643A0` leaves
perform no copy. Other selected bodies, including memory entry `80062824`,
remain explicit operations until recovered. A scalar raw copy cannot replace
an ownership-aware string, object or memory member operation.

Every original object virtual loads its physical vtable when selected. A
pending callback retains its selection and arguments; completing it releases
the callback before the next original read, so teardown mutations remain
visible. Physical field bindings are validated rather than manufacturing host
pointers from guest addresses. Unknown canonical state, represented null state,
unavailable bytes and expired physical bindings remain distinct. Faults and
recursive entry invalidate the continuation. Completed invocations are
idempotent. Abandoned or failed allocated destinations remain retained by the
services as unfinished work; this retention does not claim a guest destructor
has run.

## Focused validation and limits

`igb_reflected_clone_tests.cpp` covers all 256 policy-byte values at each of the
three policy offsets, original mode-specific call order, unsupported and null
results, live vtables, captured metadata and registry identities, delayed
base-global binding, signed counts and wrapped physical indexing. It also covers
pending member operations, callback teardown mutations, invalid physical
headers/bindings, callback faults, recursive entry, idempotence and unfinished
allocation retention.

Adapter tests exercise delayed source-allocation binding, known BLR hooks,
metadata forwarding, exact raw-copy slots, unknown delegation and an explicitly
unrecovered memory member. A separate zero-field fixture composes actual
`8006546C` and `800667F4` with prepared allocation and object virtual endpoints,
checking its physical header and instance publication. That fixture qualifies
caller/initializer composition, not complete class registration or the bodies
of its prepared virtuals.

The memory-field and unsigned-byte tables now expose their verified object
hooks `+14`, `+18` and `+48` through the same adapter. Focused tests compose
the member passes in modes zero, one and two with native initialization,
metadata lookup and member copying. The unsigned-byte table's `+54` points
directly to `800680B4`; its dispatch introduces neither a class-registration
wrapper nor an enabled/disabled core test.

Measured build, original-comparison and cold-start results belong in
[VALIDATION.md](VALIDATION.md). This module does not establish complete startup,
object serialization, rendering, audio, gameplay or a playable native game.
