# Bounded static scene decoding

Version 0.7 adds a portable decoder for a verified subset of the original IGB
scene graph. It produces placed geometry instances and source paths; the SDL
application displays them in a separate scene inspector. This is an asset
inspection boundary, not the game's scene activation, actor or collision system.
Runtime evidence and addresses are recorded in [SCENE_RESEARCH.md](SCENE_RESEARCH.md).

## Selection and supported traversal

`list_igb_scenes` enumerates explicit `igSceneInfo` objects. Each descriptor keeps
its own reference, root reference, name, root type and stored up direction.
`decode_igb_scene` takes the **descriptor reference**, not the list index or root
node reference. It follows that descriptor's slot-5 root. It does not infer a
main export from its name, merge every descriptor, or load external references.
Derived descriptor behavior remains unsupported. The up vector is reported as
metadata; it does not replace the host inspection camera.

The current little-endian static profile handles:

- Exact `igGroup` and verified `beGroupKeep`: ordered child edges.
- Exact `igNode`: the original no-op base handler.
- Exact `igTransform`: finite affine slot-8 matrices, target zero and a null
  transform input. World matrices compose as `parent * local` for column vectors.
- Exact `igSwitch`: serialized little-endian child-mask words, least-significant
  bit first. Stored disabled branches remain disabled.
- Exact `igAttrSet`: inherited attribute scope and ordered child traversal.
  Its trigger does not suppress its children.
- Exact `igGeometry`: a false trigger or `IS_INVISIBLE` flag returns without
  emission. Otherwise each geometry attribute is emitted in serialized order.
  This handler does not traverse its child list.

The low five node flags are stored light, transform, render-list-cache and
collision hints, not a generic visibility mask. Dynamic flags, unknown flags
and invisible non-geometry behavior are rejected. Unknown derived nodes, LOD
selection, actors and dynamic transform sources are not flattened into a scene.

The positional attribute allowlist is deliberately narrow: `igGeometryAttr1_5`,
`igTextureBindAttr`, `igTextureStateAttr`, `igMaterialAttr`, `igShadeModelAttr`,
`igBlendFunctionAttr` and `igBlendStateAttr`. Other attribute behavior is reported
as unsupported, including matrix, bone or projection state that could invalidate
the accumulated positions. Full lighting and blend behavior of accepted
non-positional attributes is still outside the inspection renderer.

Geometry uses the existing bounded mesh decoder. Skin-weight/index buffers and
additional vertex state are rejected for this static path. Repeated references
produce separate instances for every selected root-to-owner path. This preserves
the placement and material inheritance of shared graph nodes. A per-path cycle
check avoids treating legitimate shared nodes as cycles.

## Explicit root-child inspection

The measured map exports store zero switch masks; the game activates them later.
Default decoding therefore correctly produces empty roots. For inspection only,
`SceneOptions::inspect_root_child` explicitly selects one child of an exact
`igSwitch` export root. The child must exist and be non-null. Its index is copied
to `SceneData::inspected_root_child`, and the application labels the override.

Only that root selection is replaced. Nested switches retain their stored bits;
malformed masks still fail validation. The decoder never edits source bytes.
Clearing the option restores normal stored-state traversal. This does not claim
the selected subtree is the scene configuration used by gameplay.

## Output and failure contract

Each `SceneInstance` retains the owning node, geometry attribute, display name,
full source path and composed world matrix. Its owned `MeshData` contains world
positions, original triangle indices and recomputed bounds. UVs remain empty
until explicitly decoded for Textured inspection. Source geometry is cached
inside one decode; every instance receives independent world positions.

Transform composition uses double precision and rounds world coordinates to
float after checking range. It preserves stored rotations, translations, scale
and reflections. It is not a bit-identical emulation of PowerPC floating-point
matrix instructions. Aggregate bounds include every emitted vertex.

Unsupported behavior records a node/path/reason diagnostic and makes
`SceneData::complete` false. Other supported branches may be decoded for analysis,
but the application refuses to render those partial results as a complete scene.
Malformed fields, references, lists, matrices, active cycles and resource-limit
violations throw `SceneError`. A complete empty result is valid stored state,
although it cannot satisfy an explicit rendering smoke test.

Per-decode resource limits are:

- 4,096 scene descriptors and 4,096 emitted instances.
- 64 nodes on a path and 65,536 visited node instances.
- 4,194,304 list links read, including repeated traversal.
- 4,194,304 placed vertices and 4,194,304 emitted triangles.
- The same separate vertex/triangle limits for the source mesh cache.

Switch bit counts are bounded to 4,194,304. Count, capacity, memory extent and
child coverage must agree before mask bits are used. The underlying IGB, mesh,
image and GPU decoders retain their own smaller per-resource limits.

## Materials and native rendering

An alternative explicit `SceneOptions::root_control` evaluates the original
`ALLON` or `ALLOF` command of an exported `beSwitchCtrlInfo` targeting exactly
the selected root. It is exclusive with manual child inspection and retains
request provenance in the result. Nested masks and source bytes are preserved.
See [SCENE_CONTROLS.md](SCENE_CONTROLS.md) for validation, bounds and the distinction
between a native command effect and full startup execution.

`resolve_igb_material_on_path` uses each instance's exact root-to-owner path.
Other parents of a shared node do not affect this instance. The nearest unit-zero
binding and enable state can come from different scopes; all supplied scopes are
checked for unhandled texture modifiers. The result retains both state and image
provenance. See [MATERIALS.md](MATERIALS.md).

The native scene renderer uploads all placed geometry under one camera and depth
target. Textured mode requires a verified base image and UVs for every instance;
Solid and Wireframe remain explicit inspection modes. There is no filename or
first-image material fallback. See [SCENE_RENDERER.md](SCENE_RENDERER.md) for
buffer, image-sharing and transparency limits, and [SCENE_VIEWER.md](SCENE_VIEWER.md)
for controls and command-line options.

## Synthetic validation

The asset-free `scenes` CTest suite checks export identity, noncommuting transform
composition, aggregate bounds, nonuniform scale/reflection, shared DAG placements,
ordered repeated edges and geometry attributes, trigger/flag distinctions,
ordinary group inheritance, switch bits across word boundaries and inspection
without source mutation. Malformed roots, lists, masks, booleans, matrices,
position overflow, active cycles, depth and instance limits reject in Release.
Unknown node/attribute/skin behavior produces explicit incomplete diagnostics.
The same suite is also exercised locally with AddressSanitizer.

Optional read-only original-data diagnostics can be run as:

```powershell
.\build\Release\dw4_scenes_tests.exe 'PATH\Extracted\files\ndmw.afs' 179
```

This prints stored-state and explicit root-child-zero inspection results for each
export; it does not automatically assert that every export is supported. Original
integration measurements are kept separate from synthetic CI in
[VALIDATION.md](VALIDATION.md).
