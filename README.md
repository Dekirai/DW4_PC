# Digimon World 4 PC

An early native PC port project for **Digimon World 4 (USA, GameCube)**.
**Version 0.8 adds named package loading and explicit original switch-controller
commands; the game is not playable yet.** Startup scheduling, combat and game
saves have not been ported.

The Windows x64 application uses SDL3, OpenGL and Dear ImGui. It reads your
original local files directly. The PowerPC `main.dol` is inspected as data;
game code is not executed. This repository does not contain game files.

## Current capabilities

- Validate GameCube headers and DOL sections; identify the USA release.
- Index both AFS archives without loading the entire large files into memory.
- Browse archive entries and read bounded byte previews.
- Read Alchemy IGB v6 metadata, serialized objects, memory buffers and references.
- Resolve logical game paths from `ndmw.igb` to checked AFS archive entries.
- Read named package descriptors, preserve their ordered members and `/s`
  loading-stage metadata, and inspect explicitly selected member exports.
- Decode original indexed8/RGBA8 and BC1 textures into native RGBA pixels.
- Browse textures with zoom, pan, filtering and an alpha checkerboard.
- Decode original triangle lists and indexed triangle strips from IGB geometry.
- Inspect original models in 3D with orbit, pan, zoom, solid and wireframe modes.
- Resolve explicit local or inherited texture bindings and show supported meshes
  with their original UVs, base image and verified linear/repeat sampler.
- Read verified single-influence skin weights, joint palettes and inverse-bind
  matrices; inspect the joint hierarchy and manually pose supported meshes.
- Decode bounded Enbaya animation streams and inspect supported original clips
  with time seeking and playback, including helper-bone transforms.
- Traverse verified static scene exports with original transforms, ordered
  switch selection and separate instances for shared graph paths.
- Inspect placed map geometry under one camera and depth target, with explicit
  root-child inspection for stored disabled exports and per-instance textures.
- Apply the verified `ALLON` or `ALLOF` command of an exported switch controller
  to its selected scene root, with command provenance and unchanged source bytes.
- Render a bounded offscreen mesh viewport with depth testing and optional MSAA.
- Decode and display the original 96×32 banner from `opening.bnr` natively.
- Configure workbench window resolution, display mode, VSync and frame limit.
- Request OpenGL MSAA and report the actual sample count provided by the driver.
- Inspect SDL-detected gamepads and live input.
- Save settings locally and validate malformed configuration values.

Graphics settings currently affect the **development window and asset viewers**. Saved gameplay,
aspect ratio and controller preferences are preparation for future work;
they do not implement game features. A 144 FPS workbench does not establish
that DW4 gameplay runs correctly at 144 FPS.

## Build on Windows

Requirements: Visual Studio with **Desktop development with C++**, Windows SDK
and CMake/Ninja. Visual Studio Insiders installations are supported. The first
application build downloads two open-source dependencies pinned by commit and SHA-256.

```powershell
git clone https://github.com/Dekirai/DW4_PC.git
cd DW4_PC
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Package
```

The script builds and tests the project. The application package is written to
`dist/DW4_PC/`. Run `Start-DW4-PC.cmd` to launch it using the local configuration
in `local/settings.ini`. On first launch, enter your extracted game directory
under **Original data** and select **Remember directory**. Alternatively:

```powershell
.\scripts\run.ps1 -Data 'E:\path\Digimon World 4 - Extracted'
```

You can also build manually in an x64 developer shell:

```powershell
cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release -DDW4_BUILD_APP=ON
cmake --build build/Release --parallel
ctest --test-dir build/Release --output-on-failure
cmake --install build/Release --prefix dist/DW4_PC
```

Build only the portable data readers and tests, without dependency downloads:

```sh
cmake -S . -B build/core -DDW4_BUILD_APP=OFF
cmake --build build/core --config Debug
ctest --test-dir build/core -C Debug --output-on-failure
python -m unittest discover -s tests -p "test_*.py" -v
```

## Run with your own game data

The expected input is Dolphin's extracted directory layout:

```text
Digimon World 4 - Extracted/
  sys/boot.bin
  sys/main.dol
  sys/fst.bin
  files/area.afs
  files/ndmw.afs
  files/ndmw.igb
  files/opening.bnr
```

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted'
.\dist\DW4_PC\dw4_inspect.exe 'E:\path\Digimon World 4 - Extracted' --json
.\dist\DW4_PC\dw4_inspect.exe 'E:\path\Digimon World 4 - Extracted\files\area.afs' --list
.\dist\DW4_PC\dw4_catalog.exe 'E:\path\Digimon World 4 - Extracted' --find agumon
```

Open **Textures** to choose a logical resource or a raw archive entry. These
examples display the original font atlas and an Agumon card, respectively:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --resource bectext.igb
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --archive area.afs --entry 621
```

Open **Meshes** for native 3D inspection. This example loads Agumon's high-detail
geometry from the original archive with its bound original texture; the second
command shows its wireframe:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --resource _common/p_agumon.igb --mesh 2 --mesh-mode wireframe
```

Drag to orbit, right-drag to pan, and use the wheel to zoom. **Fit** resets the
inspection camera. **Textured** applies the image reached through a verified
single-stage binding. The viewer reports unsupported or ambiguous bindings;
it never chooses an image by filename or list order. **Solid** is the default
plain inspection shader. The default view shows stored positions. Enable **Inspect
rig**, then use the left **Rig** tab for a supported skeleton, an X-ray joint
overlay and manual joint rotation. The reference is reconstructed from the original inverse-bind matrices;
manual rotation is an inspection control, not a decoded game animation.
This is not a playable character or loaded level.

The following example rotates the `LArm01` joint in Agumon's left arm while keeping the
original image, weights and camera normalization:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --rig --bone 8 --rotate-z 45
```

This sample contains 25 bones and 20 joint matrices; five helper bones have no
inverse-bind frame and cannot be manually posed by this profile. The animation
path uses the full hierarchy, including those helpers. In the **Rig** tab, choose
**Animation** to select an original clip, play/pause or seek. For a reproducible
paused inspection:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --animation runD --animation-time 0.2
```

All 82 Agumon clips decode under the current profile. This is a single-clip asset
viewer: actor state, animation blending, action events, actor scene placement and
comparison against captured original gameplay remain separate work.

Open **Scenes** for static map inspection. The following example selects the
`BG02` export in `ndmw.afs[179]` and explicitly inspects child zero of its disabled
root switch:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --archive ndmw.afs --entry 179 --scene-root 6 --scene-inspect-child 0 --scene-mode textured
```

This subtree contains 235 geometry instances placed through 219 transforms.
The amber inspection label identifies the root override; nested switches retain
their stored masks. Omitting `--scene-inspect-child` preserves the empty stored
selection. This is a view of the original map assets, not a loaded gameplay
level. The page exposes source paths, matrices and unsupported-state diagnostics.
See [SCENE_VIEWER.md](docs/SCENE_VIEWER.md) and [SCENES.md](docs/SCENES.md).

The same map can now be reached through its original named package and enabled
through the controller targeted by the original `first.lua` command:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' `
  --package dng101b1.igb --package-member 1 --scene-root 1 `
  --scene-switch-control 3471 --scene-switch-command all-on --scene-mode textured
```

Package scene indices follow the explicit info-list order, so this is index 1
instead of the raw-entry index 6. The **State** tab provides **ALLON**, **ALLOF**
and **Stored selection**. This executes the verified native child-selection
effect for one controller; original scripts and game startup are not running.
Use `dw4_catalog EXTRACTED_ROOT --package dng101b1.igb --member 1` to inspect
member/export provenance without opening the graphics app. See
[PACKAGE_VIEWER.md](docs/PACKAGE_VIEWER.md) and [SCENE_CONTROLS.md](docs/SCENE_CONTROLS.md).

All 2,274 recognized original IGB documents passed native object loading. All
25,906 `igImage` objects found in the two archives decoded without errors.
Those totals include repeated asset instances and verify loading, not each
image's appearance against the original renderer. The viewer currently decodes
base images; mipmaps and full game material reconstruction remain separate work.

With Python 3.10+, you can also compare the extracted files against the
unmodified ISO. This read-only tool outputs a JSON verification report:

```powershell
python tools/verify_game.py 'E:\path\Digimon World 4 - Extracted' --iso 'E:\path\Digimon World 4 (USA).iso'
```

Reference build: `GDJEB2`, DOL SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.
The verified disc header has revision byte **0**, while the reference README
calls its supported version "Rev 1". The DOL hash matches exactly. The workbench
performs structural checks; the Python verifier additionally checks this hash.

## Next milestones

Original textures, logical asset lookup and object-space mesh viewing are
available, including verified base-texture bindings, original clip decoding and
static map inspection through named packages and original switch commands.
The next step is startup scheduling, controller registration and actor placement,
with comparisons against captured original frames, followed by
an original scene with a controllable Digimon. Reconstructing
the missing gameplay and engine logic is the largest remaining part of the project.

[Roadmap and acceptance criteria](docs/ROADMAP.md) ·
[Architecture](docs/ARCHITECTURE.md) ·
[Resource catalog](docs/RESOURCES.md) ·
[Package loading](docs/PACKAGES.md) ·
[Native texture formats](docs/TEXTURES.md) ·
[Mesh formats](docs/MESHES.md) ·
[Material bindings](docs/MATERIALS.md) ·
[Skinning](docs/SKINNING.md) ·
[Animation loading](docs/ANIMATIONS.md) ·
[Static scenes](docs/SCENES.md) ·
[Original scene commands](docs/SCENE_CONTROLS.md) ·
[Native renderer](docs/RENDERER.md) ·
[Upstream source audit](docs/UPSTREAM_AUDIT.md) ·
[Milestone validation](docs/VALIDATION.md)

The reference [ivanno4317/dw4-gc](https://github.com/ivanno4317/dw4-gc) is an
ongoing GameCube decompilation. It provides useful symbols and reconstructed
libraries, but does not currently contain complete gameplay source.
Dependencies, provenance and implementation boundaries are recorded in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
