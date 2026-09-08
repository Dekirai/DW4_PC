# Static scene batch rendering

The native OpenGL 3.3 viewer can upload multiple world-positioned geometry
instances to a separate `MeshRenderer`. All instances use one camera
normalization, vertex/index buffer pair, depth buffer and offscreen color target.
They are drawn together before the optional MSAA resolve. This preserves
relative placement and opaque depth relationships between instances.

The renderer consumes already decoded, transformed data. It does not select a
scene root, traverse IGB nodes, choose among node alternatives, infer materials,
or apply additional instance transforms. Those decisions belong to the portable
scene loader and the application, which retain source provenance and report
unsupported state.

## Upload contract

```cpp
dw4::viewer::MeshRenderer scene_renderer;
std::vector<dw4::viewer::MeshBatch> batches;
batches.push_back({&world_mesh_a, &verified_image_a, sampler_a});
batches.push_back({&world_mesh_b, &verified_image_b, sampler_b});
scene_renderer.upload_batches(batches);
auto color_texture = scene_renderer.render(width, height, camera,
    dw4::viewer::MeshDrawMode::textured, requested_msaa);
```

`MeshBatch::mesh` must point to a nonempty `MeshData` whose positions already
share the scene's coordinate space. Each batch's indices address that mesh's
positions. Upload checks these local indices before rebasing them into the
merged index buffer. Bounds are recomputed from the supplied positions; cached
`MeshData::bounds` values are not trusted.

All source pointers are borrowed only during `upload_batches`. The renderer
copies geometry and image data into OpenGL objects. It retains draw ranges and
GPU handles rather than source pointers. Callers may release temporary image
buffers afterward. While constructing the batch list, callers must keep mesh
and image addresses stable until upload finishes.

A texture requires a finite UV pair for every vertex and a complete RGBA base
image. Shared image pointers with equal sampler settings reuse one GPU texture.
The same image with different wrap/filter settings receives a separate GPU
copy, preserving per-batch sampling. Samplers support the existing verified
repeat, clamp-to-edge and mirrored-repeat wrapping and nearest/linear filtering.

The fixed normalization is computed once from the union of all instance bounds.
`project_to_canvas` uses this same scene coordinate space and camera. Static
batches do not support the single-mesh `update_positions` or `set_texture`
operations; these calls reject without changing the uploaded scene. Upload a
new batch list to replace static scene geometry or materials. Existing
single-mesh animation uploads keep their previous behavior.

## Modes and diagnostics

**Solid** and **Wireframe** apply their inspection shader to every batch.
**Textured** requires a verified image and UV stream for every batch. A missing
texture rejects the draw before rendering begins; the existing geometry remains
available for explicit Solid inspection.

`MeshBatch::allow_solid_fallback` is an explicit caller policy for a future
mixed-material inspection mode. It permits only a missing texture to use the
solid shader. A caller using this option must identify that mode and report the
number of untextured batches. Invalid supplied image bytes, UVs or sampler values
still reject the upload; they are never silently replaced with solid shading.

`batch_count()` counts geometry draws, `textured_batch_count()` reports texture
coverage, and `texture_count()` counts allocated image/sampler copies.
`has_texture()` is true only when every uploaded batch has a texture. Thus an
explicitly mixed scene may render successfully while `has_texture()` is false.
`triangle_count()` is the aggregate scene triangle count.

Clearing textures releases shared images once and preserves geometry. Clearing
the renderer releases geometry and materials. A failed replacement upload leaves
no stale scene. As with the original single-mesh renderer, call `shutdown()`
before destroying the current OpenGL context.

## Resource bounds and rendering scope

The uploader enforces aggregate limits before creating GPU geometry or textures:

- 1 through 4,096 batches.
- At most 4,194,304 vertices and 4,194,304 triangles across all instances.
- At most 256 MiB of image/sampler GPU copies and 64 MiB per image.
- Complete finite positions and UVs, valid per-batch triangle indices and
  nonzero finite shared spatial extent.

At these geometry limits, the merged interleaved vertex buffer occupies at most
80 MiB and the index buffer at most 48 MiB. Static uploads release their temporary
CPU arrays after upload. Repeated references count toward geometry limits because
each instance occupies its own transformed positions in the merged buffer.

The offscreen target retains the existing 4,096-pixel axis and 8,388,608-pixel
limits. MSAA requests are clamped to supported counts up to eight samples.
Resizing creates one scene target; unchanged dimensions and sample requests
reuse it. The viewer restores the caller's framebuffer, program, vertex array,
buffer, texture/sampler, upload layout, blending, depth, viewport and capability
state after each operation.

Textured rendering uses original base images with the existing unlit inspection
shader. Zero-alpha texels are discarded and partial alpha uses basic source-over
blending with depth writes. Transparent surfaces are not sorted. Original
lighting, full material state, culling policies, render passes, animated scenery
and gameplay are separate work; this renderer does not claim original-frame
parity for those features.

## Validation

The explicit local GPU test target retains all single-mesh texture, camera,
MSAA and animated-position checks, and adds synthetic scene cases that verify:

- Two separately positioned, differently textured quads remain visible under
  one normalization, with their centers projected to opposite canvas sides.
- Opaque front/back geometry shares depth and produces the same image when
  batch order is reversed.
- One resized MSAA target resolves the complete scene and is reused on the
  next identical request.
- Host mesh/image buffers can be changed after upload without altering output.
- Texture deduplication distinguishes sampler settings and preserves per-batch
  wrap behavior.
- Missing materials reject strict Textured drawing, while explicit mixed
  fallback and Solid/Wireframe remain distinguishable policies.
- Local index overruns, null meshes, incomplete/non-finite UVs and positions,
  invalid images, excessive batch/geometry counts and aggregate texture budgets
  reject with complete cleanup.
- Hostile caller GL state is restored, including sampler zero, another active
  texture unit and a pixel-unpack buffer.

The isolated Release and AddressSanitizer Debug GPU validations pass on the local
NVIDIA GeForce RTX 5070 Ti. Asset-free CI does not require a graphics driver; original room/map
inspection is an additional local acceptance check performed by the application.
