# Common bone-selector traversal

`BoneSelectContext` implements the prepared common
`igModelViewMatrixBoneSelect` callback. It consumes an already-published actor
bone array, computes the selected model-view matrix, and retains that attribute
while an explicit child traversal runs. It does not report successful geometry
rendering when the child service is missing.

The source is the supplied USA GameCube executable, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original bytes, disassembly and
instruction probes remain outside the tracked tree. The public implementation
and tests contain native code and synthetic data.

## Original scope and order

The type factory is `801BAD30`, registration is `801BAF80`, and the type has
size `0x24`. Its `_matrixIndex` field is a signed 32-bit value at object `+20`;
metadata at `80560494` and default registration at `801BB050` establish the
default `-1`. The common traversal registration at `801DB310` selects callback
`801DEE78`. This API represents entry into that callback; inherited node flags
and general traversal dispatch remain the caller's responsibility.

The callback executes these steps:

1. Acquire the next reusable model-view attribute from context `+48`, growing
   the pool if necessary, and increment its cursor. This precedes both gates.
2. Read the top bone-array pointer from context `+F8`. Return zero for a missing
   pointer or a signed negative selector index, without visiting children.
3. Compute `bone[0] * bone[index]` through `80128690`, set the attribute, and
   push it through `801D4F14`.
4. Traverse the selector's children through `8020B274`, then pop the model-view
   attribute by type through `801D505C`.
5. Return two only when the child traversal returned exactly two; map every
   other completed child result to zero.

The pool prefix is `801DEE8C..801DEEC8`; the pointer and signed-index gates
are `801DEECC..801DEF1C`; composition is `801DEF20..801DEF2C`; the remaining
attribute/child/pop/return sequence ends at `801DEF7C`. Index zero in this
specific common callback squares `bone[0]`. It does not copy the root matrix.

`original_matrix_multiply` supplies the existing full 4-by-4 arithmetic:
one rounded binary32 product followed by three ordered fused multiply-adds
per output lane. The selector adds no inverse bind, inverse view, projection,
perspective divide, or vertex deformation stage. It also does not multiply
the previously effective model-view attribute into the result.

## The actor owns the source scope

Actor traversal `801DCF30` publishes the source arrays only inside its render
branch. At `801DD128..801DD18C`, actor `cache_used` at `+79` selects either its
private bone array `+24` or the combiner's bone array through virtual `+6C`,
then pushes that pointer to context `+F8`. Separate joint/blend matrices go to
`+FC`; they are not selector inputs. Appearance traversal runs while these
stacks are live, and the actor pops them after it returns.

Appearance `801D3764` traverses its main skin, additional skins, and then
attached nodes from `+14` in list order. The attached selectors therefore use
the same chosen actor bone-array scope as that appearance's body. A prepared
caller must follow this ownership choice; private actor query matrices are
not automatically the current rendered bones.

In the supported hierarchy, bone zero contains the actor-entry model-view and
actor transform; positive bones exclude that root. The known hand attachment
bones are helpers with no joint index, so a skin palette is unsuitable.
`begin` consumes both required matrices immediately. Later shared-combiner
updates cannot change a paused traversal's selected attribute.

## Native ownership and continuation

An immutable shared `ActorBoneSelector` retains its actual child node owners.
`begin(selector, bone_array)` consumes a pool slot and executes the gates and
matrix/push prefix. `nullopt` means an absent original bone pointer; an engaged
span means a supplied array. A successful non-skipped entry returns a move-only
`BoneSelectTraversal`, with the pushed attribute visible through
`model_view()`.

`resume` invokes the actual child-list service. An absent service or its
`nullopt` result produces `children_required` and retains the attribute and
cursor. The first invoked callable is captured and used on retries; replacing
the argument later cannot replace a service that already performed effects.
That callable owns its own child-list cursor and completed effects. A returned
integer allows one pop and the exact original return mapping. Re-inspecting a
completed traversal returns its stored result without invoking children again.

Nested traversal is bounded and LIFO. A child begins inside its currently
executing parent callback. If it pauses, resumption must re-enter that retained
parent callback, which resumes its child handle. Direct external resumption of
a nested child, recursive resumption of the same callback, a second unrelated
entry, and completion over an unfinished child are rejected. Nested matrix
composition still uses that child's supplied root and bone; its pushed
attribute replaces the effective matrix until the child completes.

Exceptions fault the context and retain its uncertain stack and effects. They
do not pop attributes or replay callbacks. `reset()` is an explicit host
abandonment boundary: it invalidates old handles, releases the context's pool
and pending owners, and restores only the optional prepared base attribute.
It does not claim that an original synchronous callback returned. Reset and
context movement are forbidden during an executing child callback; reset also
guards reentry while releasing owners. Dropping a pending handle leaves its
work retained by the context until reset.

An idle `rewind_pool()` retains attribute identities and resets only the pool
cursor. A later traversal may overwrite that same reusable attribute. Shared
observations of the attribute are consequently live observations, not immutable
draw snapshots. Explicit reset detaches the old pool; separately retained
attribute owners remain alive without being reused by that context.

The native host bounds are 65,536 pooled attributes and 64 nested selectors.
Nonnegative indices must fit the supplied array, and the existing matrix
helper rejects nonfinite inputs or results. These are checked host boundaries,
not recovered original rejection branches. If such a numeric/index failure
occurs after entry, the already-consumed pool cursor is preserved and the
context requires reset.

## Validation and remaining boundaries

The asset-free suite passes 123 checks against the rebuilt Release core. It
covers noncommuting root/bone composition, zero-index
root squaring, skipped-entry allocation order, original return mapping, stable
attribute identity, explicit pool reuse, missing and retained child services,
nested pauses and restoration, ownership moves, abandonment, fault/no-replay
behavior, malformed numeric/index inputs, both host bounds, and callback-owner
destruction reentry during reset.

The independent ignored original-instruction corpus contains 144 cases. It
executes 8,480 scalar instructions from `801DEE78` and 3,584 raw paired-single
matrix instructions across 64 calls to `80128690`. Prepared hooks supply one
available pool entry, the attribute setter, push/pop operations, and completed
child results. Cases vary missing and nested source-pointer stacks, signed
indices, return values, and seeded full noncommuting matrices. The arithmetic
inputs avoid the separately documented extreme NI underflow uncertainty.
The native test runner, linked only to the rebuilt Release core, matches all
144 cases and all 1,024 computed matrix words with zero mismatches. It also
matches completed return values, pool cursor, child-call count and final stack
depth. The native input starts at the already-resolved TOP/F8 pointer; original
source-stack selection remains independently checked by the guest probe.

This slice does not implement a scene scheduler, generic child renderer, full
allocator/default materialization, or compiler. Compiler callback `801E0880`
writes the selector index into compiled transform `+08` (`_parentIdx`), whereas
late compiled traversal `801E2824` reads `+20` (`_boneIndex`). The mapping between
those distinct fields is still required before claiming compiled traversal
equivalence, including its behavior for zero or negative selector indices.

Relevant implementations are bone_select.hpp,
bone_select.cpp, and
bone_select_tests.cpp. The supplied-data probe
and reproducible native comparison command remain in ignored
`work/v33-bone-select-oracle.py`; the public executable exposes an asset-free
`--oracle` word protocol for that comparison.
