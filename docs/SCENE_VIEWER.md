# Static scene inspection

The **Scenes** page inspects exported scene roots in original local IGB resources.
It is separate from the individual **Meshes** viewer and its rig and animation
controls. A static scene inspection is not a playable map: actor spawning,
collision, game cameras, scene transitions and gameplay remain separate work.

## Controls

Connect the extracted game directory under **Original data**, then open **Scenes**.
Choose a logical resource from the checked catalog, or select an AFS archive and
entry directly. The entry read limit is independently adjustable from 1 to 256 MiB.
Select an exported root in the inspector to traverse that root's supported static
graph. Changing the source clears the previous root, instances and GPU content.

Stored switch selection is the default. A valid export can therefore show no
geometry when its stored mask enables no children. The viewer does not infer the
game's later activation. For an exported `igSwitch` root, **Inspect root child**
explicitly selects one child for inspection; edit **Child** and press **Apply**
to change it. A persistent amber label identifies the active override and states
that runtime activation is unverified. Only the root mask is overridden; nested
switches retain their stored masks. Clearing the checkbox or choosing another
export restores stored selection. LOD alternatives and dynamic actors are not
automatically combined into a claimed original scene.

The inspector exposes instance identities and their root-to-node paths, geometry
references and accumulated transforms. These are original resource references;
display names do not determine object associations. Diagnostics remain available
when a root cannot be fully represented by the supported static profile.

The camera fits the combined instance bounds. Drag to orbit, use the right or
middle mouse button to pan, and use the wheel to zoom. **Fit** restores the
inspection camera, which uses a higher angle than the individual mesh viewer to
make broad layouts easier to inspect. These are host viewing controls, not
recovered game-camera behavior.
Fit checks all eight corners of the accumulated bounds against the viewport,
centers their projection and retains a 2.5 percent margin before accepting a
closer view. It does not alter any instance transform.

**Solid** and **Wireframe** inspect the placed geometry. **Textured** requires a
verified base-image binding, original UVs and a decoded image for every rendered
instance. An unsupported material remains an explicit diagnostic; choosing
Textured does not silently assign an unrelated image or replace it with solid
shading. Full game materials and the final scene appearance remain unverified.

## Command line

`--page scenes` selects this viewer. `--scene-root` is a zero-based index in the
document's exported root list; it also selects the Scenes page when no page is
specified. The default root index for an explicitly loaded scene source is zero.
Use `--resource PATH` or the paired `--archive NAME --entry INDEX` options. Scene
selection cannot be combined with the individual mesh, texture, rig or animation
options.

`--scene-inspect-child N` makes the same explicit root-switch inspection request.
It is never selected automatically. A non-switch root or invalid child is rejected.
An empty stored selection is retained as empty; an explicit rendering smoke test
then returns code 3 because no scene geometry was rendered.

`--scene-mode solid|wireframe|textured` chooses the inspection draw mode, with
Solid as the default. A finite `--smoke-test FRAMES` requires successful static
traversal, upload and rendering for an explicitly selected source. Unsupported
roots, incomplete traversal, missing requested textures and upload/render errors
return exit code 3. Malformed command-line options return exit code 1.

## Verified native example

The USA extraction's `ndmw.afs` entry 179 exports the `BG02` root at list index 6
(`igSceneInfo` reference 3372, root node 2877). Its stored switch mask enables no
children. Explicit inspection of child 0 traverses 462 node visits and 219
transforms, producing 235 geometry instances with 31,886 placed vertices and
26,352 triangles. All 235 instances resolve verified texture bindings and UVs;
five unique original images are uploaded.

```powershell
.\build\Release\dw4_pc.exe --data 'E:\path\Extracted' --config work/scene-check.ini --archive ndmw.afs --entry 179 --scene-root 6 --scene-inspect-child 0 --scene-mode textured --smoke-test 5 --screenshot work/BG02-inspection.bmp
```

This example intentionally selects the archive pack directly. Selecting a catalog
descriptor that points to another pack does not automatically load that pack or
activate its scene. The viewer does not follow unimplemented external loading
behavior.

On 2026-09-08, the native Solid, Wireframe and Textured paths rendered this
original layout on an NVIDIA RTX 5070 Ti. Textured controls and the persistent
inspection label were reviewed at 1280 by 720 and 1920 by 1080. The larger run
used a 1290 by 603 offscreen target with eight MSAA samples. Default stored-mask
inspection remained empty; invalid child/root indices and unsupported dynamic
branches failed explicitly. Existing texture, mesh, manual-pose and animation
inspection remained functional.
A 3840 by 2160 finite run also passed with the same 235 textured instances and
five images, using a 3210 by 1683 target with eight MSAA samples.

Raw texel colors and UVs are retained. The scene's dark base-image appearance is
not corrected with an invented color adjustment; full materials and comparison
against recorded original frames remain separate work. Geometry instances share
one normalization, depth buffer and MSAA target, preserving their relative
placement. The renderer bounds batches and GPU uploads independently of the AFS
entry limit.

Original files remain external read-only inputs. Original-derived screenshots and
local integration reports are ignored outputs and are not included in the public
source or synthetic CI fixtures.
