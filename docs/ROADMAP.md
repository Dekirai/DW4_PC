# Roadmap and acceptance criteria

This is a staged native-port project. A checked data reader or settings control
does not imply the original game has been ported. Dates are intentionally not
estimated before the missing engine and game logic have been measured.

| Stage | Deliverable | Acceptance | Status |
| --- | --- | --- | --- |
| 0 | Native Windows workbench; disc, archive, banner and IGB metadata readers | Builds as x64, rejects malformed synthetic inputs, reads verified original local data | Available |
| 1 | Native original-asset viewer | One actual game texture and mesh decoded from AFS/IGB with correct layout and transforms; no manually recreated substitute | Textured object-space mesh viewer available for a verified binding profile; scene transforms, full materials and reference comparison pending |
| 2 | First playable original scene | Load a scene, spawn and control a Digimon, collision, camera, animation, one enemy and verified combat | Not implemented |
| 3 | Original game baseline | Progression, inventory, weapons, transitions, audio, saves and local 1–4-player play; repeatable reference comparisons | Not implemented |
| 4 | Full PC settings | Verified resolution, borderless/windowed, 16:9 camera/UI, 144 Hz presentation without faster simulation, VSync, MSAA and input rebinding | Host controls started; gameplay work pending |
| 5 | Content modding | Stable asset IDs, validated manifests, texture replacements and documented data schemas | Not implemented |
| 6 | Online four-player co-op | End-to-end multi-machine play, state consistency, latency/disconnect handling and save ownership | Not implemented |

## Next acceptance gate

The textured Agumon preview is a stored-geometry milestone. Continue stage 1 by
recovering the character's transform hierarchy, skin palette and vertex weights.
Validate reference ranges, matrix conventions and weight behavior with synthetic
fixtures and original-data evidence before applying them. Then display a verified
pose and scene placement, and evaluate an original animation against a recorded
reference frame. A textured mesh alone does not complete this gate or establish
movement, collision or combat behavior.

## Requested PC options

| Option | Current scope | Full-port target |
| --- | --- | --- |
| 3840×2160 | Host window preference | Verified 4K scene render target and readable UI |
| Borderless | Host fullscreen desktop window | Game renderer resizes correctly across monitors |
| 16:9 | Stored preference only | Camera, culling, HUD and menus adapted |
| 144 FPS | Workbench frame pacing | Presentation decoupled from measured original simulation |
| VSync Off | Host OpenGL swap-interval request | Validated frame pacing in gameplay |
| MSAA 8x | Host context and mesh viewport requests; actual samples reported and offscreen resolve tested | Full scene target and material coverage validation |
| Keyboard / Mouse | Tool UI input | Complete bindings and appropriate mouse camera support |
| Xbox / PlayStation | SDL device inspection | Tested gameplay mappings, hotplug and prompts |
| GameCube | SDL-mapped devices when available | Original mapping, dead zones and adapter support |
| Original / Modernized | Stored preference only | Original behavior baseline plus explicit optional changes |

## Requested extensions

- **HD texture packs:** replacement lookup after reliable texture decoding.
- **Mod loader:** versioned manifests, load order, stable IDs and validation.
- **Custom maps:** scene, collision, navigation and spawn schemas plus import tools.
- **Custom Digimon:** actor definitions, meshes, rigging, animations and abilities.
- **Custom weapons:** weapon data, models, animation hooks and damage behavior.
- **Randomizer:** identified data tables and reproducible seeds, with save metadata.
- **Difficulty rebalancing:** optional, versioned changes after original combat tests.
- **Bug fixes:** reproductions and regression checks against recovered behavior.
- **Online four-player co-op:** follows the simulation/local multiplayer gates above.

Keep each increment buildable and publish source progress to this repository.
Do not distribute the user's ISO, DOL, extracted assets or generated asset packs.
