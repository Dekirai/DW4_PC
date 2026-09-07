# Native mesh inspection renderer

The version 0.4 mesh viewer uses an OpenGL 3.3 vertex/fragment program, a vertex
array, position/UV/index buffers and a depth-tested framebuffer. It renders actual
geometry decoded from an original IGB document. ImGui displays the resulting
RGBA texture with vertically flipped UVs to account for framebuffer orientation.
It does not render a model through a screenshot, emulator or generated substitute.

The native decoder is documented in [MESHES.md](MESHES.md). Its `MeshData` output
is independent of SDL/OpenGL/ImGui. The host-only renderer resides in
`src/mesh_renderer.cpp`; projection and orbit math reside in the portable
`dw4::OrbitCamera` implementation.

## Coordinates and camera

The renderer computes a bounding-box center and bounding-sphere radius in double
precision. It uploads a normalized copy of the original positions. Original
`MeshData` positions and bounds are not changed. This keeps GPU and camera math
stable even for large finite source coordinates. A zero-extent mesh is rejected
with a diagnostic instead of dividing by zero.

The inspection camera uses a right-handed view, a 45-degree vertical field of
view and an exploratory +Y-up convention. It is aimed at the normalized mesh.
Narrow viewports increase the fitted distance. Orbit pitch is bounded away from
the poles; zoom and pan are bounded; non-finite controls are rejected. This
camera does not reconstruct the original game's timing, camera or coordinates.

- Left drag: orbit. Shift-left drag, middle drag or right drag: pan.
- Wheel: zoom. **Fit**: restore the initial fitted camera.
- **Solid**: plain face shading derived from triangle derivatives.
- **Wireframe**: triangle edges, with depth testing.
- **Textured**: the verified bound base image sampled with original UVs.

Solid shading is an inspection aid. Back-face culling is disabled to inspect
both sides. Normals, full game materials, scene transforms and skeletal poses
still require separate loading.
For a rigged character, stored positions are displayed without executing its
skinning or animation state.

## Bound texture inspection

The portable [material resolver](MATERIALS.md) verifies the explicit geometry,
scene owner, attribute scope, texture binding and image references. The current
game-data profile accepts one enabled stage-zero image with linear minification
and magnification and repeat wrapping. Ambiguous ancestry, incomplete bindings
and other sampler/material profiles report an unsupported diagnostic.

The renderer accepts a finite UV pair per vertex and an owned RGBA base image.
It uploads interleaved position/UV vertices and an RGBA8 texture. UVs and decoded
image rows are used unchanged, including negative UVs. There is no implicit V
flip or recoloring. The framebuffer flip used by ImGui is a separate display
operation. The renderer also supports nearest, clamp-to-edge and mirrored-repeat
for synthetic validation; the original-material resolver does not infer these
from unverified source state.

Textured mode displays unlit texel RGB. Zero-alpha samples are discarded and
partial alpha uses source-over blending onto an opaque viewport. This is basic
inspection compositing, without original alpha-test rules, translucent sorting,
lighting, texture matrices, multiple stages or mipmap selection. It does not
establish final original-game appearance. A missing image or UV stream is an
error in Textured mode, never a successful solid-mode fallback.

## Framebuffer and ownership

The viewport is bounded to 4,096 pixels per axis and 8,388,608 pixels in total.
The application also scales its requested render target to stay within those
bounds. It preserves the canvas aspect ratio. The target is resized only when
dimensions or sample count change.

For MSAA, the renderer requests 2, 4 or 8 samples subject to the driver's reported
limit, using color/depth renderbuffers and a separate resolved RGBA8 texture.
Without MSAA, it draws directly to the color texture with a depth renderbuffer.
Drivers may round a requested sample count upward. Target reuse compares the
normalized request; the queried actual count remains available for diagnostics.
Framebuffer completeness and GL errors are checked. The UI reports the actual
offscreen sample count, which is separate from the window framebuffer count.

The renderer restores the caller's framebuffer, viewport, program, vertex-array,
buffer, texture, polygon, depth, color-mask and changed capability state. Texture
operations additionally preserve the active unit, unit-zero texture and sampler,
pixel-unpack buffer/layout and blend equations/factors. Resources
are created lazily in the current context, and `shutdown()` runs before SDL
destroys that context. Replacing geometry clears its old image. Invalid texture
replacement clears the old texture; invalid mesh uploads clear geometry. Failed renders
are reported and cannot pass an explicit mesh smoke test.

Core-profile polygon rasterization follows the
[OpenGL 3.3 specification, section 3.6.3](https://registry.khronos.org/OpenGL/specs/gl/glspec33.core.pdf).
Core mode uses `GL_FRONT_AND_BACK`; the state query is stored in a two-integer
buffer for driver compatibility, while the first value determines the shared
core-profile mode. GPU tests verify actual restoration on the local driver.
Sampler capture uses the active-unit `GetIntegerv(SAMPLER_BINDING)` query
specified by section 3.8.2 of the same OpenGL 3.3 specification.

## Validation

Portable CTest includes camera projection, fitted-frustum, control, invalid-input
and reset tests. An additional hidden-window GPU test is built with the app:

```powershell
.\build\Release\dw4_renderer_tests.exe
```

It draws a small synthetic tetrahedron and checks pixel coverage, changed pixels
after orbit, distinct wireframe coverage, resizing, MSAA resolve, GL state
restoration, invalid uploads and cleanup. It passed on the local RTX 5070 Ti.
Texture tests use a synthetic asymmetric 2×2 image to verify RGBA channels,
unchanged V coordinates, repeat/clamp/mirror wrapping, linear filtering, alpha
and missing/malformed UV/image failures. Hostile caller state includes a different
active texture unit, an overriding sampler, an unpack buffer and padded/skipped
pixel layout; rendering and upload must restore all of it.
The primitive is a test fixture only; the application loads original game meshes.
CI builds this executable but does not require a GPU or run it automatically.

Original-data checks use explicit finite application runs:

```powershell
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/mesh.ini --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --smoke-test 5 --screenshot work/agumon.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/mesh.ini --resource _common/p_agumon.igb --mesh 2 --mesh-mode wireframe --smoke-test 5 --screenshot work/agumon-wire.bmp
```

The selected Agumon geometry resolves to `ndmw.afs[850]`, global reference 5665:
1,371 stored vertices and 1,966 emitted triangles. A simpler unindexed sample is
`ndmw.afs[179]`, mesh-list index 0, global reference 3: 24 vertices and 8 triangles.
Agumon's high geometry inherits texture 405 through binding 414 on scope node
5481, reaching image 5609 (`P_agumon01.dds`, 256×256 BC1). It has 1,371 UV pairs.
These tests validate native loading and visible drawing, not a final animated
character, material match, original scene or gameplay implementation.
