# Agumon skeleton and inverse-joint research

These findings come from read-only native `IgbDocument` probes of the local USA
`ndmw.afs`, entry 850 (`_common/p_agumon.igb`), and its original `main.dol` on
2026-09-08. The DOL SHA-1 is
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Field names and arithmetic were
examined independently; no SDK implementation, game buffer or generated
disassembly is included here.

The data supports a bounded inverse-bind joint inspection feature. It does
**not** establish an original animation pose, actor placement or the complete
game skinning pipeline. In particular, accumulating the stored bone translations
does not reproduce the mesh's inverse-bind reference space.

## Explicit graph and skeleton association

The high appearance has the same checked geometry chain used by the texture
viewer:

```text
igGeometryAttr1_5 5665
  <- igGeometry 409, igAttrList 411
  <- igBlendMatrixSelect 5481, igNodeList 5482
  <- igSkin 404, slot 3 (_skinnedGraph)
```

`igSkin` 404 is named `HighAppearance`; slot 4 is its stored bound. The DOL name
table at guest address `0x80560234` identifies these two fields as
`_skinnedGraph` and `_bound`. The selector's slot 10 is an `igIntList` reference;
slots 11 and 12 are matrices. The field-name table at guest `0x804b1418`
identifies them as `_blendMatrixIndices`, `_skeletonTransform` and
`_skeletonInverseTransform` respectively. Both matrices are identity in all
three Agumon appearances. Their general multiplication order is not established
by that identity case; a first implementation should reject nonidentity values.

There is a concrete database association, rather than a skeleton chosen by its
name or position among document objects:

- `igAnimationDatabase` 810, named `p_agumon`, slot 7 (`_skinList`) references
  list 775, containing skins 380, 404 and 5493.
- The same database's slot 5 (`_skeletonList`) references list 778, whose sole
  member is skeleton 701, named `defaultSkeleton`.
- Slot 9 (`_combinerList`) references list 774 with four combiners. Each
  combiner's inherited slot 3 explicitly references skeleton 701.

The database's names for slots 5 through 9 are recovered from the table at guest
`0x804b2168`: `_skeletonList`, `_animationList`, `_skinList`, `_appearanceList`,
`_combinerList`. A unique containing database with exactly one skeleton is a
bounded accepted association. Multiple matching owners, databases or skeletons
require an explicit context or an unsupported result.

The document has 12 `igActor` objects. Their slot 11 points to an `igAppearance`,
whose slot 3 points to the skin. Their serialized slot 8 (`_animationSystem`)
and slot 12 (`_animationDatabase`) are all null. The actor field-name table at
guest `0x804b2b2c` identifies these fields. Thus this sample does not provide a
direct populated actor-to-animation-system link to execute. The database's
stored relationship is sufficient for asset inspection, not for choosing the
active game actor or clip.

## Bone ordinals, joint indices and vertex palette

Skeleton 701 derives from `igAnimationHierarchy`, which derives from
`igNamedObject`. It has these fields:

| Slot | Recovered field | Observed data |
| ---: | --- | --- |
| 3 | `_boneTranslationArray` | Memory 4636; 300 bytes, or 25 little-endian float triples |
| 4 | `_boneInfoList` | `igSkeletonBoneInfoList` 491; 25 members |
| 5 | `_invJointArray` | Memory 4658; 1,280 bytes, or 20 little-endian 4 by 4 float matrices |
| 6 | `_jointCount` | Signed integer 20 |

The inherited translation field is named by the pointer at guest `0x80560970`,
with runtime offset `0x0c`. The skeleton's own field-name table is at guest
`0x804ac88c`, with runtime offsets `0x10`, `0x14`, `0x18`.

Each `igSkeletonBoneInfo` has a name at slot 2 and three signed integers:

| Slot | Recovered field | Meaning established by the stored relationships |
| ---: | --- | --- |
| 3 | `_parentIdx` | Parent ordinal in the 25-member bone-info list; -1 denotes the root |
| 4 | `_bmIdx` | Joint/blend-matrix index; -1 denotes a bone without a joint matrix |
| 5 | `_flags` | Stored flags; their broader execution semantics are not recovered |

These names are present at guest `0x804ac8f4`; their runtime offsets are
`0x0c`, `0x10`, `0x14`. There is one root at bone ordinal 0. Every other parent
is a valid earlier ordinal, so this sample's graph is acyclic. Five bones have
`_bmIdx == -1`: ordinals 0, 1, 2, 11 and 15. The other 20 bones contain every
joint index from 0 through 19 exactly once. Bone count and joint count are
therefore different quantities, and a palette entry must not be treated as a
bone-list ordinal.

Examples make the mapping clear: `Back` is bone ordinal 3 / joint 0;
`chest` is bone ordinal 5 / joint 2; `Head` is ordinal 6 / joint 3; `LArm01`
is ordinal 8 / joint 5. The unnamed root has flags 64; the other 24 bones have
flags 2. These flag values are observations, not independently recovered enum
meanings.

The high selector's list 5483 contains 24 signed integer palette entries,
each in joint range `[0,19]`; repeated values are allowed. Vertex array 5621
has 1,371 vertices. Its slot 7 memory 5608 contains 1,371 float weights, all
exactly 1, and slot 8 memory 5664 contains one byte index per vertex, each below
the palette length. The DOL's legacy vertex-array field-name table at guest
`0x80488974` identifies slots 7 and 8 as `_blendWeights` and `_blendIndices`.
The reference chain is:

```text
vertex byte -> selector palette entry -> joint index
            -> unique bone-info member whose _bmIdx equals that index
            -> inverse-joint matrix at that index
```

The low and shadow appearances have the same one-weight/one-byte profile, with
570 and 61 vertices. Their selectors also contain 24 palette entries. The
portable reader must validate every step, including nested memory references,
finite weights, exact lengths and the uniqueness of the joint-to-bone mapping.
These observations do not prove other influence counts or packed formats.

## Matrix layout established by executable arithmetic

The original point-transform routine at guest `0x8012d498` through
`0x8012d598` reads a three-component point and a contiguous 16-float matrix.
Its scalar operations establish the following indexing before homogeneous
division, where `f` is the flat matrix array:

```text
x' = x*f[0] + y*f[4] + z*f[8]  + f[12]
y' = x*f[1] + y*f[5] + z*f[9]  + f[13]
z' = x*f[2] + y*f[6] + z*f[10] + f[14]
w' = x*f[3] + y*f[7] + z*f[11] + f[15]
```

This arithmetic, rather than a guessed graphics-API convention, fixes the
translation positions and axis indexing. A host matrix stored column-major and
acting on column vectors can use the same decoded flat float sequence. The
equivalent row-vector representation has the transposed mathematical matrix;
do not transpose the bytes a second time merely because of naming conventions.

All 20 Agumon inverse-joint matrices have affine last rows in that host
representation, finite elements and an invertible linear block. Their linear
blocks differ from identity by at most approximately `4e-16`; translation is
stored in elements 12 through 14. The three selectors' transform/inverse pairs
are identity. The original point routine also handles homogeneous division;
the inspection profile accepts affine matrices only.

## Stored translations are not the inverse-bind reference pose

Following the stored parent indices and adding `_boneTranslationArray` triples
places `Back` at approximately `(0,3,-1.17677033)`. Its inverse-joint matrix
instead has translation `(0,-3.3,0)`, placing the recovered bind joint at
approximately `(0,3.3,0)` after inversion. The same discrepancy occurs at every
joint: the accumulated translation positions differ from inverse-derived bind
positions by approximately `(0,-0.3,-1.17677033)`, within float rounding.

The shared difference is useful evidence for a missing root or pose context,
but its cause is not established. Applying an arbitrary compensating offset
would conceal the unresolved relationship. Neither the translation array nor
an identity rotation array is, by itself, evidence of a fully evaluated original
frame.

The first inspected animation combiner has `_cacheValid == false`, an empty
animation-state list, identity quaternion results, identity animation-cache
matrices, identity bone matrices and identity blend matrices. Its slots 7, 8,
12 and 13 are named `_resultQuaternionArray`, `_animationCacheMatrixArray`,
`_boneMatrixArray`, `_blendMatrixArray` by the table at guest `0x804b24e4`.
Those serialized cache values should not be mistaken for an active original
animation pose. See [ANIMATION_RESEARCH.md](ANIMATION_RESEARCH.md) for the separate
compressed-track boundary.

## Safe manual joint inspection derived from inverse matrices

The original inverse-joint data supplies a useful narrower operation without
pretending the original animation evaluator has been recovered. In the host
column-vector convention, let `I[j]` be the stored inverse-joint matrix and
`B[j] = inverse(I[j])` its bind-space joint transform. Follow each bone's parent
chain past bones with `_bmIdx == -1` to obtain its nearest parent joint. The
result is a joint forest: `Back` (joint 0) and `LegRoot` (joint 11) have no
parent joint, because they share nonjoint helper ancestors.

For a joint with parent `p`, derive `L[j] = inverse(B[p]) * B[j]`; for a root
joint, use `L[j] = B[j]`. A user-requested local rotation `D[j]` can then be
applied as `L[j] * D[j]`, followed by parent accumulation into `P[j]`. The
inspection skin matrix is `P[j] * I[j]`. These are host operations derived
from the recovered matrix meaning, not copied game functions.

With all user deltas equal to identity, the construction must reproduce every
stored vertex within a stated numerical tolerance. A nonzero user delta should
affect only the selected joint and its joint descendants. Existing positions,
topology and UVs remain the original inputs. Nonjoint helper bones have no
inverse-joint matrix and cannot be assigned an invented bind transform for this
operation.

This feature should be labelled manual joint or inverse-bind inspection.
Reset means zero user delta. It must not be described as original animation,
an original idle pose, or a recovered scene transform. Nonidentity selector
transforms, ambiguous ownership/skeletons and unverified influence layouts
remain explicit unsupported boundaries.

## Reproduction and scope

An independent isolated native integration executable compiled the production
skinning source and checked all three original Agumon geometries. Reset through
`evaluate_skinning` had exactly zero maximum coordinate error against stored
positions. A user-authored rotation of bone 8 (`LArm01`) by 30 degrees about
its local Z axis produced these results:

| Geometry | Vertices | Vertices assigned to selected joint or descendants | Changed vertices | Changed outside that joint subtree |
| ---: | ---: | ---: | ---: | ---: |
| 262, low | 570 | 113 | 113 | 0 |
| 413, shadow | 61 | 10 | 10 | 0 |
| 5665, high | 1,371 | 252 | 252 | 0 |

Change detection used a maximum-component difference above `1e-5`. These checks
validate the recovered palette and host joint propagation against the stored
data; the manual rotation is not an original game animation sample. Texture
support remains independent: the shadow still lacks the accepted material
profile even though its skin influences are readable.

A separate complete pass over `ndmw.afs` screened all 45,172 geometry instances
in its 894 IGB documents. It attempted skin decoding only when both legacy
vertex slots 7 and 8 held non-null memory references. Of 476 candidates, 290
were supported, comprising 222,694 vertices. There were no document failures.
All supported geometries passed zero-delta evaluation, with a maximum absolute
coordinate error of approximately `1.97209e-15`; none exceeded `1e-5`.

For each supported geometry, the probe also selected the joint referenced by
its first vertex and applied local X/Y/Z rotations of 15/20/30 degrees. Every
evaluation returned the expected vertex count and finite positions. This is an
additional host-math check, not a decoded original animation or a claim that all
those original material profiles can be rendered.

The remaining candidate diagnostics were:

| Unsupported reason | Geometry instances |
| --- | ---: |
| Selector lacks exactly one explicit `igSkin` owner | 133 |
| Matrix fails the exact affine profile | 32 |
| Vertex format outside the verified one-influence profile | 20 |
| Selector lacks exactly one vertex-blend enable state | 1 |

The pass completed in approximately 1.80 seconds, below its 170-second cap,
using current production skinning source compiled into isolated scratch
objects. It included the local-bind consistency and typed skin-list guards.
Repeated geometry in different documents counts again. Geometries without
both skin buffers were screened out, not declared malformed; `area.afs` was
not included in this rig census.

Native scratch probes load the requested archive entry through
`AfsArchive::read` and `read_igb_document`, then inspect validated objects,
fields and memory spans. For this investigation the local commands included:

```powershell
.\work\scene_research.exe 'E:\path\files\ndmw.afs' 850
.\work\skin_json.exe 'E:\path\files\ndmw.afs' 850 4636 4658 5637 412
.\work\skin_integration.exe 'E:\path\files\ndmw.afs' 850
.\work\skin_corpus.exe 'E:\path\files\ndmw.afs'
```

The JSON/matrix analysis and bounded DOL probes are ignored local research
artifacts. Detailed bone/reference relationships refer to entry 850; the
separately labelled rig census covers the complete `ndmw.afs`. Production
synthetic tests must independently cover malformed
hierarchies, joint mappings, influences, matrices and noncommuting transforms;
original data is an optional local integration input, never a CI fixture.
