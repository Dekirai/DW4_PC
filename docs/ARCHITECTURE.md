# Native port architecture

## Implemented boundary

`dw4_core` is a C++20 library with explicit byte-order decoding and no console or
graphics dependencies. `assets` reads boot, DOL, AFS and BNR structures. `igb`
reads version 6 metadata; `igb_document` loads owned object records, memory
payloads and validated references. `resources` resolves logical names through
the original AFS setup. `packages` follows exported file-list descriptors into
ordered catalog-addressed members and their explicit info-list exports, retaining
preliminary `/s` loading-stage metadata. `textures` decodes verified indexed8/RGBA8 and BC1
base images. `meshes` decodes positions, triangle indices and explicitly requested
UV streams. `materials` resolves a bounded local/inherited single-stage texture
binding profile with reference provenance and unsupported-state diagnostics.
`scenes` traverses verified static export roots into placed geometry instances,
with explicit source paths, switch selection and bounded transform composition.
`scene_controls` validates exported switch-controller targets and evaluates
explicit `ALLON`/`ALLOF` child selection without modifying source bytes.
`player_placement` reads explicitly exported entry-point groups and preserves
their full affine matrices. `player_visual` follows an exported player model's
selected instance, `ONE` record and explicit LOD branch into a supported actor,
appearance and skin. It validates the authored outer transform but does not
compose that editor placement: the original generation path replaces it.
`skinning` resolves a bounded geometry/skin/database/skeleton profile, decodes
single-influence weights and inverse-bind frames, and evaluates explicit manual
joint deltas or complete bone-local transforms on the CPU. `enbaya` decodes
bounded packed streams into immutable samples; `animations` validates the
database/binding/track graph, translation mask and original one-clip pose profile.
`camera` supplies
the host orbit/projection math. `settings` loads and validates host preferences. `dw4_inspect` and
`dw4_catalog` are read-only CLIs; `dw4_pc` is the native SDL3/OpenGL/ImGui viewer
and workbench.

The scene viewer holds the map and player as separate owned IGB documents.
Numeric image references never identify assets across those documents. One
explicit player visual is appended after the static scene batches. The CPU
samples a selected clip in skin-local space, applies the entry-point matrix once,
and replaces only that batch's position range, preserving UVs, materials, indices
and shared scene normalization. Focus uses world-space visual bounds with the
same normalization. Changing a map root or member removes the old visual; a
failed render-mode replacement clears the GPU scene and can be rebuilt in Solid
mode. Clip replacement validates and uploads before publishing new clip state.

This is a visual integration boundary, without player input, collision, shadow
composition, automatic LOD, startup execution or controller lifetime management.
The startup research distinguishes `func.lua`, `boot.lua`, `start.lua` and
`first.lua` dispatch order and embedded script lookup behavior; those chunks
remain original data and are not executed by the host. See
[PLAYER_VIEWER.md](PLAYER_VIEWER.md) and [STARTUP_RESEARCH.md](STARTUP_RESEARCH.md).

AFS payloads are read on demand with limits. Guest addresses remain integers
inside diagnostics; they are never cast into host pointers. A validated DOL is
not executable native code. There is currently no PowerPC execution path,
recompiled game code, gameplay scene activation, or emulated GPU.

The original DOL BSS envelope overlaps some initialized data sections. A future
loader must zero the envelope before loading initialized sections or clear only
uncovered ranges; blindly zeroing it after section loading destroys valid data.

The application uses graphics dependencies pinned to exact upstream commits and
archive hashes. Core-only builds do not fetch dependencies. Parser tests use
small generated fixtures instead of original game bytes. `verify_game.py`
additionally checks the original DOL checksum and compares every extracted
filesystem file against its ISO extent.

## Engine reconstruction order

1. **Implemented:** resolve the AFS setup in `ndmw.igb`, including archive/entry
   identities and logical paths. See [RESOURCES.md](RESOURCES.md).
2. **Implemented:** decode IGB object instances, memory references and optional
   directory/pool names. External-object loading is still pending. On-disk schemas
   stay separate from host objects; inherited slots are format metadata, not a
   ready-to-use host ABI. See [IGB_OBJECTS.md](IGB_OBJECTS.md).
3. **In progress:** texture base images and object-space meshes are decoded and
   displayed natively. Triangle lists and grouped 16-bit indexed strips use
   recovered engine enum values and checked memory streams. Original UVs and
   a verified base-texture binding profile are displayed in Textured mode.
   Inverse-bind hierarchy inspection and manual single-influence skinning are
   available for a verified profile. Original Enbaya clips are decoded and
   evaluated with checked full-bone transforms. Static export traversal and
   original instance transforms are available for a verified profile. Full material
   state, actor/combiner behavior and comparison with the original renderer remain.
   See [SKINNING.md](SKINNING.md), [TEXTURES.md](TEXTURES.md),
   [MATERIALS.md](MATERIALS.md), [MESHES.md](MESHES.md) and the evidence in
   [MESH_RESEARCH.md](MESH_RESEARCH.md).
4. Extend static traversal into gameplay scene activation, animation and actor state. Replace OS/DVD/PAD/GX
   boundaries with native services using explicit interfaces.
5. Recover game loop, combat, inventory, scene transitions and persistence in
   narrow, behavior-tested increments. Retain a trace from each adapted function
   to reference addresses and evidence.

The OpenGL 3.3 inspection renderer consumes platform-independent `MeshData`,
normalizes a GPU copy for stable viewing and renders to a depth-tested offscreen
target with optional MSAA resolve. The original coordinate values remain in the
core data. ImGui displays the resulting texture; it does not generate the 3D
geometry. See [RENDERER.md](RENDERER.md). Choosing a final game rendering backend
is still open. Avoid making new gameplay objects depend on ImGui or SDL.

Selecting geometry resolves material provenance without decoding pixels. Entering
Textured mode explicitly reads the UV stream and bound image and uploads them to
the GPU. Unsupported bindings do not prevent solid geometry inspection. Binding
resolution uses object references and a unique checked ancestor path; asset names
are diagnostic labels, never a texture-selection fallback. This deliberately
limited resolver is not a complete scene-state traversal engine. For static scene
instances, a separate path-aware resolver validates the exact supplied ancestry
and independently inherits unit-zero binding and enable state. Shared graph nodes
therefore retain the material of each placement without guessing a global parent.

The scene decoder follows an explicitly selected `igSceneInfo` root and records
unsupported behavior instead of flattening dynamic or unknown nodes. Stored
switch bits remain authoritative by default. A separate, labeled inspection
option selects one child of a disabled root switch without changing nested
selection or source data. The application refuses incomplete traversals and uses
a separate batch renderer with common bounds, camera, depth and MSAA resolve.
See [SCENES.md](SCENES.md) and [SCENE_RENDERER.md](SCENE_RENDERER.md).

An alternative explicit scene option evaluates an exported `beSwitchCtrlInfo`
command whose target must be exactly the selected root. It is exclusive with
manual root-child inspection. The controller decoder validates export identity,
mask layout and child types, with an aggregate child-validation budget. It does
not construct runtime controller registrations or run the Lua scheduler. Package
inspection similarly preserves loading-stage metadata without executing the
original staged initialization. See [PACKAGES.md](PACKAGES.md),
[SCENE_CONTROLS.md](SCENE_CONTROLS.md) and [SCENE_ACTIVATION.md](SCENE_ACTIVATION.md).

Skin inspection requires an explicit unique skin/database association and one
referenced skeleton. Bone-list ordinals, joint indices and per-vertex palette
indices remain separate. Affine inversion reconstructs bind frames from the
original inverse-joint matrices; stored translation arrays are not substituted
for that reference space. Manual local joint deltas propagate through the
recovered joint hierarchy. The renderer updates positions while preserving
the source mesh's normalization, indices, UVs and GPU texture. A separate animation
path follows explicit database bindings, decodes a selected clip and uses the
original translation mask to select sampled or skeleton-constant translations.
Quaternion conversion preserves the original signs and norm. Helpers participate
in the full bone hierarchy. Only the verified single-clip, no-modifier skeleton
profile is accepted; actor state and animation blending remain unimplemented.
See [ANIMATIONS.md](ANIMATIONS.md) and [ANIMATION_POSE.md](ANIMATION_POSE.md).

## Timing and PC settings

Host presentation frequency and game simulation frequency must be separate.
Measure original DW4 timing before choosing a fixed simulation step; do not
assume 30 or 60 Hz. Animation, physics, input sampling and timers must preserve
their original semantics at 144 Hz presentation. Interpolation and widescreen
camera/UI behavior require game-specific work. A borderless window uses the
desktop display size; selecting 3840×2160 does not change the desktop mode or
prove a 4K internal renderer exists.

SDL-mapped gamepads can be inspected now. Keyboard/mouse gameplay, Xbox,
PlayStation and GameCube gameplay mappings remain separate tasks. Native
GameCube adapters may require dedicated adapter handling and calibration.

## Modding and online co-op dependencies

Stable asset identity and a validated loader come before texture overrides or
mod manifests. Version the future mod API and distinguish declarative content
from executable code. Original versus modernized gameplay should share a tested
original baseline before opting into changes.

Four-player online co-op requires proven local multiplayer, identified player
and world state, authoritative simulation ownership, reproducible RNG handling,
save ownership, reconnect/error behavior and latency testing. The network model
(lockstep, rollback or host-authoritative snapshots) remains undecided until
simulation behavior is understood. No networking implementation is claimed by
this milestone.
