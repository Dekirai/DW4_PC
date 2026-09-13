# Digimon World 4 PC

An early native PC port project for **Digimon World 4 (USA, GameCube)**.

The selected implementation path is now **DolRecomp with the ModernGekko
template**: compiled original PowerPC logic with a Dolphin-derived runtime for
graphics, audio and console services. The supplied USA executable translates
to C, and the runtime and complete Windows game module now build locally.
Bounded original startup and an offscreen original boot image are verified;
complete gameplay, audio, saves and controller validation remain in progress. See
[RECOMPILATION.md](docs/RECOMPILATION.md) for the tool inputs and measured results.
Builds and tests run locally; GitHub Actions is disabled.

See [PORTING_STRATEGY.md](docs/PORTING_STRATEGY.md) for the September 13 project
review, retained work, tool comparison and concrete migration checkpoints.
`Play-Recompiled-DW4.cmd` starts the verified developer recompilation with its
separate `work/recomp-user` profile. Existing `Play-DW4-PC.cmd` and
`Start-DW4-PC.cmd` start the older manual walking preview/workbench.
To keep using a packaged preview's imported data and saves, launch that
package's `DW4_PC.exe` directly.

The current priority is performance: the first diagnostic build was slow and
the owner reports audio stutter at the main menu. The optimized build now
advances about 60 unique-frame counter steps per second at normal simulation
speed in a bounded boot test, compared with about 40 in the diagnostic build.
This does not yet validate gameplay performance or audible output.
The guarded scheduler optimization also reduces measured boot CPU cost by
about 46% per frame-counter advance in the final package comparison, at normal
simulation speed. See [RECOMP_PERFORMANCE.md](docs/RECOMP_PERFORMANCE.md).
Real-device audio diagnostics now distinguish source activity, mixer gaps and
Cubeb callback timing during finite hidden tests at zero device volume. See
[RECOMP_AUDIO.md](docs/RECOMP_AUDIO.md) for the method and its limits.
The longer opening-movie test exposes a later slowdown to approximately 30%
simulation speed at both 1x and 3x EFB, with repeated audio-buffer replay.
The earlier logo performance does not establish smooth movie or menu playback.
An opt-in CPU probe identifies the original movie's pixel-conversion block.
A rebuilt package reduces CPU cost in one counter-aligned comparison with its
immediate predecessor, but movie speed and audio-source replay remain inadequate.
See [RECOMP_CPU_PROFILING.md](docs/RECOMP_CPU_PROFILING.md) and the bounded
[comparison](docs/RECOMP_PERFORMANCE.md#rebuilt-module-comparison).
A controlled intro skip and second Start press now reach the original
"Game Start / Option" main menu. Its short 1x test runs near 60 counter advances/s
at normal simulation speed, with no audio FIFO replay in the final window.
Audible quality, higher-resolution menu performance and gameplay remain unverified.
The PC target includes at
least 60 FPS on measured hardware, presentation options up to 300 FPS without
faster gameplay, 16:9, higher render resolution, HD textures and an ImGui menu
for graphics, cheats, save states and other QoL. Mods, custom content and online
four-player co-op remain planned. These features are not established by the
existing walking preview's settings. [The roadmap](docs/ROADMAP.md) separates
the active acceptance gates from earlier manual-engine work.

The local experimental package now starts through `DW4_PC.exe`, accepts the
user's USA GameCube ROM and prepares its required files automatically. Its
imported files match the independent extracted reference; original assets are
not bundled. End users need no compiler or manual disc extraction. Developers
can create a package with `scripts/package-recomp.ps1` after building, or use
`scripts/start-recomp.ps1` for the separate developer profile. Builds never
launch the game automatically. See [ROM_IMPORT.md](docs/ROM_IMPORT.md).

The application now hosts the game in-process; the package no longer includes
or requires `moderngekko-run.exe`. The default `savedata` directory sits beside
the actual EXE. Earlier AppData/Documents profiles are preserved. New builds
embed the required service resources and omit the upstream `Sys` tree. The
game module in `data/game.dll` and Visual C++ runtime DLLs remain dependencies
while work continues toward a single-file application. F1 opens an in-game ImGui menu
with pause/resume, save-state actions, mute, fullscreen and live graphics controls;
the old native File/View menu is removed. Graphics changes currently apply only
to the running session. See [RECOMP_INGAME_MENU.md](docs/RECOMP_INGAME_MENU.md)
for the implemented controls and bounded validation. The keyboard editor adds
saved per-player key bindings alongside existing gamepad controls. Packaging,
resource embedding and legacy configuration migration are described in
[PC_APPLICATION.md](docs/PC_APPLICATION.md), including validation boundaries.
The revised internal save layout remains pending.

The [language-data audit](docs/LOCALIZATION.md) found that the French, German,
Italian and Spanish named text packs duplicate English in the supplied USA
data. Japanese is a candidate requiring runtime validation. An in-game language
selector remains planned; pack names alone do not establish translated modes.

## Retained manual core and walking preview

The existing manually recovered core remains available for reference and tests.
Version 0.45 connects original static geometry to the native collision candidate
pipeline. Canonical vertex/index/group owners feed the original primitive scan,
typed node dispatch and actual query result storage. Point, segment, matrix and
triangle operations retain their original arithmetic and publication order.
Dynamic bound/inverse operations and the complete wall/floor query handlers
remain required before the original player can consume a complete collision
response. See [HITLAND_GEOMETRY.md](docs/HITLAND_GEOMETRY.md),
[HITLAND_PRIMITIVES.md](docs/HITLAND_PRIMITIVES.md) and
[HITLAND_NODE_DISPATCH.md](docs/HITLAND_NODE_DISPATCH.md).

This is a core execution milestone. Original four-slot GENERATE acceptance
and its prepared service prerequisites are tracked in
[VALIDATION.md](docs/VALIDATION.md). The original frame scheduler, device-to-player
assignment, gameplay camera, complete collision queries, combat and complete
game remain unfinished. The visible
walking preview is unchanged. The full original game, including local
one-to-four-player play, remains the target. See
[ORIGINAL_BASELINE.md](docs/ORIGINAL_BASELINE.md).

**The native walking preview lets you control Agumon in an original
map with keyboard or an SDL-mapped gamepad.** It uses original map, model,
textures, floor/wall collision geometry, ACTION animations and weapon attachment.
Movement and the
follow camera are provisional native implementations. The complete original
game, combat, progression, audio and saves remain unimplemented.

The existing walking-preview application uses SDL3, OpenGL and Dear ImGui. It
reads your original local files directly. In this separate application, the
PowerPC `main.dol` is inspected as data and never executed. A bounded native Lua
4 interpreter executes selected embedded
boot bytecode with explicitly implemented host methods. This repository does
not contain game files.

## Manual-core and walking-preview capabilities

- Produce original static triangle candidates through canonical
  `beHitLandModelInfo` selection, registered node handlers and live geometry
  attribute/vertex/index owners. Results retain the actual query segment,
  matrix-stack element, node path and original group/stream cursor. Dynamic
  source matrices still update before broad-phase rejection; complete dynamic
  geometry operations, CHECKLAND/CHECKWALL response selection and HITLANDMODEL
  delivery remain required. See [HITLAND_GEOMETRY.md](docs/HITLAND_GEOMETRY.md).

- Route original player wall/floor checks through canonical HITLANDDELIVER
  info/RAM records with a distinct manager and shared model nodes. The five
  recipient commands preserve their original query/response and matrix-update
  order. Original-data integration reaches the actual HITLANDMODEL query
  boundary; complete triangle traversal and wall/land result selection remain
  required. Original direction normalization and XZ broad-phase arithmetic
  have separate instruction comparisons. See
  [MODEL_MOVEMENT_COLLISION.md](docs/MODEL_MOVEMENT_COLLISION.md),
  [HITLAND_DELIVER.md](docs/HITLAND_DELIVER.md) and
  [HITLAND_MATH.md](docs/HITLAND_MATH.md).

- Execute one actual player-manager frame with independently captured input,
  status, motion and wait invocations. Direct wait completion resumes the live
  command loop; delayed subcommands read the current system delta per entry.
  Required camera, sound/effect, position-lock, ride and collision-query operations
  retain explicit boundaries. This is core integration, not a complete game
  loop or connected multiplayer. See [MODEL_FRAME.md](docs/MODEL_FRAME.md),
  [MODEL_MOTION_ACTUAL.md](docs/MODEL_MOTION_ACTUAL.md),
  [MODEL_WAIT_ACTUAL.md](docs/MODEL_WAIT_ACTUAL.md) and
  [MODEL_SUBCOMMAND_UPDATE.md](docs/MODEL_SUBCOMMAND_UPDATE.md).

- Execute ordinary MODELCTRL entries against actual mutable sequence and command
  owners, preserving first-name lookup, list aliases, selected data and the
  captured work generation. Cleanup, direct JUMP, command-loop return and player
  `+B8` run in their original order. Required external services retain explicit
  boundaries. See [MODEL_SEQUENCE.md](docs/MODEL_SEQUENCE.md) and
  [MODEL_DISPATCH.md](docs/MODEL_DISPATCH.md).

- Dispatch defined ACTION programs through the native messenger, retaining
  animation/service owners, waits and incomplete broadcasts. Existing ACTION
  queries keep their own behavior. MODELCTRL timers resolve current manager
  context at each helper; the existing ACTION runtime retains the timestamp of
  each accepted call across host retries. See [MODEL_TIMER.md](docs/MODEL_TIMER.md) and
  [ACTION_DISPATCH.md](docs/ACTION_DISPATCH.md).

- Query original PLAYERPOINT groups through the native messenger and apply the
  original vector/matrix placement rules to the actual model node. Shared node
  matrices survive retained work owners, while candidate motion updates remain
  independent until publication. These helpers are connected to fresh player-work
  allocation and the retained GENERATE caller. See [PLAYER_POINT.md](docs/PLAYER_POINT.md),
  [MODEL_PLACEMENT.md](docs/MODEL_PLACEMENT.md) and [PLAYER_SPAWN.md](docs/PLAYER_SPAWN.md).

- Complete the prepared post-GETPLAYER startup through all five original
  number/meter messages and the map-dependent metal/main-lobby tail. NUMBERCTRL
  mutates actual digit UV owners, METERCTRL preserves shared HP/MHP bindings and
  node animation times, and PLAYER SETMETAL retains the real status lists and
  nested GAMERAM row. Independent original-instruction comparisons cover the
  caller and recipients. The walking preview remains the existing original-asset
  walking profile; these changes do not yet draw its HUD or create a complete
  gameplay session. See [MODEL_PLAYER_STARTUP.md](docs/MODEL_PLAYER_STARTUP.md),
  [NUMBER_CONTROL.md](docs/NUMBER_CONTROL.md), [METER_CONTROL.md](docs/METER_CONTROL.md)
  and [MODEL_STATUS.md](docs/MODEL_STATUS.md).

- Traverse common weapon children through their actual ordered attribute scopes,
  shared geometry owners and stored transforms. Render body and weapons with
  separately captured model-views and one shared depth/MSAA target. Original
  ACTION and PLAYERARMS messages run on the same prepared actor that is drawn.
  This uses an explicit reset graphics baseline and the host inspection camera;
  complete original materials, compiled traversal and the game frame remain
  pending. See [SCENE_TRAVERSAL.md](docs/SCENE_TRAVERSAL.md),
  [PREPARED_RENDERING.md](docs/PREPARED_RENDERING.md) and
  [WEAPON_PREVIEW.md](docs/WEAPON_PREVIEW.md).

- Decode original weapon node/bone/sequence graphs, preserve shared loaded scene
  owners, and execute prepared PLAYERARMS GENERATE/REMOVE through real PLAYER,
  ACTION and MODELCTRL endpoints. Four original Agumon slots pass deferred
  generation and two-hand weapon changes across all twelve appearance branches.
  The common bone-selector calculation is independently compared against original
  instructions. Original frame scheduling and gameplay remain
  required. See [WEAPON_RUNTIME.md](docs/WEAPON_RUNTIME.md),
  [ACTOR_SCENE.md](docs/ACTOR_SCENE.md) and [BONE_SELECT.md](docs/BONE_SELECT.md).

- Retain original deferred message packets, signed deadlines, pause shifts and
  callback order through missing-service interruptions. The prepared player
  caller resets its modifier prefixes, starts unused data, queues PLAYERARMS
  and recalculates stats. Its original UI and map tail now continues through
  the recipient owners described above and the actual player constructor.
  The original outer loop remains required.
  See [DEFERRED_MESSENGER.md](docs/DEFERRED_MESSENGER.md) and
  [MODEL_PLAYER_START.md](docs/MODEL_PLAYER_START.md).

- Initialize selected player data through the original ordered GAMESTART path,
  retaining database calls and shared objects across interruptions. Four Agumon
  player slots complete local integration with the supplied original databases
  and an explicitly prepared font. Complete startup and gameplay
  scheduling remain required. See [GAME_START.md](docs/GAME_START.md),
  [GAME_ITEMS.md](docs/GAME_ITEMS.md), [GAME_STATS.md](docs/GAME_STATS.md) and
  [GAME_NAME.md](docs/GAME_NAME.md).

- Decode the original six database column groups and retain their typed owners
  through DATABASE registration and GETDATABASE. GAMERAM resolves weapon or
  technique skills before the original no-growth gate, including a retained
  pcTech query. Actual skill growth and damage effects remain required.
  See [DATABASE.md](docs/DATABASE.md),
  [DATABASE_RUNTIME.md](docs/DATABASE_RUNTIME.md) and [GAME_RAM.md](docs/GAME_RAM.md).

- Construct the original common-data list and four separate player-data rows,
  preserve shared objects across resets, and route GETPLAYER/GAMEDATAINIT through
  the native GAMERAM. Selected original Hit scripts reach its verified magic
  dispatch gates; skill growth and actual damage/status effects remain required.
  See [GAME_DATA.md](docs/GAME_DATA.md), [GAME_RAM.md](docs/GAME_RAM.md) and
  [MODEL_SCRIPT_MESSENGER.md](docs/MODEL_SCRIPT_MESSENGER.md).

- Execute selected target-Hit chunks in one shared Lua VM, retain incomplete
  native calls and nested scripts, and apply the original conditional HP merge.
  Explicit script registration and model work remain prerequisites; damage
  services and original per-frame collision/script integration are still required. See
  [MODEL_SCRIPT_RUNTIME.md](docs/MODEL_SCRIPT_RUNTIME.md).

- Route original HITBOX queries to source-qualified target bodies, preserve
  timestamp-based box updates and resume nested record/hit-script operations.
  Original damage services and per-frame collision integration remain required.
  See [MODEL_COLLISION.md](docs/MODEL_COLLISION.md) and
  [MODEL_HIT_BODY.md](docs/MODEL_HIT_BODY.md).

- Run original HITBOX Set/AllClear commands in the same ordered queue as sounds
  and effects, and send their checked boxes through the original messenger.
  MUTEKI fields and LUACALL callback assignment retain their original sequence
  behavior. Complete collision managers and damage/status script services remain
  required before combat works. See [MODEL_HITBOX.md](docs/MODEL_HITBOX.md) and
  [COLLISION_MATH.md](docs/COLLISION_MATH.md).

- Decode original WCHECK occurrences, retain each player's attack wait/latch,
  and resume the live sequence cursor through qualified ACTION queries.
  The actual player-frame input/wait caller and direct ordinary entry are
  connected in the core; device assignment and connected multiplayer remain
  pending. See [MODEL_WAIT.md](docs/MODEL_WAIT.md) and
  [MODEL_WAIT_ACTUAL.md](docs/MODEL_WAIT_ACTUAL.md).

- Bind an original MODELCTRL/ACTION instance through its actual node and actor
  graph, route loaded Se/ActionStarter consumers across source files, and retain
  delayed sound/effect work through service suspension and sequence cleanup.
  Original audio/effect generation managers and the full game frame remain pending.
  See [ACTION_EVENT_RUNTIME.md](docs/ACTION_EVENT_RUNTIME.md).

- Play the original `dng101b1` / `BG02` map with camera-relative movement,
  original floor following, automatic idle/run animation, pause and restart.
  See [PLAY_PREVIEW.md](docs/PLAY_PREVIEW.md) for controls and current limits.
- Validate GameCube headers and DOL sections; identify the USA release.
- Index both AFS archives without loading the entire large files into memory.
- Browse archive entries and read bounded byte previews.
- Read Alchemy IGB v6 metadata, serialized objects, memory buffers and references.
- Resolve logical game paths from `ndmw.igb` to checked AFS archive entries.
- Read named package descriptors, preserve their ordered members and `/s`
  loading-stage metadata, and inspect explicitly selected member exports.
- Read exported embedded Lua records and resolve scheduled script names with the
  original first-match-per-info rule, preserving multiple registered matches.
- Execute original `func.lua` and `boot.lua` with persistent Lua globals and
  explicit player/difficulty/camera inspection inputs. Build the original
  common-resource queue, preserve exact duplicate semantics and resolve requests
  using the original ASCII lowercase lookup.
- Keep the successful Lua state and all initial/common IGB documents alive in
  an owned inspection session, with source-qualified export routing. Its
  aggregate routes span stages and are not the original later registration queue.
- Load only preliminary `/s` members before ordinary package files and boot.
  Initialize supported StaticInfo/SwitchCtrl callbacks into owned RAM, manager
  and messenger state; stop the ordered callback prefix at an unknown handler.
  See [CONTROLLERS.md](docs/CONTROLLERS.md).
- Process four independent original pad states in the portable core, with
  recovered button edges, stick directions and repeat timing. Device assignment
  and an original multiplayer gameplay session remain pending.
- Advance original six-mode motion objects with preserved speed, expiry,
  zero-frame arithmetic and a shared original elapsed timer. Decode MOVE/MOVEEX
  records and apply local model-basis translation. See [MOTION.md](docs/MOTION.md).
- Apply recovered Euler turning before local movement, preserving arc/timer
  rules, matrix arithmetic and independently owned model state. Original
  per-frame input, movement and collision integration remain pending; the preview
  still uses its provisional controller. See [MODEL_MOTION.md](docs/MODEL_MOTION.md).
- Decode CTRL/PDMOVE commands, prepare movement timers and heading, and apply
  the original post-input WAIT/WALK request rules. Player turn rates are recovered;
  the original post-input frame caller remains pending. These core
  effects do not change the preview. See [MODEL_CONTROL.md](docs/MODEL_CONTROL.md).
- Run supported original sequence commands with retained cursors and label caches.
  Preserve weapon-section boundaries, nested local jumps and sequence-stop motion.
  Actual entries send ACTION messages to retained action runtimes and complete
  the player callback after a real loop return. Original frame scheduling remains
  pending. See [MODEL_SEQUENCE.md](docs/MODEL_SEQUENCE.md).
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
- Sample decoded clips through recovered integer animation clocks and shared
  source caches; prepare original blend weights and mix ordered bone layers.
  See [ANIMATION_TIMING.md](docs/ANIMATION_TIMING.md).
- Read prepared original animation combiners, retain independent state clocks,
  retire completed transitions and resume interrupted blends from the last cached
  pose. Apply explicit MOTIONSET and motion-update effects to a resolved clip.
  Actor-aware execution and the prepared preview use these owners; additional
  original combiner and material profiles remain pending.
  See [ANIMATION_COMBINER.md](docs/ANIMATION_COMBINER.md).
- Execute prepared ACTION2 programs with original motion, joint, fade, wait and
  flash commands, same-action suppression, named timers and freeze behavior.
  Four original Agumon sequence instances drive their own combined poses in core
  integration tests. Prepared actor groups now allocate all ordered actor buffers
  and release them through the original NDSP path, preserving node clocks for
  reactivation. Ordinary actor traversal and cached bone queries now use the
  original shared hierarchy. Loaded event consumers and ordinary appearance
  traversal are connected for verified profiles; complete original rendering
  and frame scheduling remain pending. See [ACTIONS.md](docs/ACTIONS.md) and
  [ACTION_ACTORS.md](docs/ACTION_ACTORS.md).
- Evaluate the original no-modifier bone/joint hierarchy with ordered float
  matrix operations, helper bones, retained cache validity and node-adjusted
  traversal time. See [ANIMATION_HIERARCHY.md](docs/ANIMATION_HIERARCHY.md).
- Render an explicitly selected actor's main skin using its original shared
  palette caches and recovered single-influence CPU deformation. The captured
  root is applied once before projection. This prepared clip view retains a
  host inspection camera. See [ACTOR_SKIN.md](docs/ACTOR_SKIN.md).
- Traverse verified static scene exports with original transforms, ordered
  switch selection and separate instances for shared graph paths.
- Inspect placed map geometry under one camera and depth target, with explicit
  root-child inspection for stored disabled exports and per-instance textures.
- Apply the verified `ALLON` or `ALLOF` command of an exported switch controller
  to its selected scene root, with command provenance and unchanged source bytes.
- Follow an explicitly selected player model instance and appearance branch into
  its original skin, then replace the outer model transform with a checked map
  entry-point matrix. Animate that visual alongside the map in one depth target.
- Focus the inspection camera on the placed visual, select its original clips,
  seek, play/pause and update only its GPU vertex range during playback.
- Render a bounded offscreen mesh viewport with depth testing and optional MSAA.
- Decode and display the original 96×32 banner from `opening.bnr` natively.
- Configure workbench window resolution, display mode, VSync and frame limit.
- Request OpenGL MSAA and report the actual sample count provided by the driver.
- Inspect SDL-detected gamepads and live input.
- Save settings locally and validate malformed configuration values.

Graphics settings currently affect the **walking preview, development window and asset viewers**. Saved gameplay,
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

Select **Play walking preview** on the Overview page. Move with **WASD**, arrow
keys or a gamepad's left stick; right-drag to orbit, use the wheel to zoom,
**Escape / Start** to pause and **R** to restart. To launch directly:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --play
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

All 82 Agumon clips decode under the current profile. To render through the
prepared original actor's shared hierarchy and palette caches, select its
ACTION2 info, RAM slot and ordered actor explicitly:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' `
  --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured `
  --actor-info 546 --actor-ram 0 --actor-index 1 `
  --animation waitS --animation-time 0.4 --animation-play
```

This selects Agumon's high-detail main skin. The geometry must belong to the
selected actor; mismatched selections reject. Clip playback uses original
node/source clocks, cached float hierarchy and CPU skin arithmetic, with an
explicit host fitting transform and inspection camera. Action dispatch/events,
automatic LOD and original-frame comparison remain separate work. See
[ACTOR_SKIN.md](docs/ACTOR_SKIN.md).

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
effect for one controller; this scene view does not run original scripts or game startup.
Use `dw4_catalog EXTRACTED_ROOT --package dng101b1.igb --member 1` to inspect
member/export provenance without opening the graphics app. See
[PACKAGE_VIEWER.md](docs/PACKAGE_VIEWER.md) and [SCENE_CONTROLS.md](docs/SCENE_CONTROLS.md).

To add an animated Agumon visual at the first explicitly selected entry point:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' `
  --package dng101b1.igb --package-member 1 --scene-root 1 `
  --scene-switch-control 3471 --scene-switch-command all-on --scene-mode textured `
  --scene-player-resource _common/p_agumon.igb --scene-player-model 1386 `
  --scene-player-slot 0 --scene-player-branch 1 `
  --scene-player-point 115 --scene-player-group 0 `
  --scene-player-animation waitS --scene-player-play
```

The **Player** tab exposes the model, placement and clip selectors. **Focus
player** recenters the camera; **Fit** shows the map. These are explicit
inspection choices, including the entry group and appearance branch. The
application does not execute the original player-generation command or choose a
gameplay spawn automatically. One visual is supported at a time; slot 0–3 selects
the original model instance and matching point, without creating a multiplayer
session. See [PLAYER_VIEWER.md](docs/PLAYER_VIEWER.md).

Open **Startup** to execute a package's first two scheduled scripts. This runs
the original Lua bytecode through native C++ code, with a strict set of resource
queue and state-query bindings. The default inspection inputs have no joined
players, difficulty 0 and camera mode 0; they are not imported save data.

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' `
  --boot-package dng101b1.igb

.\dist\DW4_PC\dw4_boot.exe 'E:\path\Digimon World 4 - Extracted' `
  --package dng101b1.igb --boot-player-0 p_agumon `
  --boot-weapon-0-0 w_sword01 --boot-weapon-0-1 w_gun01
```

The first example produces 94 distinct resource requests. The second explicitly
configures one joined player with two weapon names and produces seven. Original
USA country, area and pack-only methods keep their verified constant return
value 1. The queue's exact request spelling is retained, while catalog lookup
lowercases ASCII A–Z as the original resolver does. Every ordered host call and
queue entry for three test profiles matches an independent official Lua 4.0.1
reference run.

The inspection session also loads and retains every requested IGB document. The one-player
example owns nine files containing 454 exports; the original routing rules
produce one Lua info, 17 held scenes and 436 aggregate deferred infos. The Startup page
exposes Resources, Loaded files and Info routing views. The actual Lua state
and its function definitions remain alive alongside the loaded documents.

This inspection bypasses preliminary initialization and the sound-transfer gate,
which the original performs before func/boot. Its aggregate queue must not be
replayed as the original later registration queue. The original loading-screen
state, remaining runtime managers and layer/text initialization are still pending.
`start.lua` and `first.lua` do not run; this page
does not automatically create a player or turn the scene inspector into gameplay.
Unknown native effects stop explicitly. See [STARTUP_VIEWER.md](docs/STARTUP_VIEWER.md)
and [STARTUP_LOADING.md](docs/STARTUP_LOADING.md).

To exercise only the recovered preliminary loading and callback boundary:

```powershell
.\dist\DW4_PC\dw4_boot.exe 'E:\path\Digimon World 4 - Extracted' --package dng101b1.igb --preliminary
```

This command supplies a fresh prepared system/messenger callback context and
reads only `/s` members. On the original package it initializes StaticInfo and
stops at the next Sound callback, preserving the completed native effects.
Exit code 3 identifies the remaining native barrier; no ordinary files, func/boot
or common resources are executed. Full original manager construction and audio
services are separate prerequisites.

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
static map inspection through named packages and original switch commands, plus
explicitly placed animated player visuals. Original `func.lua` and `boot.lua`
execute natively in the inspection path. Preliminary loading and the first
controller callbacks now have separate native implementations. Actual player
construction, ordinary sequence execution and its player callback are connected
in the core. The next steps are original audio services and manager preparation,
layer/text state and connecting that lifecycle to `start.lua` and the original
per-frame input, movement, waits, collision and camera paths.
Original-reference comparisons and independent controller slots are required
throughout. Reconstructing
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
[Original animation timing](docs/ANIMATION_TIMING.md) ·
[Static scenes](docs/SCENES.md) ·
[Original scene commands](docs/SCENE_CONTROLS.md) ·
[Player visuals](docs/PLAYER_VISUAL.md) ·
[Player placement](docs/PLAYER_PLACEMENT_RESEARCH.md) ·
[Original startup](docs/STARTUP_RESEARCH.md) ·
[Original movement core](docs/MOTION.md) ·
[Embedded scripts](docs/SCRIPTS.md) ·
[Native Lua 4](docs/LUA4.md) ·
[Native renderer](docs/RENDERER.md) ·
[Upstream source audit](docs/UPSTREAM_AUDIT.md) ·
[Milestone validation](docs/VALIDATION.md)

The reference [ivanno4317/dw4-gc](https://github.com/ivanno4317/dw4-gc) is an
ongoing GameCube decompilation. It provides useful symbols and reconstructed
libraries, but does not currently contain complete gameplay source.
Dependencies, provenance and implementation boundaries are recorded in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
