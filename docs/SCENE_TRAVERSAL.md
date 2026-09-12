# Common attachment child traversal

`SceneTraversalContext` executes the prepared normal common node callbacks
beneath an actual `BoneSelectChildren` scope. It follows retained
`ActorSceneNode` owners in source child-list order. Repeated DAG edges are
separate visits, so the original two-hand weapon paths can share a geometry
owner while producing separate submissions with different model-view matrices.
No mesh is flattened into world space and no replacement weapon actor is made.

## Original evidence and supported callbacks

The supplied USA `main.dol` has SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. The implementation follows:

| Original entry | Represented behavior |
| --- | --- |
| `8020B06C` | Explicit context previsit policy, registered normal dispatch, previsit return handling |
| `8020B26C` | Normal base `igNode` returns zero |
| `8020B274` | Ordered children; child return one stops siblings and becomes zero, two stops and propagates |
| `801DD9BC` | Normal `igAttrSet` pushes source attributes, optionally flushes changed state, traverses children, then pops in reverse order |
| `801DDB84` | Normal geometry trigger/invisible gates, matrix-list stack gate, effective blend-state/root substitution, submission |
| `801DE28C`, `801DC85C`, `801DCDF8` | Normal target-zero transform update/composition, children and scope restoration |
| `801D73A8` | Null transform input leaves the stored local matrix unchanged |
| `801D8D98` | Transform context time query uses the time stack, a cached clock, or initializes that cache |
| `80205684` | Geometry enters the existing render submission layer; submission is not GPU completion |

The normal attribute callback is `801DD9BC`. The separate override callback is
`801DD7C0`. `igOverrideAttrSet` is an explicit registered-handler boundary in
this implementation, not an alias of normal attribute-set traversal.

The represented normal attribute stack types are `igShadeModelAttr`,
`igTextureStateAttr`, `igMaterialAttr`, `igTextureBindAttr`,
`igVertexBlendStateAttr` and `igVertexBlendMatrixListAttr`. Other local types
require their original stack/effect implementation. In particular, an
unimplemented model-view attribute cannot silently leave the matrix unchanged.
Geometry's own ordered attribute list is retained separately for the submission
service; it is neither pushed as a normal attribute scope nor reduced to a
single assumed mesh descriptor.

The prepared attribute-manager profile has no installed stack-pop callbacks.
This matches freshly registered stacks: `801D3D2C` writes null to stack `+1C`
at `801D3F5C`. A caller that installs custom pop callbacks must provide their
implementation before using this profile. The traversal tracks effective normal
owners and restores its source-order pushes; the renderer still owns original
changed-state execution, render-list wrappers and GPU state application.

## Retained calls and consumed submissions

Begin the scene traversal inside the actual selector child callback, using that
callback's `BoneSelectContext` and shared selector. The scene context copies its
current model-view value and retains the actual selected model-view owner,
selector, node paths and immutable source documents. A move-only handle resumes
through the same context.

`SceneTraversalServices` provides separate geometry, changed-state-list,
previsit, registered-node and clock services. Each actual service invocation
retains its first supplied callable and the exact request object across pauses.
A missing service can be supplied on a later resume. A false geometry/flush
result or disengaged previsit/node/clock result preserves the current request,
scope and cursor. Completed children and submissions are not replayed.

Geometry and attribute-flush callbacks return a consumption boolean, not an
invented child return value. The geometry consumer must queue the actual render
work into its render service. Merely inspecting a request does not establish a
rendered frame. Only registered-node and previsit services return their actual
signed dispatch result. The normal geometry handler completes with zero after
its submission is consumed, as the original does.

Each `SceneSubmissionRequest` contains:

- The actual loaded node owner and ordered root-to-node owner/reference path.
- Its current binary32 model-view matrix, separate from any projection.
- The canonical source attribute-list reference and every attribute owner in
  source order, including repeated references.
- Effective represented inherited/local owners and original geometry flags.

`SceneAttributeOwner` retains the immutable source through its node owner,
canonical physical attribute reference, exact type name and signed serialized
unit. Within the context, physical reference aliases share one owner. Separate
source loads do not share ownership merely because their names and numbers
match. Effective attributes are a state description, not an asserted engine
state-execution order; source attribute-list order remains explicit separately.

An outer, already effective override can be marked on a supplied inherited
attribute. Its owner remains selected while a normal local attribute is pushed.
This does not execute entry or exit of an arbitrary override node.

While a child service pauses, normal parent attributes and transform state
remain in scope. They pop only after actual child completion, including return
two. Exceptions preserve the uncertain prefix and mark the context faulted.
Repeated fault inspection never invokes the service again. `reset()` is an
explicit host cancellation boundary: it invalidates handles and discards
unfinished state without claiming an original callback return. Reentrant resume
or reset from a service is checked.

## Required inherited graphics facts

Missing local vertex-blend attributes do **not** mean that blending is disabled.
The caller supplies the prepared context facts in `SceneGraphicsScope`:

- `inherited_vertex_blend_list_present` means the original matrix-list **stack
  count is nonzero**. `801DDBFC` tests that count. It does not test a default
  attribute owner or the attribute's list payload. Therefore an effective
  default matrix-list owner with an empty stack is valid. Any locally pushed
  unit-zero matrix-list attribute makes the stack nonempty, including while an
  outer override selects a different payload. The nonempty-list palette path
  remains an explicit checked boundary.
- `inherited_vertex_blend_enabled` supplies the effective default state when no
  represented loaded owner supersedes it. A loaded effective unit-zero
  `igVertexBlendStateAttr` supplies its actual raw nonzero byte instead.
- If blending is enabled, `bone_root_known` and `bone_root` identify the actual
  top bone-array pointer and, when non-null, a captured copy of bone zero.
  Non-null substitutes that root for geometry submission; a known null pointer
  leaves the current model-view unchanged. Unknown pointer state is checked.
- `previsit_enabled` states the prepared context callback policy. If enabled,
  traversal calls the actual supplied service before each normal node handler.
- `prepared_transform_clock` identifies an actual supplied time-stack or cached
  clock value. Otherwise transforms call the real retained clock service before
  update/composition. Zero is a valid captured time. Null transform input makes
  the returned time irrelevant to the matrix, but does not justify silently
  skipping a clock query that could initialize the original global cache.

A reset inspection context can truthfully supply disabled vertex blending,
empty matrix-list stacks and fresh null pop callbacks. The original graphics
reset explicitly disables blending at `800EA358..800EA36C`. Default attribute
manager `800C5848` snapshots actual graphics state through the attribute virtual
method; blend default `800C55FC` reads graphics byte `+464` through `800F8E24`.
That default is not a universal value for an arbitrary later gameplay frame.
The caller must identify its actual prepared baseline; this API does not infer
one from a weapon's four local material/texture attributes.

## Original weapon paths

Local original-data tests consume five attachment submissions:

| Resource | Actual path |
| --- | --- |
| `_common/w_common05.igb` | AttrSet `3` to Geometry `44` |
| `_common/w_common04.igb` | AttrSet `31` to Geometry `24`; AttrSet `35` to Transform `23` to the same Geometry `24` |
| `_common/w_common03.igb` | AttrSet `5` to Geometry `34`; AttrSet `10` to Transform `37` to the same Geometry `34` |

These parents have trigger zero, which still pushes all four ordered shade,
texture-state, material and texture-bind owners before their children. Actual
left flags two mean `HAS_TRANSFORM`; they do not disable the left branch.
Geometry flags decimal 28 are `0x1C`, including the original cache/cache-delete
and collision bits. Only `0x20` is the normal geometry invisible gate. A disabled
geometry trigger or that invisible flag returns before submission. Geometry
children are not traversed by its normal handler.

The actual left transforms have target zero and null input. Under the selector's
already pushed model-view, composition is `selected * stored_local`, using
`original_matrix_multiply`. The exact stored normal, nonzero words `250D3000`
and `A50D3000` survive; no idealized half-turn replaces them. The current scope
does not reapply actor root, inverse bind, camera, projection or a joint palette.

## Validation and remaining scope

`scene_traversal_tests` runs 41 asset-free checks, or 56 with the original data
root argument. They exercise retained geometry and clock requests, callback
identity, trigger-zero inheritance, trigger-one flush-before-children, return
one/two, previsit bypass, flags, geometry-child suppression, shared left paths,
exact local matrix words, enabled/root and disabled/override blending,
default-owner versus nonempty-stack distinction, malformed units, unsupported
matrix effects, fault retention and reset/reentrancy boundaries. Original data
stays external to asset-free CI.

The represented path is common traversal, not compiled-selector execution.
The nonempty matrix-list branch, arbitrary input/target transforms, override
node entry/exit, unimplemented registered handlers, full inherited graphics
state scheduling, render-list materialization and original GPU pixel parity
remain required. Full player/appearance scheduling, original camera and complete
gameplay remain separate acceptance requirements.
