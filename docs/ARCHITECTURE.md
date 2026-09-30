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
3. Pending stage change and the stage loader states (package members,
   `func.lua` / `boot.lua`, common files queued with `CFLsetName`, info
   registration, `start.lua`, `first.lua`).
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
for example a hit script (8030B6EC). Unimplemented methods are reported in
the F1 notes and answer 0 so the original scripts keep running.

## Managers

| Manager | Original | Notes |
| --- | --- | --- |
| `GameSystem` | beSystem / GAMERAM script services | players, difficulty, modes, flags, counters, text call number |
| `SceneManager`, `LayerManager`, `BkColorManager` | igSceneInfo, LAYERCTL, BKCOLOR | layer ORDER/SWITCH/REMOVE, clear colour |
| `CameraManager` | beCameraCtrl | follow and box cameras, named demo camera paths |
| `StageCtlManager` | beNDMWStageCtl | NEXTSTAGE, PADINPUT |
| `LogoManager`, `SoundManager` | beNDMWLogo, sound/SE/movie | music, ambience and sound effects play; movies are reported |
| `ModelCtrlManager` | beModelCtrl (8030D910, 8030C3AC) | sequences, waits, motion, player control and attack chains (8034C3C8, 8034D214), HIT bodies, HITBOX queue, MUTEKI, CHKBULLET (8030E56C), SETLABEL sections, provisional enemy AI |
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
looping sound (type 4) stops at the model's next action. `render()` mixes the
streams and effect voices as 48 kHz stereo PCM that `dw4_game` queues on an
SDL3 audio stream (`DW4_AUDIO_DUMP=FILE.wav` records it, `DW4_TRACE_SOUND=1`
logs the effects).

Each manager documents the addresses it follows in its header comment. Shared
helpers such as COMMDLCTRL (`CommonModelControl`), MESWIN (`MessageWindow`)
and DEGISTATECTRL (`StatusWindow`) are separate classes.

## Scene graph and rendering

`SceneCache` builds one `Node` per IGB object (attribute sets, switches,
transforms with `igTransformSequence1_5`, geometry, actors, LOD, billboards,
blend/bone selects, `beModelCtrlNode2` with its scaled node clock). Managers
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

`CollisionWorld` is a provisional host query over the original
`beHitLandModelInfo` geometry: kind bit 1 triangles are floors (CHECKLAND),
kind bit 2 triangles walls (CHECKWALL). Step-up, jumps over ledges and landing
work; the original CHECKLAND/CHECKWALL responses (80300444 / 802FE7F0) are still
to be reconstructed. Kind bit 0x400 geometry follows its model's transform in
the original and is left out until that dynamic collision exists.

## Adding a manager

1. Find the manager's vtable (RTTI record + 8) and its handlers: +0x7C add
   info, +0x84 per-RAM update, +0x88 stage begin, +0x90 command handler; the
   command names come from the table passed to 800635C8.
2. Add a `Manager` subclass that `accepts` the info type, registers its RAM
   rows under the original category, implements `command` and `update`.
3. Register it in `register_default_managers` before `PassiveManager`.
4. Verify with a smoke test (`--smoke-test`, `--input`, `--screenshot-frames`).
