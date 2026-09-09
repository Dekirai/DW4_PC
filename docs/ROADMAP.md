# Roadmap and acceptance criteria

This is a staged native-port project. A checked data reader or settings control
does not imply the original game has been ported. Dates are intentionally not
estimated before the missing engine and game logic have been measured.

| Stage | Deliverable | Acceptance | Status |
| --- | --- | --- | --- |
| 0 | Native Windows workbench; disc, archive, banner and IGB metadata readers | Builds as x64, rejects malformed synthetic inputs, reads verified original local data | Available |
| 1 | Native original-asset viewer | One actual game texture and mesh decoded from AFS/IGB with correct layout and transforms; no manually recreated substitute | Textured mesh, original single-clip animation, named packages and static scenes with explicit original switch commands available for verified profiles; full materials and original-frame comparison pending |
| 2 | First playable original scene | Load a scene, spawn and control a Digimon, collision, camera, animation, one enemy and verified combat | Native Agumon walking preview with original floor geometry, animations and host follow camera available; original actor lifecycle, complete collision, enemies and combat pending |
| 3 | Original game baseline | Progression, inventory, weapons, transitions, audio, saves and local 1–4-player play; repeatable reference comparisons | Not implemented |
| 4 | Full PC settings | Verified resolution, borderless/windowed, 16:9 camera/UI, 144 Hz presentation without faster simulation, VSync, MSAA and input rebinding | Host controls started; gameplay work pending |
| 5 | Content modding | Stable asset IDs, validated manifests, texture replacements and documented data schemas | Not implemented |
| 6 | Online four-player co-op | End-to-end multi-machine play, state consistency, latency/disconnect handling and save ownership | Not implemented |

## Next acceptance gate

The current acceptance target is the full original game, including local
one-to-four-player play with independent connected controllers. Quality-of-life
changes and optional enhancements follow that baseline. The walking preview
does not satisfy this target. Track reference evidence and outstanding original
systems in [ORIGINAL_BASELINE.md](ORIGINAL_BASELINE.md).

Version 0.22 adds explicit ordinary actor traversal, original float hierarchy
calculation and cached bone queries. Four original Agumon instances pass 720
traversals across their three appearances; independent hierarchy instruction
checks match 12,976 float words. Shared arrays persist through NDSP and are
invalidated by new local sampling. Complete renderer context/appearance
traversal and outer player scheduling remain required before gameplay integration.
See [ANIMATION_HIERARCHY.md](ANIMATION_HIERARCHY.md).

Version 0.21 adds prepared ACTION actor ownership and NDSP cleanup. All ordered
actor buffers belong to their selected RAM; activation, deactivation and
reactivation preserve the original work lifetime and retained node clocks.
Actor traversal, cached bone queries, messenger consumers and full player
construction remain required. See [ACTION_ACTORS.md](ACTION_ACTORS.md).

Version 0.20 adds prepared ACTION2 execution: exact action dispatch, retained
cursors, motion/joint/fade/wait/flash effects, original node timing and freeze.
All 182 ordinary Agumon actions complete their command/effect phases in local
core integration. Four actual weapon sequences additionally dispatch actions
into 240 poses; 20,000 numeric/state instruction comparisons pass. Complete
actor construction, broadcast consumers, scene attribute attachment and outer
player callbacks remain required before connecting original gameplay.
The preview and connected local multiplayer remain separate unfinished work.
See [ACTIONS.md](ACTIONS.md).

Version 0.19 adds prepared combiner decoding, owned states, priority-ordered
retirement, cached-pose snapshots and explicit MOTIONSET/motion-update effects.
Four original Agumon combiners pass 240 combined poses with interrupted weapon
idle/run transitions. Independent instruction checks cover 10,000 state updates
and 30,556 matrix/quaternion conversions. Version 0.20 adds the prepared ACTION2
path above; full actor/work construction and event integration remain. This
core increment does not change the walking preview or provide connected local
multiplayer. See [ANIMATION_COMBINER.md](ANIMATION_COMBINER.md).

Version 0.18 adds original animation clocks, MOTIONSET numeric blend fields,
source play modes and ordered pose-layer mixing. Instruction comparisons pass
30,000 timing operations and 10,000 layer tuples; all 82 Agumon clips pass timed
pose integration. Version 0.19 extends those primitives into the bounded
combiner path described above. The walking preview retains its provisional
behavior. See [ANIMATION_TIMING.md](ANIMATION_TIMING.md).

Version 0.17 retains model sequence programs, weapon label selection and command
positions in native runtime state. It runs the supported control/motion commands,
preserves label barriers and local jumps, and exposes required ACTION dispatch.
All 9,060 SEQ programs found in the original archive corpus decode; their
447,274 command occurrences include unsupported runtime effects. Decoding those
records is not execution of the whole corpus. Next implement ACTION manager
behavior, full sequence-entry/outer callbacks and original wait/resume rules,
then connect original player state, collision and camera. See [MODEL_SEQUENCE.md](MODEL_SEQUENCE.md).

Version 0.16 adds original CTRL/PDMOVE effects, movement timer/position-lock
preparation, player turn rates and post-input WAIT/WALK requests. All 668 original
CTRL/PDMOVE records decode; 10,000 bounded instruction comparisons pass. Explicit
command effects also pass at all eight stored entry matrices. Full player work
construction, named RAM ownership, input and sequence/action execution remain
the next boundary before collision and camera integration. These core changes
do not replace the provisional walking controller. See [MODEL_CONTROL.md](MODEL_CONTROL.md).

Version 0.15 connects the original motion channels to recovered turning and an
owned model matrix in their original order. Four original entry matrices and
Agumon commands pass data integration checks; original player preparation,
input/sequence/action dispatch, camera and collision response remain the next
boundary. These checks do not establish connected local multiplayer or original
gameplay. See [MODEL_MOTION.md](MODEL_MOTION.md).

Version 0.14 implements original scalar movement channels, MOVE/MOVEEX decoding,
local model translation and the cached elapsed timer. All 3,796 discovered
original commands decode, including 131 zero-frame records. The next movement
boundary is retained player/sequence state, original turning and collision
integration with reference comparisons. This core does not replace the preview's
provisional controller. See [MOTION.md](MOTION.md).

Version 0.13 separates preliminary loading from aggregate boot inspection and
implements StaticInfo/SwitchCtrl registration effects plus four independent
original pad states. The original preliminary sound gate still blocks complete
startup. These foundations do not satisfy the original multiplayer or game
baseline gates. See [CONTROLLERS.md](CONTROLLERS.md).

Version 0.12 adds a controllable original-map walking preview in response to the
owner's request for an initial interactive state. Its bounded host controller
follows decoded original floor collision triangles and uses original idle/run
clips. The camera and movement constants are provisional. Pause and restart are
native preview state; this does not complete original startup or stage 2's
enemy/combat criteria. Static original wall geometry now constrains a provisional
body circle; door state remains fixed. See [PLAY_PREVIEW.md](PLAY_PREVIEW.md).
Next, recover original movement and collision response, then one enemy and
verified combat.

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
Version 0.11 consumes that queue, retains the live Lua state and decoded files,
and reconstructs the original immediate/held/deferred info-routing order.
Continue by implementing the queued managers' real initialization callbacks,
the original loading-screen state and layer/text prerequisites, then the native
calls reached by `start.lua`. A retained info record is not an initialized
controller; generic registration invokes additional per-info and per-RAM work.
Do not skip an unsupported host operation or mark a later startup phase complete
because a script buffer exists. The separate walking preview explicitly loads
one verified asset profile; it does not mark those startup stages complete.

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
