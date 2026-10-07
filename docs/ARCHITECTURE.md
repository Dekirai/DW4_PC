# Engine architecture

The runtime mirrors the original GameCube engine's structure instead of
inventing a new one. Every serialized `be*Info` object in a stage package is
handed to the manager that owns its type, the managers talk only through the
messenger, and the stage's embedded Lua 4 scripts drive the game flow exactly
as they do on the console. Addresses below refer to the USA revision-0
`main.dol` (SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`).

## Layers

| Library | Responsibility | Depends on |
| --- | --- | --- |
| `dw4_core` | Original data formats and primitives: IGB documents, AFS packages and the logical resource mount, textures, meshes, skinning, Enbaya animation, Lua 4 bytecode VM, `bePadData` pads, `beModelCtrlMove` motion channels | C++ standard library |
| `dw4_engine` | The game: frame step, messenger, demo manager, Lua host, scene graph, managers | `dw4_core` |
| `dw4_game` | Host: SDL3 window and input, OpenGL 3.3 renderer, 4:3 presentation, F1 debug menu, smoke-test automation | `dw4_engine`, SDL3, Dear ImGui |

`dw4_core` and `dw4_engine` have no SDL, OpenGL or ImGui dependency.

## Frame step (`Engine::step`)

One step is one original 60 Hz frame (16,666,666 ns). In order:

1. Pads: `dw4::OriginalPads` computes trigger/repeat masks from the host
   samples (disabled while STAGECTL `PADINPUT DISABLE` is active).
2. The frame clock advances, so updates and the following render observe the
   same time.
3. Pending stage change and the stage loader states (802F8A64): the loading
   screen `loadingUSA/ld<stage name from its third letter>.igb`, package
   members, `func.lua` / `boot.lua`, common files queued with `CFLsetName`,
   the wait for the loading screen's minimum play time (its beLoadingNode
   then holds its lock time on the LOADING layer until the scripts remove
   it), info registration, `start.lua`, `first.lua`.
4. Deferred messenger sends (`sd(..., delay)`), then the demo manager
   (`DMsetSD` / `DMchkSD` / switch records, one blocking record at a time).
5. Every manager's `update`, in registration order.

`Engine::render` traverses the attached scene roots (layer switches under
`ROOTATTR`) into draw items for the host renderer.

## Messenger and scripts

`Messenger` holds `(category, name, index) -> RAM` rows registered by the
managers. `send` selects all matching rows (nil name and index -1 are
wildcards) and calls each manager's `command`; results come back in the shared
`Packet` (booleans, integers, floats, vectors, strings, matrices), like the
original buffer object. Packet arguments from scripts use the original compact
form `"a/b/I<n>/F<x>/V<x,y,z>"`.

`lua_host.cpp` binds the `be:` methods the scripts call. Model methods without
a name (`MDLsetSeq`, `MDLgetRamNo`, `MDLgetPosStr`, `BLTgetShootRamNo`, ...)
read the `ModelScriptContext` pushed by the code that runs a model script,
for example a hit script (8030B6EC). A sequence's LUACALL runs
`<name>_<model>.lua` at once with its mode as the call name that
`MDLisCallName` tests ("swof", "magic", "setaid", ...); the modes BHit, WHit
and FHit keep the name for when the model's hitbox hits something or it meets
a wall or the floor (bullets erase themselves; the wall and floor checks use
the HITLANDDELIVER responses). `DW4_TRACE_LUACALL=1` logs the calls.
Unimplemented methods are reported in the F1 notes and answer 0 so the
original scripts keep running.

## Managers

| Manager | Original | Notes |
| --- | --- | --- |
| `GameSystem` | beSystem / GAMERAM script services | players, difficulty, modes, flags, counters, text call number |
| `SceneManager`, `LayerManager`, `BkColorManager` | igSceneInfo, LAYERCTL, BKCOLOR | layer ORDER/SWITCH/REMOVE, clear colour |
| `CameraManager` | beCameraCtrl | box camera (802F2BC0: pivot at the players' centre, per-box rotation and distance transforms blended over 120 steps, SHAKE), named demo camera paths |
| `StageCtlManager` | beNDMWStageCtl | NEXTSTAGE, PADINPUT |
| `LogoManager`, `SoundManager` | beNDMWLogo, sound/SE/movie | music, ambience, sound effects and the opening movie play |
| `ModelCtrlManager` | beModelCtrl (8030D910, 8030C3AC) | sequences, waits, motion, player control and attack chains (8034C3C8, 8034D214), HIT bodies, HITBOX queue, MUTEKI, CHKBULLET (8030E56C), SETLABEL sections, AIM / AIH AI (a provisional chase fallback without AI maps), rides and floor effects |
| `GeneraterManager` | beGenerater (802FCF68, 802FD5C4) | enemy waves from DATABASE enemy rows, start/fill modes, `_callLua` on clear |
| `WeaponManager` | beWeapon / PLAYERARMS (80318474) | weapon sequence labels for the carrying player |
| `ActionManager` | beAction2 (802F1794) | joint/fade/flash/wait, motions on actors |
| `TextCtlManager` | beFont (802FA900) | per-line TEXTSET/SWITCH/ALIGN/COLOR from the pack unicode strings |
| `SelectCtrlManager` | beSelectCtrl (803132C0) | cursor menus and prompts |
| `Title2Manager` | beNDMWTitle2 (80369284) | press start, menu, option window |
| `LoadCtrl2Manager`, `LoadIntf2Manager` | beNDMWLoadCtrl2 (80368050), beNDMWLoadIntf2 (8035C958) | new game and character select panels, calling a character from a card |
| `SaveCtrlManager`, `SaveIntfManager` | beNDMWSaveCtrl (803788A8), beNDMWSaveIntf (80379790) | save room panels (`save_menu.cpp`: McUtilCtrl card worker, SELSLOT, DATASELINTF, DEGISTATECTRL) |
| `SaveUtilManager` | beSaveUtil (80323D20) | SAVEUTILITY requests on GCI card folders |
| `DatabaseManager` | beDBManager | beDataBase string and integer columns |
| `MatCtrlManager` | beMatCtrl (803030A8) | floor mats running `<name>.lua` |
| `TextureCtrlManager`, `PlayerPointManager`, `SwitchCtrlManager`, `HitLandManager` | beTextureCtrl, bePoint01, SWITCHCTRL, beHitLandModel | pack texture binding, spawn points, switches, collision geometry |
| `PassiveManager` | everything else | accepted and listed as not reconstructed |

`combat.cpp` holds the player-to-enemy FIGHTCALC path (GAMERAM 803717A4:
P2EISHITCHECK, P2EISCRITICAL, P2EDAMAGE, the damage rank at 802EE91C) and the
level-1 player status from the shinka and weapon databases (GAME_STATS.md),
plus the status effect chances and times of both directions (E2P*/P2E*).
`status.cpp` is the beNDMWMdlPEBase status work of players and enemies: the
status commands (803AEBB4), the per-model update after control with named
model timers (803ACD88), the sequence-change check that breaks sleep and stun
(803AE1A4) and the status icons (803ACB30).

`SoundManager` follows the beAsmSound2 command handler (80314F14): a stage's
beSoundInfo lists its sounds (type 1 BGM, 2 ambience, 3/4 sound effects) and
the files they index; stream files are written "[archive.entry]" (802A9828)
and name CRI ADX files decoded by `dw4::adx` in the core. Sound effects carry
a request id 0xA9GGNN00 into a CRI Sound Factory bank (`dw4::sfp`): the common
allse.sfp (_common/sbiallse.igb) or a stage bank from beStaticInfo. The bank's
request table leads to a short sequence (803C49F4, millisecond delta times)
whose notes play drum-program splits, each a DSP ADPCM sample. Effects without
a request id are ADX files on two SE stream channels. beSeInfo holds each
model's SE set: the sounds an ACTION2 action plays after a frame delay; a
looping sound (type 4) stops at the model's next action. Levels follow the
AX mixer (MIXInitChannel 80259334): the left side is attenuated by the pan
table at the pan and the right side by the table at 127 - pan, so effect
voices (split pan through the 8045D130 curve; every shipped split is centred)
and mono streams play at -3 dB per side while stereo streams keep each channel
at full level on its own side (802A7F34). `render()` mixes the
streams and effect voices as 48 kHz stereo PCM that `dw4_game` queues on an
SDL3 audio stream (`DW4_AUDIO_DUMP=FILE.wav` records it, `DW4_TRACE_SOUND=1`
logs the effects). MOVIE PLAY (8031EDFC) looks up the beMovieInfoData of the
file ([5] end script), splits the Sofdec program stream with `dw4::sfd`
(MPEG-1 video 0xE0, ADX sound 0xC0) and plays the sound on a movie channel;
the host decodes the video with pl_mpeg and draws the picture for the time
since start over the screen. The movie ends after its last picture or on A or
START from player 1 (8031EC10), then runs its end script.

Each manager documents the addresses it follows in its header comment. Shared
helpers such as COMMDLCTRL (`CommonModelControl`), MESWIN (`MessageWindow`)
and DEGISTATECTRL (`StatusWindow`) are separate classes.

## Scene graph and rendering

`SceneCache` builds one `Node` per IGB object (attribute sets, switches,
transforms with `igTransformSequence1_5` or the quantized
`igCompressedAnimationSequenceQS` (keys base + value * range, key times
`(value & 0x7FFF) << 23` ns), geometry, actors, LOD, billboards, blend/bone
selects, `beModelCtrlNode2` with its scaled node clock). Sequences are
evaluated like 80209B74 / 80209578 (QS: 801E2FF0 / 801E32B8): only the
`_drivenChannels` apply (translation, quaternion via the 8012BBF4 layout,
Euler degrees via 801292D4, scale), each with its `_interpolationMethod`
(step, linear, Bezier control points; quaternions 8012C15C or the 8012BE90
slerp), and the play mode only beyond first key + `_animationDuration`, so
the end time shows the last key. Managers
change nodes through overrides (switch masks, colours, runtime text geometry,
actor poses). Matrices are row-vector and share the column-major memory layout
of the original. The GL renderer maps IGB attributes to fixed-function-like
state in one shader (lighting, fog, alpha test, skinning palette of 64).
Lighting is on unless an igLightingStateAttr turns it off (the Alchemy
default); the stage root "ROOTATTR" igLightStateSet switches its light sets on
for the scene, and each lit vertex gets the lights' ambient times the material
ambient plus their diffuse, over the material or vertex colour (GX channel).
LIGHTCTRL SETCOLOR (beLightCtrl, 803248AC) recolours a light set per room.

Particle effects (`y_*` names) are beParticleCtrl2Info instances answering as
MODELCTRL rows (`ParticleCtrlManager`): FIND_SPACE and GENERATE place an
instance on a model (offset, radian rotations, optional follow), start its
ParticleTimer and switch it off after the info's duration. A ParticleTimer node
draws its igParticleAttr arrays (`dw4/engine/particles.hpp`): each 180-byte
particle is a quadratic in time for position, RGBA, size and texture rectangle,
built into camera-facing quads every frame (`DW4_TRACE_PARTICLES=1` logs the
instances).

Water surfaces (`WaterPlainManager`, beWaterPlain) run the original wave
equation on their grid: players and enemies in the height bands and the
beWaterMove drops set grid points down, each frame computes
1.84 h + 0.04 (neighbours) - previous times the point's damping (0.99), clamped,
and writes the heights and neighbour normals into the surface mesh
(`DW4_TRACE_WATER=1`).

Projected shadows follow the stage's igProjectiveShadowShader (`ShadowManager`,
beShadow01): beShadow01Info entries name casters (model switches, or
`beCopyModelCtrl2` "Kage" proxies that copy a model instance's matrix and
actor pose) and receivers (simplified ground meshes outside the visible scene).
`Engine::render` hands the renderer a `ShadowFrame`: the casters are drawn
black into a 256x256 map from an orthographic light 200 units above the
players' centre (+-100 units, looking down), and the receivers multiply the map
(darkness 0.4) over the 3D scene before the 2D layers. `DW4_TRACE_SHADOW=1`
logs the light and item counts.

## Collision

`CollisionWorld` holds the `beHitLandModelInfo` geometry and answers the
original HITLANDMODEL queries: segments against the triangles of the infos
whose kind meets the query flags (front faces only, 801238E8). CHECKLAND
(80300444) casts four vertical rays at the corners of the half-width and picks
the highest floor inside the up/down window, else the nearest below; CHECKWALL
(802FE7F0) runs the wide preflight, the longitudinal pull-back along the
movement and the lateral push along the nearest wall's normal (H = 1.42
width). Kind 0x400 geometry follows its model's node; a check-switch child
that is a transform follows that transform's scene node (the limit circle
under the beChangePosTransform, which `ChangePosManager` keeps at the players'
centre while everyone is within 40 units, so players cannot leave each other).

HITLANDDELIVER rows (beHitLandDelivInfoRam, 802FDA0C) carry each model's
cylinder: HITWALL queries from the previous to the current node position and
moves the node out, HITFLOORCHECK keeps the land position (with a ground plane
at Y 0), HITFLOORADJUST and HITFLOOR put the node on it. The model consumer
(80309C74, after the frame's motion and wait check) runs HITWALL (WHit), then
the floor: a falling model lands (vertical motion stopped, FHit), a walking one
keeps to floors within its range or enters SQ_FALL; a rising jump skips it.
MUTEKI ONWALL/OFFWALL/ONFLOOR/OFFFLOOR and CTRL onFall/noFall switch the
checks. Walls and floors run only in frames where a translation channel moved
the model (ctrl work +78) or a ride carried it.

Rides (ctrl work +CC, beModelCtrlInfoRideOn): landing from a fall on a named
HitLand info (a model's collision such as a boat, train, box or moving
scaffold) rides that model (MODELCTRL name child GETMATRIX); an unnamed floor
ends the ride. Each frame (8030CEB0, before the wait check) the model keeps its
place relative to the ridden matrix of the last frame. The stage scripts'
floor effects use the same slot: SETCONVEYOR (a fixed step per frame),
SETQUICKSAND (a pull towards a centre, faster inside the inner radius) and
SETSLIPPEDFLOOR (the model's own steps blend into a sliding velocity). While
riding, HITWALLFULL sends CHECKWALLFULL (802FF748): eight rays round the
model, first 3.5 width against the ridden walls (kind 0x1000), else 2.15 width
against all walls. Routes (boats, trains, rails, some bosses) follow the
igTransformRecorder under an animated route transform (MOVERL JOINT,
GETTRCDATA). `DW4_TRACE_HITLAND=1` logs wall hits, `=2` every wall candidate;
`DW4_TRACE_RIDE=1` logs rides, `=2` every ride step.

## Adding a manager

1. Find the manager's vtable (RTTI record + 8) and its handlers: +0x7C add
   info, +0x84 per-RAM update, +0x88 stage begin, +0x90 command handler; the
   command names come from the table passed to 800635C8.
2. Add a `Manager` subclass that `accepts` the info type, registers its RAM
   rows under the original category, implements `command` and `update`.
3. Register it in `register_default_managers` before `PassiveManager`.
4. Verify with a smoke test (`--smoke-test`, `--input`, `--screenshot-frames`).
