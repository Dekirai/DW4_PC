# Roadmap and acceptance criteria

This is a staged native-port project. A checked data reader or settings control
does not imply the original game has been ported. Dates are intentionally not
estimated before the missing engine and game logic have been measured.

| Stage | Deliverable | Acceptance | Status |
| --- | --- | --- | --- |
| 0 | Native Windows workbench; disc, archive, banner and IGB metadata readers | Builds as x64, rejects malformed synthetic inputs, reads verified original local data | Available |
| 1 | Native original-asset viewer | One actual game texture and mesh decoded from AFS/IGB with correct layout and transforms; no manually recreated substitute | Textured mesh, original single-clip animation, named packages and static scenes with explicit original switch commands available for verified profiles; full materials and original-frame comparison pending |
| 2 | First playable original scene | Load a scene, spawn and control a Digimon, collision, camera, animation, one enemy and verified combat | Explicit animated player visual placement available; gameplay spawning, control, collision and combat pending |
| 3 | Original game baseline | Progression, inventory, weapons, transitions, audio, saves and local 1–4-player play; repeatable reference comparisons | Not implemented |
| 4 | Full PC settings | Verified resolution, borderless/windowed, 16:9 camera/UI, 144 Hz presentation without faster simulation, VSync, MSAA and input rebinding | Host controls started; gameplay work pending |
| 5 | Content modding | Stable asset IDs, validated manifests, texture replacements and documented data schemas | Not implemented |
| 6 | Online four-player co-op | End-to-end multi-machine play, state consistency, latency/disconnect handling and save ownership | Not implemented |

## Next acceptance gate

The Agumon viewer decodes original compressed animation tracks and evaluates
complete bone transforms, including helpers. Synthetic fixtures and original DOL
arithmetic establish the current sampling, quaternion and hierarchy profile.
The scene viewer now composes verified static transforms and preserves ordered
draw instances. Named packages preserve original member and export order. The
`BG02` map's disabled switch can now be enabled through its original exported
controller's `ALLON` command. That command is present in `first.lua`, and its
dispatcher and mask effect have been traced. The host currently replays it only
on explicit request. A selected original player model/instance/appearance can
now share this scene's depth target, with the original full entry-point matrix
replacing its authored outer model transform. Single-clip playback updates only
the visual's vertex range. Original startup dispatch order and player-matrix
assignment are documented. The first two startup scripts now execute natively;
later controller state remains unimplemented. Continue with startup dependencies
and player lifecycle, then recover movement, collision and the camera. Compare matching camera/animation
states against original frames before claiming visual parity. Explicit visual
placement and clip playback do not establish gameplay spawning or simulation.

Version 0.10 executes `func.lua` and `boot.lua` in a bounded native Lua 4 VM.
The host implements proven common-file queue methods and explicit inspection
state queries, retaining USA constant getters and original name/dedup rules.
Three profiles match official Lua 4.0.1 execution call-for-call and queue-for-queue.
Continue by consuming the queue and registering loaded info records, then
implement layer/text prerequisites and the native calls reached by `start.lua`.
Do not skip an unsupported host operation or mark a later startup phase complete
because a script buffer exists. The game still has no controllable player.

## Requested PC options

| Option | Current scope | Full-port target |
| --- | --- | --- |
| 3840×2160 | Host window preference | Verified 4K scene render target and readable UI |
| Borderless | Host fullscreen desktop window | Game renderer resizes correctly across monitors |
| 16:9 | Stored preference only | Camera, culling, HUD and menus adapted |
| 144 FPS | Workbench frame pacing | Presentation decoupled from measured original simulation |
| VSync Off | Host OpenGL swap-interval request | Validated frame pacing in gameplay |
| MSAA 8x | Host context and mesh/static-scene viewport requests; actual samples reported and offscreen resolve tested | Gameplay render passes and material coverage validation |
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
