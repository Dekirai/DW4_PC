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

One step is one original frame, one NTSC field: the world clock advances by
the field length, 16,683,333 or 16,683,334 ns (50,050,000 ns per three
fields), as the original adds the measured OSGetTime delta (8028AF84) and
runs one frame per field. Delays and frame counts in the data still use the
nominal 16,666,666 ns frame (`kFrameNs`), so an n-frame wait ends after
exactly n steps. In order:

1. Pads: `dw4::OriginalPads` computes trigger/repeat masks from the host
   samples (disabled while STAGECTL `PADINPUT DISABLE` is active).
2. The frame clock advances, so updates and the following render observe the
   same time.
   beSystem's update (80316258) then reads the console's reset button
   (`dw4::ConsoleReset`: B, X and START held on a controller for more than
   three seconds tap it, as the controller hardware does); its latched
   release turns the picture black and nothing else runs: after
   `Engine::kRebootFields` steps the host starts a new engine on the boot
   stage (the console restarted the disc, OSResetSystem).
3. Pending stage change and the stage loader states (802F8A64): the loading
   screen `loadingUSA/ld<stage name from its third letter>.igb`, package
   members, `func.lua` / `boot.lua`, common files queued with `CFLsetName`,
   the wait for the loading screen's minimum play time (its beLoadingNode
   then holds its lock time on the LOADING layer until the scripts remove
   it), info registration, `start.lua`, `first.lua`.
4. Deferred messenger sends (80304C58: every `sd(..., delay)`, also a delay
   of 0, and the engine's own posts arrive here, in a later frame than they
   were sent; none while a SUBWND program or the soft reset pauses the queue,
   see docs/research/DEFERRED_MESSENGER.md), then the demo manager
   (`DMsetSD` / `DMchkSD` / switch records, one blocking record at a time).
5. The managers' updates in the original scheduler order
   (`Engine::update_managers`, igSerialScheduler 8028B7B0): first beSystem,
   which its plugin schedules at registration, then every manager in the
   order its first info of the stage was accepted (802F251C -> 8028A398 ->
   80041E40, append-unique; a stage clear unschedules them, 802F2628). The
   stage files list their infos so that ACTION2 runs before the models and
   the models before SELECTCTRL, the status window and the quick panels;
   TEXTCTL (beFont, common file) comes last. Each model class of the game
   plugin (beNDMWMdlPlayer, Enemy, Object, ...) is a manager of its own with
   its own slot, so the player, from the common `f_m*.igb`, updates after
   the stage's menu managers. Managers without an info in the stage, which
   the original does not run, follow (idle). `DW4_TRACE_SCHEDULE=1` logs the
   order at each stage start.

Every random number comes from one generator (`SystemRandom`, beSystem's
igMersenneTwisterRandomNumber seeded 5150 at boot) through `fight::random()`.

`Engine::render` traverses the attached scene roots (layer switches under
`ROOTATTR`) into draw items for the host renderer. That traversal is part of
the simulation: node animation clocks (NodeAnim), the motion combiners of the
actors (`ActionManager::pose_for`), LOD choices and the transform recorders
advance in it, so every picture the original draws must be traversed.
`Engine::traverse_scene` does the same traversal (shadow casters and receivers
included) without draw items, carrying only the view and projection matrices
down instead of the render state; test runs without pictures, catch-up steps
and save state replays use it.

The traversal culls like the stage camera's igFrustCullTraversal (created by
8040B3EC; node test 801E9AB4, box 801E97C8 / 801E898C, clip test 801E8BF0,
dispatch 801E994C). A node with an igNode bound (always an igAABox in the data:
actors, ParticleTimers, many igTransform / igGeometry / igGroup nodes) is
tested in its parent's space against the current modelview and projection
before its own handler runs: outside (no corner inside one clip plane, or every
corner behind the eye) skips it with its subtree, updates included, so an
actor out of view evaluates no combiner and a recorder, LOD cache, NodeAnim or
ParticleTimer clock below a culled bound keeps its value; inside, also inside
the frustum widened by 1.1 (801E9BE0), stops the tests for the subtree. The
test uses the game's camera (`TraversalContext::cull_camera`), never a debug
camera. The shadow shader dispatches its casters on the same traversal, so
they are culled against the light's view and box; the receivers against the
camera. A ParticleTimer's effect time advances only when the traversal visits
it (_timeRunning / _timeBias / _time, 8031A0A4): an effect started out of view
counts its time from its first visit. beParticleCtrl2's lifetime test reads the
same bias (80312268: world time minus _timeBias against the lifetime, no
exception for ONFREEZE), so such an effect usually stops at the next update,
before it is ever drawn, and a frozen effect out of view keeps ageing. The bone
matrices GETBONEMATRIX reads (802F18A4) are the ones the last traversal of the
model's actors left (`ActionManager::ActionRam::traversed`): the updates run
before the traversal, so a bone follower is a frame behind its bone, and it
stays put while its actor is culled or switched off. GETBONEMATRIX answers only
while the model's action runs (RAM +0x14, `action_active`): after NDSP the
follower gets no answer and keeps its last matrix. `Engine::render` also tests the picture's own
frustum (the renderer widens perspective projections for widescreen, and a
debug camera shows everything): a subtree outside the original's frustum but
in the picture is drawn without its updates (`ActionManager::peek_pose`, copied
clocks and caches), so the simulation does not depend on the presentation.

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
The command loop (`ModelCtrlManager::run_commands`, 8030C3AC) keeps the list
and index it loaded at entry: a sequence a LUACALL or AIMAP script (or an
ACTION start) selects in the middle runs nested, then the old list goes on
and its index becomes the new sequence's cursor; only JUMP skips that store
(Lucemon's SQ_SWITCH7 inherits index 4 from SQ_CHACK1).
`DW4_TRACE_SEQINT=1` logs these hand-overs. Delayed model subcommands
(SETEFFECT, HITBOX) count the system frame step (world time delta /
16666667, `Engine::system_step`, 80310754), not 1.0 per frame.
Unimplemented methods are reported in the F1 notes and answer 0 so the
original scripts keep running.

## Managers

| Manager | Original | Notes |
| --- | --- | --- |
| `GameSystem` | beSystem / GAMERAM script services | players, difficulty, modes, flags, counters, text call number |
| `SceneManager`, `LayerManager`, `BkColorManager` | igSceneInfo, LAYERCTL, BKCOLOR | layer ORDER/SWITCH/REMOVE, clear colour |
| `CameraManager` | beCameraCtrl | one current viewer (8040F870), set by a demo camera info (802F3568, its end pose held until the box camera replaces it) or by the box camera on a box change (802F2BC0: pivot at the players' centre, per-box rotation and distance transforms blended over 120 steps: translation 8012D3F4, rotation the normalized linear quaternion blend 8012C15C written by 801299D8, SHAKE); igCamera horizontal field of view on the 640x448 viewer (802F414C, 801DB6E4) |
| `StageCtlManager` | beNDMWStageCtl | NEXTSTAGE, PADINPUT |
| `LogoManager`, `SoundManager` | beNDMWLogo, sound/SE/movie | music, ambience, sound effects and the opening movie play |
| `ModelCtrlManager` | beModelCtrl (8030D910, 8030C3AC) | sequences, waits, motion, player control and attack chains (8034C3C8, 8034D214), HIT bodies, HITBOX queue, MUTEKI, CHKBULLET (8030E56C), SETLABEL sections, AIM / AIH AI (a provisional chase fallback without AI maps), rides and floor effects |
| `GeneraterManager` | beGenerater (802FCF68, 802FD5C4) | enemy waves from DATABASE enemy rows, start/fill modes, `_callLua` on clear |
| `WeaponManager` | beWeapon / PLAYERARMS (80318474) | weapon sequence labels for the carrying player |
| `ActionManager` | beAction2 (802F1794) | joint/fade/flash/wait, motions on actors; an action start sets the switch-mask bit (visibility, kind 0x400 collision) before the action lookup, so an unknown name still shows the model |
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
whose notes play drum-program splits, each a DSP ADPCM sample, at SEQ volume
127 under the SNDF master volume (beSoundInfo [10], 75, set once, 802F7244).
Effects without a request id are ADX files on four SE stream channels, each
start taking a free one or else the oldest of lower or equal priority
(beSoundData [9], 802F56B0). The PLAY handler (80314FC8) ignores a sound effect
started less than 50 ms of world time before (803151B0), so two windows
opening in the same frame sound once. The sound entries keep the Cri's
serials (play ids), names and fades: a stream plays at its volume plus the
stage's base volume (beSoundInfo [9], -30, ADXT_SetOutVol), VOLUMECTL2 and a
replaced BGM's fade-out step through the original's fade tables (8031D5D0 /
8031DCB0, `dw4::CriFade`) only while the stream runs unpaused, and an entry
that stopped or played out is freed at once (802F53B8). beSeInfo holds each
model's SE set: the sounds an ACTION2 action plays after a frame delay (through
the PLAY handler, 803105DC); a looping sound (type 4) stops at the model's next
action. The SE sets hear the action through ACTION_BROADCAST (803130D4 sends
MODELCTRL SETSOUND), which an action start sends after its first commands ran
and only when the action changes (802F1794, 80305ED0 at 0x802F21D8), so the
same action given again plays nothing. Levels follow the
AX mixer (MIXInitChannel 80259334): the left side is attenuated by the pan
table at the pan and the right side by the table at 127 - pan, so effect
voices (split pan through the 8045D130 curve; every shipped split is centred)
and mono streams play at -3 dB per side while stereo streams keep each channel
at full level on its own side (802A7F34). Effect voices also feed the AX
AUX A bus, where the SNDF runs a reverb: every stage's first beCriFxInfo is
named REVERB, and the SOUND RAM's first update sends its _reverb (bank << 8 |
preset) to the SNDF (802F6F10), the Cri's stage end turns the effects off
again (802F523C). The SNDF (803BF698) sets up that preset of the bank with
id 0 (allse.sfp, DTPK +0x4D; its effect table at TBLD +0x70 holds five
ReverbStd presets: time, pre-delay, damping, coloration and mix in
thousandths; the boot stages use preset 4, dng102b1 preset 1, stagBS03
preset 3) as AXFX ReverbStd (`dw4::axfx`, 80242848 / 80242CC4: per channel
a pre-delay, two combs, an all-pass, a low-pass and a second all-pass in
single precision at 32 kHz). A voice sends at its split's AUX A level
(+0x17: 20 log10(level / 127) after the fader, 64 = -5.9 dB in almost every
split, 0 = none); streams send nothing (the RNA's AUX levels stay at
-99.9 dB, 802A7B5C). `render()` runs the bus in AX frames of 240 output
samples (160 at 32 kHz) with the AX library's three rotating AUX buffers
(8023CE40), so a send comes back two frames later. `render()` mixes the
streams and effect voices as 48 kHz stereo PCM that `dw4_game` queues on an
SDL3 audio stream (`DW4_AUDIO_DUMP=FILE.wav` records it, `DW4_TRACE_SOUND=1`
logs the effects). MOVIE PLAY (8031EDFC) looks up the beMovieInfoData of the
file ([5] end script), splits the Sofdec program stream with `dw4::sfd`
(MPEG-1 video 0xE0, ADX sound 0xC0) and plays the sound on a movie channel;
the host decodes the video with pl_mpeg and draws the picture for the time
since start over the screen. The movie ends after its last picture or on A or
START from player 1 (8031EC10), then runs its end script.

When a stream is ready matters to the scripts: demos wait with
`DMchkSD SOUND ISALLREADY` / `ISREADY`, which test ADXT status 3 (PLAYING,
802F6FB0), and the original only gets there after the stream's first block
came off the disc. `dw4::CriStreams` (include/dw4/disc_timing.hpp, in the
core) models that on the engine clock, once per field from the sound driver
tick (also during loads): the six beCri ADXT handles (two stereo 48 kHz for
BGM and ambience with a 53-sector input ring, four mono with 13 sectors),
their ADXSTM readers (one DVD read at a time through gcCi, the lowest handle
first, 25 sectors before the header is parsed, refills below 85 %), the
decoder (+72 samples per call), the ARAM renderer (4096 samples, played
halves given back), the DSP's progress as the renderer reads it (the voice
address AX copies back at each 5 ms frame boundary, one frame behind: 240
samples per frame of a 48 kHz voice, frames of 2427840 ticks at the AI's
54 MHz / 1686; a pause stops the voices and drops the ARAM ring, 802A7534)
and `dw4::DiscDrive`, Dolphin's GameCube drive timing
(radius-dependent read speed, seek by head travel, rotational latency by
Dolphin's formula, whose disc angle follows the whole seconds of its emulated
clock, 1 MiB read-ahead; the captures' every DVD interrupt follows it to 0.006
field once Dolphin's clock is placed 107.33e6 ticks ahead of the port's world
clock, `DiscDrive::kDolphinClockOffset`). The disc offsets come from the disc
image's FST or the extraction's sys/fst.bin. tests/disc_tests.cpp replays
three captured stage starts (stagBS01, stagBS02, dngQ1B1e) read for read. A
replaced BGM fades over the original's table (8031D5D0) and stops once the
fade reaches -21 dB with the stage's base volume (beSoundInfo [9]). The stage
loader's own reads share the drive: like the original's loading thread
(igAdxFile 802AA1B0 over a CRI file stream that comes after the ADXT readers
in each server pass) the loader reads every file of a load in 64-sector
requests and asks for the next chunk once the thread has used the last one,
which takes it whole fields more where the chunk holds the reference index,
the start of the object buffer (the objects are made there, 0.34 fields per
1000), object records or the start of the memory blocks; after the last
chunk the file is ready by the time its memory blocks take, then routed in
the next update. The teardown of the outgoing stage, routing a large member,
state 5 and start.lua are long updates that cover the retraces after them.
So a stage starts 500-900 fields after its NEXTSTAGE as in the original (a
constant 106 before). The load time depends strongly on where the BGM's
refill cycle stands when it starts: Dolphin itself gives 497 or 607 fields
for dng307b1 at two login times 155 fields apart. Against captures of 15
loads at both BGM phases (the boot's three: 234 / 61 / 118 fields against
233.8 / 62 / 121) the port's BGM refills come on the original's fields with
its sizes; what remains is the loading thread's share of a field (a chunk a
field early or late now and then, which can swap a chunk with a refill), on
average -4 fields and 15 fields apart per load. The port parses each file
when the original asks for it.
`DW4_TRACE_SOUND=1` logs every simulated read,
PLAYING, stream start, pause / resume, fade stop and ISREADY / ISALLREADY
change (`DW4_TRACE_CRI_RING=1` also the ring of each refill and the
renderer's view of the stereo voices every field),
`DW4_TRACE_LOADER=1` the loader's requests, routings, states and long updates
(`DW4_TRACE_LOADER_CHUNKS=1` also what each chunk of a file holds), and
`DW4_DISC_CLOCK=<ticks>` / `DW4_AX_PHASE=<ticks>` replace the drive's clock
offset and the DSP's frame grid to line a run up with a capture.

Fast loading (`Cheats::fast_loading`, the F1 menu's *Loading: Fast*, a PC
option and not the original timing) is a separate switch around that model:
`begin_stage_load` latches it for the load and the stage it starts
(`Engine::fast_loading`), so a change in the menu applies from the next load
on and a save state's replay sees the value its snapshot holds. With it the
loader runs every state in the original order, one state per update, but
`loader_request` hands the file over at once (no `read_entry` on the loader's
CRI stream, no chunk processing or post-processing fields), the update costs
(teardown, routing, state 5, start.lua) cover no retraces, the boot's timing
reads are skipped, state 4 does not wait for the loading screen's minimum
time (it still sets the lock time), and the queue states take several files
per update (`loader_queue_fast`: parsed and routed in order while the files of
the update stay within `kFastLoadBytes`, 1 MiB; a larger file has an update
of its own). The sound driver sets `DiscDrive::instant` from the latched value
before each tick: a read takes no time and ends at the next tick, still one
read at a time, so ADX streams become PLAYING a few fields after they start
and ISALLREADY waits in the stage scripts end almost at once. A stage change
takes 15-16 fields from NEXTSTAGE to first.lua (boot included) instead of
60-900; the stage's fades, demos and the scripts' own waits are unchanged.
Memory card access (card_timing.hpp) keeps its timing: it is not part of a
stage load, takes under two seconds per save and paces the save room's
messages. With the option off nothing of this runs and the frames are those of
the original timing. The value is part of the snapshot's cheats (snapshot 27)
and of the online start message (protocol 27, the host's choice for every
machine). `DW4_TRACE_LOADTIME=1` prints the fields and real time of every load
(`=2` every update of it).

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
Skinning follows the original's CPU blend in the igVertexArray immediate
draw (0x800EDD24): positions sum the weighted bone transforms up to the first
zero weight, normals go through the first blend index's 3x3 alone. Constant
colours reach the GPU as the 8-bit values GX holds
(`include/dw4/engine/gx_color.hpp`): igColorAttr colours (IGB attributes,
the FADESET / FLASHSET overrides, text line colours, the shadow grey),
igClearAttr's clear and particle vertex colours rounded like 8012E1D8; the
lighting registers and the fog colour truncated.
Lighting is on unless an igLightingStateAttr turns it off (the Alchemy
default); the stage root "ROOTATTR" igLightStateSet switches its light sets on
for the scene, and each lit vertex gets the lights' ambient times the material
ambient plus their diffuse, over the material or vertex colour (GX channel).
The normal goes through the normal matrix the modelview flush loads (800F805C:
the transpose of the modelview's inverse, PSMTXInverse / PSMTXTranspose, a
singular matrix keeps the previous one) and is normalised for the lighting
like Dolphin's XF emulation, so a uniform scale changes nothing and a
non-uniform one (the dng103f1 arrow shooter's bow while it draws) tilts the
normals the inverse way. Texgen source 2 uses the plain modelview rotation
with GXSetTexCoordGen2's normalise flag (0x800FD4DC).
LIGHTCTRL SETCOLOR (beLightCtrl, 803248AC) recolours a light set per room.

An igBillboard (traversal 801DD62C, matrix 801D5ADC, basis 801D53EC) replaces
its parent's world matrix with an orthonormal basis at the parent's world
position (plus `_center` through the parent's rotation): Y is the mode's up
vector and Z points at the eye (the inverse view's translation), so no scale of
the parent chain reaches the children (`engine::billboard_matrix`). `_mode` 0
(and any value outside 1..3) keeps world Y as the up vector, an upright
billboard that only turns about it (light pillars, `powerup`, stage props);
1 uses `_axis` through the parent's rotation; 2 and 3 make world Y and the
camera's Y orthogonal to the eye direction first, so the face looks straight
at the eye (3: damage numbers, status icons, most effect sprites).

Particle effects (`y_*` names) are beParticleCtrl2Info instances answering as
MODELCTRL rows (`ParticleCtrlManager`): FIND_SPACE and GENERATE place an
instance on a model (offset, radian rotations, optional follow), start its
ParticleTimer and switch it off once the world time minus the timer's
_timeBias reaches the info's duration (see the cull above). A ParticleTimer node
draws its igParticleAttr arrays (`dw4/engine/particles.hpp`): each 180-byte
particle is a quadratic in time for position, RGBA, size and texture rectangle,
built into camera-facing quads every frame (`DW4_TRACE_PARTICLES=1` logs the
instances). The centres go through the full model transform; the sizes are
view-space sizes the model scale never enters (800E3618 / 800E3650 store half
sizes, 800E1C70 builds the corners around the transformed centre). 800E1C70's
screen-space path (igParticleFormat `_spriteSpace` SCREEN_COORDINATE) is
unused: every particle format in the data is POINTS in world coordinates.

Water surfaces (`WaterPlainManager`, beWaterPlain) run the original wave
equation on their grid: players and enemies in the height bands and the
beWaterMove drops set grid points down, each frame computes
1.84 h + 0.04 (neighbours) - previous times the point's damping (0.99), clamped,
and writes the heights and neighbour normals into the surface mesh
(`DW4_TRACE_WATER=1`). A surface only runs while its box is in the camera's
view: out of view it is flattened, and when it comes back (as on its first
update) the drop cycle starts again from that moment.

Projected shadows follow the stage's igProjectiveShadowShader (`ShadowManager`,
beShadow01): beShadow01Info entries name casters (model switches, or
`beCopyModelCtrl2` "Kage" proxies that copy a model instance's matrix and
actor pose) and receivers (ground meshes: visible floor items or simplified
copies outside the visible scene).
`Engine::render` hands the renderer a `ShadowFrame`: the map (_shadowTexW x
_shadowTexH, default 128, the stages ship 256) is cleared white and the casters
are drawn in the grey _shadowFactor (801FDCCC) into an inner viewport that
keeps a 7-texel border (801FD040 / loc_801FCBE4: ceil(6 * blur offset) + 1),
seen from the light with lookAt(light, light + igLightAttr direction,
_upVector) (801FE5E4) and an orthographic box from _left/_right/_bottom/_top
(+-5 by default, +-100 for beShadow01's +-5). The caster pass locks only
lighting, depth, texture unit 0, the shadow colour and the viewport, so each
caster keeps its own blending (an additive effect ring adds its grey to the
white map and casts nothing) and its own culling. The light view is only the
base of the casters' modelview stack; the traversal's view stays the
camera's, and a billboard caster (the xdataup / powerup markers, stagBS06's
ball) starts from world * light view * inverse camera view, so it faces the
light's eye (`TraversalContext::modelview_base`). The receivers multiply the map
(ZERO / SOURCE_COLOR) over the opaque 3D scene, before the blended items. The
receiver path (801FE190) sets the texture, blending and a disabled alpha test
but no depth, cull or offset state: each receiver is drawn again on its own
vertices with its own depth test, function, write and culling. A decal
receiver drawn as a whole square so keeps the floor under it from being shaded
twice, and a receiver's depth hides blended items under it that are drawn
later (the centre tile of stagBS05's boss gate).
`DW4_TRACE_SHADOW=1` logs the light, the box and the item counts.

## Collision

`CollisionWorld` holds the `beHitLandModelInfo` geometry and answers the
original HITLANDMODEL queries: segments against the triangles of the infos
whose kind meets the query flags (front faces only, 801238E8). CHECKLAND
(80300444) casts four vertical rays at the corners of the half-width and picks
the highest floor inside the up/down window, else the nearest below; CHECKWALL
(802FE7F0) runs the wide preflight, the longitudinal pull-back along the
movement and the lateral push along the nearest wall's normal (H = 1.42
width). Kind 0x400 geometry follows its model's node, read when the query
runs (803009D0 copies the source matrix in every candidate pass, so a car or
boat moved earlier in the frame is met where it is now). Such a child counts
only while its bit of the check switch mask is set, read in the same pass;
the check switch shares that bit mask object with the model's ACTION switch,
so `ActionManager::mask_switch` gives the node whose bits an action start sets
and NDSP or FADESET HIDE clear: a box or wall that REMOVE hides loses its
collision at once, a hidden fade scaffold (dng310f3) carries no one, and ONHIT / OFFHIT (by the info's name list, empty for these) never touch
them. The broad phase takes such a child when the bound of the copy
transform's first child, moved by the model's translation but not turned,
meets the query box in X and Z (80300974), so a turned car or boat can stick
out of it and be skipped there. A check-switch child
that is a transform follows that transform's scene node (the limit circle
under the beChangePosTransform, which `ChangePosManager` keeps at the players'
centre while everyone is within 40 units, so players cannot leave each other).

The test bots' route planning runs its walking maps inside a
`CollisionWorld::Still` scope: nothing moves meanwhile, so the enabled moving
children and their placement are read once, and the check_land / check_wall
answers are kept with the collision state they were found in (all group
switches plus each enabled moving child and its matrix, compared word for
word). The next plan in the same state reads them; answers are the same as
the queries'. The simulation itself never uses such a scope.

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
`DW4_TRACE_RIDE=1` logs rides, `=2` every ride step. While the position lock
(`_isPosLock`, ctrl work +A8) is set - a MOVERL follow or an effect form
GENERATE with the lock flag (8030D260) - 8030CB2C skips the motion step
8030A448 and places the model instead (0x8030CD54): on the effect's source node
plus its offset (the GENERATE rotation is not repeated), else on the recorder,
else on the followed bone, before the wait, the subcommands and the walls. The
next MOVE, MOVEEX or MOVERL releases the lock (8030A71C). `DW4_TRACE_LOCK=1`
logs locks, releases and locked models whose walls and floors run.

## Adding a manager

1. Find the manager's vtable (RTTI record + 8) and its handlers: +0x7C add
   info, +0x84 per-RAM update, +0x88 stage begin, +0x90 command handler; the
   command names come from the table passed to 800635C8.
2. Add a `Manager` subclass that `accepts` the info type, registers its RAM
   rows under the original category, implements `command` and `update`.
3. Register it in `register_default_managers` before `PassiveManager`.
4. Verify with a smoke test (`--smoke-test`, `--input`, `--screenshot-frames`).
