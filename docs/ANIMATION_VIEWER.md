# Native animation inspection

The mesh viewer can decode a supported original Enbaya clip, evaluate its checked
skeleton mapping, and update the native textured mesh and joint overlay. This is
single-clip animation inspection. It is not a playable actor or a claim that the
final scene appearance matches the original game.

## Controls

1. Connect the extracted game directory under **Original data**.
2. Open **Meshes**, choose `_common/p_agumon.igb`, and select geometry index 2
   under **Geometry** for the high-detail player model.
3. Choose **Textured** and enable **Inspect rig**.
4. In the **Rig** tab, change **Manual joints** to **Animation** and choose an
   original named clip, such as `waitS` or `runD`.

Selecting a clip decodes only that clip and starts paused at zero. **Play** and
**Pause** use the host's monotonic clock; the presentation frame limit does not
change clip speed. **Reset** pauses at zero. Moving the time slider also pauses
and samples the selected time. **Loop** wraps playback at the clip duration;
with looping disabled, playback stops at the endpoint. Playback also stops
advancing while another workbench page is selected.

**X-ray joints** draws the evaluated joint pivots and connections over the mesh.
The overlay deliberately remains visible through surfaces. **Details** shows the
geometry/skin/database/skeleton and animation-source reference chains, decoded
track/frame counts, and the current displacement from stored geometry.

The **Manual joints** mode remains separate. Returning to it restores the manual
pose controls; returning to **Animation** resamples the selected clip time while
paused. Changing the mesh or data source clears its previous animation selection.
Unsupported clips remain explicit diagnostics. They are not replaced with a
procedural animation or an unrelated clip.

## Reproducible native frames

The command line selects clips by exact, case-sensitive name within the selected
rig's owning database. Missing or duplicate names are rejected. `--animation`
implies `--rig`; choose the mesh index explicitly when a document has several
geometries. `--animation-time` defaults to zero and must be finite and within the
clip duration. Animation selection cannot be combined with manual bone rotation.

```powershell
.\build\Release\dw4_pc.exe --data 'E:\path\Extracted' --config work/animation-check.ini --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --animation waitS --animation-time 0 --smoke-test 5 --screenshot work/waitS-start.bmp
.\build\Release\dw4_pc.exe --data 'E:\path\Extracted' --config work/animation-check.ini --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --animation waitS --animation-time 0.2541666625 --smoke-test 5 --screenshot work/waitS-quarter.bmp
.\build\Release\dw4_pc.exe --data 'E:\path\Extracted' --config work/animation-check.ini --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --animation runD --animation-time 0.17083333425 --smoke-test 5 --screenshot work/runD-quarter.bmp
```

These commands remain paused for repeatable comparisons. The finite smoke run
requires successful clip decoding, pose upload and mesh rendering; an explicit
unsupported clip, missing binding or out-of-duration time exits with code 3.
Malformed CLI syntax or non-finite/negative time exits with code 1.

`--animation-play` explicitly starts playback and can also exercise the live
clock path in a hidden finite run. Such a screenshot has a wall-clock-dependent
time rather than a fixed sample. Playback smoke tests require at least two
frames and verify that additional evaluated poses were uploaded:

```powershell
.\build\Release\dw4_pc.exe --data 'E:\path\Extracted' --config work/animation-check.ini --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --animation runD --animation-time 0.68 --animation-play --smoke-test 32
```

## Verified scope and limits

On 2026-09-08, original high-detail Agumon `waitS` and `runD` were rendered at
zero and one quarter of their duration. The actual mesh canvas differs between
the paired samples. A 32-frame playback test uploaded 33 poses and crossed the
`runD` endpoint with looping enabled while retaining the original texture and
UVs. Controls and the model were inspected at 1280 by 720 and 1920 by 1080; the
larger run used actual 8-sample MSAA for the native mesh target.

The decoder owns bounded samples and reads no compressed bytes during seeks.
The current graph profile requires the selected skin's checked database,
skeleton, identity track mapping, supported translation mask and one local
Enbaya source. Helpers participate in hierarchy evaluation. Original quaternion
conversion and translation selection follow recovered engine evidence.

Recorded original-game frame comparison, full materials, scene placement,
modifiers, blending, transitions, gameplay events, locomotion, collisions and
combat remain incomplete. A clip named `runD` does not make the character
controllable or reproduce the game's actor movement system. See
[ANIMATION_RESEARCH.md](ANIMATION_RESEARCH.md) for the source investigation and
[SKINNING.md](SKINNING.md) for the separate manual inspection profile.

Original game files and rendered screenshots remain local, ignored integration
inputs and outputs. They are not included in public source or CI fixtures.
