# Prepared actor skin draws

The portable `actor_skin` module consumes an explicitly selected actor's
published animation caches. It binds the actor's main appearance skin to one
geometry, captures the selected joint palette and root matrix by value, and
deforms source positions with the recovered single-influence CPU arithmetic.
The prepared renderer then applies that captured model-view and a separately
supplied projection. This connects actor caches to visible geometry; original
camera selection, complete appearance/material traversal and gameplay scheduling
remain separate work.

Related boundaries are described in [ACTION_ACTORS.md](ACTION_ACTORS.md),
[ANIMATION_HIERARCHY.md](ANIMATION_HIERARCHY.md) and [SKINNING.md](../SKINNING.md).

## Binding and ownership

`ActorSkinBinding` owns immutable decoded skin data and identifies its source,
RAM reference, combiner reference, actor index/reference, appearance, main skin,
selector, database, skeleton and geometry. Reference numbers alone cannot
identify an object across different source documents.

`decode_actor_skin_binding()` follows the selected RAM's ordered actor list,
checks the actor's appearance, and requires the selected geometry to belong to
that appearance's explicit main skin and selector. The decoded skeleton must
match the owning combiner's complete ordered bone definitions, joint ownership
and serialized inverse matrices. Matching counts alone are insufficient.

The current profile accepts little-endian IGB documents, one full-weight
influence per vertex, finite affine matrices, and the supported skin graph.
Appearance lists for additional skins, bone-selected model-view attachments,
named bindings and child nodes must be empty. The supplied single-influence
vertex formats are `0x10111` and `0x10113`. Palette entries may repeat and joint
indices may be permuted; their original order is preserved. All references,
array sizes, palette indices and source coordinates are checked before a
binding is published.

`capture_actor_skin()` selects the actor's existing private caches when
`cache_used` is true, otherwise its combiner's shared caches. It validates the
source/RAM/combiner/actor/appearance relationship and requires the original
activation arrays and matching skeleton dimensions. It copies:

- Bone-cache matrix zero into `ActorSkinFrame::model_view`.
- Each selected joint-cache matrix into `palette_matrices`, in selector order.
- Joint-origin positions from the owning accumulated bone matrices, in joint
  ordinal order, for diagnostics. The existing normalized-mesh overlay is
  disabled for prepared actor draws.

Capture does not sample animation, evaluate hierarchy, change validity flags,
allocate substitute identities or transform vertices. Callers perform the
appropriate actor traversal before capturing a fresh frame. This distinction
preserves cached reads and allows a caller to inspect an existing cache without
silently updating it.

Each frame carries the immutable binding's identity. `evaluate_actor_skin()`
rejects a frame from another binding even when its dimensions match. Copies of
the same binding retain this identity, and frame matrices are independently
owned values. Later traversal can therefore overwrite the shared combiner root
or reuse its joint caches without changing an earlier captured draw.

## Transform sequence

Ordinary actor traversal stores root zero as the supplied model-view multiplied
by the actor transform. The caller's model-view already includes the applicable
outer/node transforms. The hierarchy starts at bone one; a parent index of zero
copies the child's local matrix and excludes the traversal root. Each published
joint matrix already contains its accumulated bone transform and serialized
inverse bind. See [ANIMATION_HIERARCHY.md](ANIMATION_HIERARCHY.md) for ordering.

The skin consumer uses those published values directly:

```
source XYZ
    -> selected joint-cache matrix
    -> actor-local deformed XYZ
    -> captured root/model-view
    -> supplied projection
```

Neither capture nor deformation applies inverse bind again. The root belongs
in the model-view stage once. Changing only that root leaves actor-local
deformed positions unchanged. Combining root into the palette and applying it
again during rendering would duplicate the actor/view transform.

`evaluate_actor_skin()` returns a `SkinPose` using the captured palette and
joint origins. It does not call the older pose reconstruction path, resample a
clip, promote cached matrices into a wider hierarchy evaluation, or mutate the
actor/combiner.

## Original CPU point arithmetic

The ordinary indexed draw setup is `800FA438`. Its one-influence loop at
`800FAA4C..800FAAE0` reads an unsigned byte from the vertex buffer's influence
index array, multiplies it by 64, and selects a full matrix from the graphics
context's palette pointer. It transforms each source position and stores a
12-byte XYZ triple. `800FAAE4..800FAAFC` binds that deformed array as the GX
position array with stride 12. The geometry's vertex indices then select these
positions. An equivalent nonindexed point sequence is in `800F01BC..800F0258`.

For column-major matrix M, input `(x,y,z)`, and output row r=0,1,2,
`original_skin_point()` implements:

```
t = single_multiply(M[r], x)
t = single_fused_multiply_add(M[4+r], y, t)
t = single_fused_multiply_add(M[8+r], z, t)
result[r] = single_add(M[12+r], t)
```

The translation addition is a separate rounded operation after the two fused
accumulations. The raw paired-single loop has no horizontal sum or perspective
divide. It computes a fourth lane but discards it; a finite-input overflow in
that discarded lane does not change otherwise finite XYZ. The checked host
helper rejects nonfinite inputs and unusable XYZ results.

The source palette byte is an unmasked CPU matrix ordinal, not GX PNMTXIDX.
The original one-influence branch does not read the weight buffer: it uses the
selected matrix as one full influence. Requiring a stored weight of exactly one
is the supported decoder's profile check. Multiweight execution has a different
loop and is not implemented by this boundary.

The helper follows the researched startup NI=1/RN=00 numeric profile used by
the hierarchy matrix helper. Quantized float loads retain subnormal inputs;
each tiny arithmetic result flushes to signed zero before binary32
nearest-even rounding, and quantized stores flush subnormal outputs. The
extreme fused min-normal boundary remains an interpretation of the pre-round
rule, not a physical Gekko measurement. Its source evidence and limitation are
documented in [ANIMATION_HIERARCHY.md](ANIMATION_HIERARCHY.md).

## Prepared rendering boundary

`MeshRenderer::upload_prepared()` retains original source coordinates instead
of baking the ordinary mesh inspector's center/radius normalization into the
vertices. `render_prepared()` requires this upload mode and applies model-view
and projection as separate GPU stages. It uses the existing checked mesh,
texture, render-target and MSAA infrastructure; it does not supply an original
game camera or reproduce GX numeric rounding.

Ordinary `render()`, `project_to_canvas()` and `focus_camera()` reject a prepared
upload because their implicit normalization/camera would disagree with these
explicit matrices. Position updates retain the original upload mode.

In the original common traversal, selector callback `801DEF98` copies selected
joint-cache matrices into an `igBlendMatricesAttr`. Its apply method
`800BD6D0 -> 800ED404` publishes the contiguous pointer and count without root
composition. The common geometry callback `801DDB84`, when vertex blending is
enabled and no explicit matrix-list override is present, pushes bone-cache
matrix zero as model-view before submission (`801DDEF0..801DDF54`).

Original draw entry `800ED784` flushes matrix state with `800F8B38`, then calls
`800FA438` before primitive emission. Matrix flush `800F84EC -> 800F805C`
packs the engine's column-major matrix into three GX rows:

```
M[0] M[4] M[8]  M[12]
M[1] M[5] M[9]  M[13]
M[2] M[6] M[10] M[14]
```

It uploads position matrix zero and selects it with `GXSetCurrentMtx(0)`.
Thus the ordinary body path applies the root/model-view after CPU deformation.
The local SDK `GXTransform.c` confirms a 12-float row-major upload. Dolphin's
[vertex loader](https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/Source/Core/VideoCommon/VertexLoader.cpp)
and [vertex shader generator](https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/Source/Core/VideoCommon/VertexShaderGen.cpp)
independently show that GX selects one three-row matrix per vertex. GX matrix
selection does not itself perform the CPU palette-weight blend.

## Explicit app selection

Actor inspection extends the existing mesh animation options. Supply all three
selectors together with a nonempty `--animation NAME`:

| Option | Selection | Range |
| --- | --- | --- |
| `--actor-info` | ACTION info object reference in the selected source | 0..1048575 |
| `--actor-ram` | RAM slot within that info | 0..1023 |
| `--actor-index` | Ordered actor index within that RAM | 0..63 |

The existing animation option implies `--rig`; rig inspection defaults to
mesh zero unless `--mesh INDEX` is supplied. The source must be selected with
`--resource NAME` or `--archive NAME --entry INDEX`. CLI range acceptance does
not assert that a source contains the chosen object, RAM, actor or mesh; those
relationships are validated when the asset is loaded.

In the supplied Agumon ACTION2 info 546, each RAM slot 0..3 has the following
ordered actors. The actor order differs from the geometry list order:

| Actor index | Main-skin geometry | Mesh index | Inspection |
| --- | --- | --- | --- |
| 0 | 413 | 1 | Shadow geometry; Solid mode only in the current material profile |
| 1 | 5665 | 2 | High-detail body with its verified texture |
| 2 | 262 | 0 | Low-detail body with its verified texture |

For example, `--resource _common/p_agumon.igb --mesh 2 --mesh-mode textured
--actor-info 546 --actor-ram 0 --actor-index 1 --animation waitS
--animation-time 0.4` opens a paused high-detail actor draw. The app decodes the
owning clip library and validates all RAM cache ownership through the prepared
actor factory, then explicitly activates the selected group. It adds the chosen
base clip without running ACTION commands. Changing the clip starts from the
authored prepared combiner instead of relying on clear/add to invalidate a
same-time cache.

Seeking converts seconds to an explicit integer parent timestamp and applies
the original node clock once. Source playback clamps at the clip end; the
inspection UI implements its own optional looping. Pure camera changes reuse
that node timestamp and shared local/joint caches, replacing only the traversal
root. A host fit transform and orbit view position the unnormalized model;
they do not represent recovered original camera or spawn behavior.

The actor flags reject duplicates and incomplete selections. Existing
constraints prohibit combining animation with manual bone rotation, scene
inspection or the provisional `--play` path. Clip time/playback controls remain
explicit inspection inputs. This path does not imply original ACTION event
routing, automatic LOD, player construction or an original gameplay camera.

## Provenance and validation scope

The original instruction addresses refer to the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original game bytes and generated
disassembly/probes stay outside tracked deliverables. The distributed native
application does not execute the DOL.

A local independent probe executed the actual integer control flow and raw
paired-single opcodes from `800FAA4C..800FAAE0` over synthetic palette memory.
The corpus contained 1,612 cases, 7,327 positions and 226,991 executed
instructions. It matched the standalone native point helper for 6,298 finite
positions, comprising 18,894 exact XYZ words, and 1,029 overflow/NaN rejections
from finite inputs, with zero mismatches. Cases covered repeated/permuted
palette indices, empty input, noncommuting values, cancellation, separate
translation rounding, broad finite exponents, subnormal inputs and discarded-W
overflow. This establishes agreement with the stated instruction semantic
model; it is not a physical-console or raster comparison.

The complete queued record sort/merge/executor order, full effective material
and lighting stacks, named appearance attachments, additional skins,
multiweight vertices, normals, actor modifiers/forced-cache behavior, original
camera/projection selection and GX raster equivalence remain outside this
gate. See [VALIDATION.md](VALIDATION.md) for separately measured build, GPU,
packaged app and original-data integration results.
