# Native inverse-bind joint inspection

The `skinning` module reads a verified original skin, skeleton hierarchy,
matrix palette and single-influence vertex buffers. It recovers joint frames
from the stored inverse-joint matrices and supports a user-authored local bone
rotation. The native viewer can display the result while retaining the original
topology, UVs and supported texture binding.

This is **manual inverse-bind inspection**. It does not decode original game
animation, recover an idle pose, execute the animation combiner, or establish
actor placement in a scene. Reset removes the user rotation and reconstructs
the original stored mesh positions.

Version 0.6 also exposes `original_bone_transform()` and
`evaluate_bone_transforms()` for a complete bone-local transform array. This
separate path includes helper ancestors and supports the checked original
animation loader. See [ANIMATIONS.md](ANIMATIONS.md) and the original arithmetic
evidence in [ANIMATION_POSE.md](ANIMATION_POSE.md).

## Public API

```cpp
auto info = dw4::inspect_igb_skinning(document, geometry_reference);
if (info.supported) {
    auto skin = dw4::decode_igb_skinning(document, geometry_reference);
    auto baseline = dw4::evaluate_skinning(skin);
    auto manual = dw4::evaluate_skinning(
        skin, dw4::BoneRotation{bone_index, {0, 0, 30}});
}
```

`SkinningInfo` reports support or an explicit diagnostic, source references,
skin/skeleton names, geometry/vertex/bone/joint counts and provenance.
`SkinningData` owns the original positions, decoded influences, palette,
complete bone list and joint matrices. `SkinPose` returns posed positions and
joint origins in joint-index order. The data has no SDL, ImGui or graphics API
dependency.

`SkinBone::parent` is a bone ordinal. `SkinBone::joint_index` is the separate
matrix-array index, with `-1` identifying a helper without a stored inverse-joint
matrix. `SkinJoint::parent_joint` is the nearest ancestor that has a matrix.
The viewer can list all bone names but permits rotation only for joint-bearing
bones. It does not assign invented bind frames to helpers.

All object references belong to the same `IgbDocument`. Archive and entry
identity remain the caller's responsibility. The inspection call performs
checked decoding; it is not a constant-cost metadata query. Unsupported data
does not prevent the independent stored-mesh or texture viewer from operating.

## Explicit graph association

The current profile requires this checked chain:

1. Exactly one `igGeometry` owns the selected `igGeometryAttr1_5` through its
   slot-8 `igAttrList`.
2. Exactly one direct `igBlendMatrixSelect` parent contains that geometry in its
   slot-7 `igNodeList`.
3. The selector has exactly one enabled `igVertexBlendStateAttr`; child blend
   overrides are unsupported. Its slot-11 `_skeletonTransform` and slot-12
   `_skeletonInverseTransform` must be identity within `1e-6` per element.
4. Exactly one `igSkin` references that selector through slot 3, `_skinnedGraph`.
5. Exactly one `igAnimationDatabase` explicitly includes that skin in its slot-7
   `_skinList`. The same database's slot-5 `_skeletonList` must contain exactly
   one local `igSkeleton`.

This is a container association with an unambiguous skeleton. The implementation
never chooses the first skeleton in a document or matches its name. Actors
whose appearance references the skin are recorded only as provenance. In the
investigated Agumon data, actor animation-system/database fields are null; no
direct actor-to-skeleton pointer is invented.

The selector's broader transform multiplication order remains unverified,
which is why nonidentity transforms are rejected. Multiple skin owners,
databases or database skeletons remain explicit unsupported cases.

## Vertex influences and matrix indices

`igVertexArray1_1` slot 7 is `_blendWeights`; slot 8 is `_blendIndices`. The
original DOL field-name table is at guest address `0x80488974`, with runtime
offsets at `0x804889a4`. This legacy serialized layout is distinct from other
vertex APIs in the same executable.

The decoder currently accepts format `0x10111` or `0x10113`, zero extra
vertex state, and a null point-sprite memory reference. It requires exactly
one little-endian float per vertex in slot 7 and one unsigned byte per vertex
in slot 8. Every weight must be finite and **exactly 1**. Weights are not
renormalized, and unsupported influence counts are not truncated.

The byte selects an entry of the selector's slot-10 `igIntList`, identified
as `_blendMatrixIndices` by the DOL table at guest `0x804b1418`. The palette
stores little-endian signed-width 32-bit values; accepted values must be
nonnegative joint-array indices. Its memory must match capacity exactly,
and every palette entry is range checked, including unused entries.

Each `igSkeletonBoneInfo` supplies `_parentIdx` in slot 3 and `_bmIdx` in
slot 4. `_bmIdx` associates the bone with a joint-array index; `-1` means no
joint matrix. Every joint must have exactly one bone, and every parent chain
must be in range and acyclic. Bone ordinals, byte palette indices and joint
indices are kept separate throughout decoding and evaluation.

Read-only screening of all original vertex arrays found:

| Archive | IGB files | Vertex arrays | Single-influence arrays | Two-influence arrays |
| --- | ---: | ---: | ---: | ---: |
| `area.afs` | 1,379 | 152,896 | 0 | 0 |
| `ndmw.afs` | 894 | 45,172 | 456 | 20 |

All 456 single-influence arrays had exactly unit weights and the accepted
4-byte/1-byte strides. The 20 other skinned arrays use format `0x10223`, with
8 weight bytes and 2 index bytes per vertex; that profile remains unsupported.
Unskinned arrays have no influence buffers and are not classified as corrupt.

## Matrix meaning and manual evaluation

`igSkeleton` slot 4 is `_boneInfoList`, slot 5 is `_invJointArray`, and slot 6
is `_jointCount`. The inverse array must contain exactly 64 bytes per joint,
decoded as sixteen little-endian floats. The stored matrix order is retained
in host column-major matrices acting on column vectors, with translation at
elements 12, 13 and 14.

The original DOL point-transform routine at guest addresses
`0x8012d498` through `0x8012d598` independently establishes this arithmetic:

```text
x' = x*m[0] + y*m[4] + z*m[8]  + m[12]
y' = x*m[1] + y*m[5] + z*m[9]  + m[13]
z' = x*m[2] + y*m[6] + z*m[10] + m[14]
```

Matrices must be finite and exactly affine: elements 3, 7 and 11 are zero,
and element 15 is one. The linear part is inverted with scaled pivoting;
singular or numerically unstable pivots are rejected. Both multiplication
orders must cancel to identity within `1e-6` per element.

For stored inverse-joint matrix `I[j]`, derive bind-world matrix
`B[j] = inverse(I[j])`. For a root joint, local bind matrix `L[j] = B[j]`.
For joint parent `p`, use `L[j] = I[p] * B[j]`. Helper bones are skipped when
finding the parent joint because they have no recovered inverse-joint frame.

A user delta `D` is applied to the selected joint as `L[j] * D`; all other
joint local matrices remain unchanged. Parent accumulation produces posed
world matrices `P[j]`. The vertex's inspection transform is `P[j] * I[j]`.
The delta applies X, then Y, then Z rotations in the recovered local frame,
represented as `Rz * Ry * Rx` under the host column-vector convention.

With no delta, the hierarchy reconstructs the original bind-world matrices
and their inverse products cancel. The evaluator also checks public data for
consistent local/world/parent matrices, inverse cancellation, palette mapping,
source counts and finite outputs. Mutating a cached local matrix cannot
silently make Reset deform the model. Evaluation never modifies source data.

The skeleton's separate float3 offset array is deliberately unused. Accumulating
those offsets does not reproduce the inverse-joint reference space in Agumon.
Serialized animation-combiner caches are also not treated as an active pose.
Primary evidence and these limitations are documented in
[SKINNING_RESEARCH.md](SKINNING_RESEARCH.md).

## Original Agumon validation

For `_common/p_agumon.igb`, `ndmw.afs` entry 850, the high-detail mesh is
list index 2 / geometry reference 5665. Its checked chain is geometry 5665,
owner 409, selector 5481, skin 404, database 810, skeleton 701. The skin has
1,371 vertices, 25 bone entries, 20 joint matrices and a 24-entry palette.

For example, vertex palette index 7 selects joint 5, which belongs to bone
ordinal 8, `LArm01`. This distinction matters: applying the byte directly as
a bone ordinal would assign the wrong matrix.

The inverse-bind Reset evaluation reproduced every stored coordinate exactly
for all three Agumon geometries. An independent 30-degree local Z rotation of
bone 8 moved exactly the vertices assigned to that joint or its descendants:

| Appearance / geometry | Vertices | Changed in the arm subtree | Changed outside the subtree |
| --- | ---: | ---: | ---: |
| Low / 262 | 570 | 113 | 0 |
| Shadow / 413 | 61 | 10 | 0 |
| High / 5665 | 1,371 | 252 | 0 |

Changes were detected above `1e-5` per component. This verifies the native
palette mapping and hierarchy propagation. It is a manual pose, not a frame
decoded from an original animation. The shadow's influence data is supported
independently of its unsupported texture-material profile.

A separate complete `ndmw.afs` integration pass screened 45,172 geometries
across 894 IGB files, then tested all 476 with both skin buffers present.
It supported 290 geometries containing 222,694 vertices. The largest Reset
coordinate error was approximately `1.97209e-15`, below the `1e-5` check.
Every supported geometry also returned finite positions with the correct count
under a manual local X/Y/Z rotation of 15/20/30 degrees at its first weighted
joint. There were no document or pose failures.

The other candidates reported 133 ambiguous/missing unique skin owners,
32 matrices outside the exact affine profile, 20 unsupported influence formats,
and one missing/ambiguous blend-enable state. Those are conservative supported-
profile boundaries, not claims that the original assets are malformed.

## Limits and reproducible tests

Limits are 1,048,576 vertices, 1,024 bones, 256 joints, 256 palette entries,
and 4,194,304 inspected graph-list elements per decode. Existing IGB and mesh
input/count limits also apply. Manual rotations must be finite and between
minus and plus 360 degrees per axis; the UI may offer a narrower range.
Pose matrices and final float positions are checked for finite range.

Run the synthetic checks without original assets:

```powershell
.\build\Release\dw4_skinning_tests.exe
```

Generated IGB fixtures cover the full graph and byte layouts, distinct
bone/joint/palette indices, helper ancestry, reference Reset, parent rotation,
noncommuting child-frame rotation, invalid influence sizes/weights/indices,
ambiguous ownership and skeletons, cycles, allocation caps, malformed matrices,
and inconsistent public pose data. Checks remain active in Release builds.

Require the original high-detail Agumon skin to decode and pass Reset:

```powershell
.\build\Release\dw4_skinning_tests.exe 'E:\path\files\ndmw.afs' 850 5665
```

Omit the last argument to report all geometries in that entry, including
unsupported diagnostics. For a native manual inspection, load entry 850,
select mesh index 2 and a supported rendering mode, then enable the rig and
rotate bone 8, `LArm01`. Reset returns to the stored geometry. The joint overlay
is an inspection overlay, not original game rendering.

Original input files, probe logs, extracted buffers and screenshots remain
outside Git. The published source contains independently written C++ and
synthetic fixtures; original game data is an optional local integration input.
