# Shared prepared GPU passes

`MeshRenderer::upload_prepared_batches()` uploads ordered mesh occurrences in
their source coordinates. `render_prepared_batches()` draws them into one
offscreen color/depth target using a captured model-view for each occurrence
and a common projection. This permits a CPU-deformed actor skin and rigid
bone-selected weapon children to share visibility and presentation without
normalizing their coordinates or applying the actor transform to an attachment
again.

The renderer consumes prepared draws. The caller remains responsible for
original scene traversal, actor/appearance selection, attachment order and
capturing matrices while the owning actor's bone array is active. The renderer
does not perform bone selection, CPU skinning or original camera selection.
See [ACTOR_SKIN.md](ACTOR_SKIN.md) and [BONE_SELECT.md](BONE_SELECT.md).

## Ownership and coordinates

Each `MeshBatch` describes one draw occurrence, even if multiple batches use
the same mesh pointer. The GPU holds separate vertex subranges so updating one
occurrence cannot move another. Positions and image bytes are borrowed only
during upload. Images sharing the same pointer and sampler options use one GPU
texture; unequal options retain separate images. UVs, index order, material
coverage and source upload order are preserved.

`update_batch_positions()` changes the selected occurrence's source positions
without replacing its indices, UVs, textures or render target. Prepared updates
store raw coordinates; ordinary scene updates retain the initial shared scene
normalization. The existing single-mesh prepared and normalized scene APIs
remain available. A multi-batch prepared upload requires the multi-batch render
API with exactly one matrix per occurrence.

## Pass execution and checked failures

The entire matrix span, common projection, viewport, draw mode and material
coverage are checked before allocating, clearing or drawing into the target.
An invalid later matrix or missing later material therefore leaves the previous
target pixels, sample count and successful-frame counter unchanged. Pose counts
and all positions are validated before the selected GPU buffer subrange changes.
Upload failure retains the existing upload contract: prior mesh data is cleared
and invalid replacements are not drawable.

The color/depth target is cleared once. Each batch receives its own model-view
uniform immediately before drawing in upload order. The vertex shader executes
`model_view * point` followed by a separate projection stage; no CPU-composed
projection or renderer normalization is inserted. All draws share depth and
the requested MSAA target, and resolve once after the final batch. A successful
pass increments the frame counter once. Caller OpenGL state is restored.

Upload limits remain 4,096 batches, 4,194,304 vertices and triangles, 256 MiB
of unique image copies and 64 MiB per image. Viewports remain bounded to 4,096
per axis and 8,388,608 pixels. Sample requests use the existing driver-clamped
0/2/4/8 policy. These limits and validation do not establish original material
fidelity: texture blending, depth state, Solid and Wireframe remain the native
inspection renderer's represented behavior. Full original render-state and
camera comparison remain required.

## Validation

The asset-free renderer GPU test uses independently calculated clip-space
corners and single-draw reference frames to check repeated source geometry under
distinct, noncommuting model-view/projection matrices. It also checks raw pose
updates and exact reset, opaque shared-depth visibility in both orders,
equal-depth ordered replacement, resize/MSAA, sampler sharing, released borrowed
buffers and complete material validation. Every element of both model-views and
the projection is tested with NaN and positive/negative infinity before a target
resize request. Wrong matrix counts, viewport limits, draw mode, pose sizes and
late missing materials preserve the last valid target. Existing normalized
scene and single-prepared regressions run in the same executable.

## Live player and weapon adapter

The application-side `WeaponPreview` retains one actual loaded player scene,
`PlayerRuntimeBinding`, registered `ActionRuntime`, `ActionEventRuntime` and
`WeaponRuntime`. It resolves the explicit ACTION actor ordinal to its original
LOD branch and rejects a mismatched selected geometry. It dispatches the named
ACTION through the owned event services, executes original PLAYERARMS GENERATE,
and requires resulting selector owners to belong to that same ACTION appearance
list and the selected actor's appearance.

Each frame advances that same ACTION owner, traverses the selected actor and
captures the body palette and actual actor-selected bone array before visiting
its ordered attachments. Common child traversal receives an explicit reset
inspection graphics scope: vertex blending disabled, matrix-list stack empty,
the captured bone root and actual converted actor node time. Unknown child,
changed-state flush, ACTION, appearance override or material services stop with
a diagnostic. The represented body and weapon material profile requires the
verified neutral material, smooth shade, local image and stage-zero sampler;
the body's already-consumed CPU skinning state is checked separately.

Body positions change in batch zero. Every geometry occurrence retains its
immutable mesh, UV/image owners and path while its captured model-view changes.
Equal-time camera or placement updates retain the live animation clocks.
Explicit `dispatch_action()` changes ACTION through that same runtime, preserving
its combiner history. Time is monotonic within the nonnegative signed 64-bit
nanosecond range; the application's separate bounded seek controls remain an
inspection policy. These operations support the provisional walking camera and
locomotion choices without claiming the original player input/sequence loop.

`source_normalization()` exposes an ordinary uploaded scene's checked fitting
matrix so a raw combined pass can use the same inspection camera. Its float GPU
composition can differ at raster edges from the ordinary per-vertex double
normalization; the GPU test compares their placement and bounds this difference.
