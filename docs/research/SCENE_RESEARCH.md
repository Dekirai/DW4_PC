# Original static scene research

This document records the USA executable and local IGB evidence behind the
native scene inspection profile. It establishes stored graph traversal and
placement of supported static geometry. It does not establish game scene
activation, camera behavior, collision, lighting, or a matching original game
frame. Original resource contents and disassembly remain in ignored `work/`.

The primary executable is the extracted `sys/main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877` (`GDJEB2`). Addresses below are guest
virtual addresses, not host pointers or file offsets. Field positions were
cross-checked against little-endian original serialized objects using the
native bounded IGB reader. The implementation is independently written; no
Alchemy SDK or original machine code is linked into the application.

## Object fields and graph ownership

| Object | Serialized slot | Meaning | Executable evidence |
| --- | ---: | --- | --- |
| `igInfo` | 3 / 4 | `_directory` / `_resolveState` | Names and offsets at `0x8055d404`; runtime offsets `0x0c` / `0x10` |
| `igSceneInfo` | 5 | `_sceneGraph`, explicit root node | Field table `0x804ad3dc`, runtime offset `0x14` |
| `igSceneInfo` | 6 / 7 | `_textures` / `_cameras` | Same table, runtime offsets `0x18` / `0x1c` |
| `igSceneInfo` | 8 / 9 | `_animationBegin` / `_animationEnd` | Same table, runtime offsets `0x20` / `0x28` |
| `igSceneInfo` | 10 / 11 | `_upVector` / `_sceneGraphList` | Same table, runtime offsets `0x30` / `0x3c` |
| `igNode` | 3 / 4 | `_bound` / `_parentList` | Table `0x804ade78`, runtime offsets `0x0c` / `0x10` |
| `igNode` | 5 / 6 | `_flags` / `_handle` | Same table, runtime offsets `0x14` / `0x18` |
| `igGroup` | 7 | `_childList`, an `igNodeList` | Table `0x8056061c`, runtime offset `0x1c` |
| `igTransform` | 8 / 9 | `_m` / `_target` | Table `0x804abb40`, runtime offsets `0x20` / `0x60` |
| `igTransform` | 10 / 11 | `_mvm` / `_transformInput` | Same table, runtime offsets `0x64` / `0x68` |
| `igAttrSet` | 8 / 9 | `_attributes` / `_trigger` | Table `0x80560880`, runtime offsets `0x20` / `0x24` |
| `igSwitch` | 8 | `_childMask`, an `igBitMask` | Table `0x805601b8`, runtime offset `0x20` |

`igGeometry` inherits `igAttrSet`; its mesh descriptors are attributes in slot
8, not children in slot 7. A graph edge is a child-list element, not an arbitrary
object reference. Bounds, metadata, images, and attributes are not scene
children. Serialized parent lists and runtime handles may be omitted; the
inspection traversal follows explicit child edges from the chosen root.

The caller selects a specific `igSceneInfo` and its slot-5 root. Multiple
descriptors within a packed resource are separate exports. Slot 11 is recorded
as a separate field, not merged with slot 5 or treated as a second collection
of visible roots. The map candidate below has an empty slot-11 list.

The `igInfo` slot-4 boolean is **resolve state**, not visibility. Similarly,
`igAttrSet::_trigger == false` does not hide its children or cancel inherited
attribute state. Neither serialized boolean should become a general enabled
checkbox based on its position alone.

## Ordered traversal and shared nodes

The base traversal registration at `0x8020aee8` checks metadata inheritance
through `0x80065bdc`. An `igGroup` gets handler `0x8020b274`; a base `igNode`
gets `0x8020b26c`, which returns without drawing or traversing. The group
handler reads the child list, visits its elements in order through
`0x8020b06c`, and respects traversal stop results.

The game-specific `beGroupKeep` has the same `0x20` object size and inherited
fields as `igGroup`. Its constructor at `0x802d672c`, registration at
`0x802d68e4`, and vtable at `0x804d6c84` add no scene behavior; the vtable
matches `igGroup` at `0x804b49cc` except for its metadata getter. It receives
the ordinary group handler through the inheritance registration. This is a
verified specific subclass, not permission to flatten arbitrary derived nodes.

Shared nodes are real instances: different incoming paths may place the same
geometry at different locations and under different materials. Detect cycles
on the current ancestor path. A global visited set would incorrectly discard
repeated map pieces. Bounds must include transformed vertices from every
emitted instance. Preserve the root-to-owner path for material resolution.

## Transform convention

The original point transform at `0x8012d498` uses the flat matrix as:

```text
x' = x*m[0] + y*m[4] + z*m[8]  + m[12]
y' = x*m[1] + y*m[5] + z*m[9]  + m[13]
z' = x*m[2] + y*m[6] + z*m[10] + m[14]
```

These are column-major matrices acting on column vectors. Keep the original
16-float order; do not transpose the stored matrix. The paired-single matrix
multiply at `0x80128690`, called with destination, A, B, computes host `B*A`.
That operation was independently checked with noncommuting synthetic matrices
during [animation pose research](ANIMATION_POSE.md).

The transform render handler at `0x801de28c` calls `0x801dc85c`, then
`0x801dcdf8` to traverse children and restore the matrix stack. At
`0x801dc8a8`, target zero selects the current model-view matrix stack. If a
parent matrix is present, `0x801dc970` through `0x801dc97c` calls the multiply
with the local matrix at node offset `0x20` as A and the stack top as B.
The result is therefore **parent * local**. A missing stack top copies the
local matrix. Targets 1–8 and 9–16 select other matrix state and are outside
the static positional profile.

Before composition, the handler invokes the node's update method. The
`igTransform` update at `0x801d73a8` calls `_transformInput` when non-null and
writes `_m`; a non-null source cannot be ignored as if `_m` were a final
static transform. Accept target zero, null input, and finite affine matrices
only. A runtime `_mvm` cache is not an additional stored parent transform.

The host uses bounded double-precision composition and finite float output.
This is an arithmetic interpretation of the recovered operations, not a claim
of bit-exact PowerPC paired-single execution.

## Flags, attributes, and geometry emission

The named enum table at `0x804addfc` and its values at `0x804ade20` identify:

| Bit | Name |
| ---: | --- |
| `0x01` | `HAS_LIGHT` |
| `0x02` | `HAS_TRANSFORM` |
| `0x04` | `USE_RENDER_LIST` |
| `0x08` | `USE_RENDER_LIST_DELETE_ORIGINAL` |
| `0x10` | `IS_COLLIDABLE` |
| `0x20` | `IS_INVISIBLE` |
| `0x40` | `IS_DYNAMIC` |

The frequently stored values 2, 16, 20, and 28 are not disabled-state values.
For stored position inspection, the low five bits do not suppress geometry.
Flags 4 and 8 select render-list caching: `0x8020579c` tests `0x0c`, and the
following branch retains the same attribute list in a render-list wrapper.
Lighting, collision, and this cache optimization are not implemented by a
plain scene inspector. Reject dynamic and unknown flags. Invisible flags on
unverified node types must not be generalized into an inherited visibility
rule.

The normal `igAttrSet` handler `0x801dd9bc` pushes each attribute onto its
type/unit state stack, then tests `_trigger` at `0x801dda80`. A true trigger
dispatches state; either value proceeds to children at `0x801ddab4`, then pops attributes in
reverse order. Thus a lower texture binding can coexist with a higher texture
state. A material reader must still validate every applicable path scope and
honor intervening overrides; this evidence does not justify choosing an image
by name or skipping unsupported texture state.

Type registration `0x801c8c48` associates `igAttrSet` with global `r13+0x478`;
common registration `0x801db310` maps that type to callback record `0x80415260`,
whose function is `0x801dd9bc`. The separate `igOverrideAttrSet` type is registered
at `0x801b6a20` in `r13-0x36c` and maps to record `0x80415258`, function
`0x801dd7c0`. That override handler additionally sets and restores an explicit
attribute-stack selection index. Its behavior is outside the normal AttrSet
inspection profile.

The geometry handler at `0x801ddb84` returns immediately when its trigger is
false (`0x801ddb98`) or its invisible flag is set (`0x801ddbb4`). Otherwise it
dispatches the geometry's attribute list through `0x80205684`. This handler
does not traverse the geometry's child list. Empty or inactive geometry is not
an excuse to infer a different child traversal.

Geometry attribute lists can contain several meshes. At `0x80205848`, lists
of length one and two have explicit append paths; `0x80205930` appends the
entire list for other lengths. The cache path retains that full list too.
Multiple `igGeometryAttr1_5` descriptors are therefore emitted in serialized
order, rather than selecting the last descriptor. An inspector should reject
unsupported attribute behavior that intervenes in this list, not discard the
remaining meshes silently.

Attribute validation also protects position composition. Model-view,
projection, bone, blend-matrix, and other position-changing attributes can
alter the matrix stacks independently of `igTransform` nodes. Do not ignore
them merely because they are in an attribute list. The accepted static profile
uses a specific non-position-changing attribute allowlist; texture and visual
material reconstruction have their own narrower support checks.

## Switches and level of detail

Switch handler `0x801dc44c` reads `_childMask` and visits child i only when
`word[i/32] & (1u << (i%32))` is nonzero. It uses all selected bits, not a
single first child. Serialized mask words are little-endian in the measured
IGB profile. `igBitMask` slots 2 and 3 are word count and capacity; slot 4 is
the memory reference and slot 5 is the bit count. Validate all counts and
memory sizes before use. A nonempty switch with a null mask is unsupported;
the original handler does not provide a null-mask fallback.

`igLod` is different. Its field table at `0x804aecfc` identifies `_rangeList`,
`_lastRange`, `_lastIndex`, `_lastLower`, `_lastUpper`, `_center`, and
`_hysteresisList` at slots 8–14. Handler `0x801de598` transforms its center
using current matrix state, computes squared distance, and asks `0x801f799c`
to choose one range child. That method uses and updates range/hysteresis
caches. Reject LOD in the initial static profile: traversing every child or
treating a serialized cached index as a final runtime selection is incorrect.

## Original map candidate and inspection boundary

The resource catalog maps `dng101b1.igb` to `ndmw.afs` entry 80. Its
`beFileListInfo` points to the packed data in entry 179. In that document,
`igSceneInfo` reference **3372**, list index **6**, points to root switch
**2877**, named **BG02**. The root's only child is `igAttrSet` **3342**.
This subtree contains static room geometry, materials, and images within the
same IGB; it requires no external-resource lookup to inspect its positions.

The stored root mask is **zero**: `igBitMask` 2280 has one bit and its memory
3107 contains a zero word. Exact stored traversal produces no geometry. The
engine's later activation of this map is not yet reconstructed. A read-only
scan found no geometry-containing switch-root scene export with a nonzero
stored mask among `ndmw.afs` entries 177–972 or any `area.afs` entry.
This observation must not become an automatic all-switches-enabled policy.

For a useful explicit inspection, select **root child 0** for this export.
That control replaces only the selected root switch's mask; it preserves
nested switches and reports the override in the result. It does not modify
the original file or claim the same activation state as gameplay. There are
no further switches or dynamic transform inputs below this particular child.
The subtree contains 219 nonidentity transforms, with repeated paths to shared
geometry. For example, transform 3269 translates by `(-180, 0, 120)`.

The scene's attribute types are `igGeometryAttr1_5`, `igTextureBindAttr`,
`igTextureStateAttr`, `igMaterialAttr`, `igShadeModelAttr`,
`igBlendFunctionAttr`, and `igBlendStateAttr`. Two original geometry nodes
contain six and five mesh attributes respectively, which provides a concrete
multi-mesh emission case. Texture state and bindings are split across ancestor
scopes; a same-scope-only material resolver correctly reports that narrower
profile unsupported even when solid scene geometry can be inspected.

The final explicit root-child-0 integration produces:

| Measurement | Result |
| --- | ---: |
| Node visits, including repeated instance paths | 462 |
| Nonidentity transform nodes | 219 |
| Emitted mesh instances | 235 |
| Distinct geometry attributes | 29 |
| World vertices | 31,886 |
| Triangles | 26,352 |

The approximate world bounds are `(-520.028, -7.99794, -580)` through
`(480, 70.1, 620)`. These are the selected BG02 export's bounds, not a claim
that every active dungeon object has been assembled.

An independent ignored probe exports raw node fields, child edges, matrices,
source position buffers, and native instance results. A separate Python walk
reconstructs the selected graph and interprets the bounded actual instruction
sequences at `0x80128690` and `0x8012d498`, rounding arithmetic to float32.
The point routine preserves the original y-term multiply followed by x and z
fused additions and translation, rather than mirroring the native loop order.
Affine output w equals one, so the original perspective-division branch is
not entered. Unsupported instructions abort the probe.

All 235 owner/geometry/path tuples, 462 visits, and 219 transforms matched.
The maximum matrix component difference was zero for this map; the maximum
world-position component difference over all 31,886 vertices was
`9.5367431640625e-7`. This checks original graph placement and recovered
arithmetic. It does not model every PowerPC floating-point exception/estimate
detail or substitute for a captured original frame.

Local verification artifacts are `work/scene_original_compare.cpp`,
`work/scene_compare_original.py`, and `work/scene-original-comparison.txt`.
They are intentionally not distributed because they consume and report
original asset data. The graph export is read-only and the inspection option
is supplied explicitly; the original archive is never modified.

The 1080p solid and textured screenshots were also reviewed for obvious
structural duplication. No two emitted instances have the same geometry and
world transform. Comparing triangle vertices rounded to `1e-4` found no exact
duplicate world triangles. The original meshes do contain overlapping
coplanar surfaces: for example, triangles 127 and 477 of geometry 3282 lie
at y=70 within the same instance and overlap by approximately 14.52 square
units. A bounded plane/group and spatial-cell probe found 2,282 positive-area
overlap pairs, mostly with the same winding. This is evidence of overlapping
source surfaces, not an extra graph instance added by the native traversal.

The screenshots retain visible stippling and dark areas. The viewer uses
basic source-over texture blending, depth writes, and no face culling; it does
not reproduce original pass selection, depth policy, or transparency ordering.
Coplanar source surfaces can contribute to the stippling, but the exact
original rendering difference is not established without a captured game
frame. No additional structural emission defect was identified in this review.

## Validation boundaries

Synthetic tests should cover noncommuting parent/local transforms, a shared
node reached along different paths, cycles, invalid matrix fields, dynamic
inputs, flag and trigger behavior, masked child selection across a 32-bit word
boundary, ordered multiple mesh attributes, and bounded malformed lists.
Keep decoded original map integration and screenshots separate from asset-free
CI. A successful solid or wireframe scene inspection demonstrates native
static placement; it does not demonstrate the original renderer, scene scripts,
full environment assembly, original camera, or a playable loaded level.
