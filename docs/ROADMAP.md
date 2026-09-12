# Roadmap and acceptance criteria

This is a staged native-port project. A checked data reader or settings control
does not imply the original game has been ported. Dates are intentionally not
estimated before the missing engine and game logic have been measured.

| Stage | Deliverable | Acceptance | Status |
| --- | --- | --- | --- |
| 0 | Native Windows workbench; disc, archive, banner and IGB metadata readers | Builds as x64, rejects malformed synthetic inputs, reads verified original local data | Available |
| 1 | Native original-asset viewer | One actual game texture and mesh decoded from AFS/IGB with correct layout and transforms; no manually recreated substitute | Textured mesh, original single-clip animation, named packages and static scenes with explicit original switch commands available for verified profiles; full materials and original-frame comparison pending |
| 2 | First playable original scene | Load a scene, spawn and control a Digimon, collision, camera, animation, one enemy and verified combat | Native Agumon walking preview available; actual construction/sequence and player-frame input/status/motion/wait connected in the core; original scheduling, camera/collision response, enemies and combat pending |
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

Version 0.43 connects that actual player frame through its outer wall/floor
consumer and shared-node HITLANDDELIVER recipient. The first original Agumon
frame reaches a real HITLANDMODEL query, using the authored previous-position
state. Native direction and broad-phase arithmetic have instruction comparisons;
the complete query graph, traversal and result selection remain required.
Four original Agumon GENERATE calls and this first frame prefix form the local
original-data acceptance scope. Results and explicitly prepared prerequisites
are tracked in [VALIDATION.md](VALIDATION.md).

The next gate is to connect that core lifecycle to the original startup and
per-frame execution order: assigned device input, remaining position-lock/ride
and HITLANDMODEL wall/land query responses, camera and rendering. Complete original manager
attachment, audio/effect services, HUD traversal, enemies and verified combat
remain part of that work. The visible walking demo is unchanged and still uses
its provisional controller and follow camera. Completing GENERATE does not
complete a gameplay frame or establish local multiplayer.

The integration also needs broader original evidence for shared-work execution
across distinct RAMs and ACTION/Lua-triggered nested sequence entry; those paths
currently stop at checked execution boundaries. The retained ACTION call
timestamp remains an explicit execution profile, separate from MODELCTRL's
per-helper live context reads. See
[ARCHITECTURE.md](ARCHITECTURE.md), [MODEL_DISPATCH.md](MODEL_DISPATCH.md) and
[ACTION_DISPATCH.md](ACTION_DISPATCH.md).

## Earlier milestone acceptance notes

The following versioned notes describe the implemented scope and outstanding
work at each earlier milestone. Their historical next steps are superseded by
the current gate above where those paths have since been connected.

Version 0.40 connects actual named RAM timers to movement, turning, MOVE/MUTEKI
and positive invulnerability countdown. Sequence/control staging publishes work
without reading or writing timers. Original four-slot Agumon WAIT/WALK branches
exercise that binding with explicit ACTION request boundaries. At that milestone
the next requirements were complete direct sequence entry, player `+B8`, a
retained execution bundle per work owner and original manager/context scheduling.
This timer integration does not change the visible walking demo or complete
the original gameplay loop. See [MODEL_TIMER.md](MODEL_TIMER.md).

Version 0.39 connects actual loaded RAM/ONE owners to native player construction
and the retained outer GENERATE branch. Four original Agumon owners can reach
the post-startup ordinary sequence boundary through original point and game-data
providers. Full direct sequence entry, player `+B8` and a retained execution
bundle per actual work owner were the next lifecycle requirements at that time.
Named RAM timers supplied a separate prerequisite for those consumers.

Version 0.38 supplies one canonical 68-field player-work allocation and routes
the existing checked MODELCTRL, motion/control, sequence/wait, script and status
consumers through it. Fresh defaults and retained old-generation views are
covered separately from prepared value publication. The next player-lifecycle
work continues with the full retained GENERATE continuation.

Version 0.37 establishes actual list storage for existing movement, PDMOVE,
subcommand and label consumers. Retained views share original allocations;
validation candidates detach and publication preserves existing owners. The
next player-lifecycle work requires actual RAM/ONE routing and the full retained
GENERATE continuation.

Version 0.36 connects original PLAYERPOINT queries to the recovered model
placement helper in prepared integration. Node matrices now outlive individual
work owners; fresh work allocation, the full GENERATE continuation and original
frame scheduling still remain. See [PLAYER_SPAWN.md](PLAYER_SPAWN.md).

Version 0.35 connects the original post-GETPLAYER caller to actual number,
meter and status recipients across four Agumon slots. Digit UV mutations,
shared HP/MHP bindings, meter sample times and map-dependent SETMETAL/main-lobby
effects retain original ownership and command order. Full earlier player
construction, HUD traversal, original input/sequence/camera scheduling and
combat remain required. See [MODEL_PLAYER_STARTUP.md](MODEL_PLAYER_STARTUP.md).

Version 0.34 connects common weapon child traversal and shared prepared GPU
rendering to the original ACTION owner. A refreshed walking preview displays
the original common05 weapon while host movement selects its stand/run actions.
Original geometry gates and normal attribute scope restoration are compared
against bounded original instructions. Complete original materials, compiled
traversal, player construction, game camera and locomotion/combat state machines
remain required. See [WEAPON_PREVIEW.md](WEAPON_PREVIEW.md).

Version 0.33 connects original weapon registration, GENERATE/REMOVE, live PLAYER
name queries, ACTION appearance/skeleton results and nullable MODELCTRL labels.
Four prepared original Agumon starts deliver their real deferred common05
requests, replace weapons across all twelve appearances and exercise the common
bone-selector calculation. Generic child rendering, the compiled selector path,
subsequent NUMBERCTRL/METERCTRL calls, full player construction and original
outer scheduling remain required. See [WEAPON_RUNTIME.md](WEAPON_RUNTIME.md),
[ACTOR_SCENE.md](ACTOR_SCENE.md) and [BONE_SELECT.md](BONE_SELECT.md).

Version 0.32 connects the prepared post-GETPLAYER MODELCTRL prefix through exact
modifier-array resets, GAMESTART, BASE_ISUSE, deferred PLAYERARMS GENERATE and
RECALCPLAYER. The messenger owns the original deferred queue and explicit manager
update continuation. Its prepared weapon-delivery boundary is extended in 0.33;
the later player UI and construction effects remain required before gameplay.
See [MODEL_PLAYER_START.md](MODEL_PLAYER_START.md) and
[DEFERRED_MESSENGER.md](DEFERRED_MESSENGER.md).

Version 0.31 connects original GAMESTART selection, base-array copying, initial
weapons, full represented status/technique calculation and both name routes.
All four Agumon data rows complete this command with explicitly prepared original
database and font services. Existing skill selection can consume their populated
WAZALIST and reach the remaining growth boundary. Full MODELCTRL player/weapon
construction, other dynamic item providers, skill growth, named damage/status
effects, startup and gameplay scheduling remain the next work. See
[GAME_START.md](GAME_START.md) and [GAME_RAM.md](GAME_RAM.md).

Version 0.28 connects prepared target-Hit work to ordered script registration,
shared HP/context publication and resumable original Lua chunks. Selected
Agumon early-return paths can execute; ordinary damage paths require their
actual native services. Complete player/GAMERAM construction, damage/status
effects and original gameplay scheduling remain the next integration work.
See [MODEL_SCRIPT_RUNTIME.md](MODEL_SCRIPT_RUNTIME.md).

Version 0.27 routes HITBOX queries through prepared target body caches, ordered
overlap gates and retained record/hit-script/heading phases. Four Agumon RAMs
retain independent bodies and authored collision categories. Damage services,
player creation and the outer game loop remain required. The
first Set129 still needs its separate HITPLAYERTALK recipients. See
[MODEL_COLLISION.md](MODEL_COLLISION.md).

Version 0.26 runs original HITBOX commands through timed work, exact query-box
math and resumable CHKBULLET/BHit service dispatch. Original SQ_ATCK1 preparation
can now reach TALKWF and WCHECK without skipping the first hitbox. Complete
target collision managers, script context, player construction and outer
entry/status effects are still required; this does not establish attack damage.
See [MODEL_HITBOX.md](MODEL_HITBOX.md).

Version 0.25 resolves original common/player waits against registered live
ACTION work and separate original pad slots. WCHECK advances and retains its
cursor; unavailable services suspend without replaying completed commands or
nested dispatches. Complete original player creation, sequence-entry/status
callbacks and device assignment are still required. This prepared integration
does not establish combat or connected multiplayer. See [MODEL_WAIT.md](MODEL_WAIT.md).

Version 0.24 binds prepared player MODELCTRL/ACTION graphs and routes original
ACTION broadcasts through loaded Se/ActionStarter tables into owned subcommand
queues. Native messenger delivery and sequence cleanup preserve completed work
when an external service is missing. Original audio/effect managers, complete
player construction, input/collision callbacks and outer frame scheduling remain
required before these prepared owners become a gameplay session. See
[ACTION_EVENT_RUNTIME.md](ACTION_EVENT_RUNTIME.md).

Version 0.23 connects an explicitly selected actor's main skin to the cached
joint palette, recovered CPU vertex deformation and a native prepared draw.
The captured root/model-view is applied once before a separate projection.
This visible integration uses a host inspection camera; original camera,
complete effective appearance/material traversal, action events and player
scheduling remain required. See [ACTOR_SKIN.md](ACTOR_SKIN.md).

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
