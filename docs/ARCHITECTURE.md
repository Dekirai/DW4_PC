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

The host's presence features (overhead name tags, Discord Rich Presence,
achievements) only read the engine after a step. The one thing the engine
does for them is the observe-only event list (`engine/port_events.hpp`,
`Engine::set_port_events` / `take_port_events`): a few hooks in the game
logic (boss death scripts and defeat flags, quest goals and story flags in
`FLGset`, level ups, technique changes in `RECALCPLAYER`, the Digi-Lab's
form change, cards, player defeats, critical hits, enemy defeats) append an
event while a host collects them. Collecting is off on a new engine (so a
save state's replay reports nothing), nothing in the engine reads the list,
and it is neither in `fingerprint()` nor in a `StageSnapshot`: the
determinism reference (`DW4_TRACE_FINGERPRINT`) and the audio dump are the
same with collecting on and off (`tests/port_events_tests.cpp` checks the
fingerprint too).

## Frame step (`Engine::step`)

One step is one original frame, one NTSC field: the world clock advances by
the field length, 16,683,333 or 16,683,334 ns (50,050,000 ns per three
fields), as the original adds the measured OSGetTime delta (8028AF84) and
runs one frame per field. Delays and frame counts in the data still use the
nominal 16,666,666 ns frame (`kFrameNs`), so an n-frame wait ends after
exactly n steps. In order:

1. Pads: `dw4::OriginalPads` computes trigger/repeat masks from the host
   samples every frame (bePadManager 80311F84). STAGECTL `PADINPUT DISABLE`
   (+0x1D of the pad module, 0x80348108..0x803481A8) only changes what the
   module's getter 80312138 hands out: while it holds, `Engine::pads()` gives
   the fifth pad, which the update never touches (no input, not connected),
   checked at each read, so an ENABLE sent by this frame's demo already
   counts for the managers that read after it, a press read in the frame of
   the ENABLE counts and a button held across it gives no trigger. The
   original schedules bePadManager after beMessenger and the stage loader
   (item 3), so a delivery or `start.lua` / `first.lua` would read the
   previous frame's slots; the port updates them first because nothing before
   the managers reads them (every DOL call of the getter is in a manager's
   RAM update or a class update below one, no script function reads a pad).
   (After NEXTSTAGE's frame the steps of the application loop's module reload
   come first: covered fields without any of this, see the stage loader
   below.)
2. The frame clock advances, so updates and the following render observe the
   same time.
   beSystem's update (80316258) then reads the console's reset button
   (`dw4::ConsoleReset`: B, X and START held on a controller for more than
   three seconds tap it, as the controller hardware does); its latched
   release turns the picture black and nothing else runs: after
   `Engine::kRebootFields` steps the host starts a new engine on the boot
   stage (the console restarted the disc, OSResetSystem).
3. The pending stage change (beSystem), then the camera viewer's frame
   (`CameraManager::begin_frame`) and the deferred messenger sends (80304C58:
   every `sd(..., delay)`, also a delay of 0, and the engine's own posts
   arrive here, in a later frame than they were sent; none while a SUBWND
   program or the soft reset pauses the queue, see
   docs/research/DEFERRED_MESSENGER.md). The serial scheduler runs the
   modules in this order (Dolphin, round 44: the viewer 8040DA1C, beSystem
   80316258, beMessenger 80304C58, beCri 802F53B8, the stage loader
   802F8A64, bePadManager 80311F84, beDemoManager 802F7EC0, then the
   managers), so the loader's sends and the posts of `start.lua` and
   `first.lua` follow the frame's pass, and every message object swap of a
   stage start comes in Dolphin's order.
4. The stage loader states (802F8A64): the loading screen
   `loadingUSA/ld<stage name from its third letter>.igb`, package members,
   `func.lua` / `boot.lua`, common files queued with `CFLsetName`, the wait
   for the loading screen's minimum play time (its beLoadingNode then holds
   its lock time on the LOADING layer until the scripts remove it), info
   registration, `start.lua`, `first.lua`. Then the demo manager (`DMsetSD` /
   `DMchkSD` / switch records, one blocking record at a time).
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
   order at each stage start. Then the CRI servers of the next retrace
   (`SoundManager::server_tick`, the original's ADXT server 8029A60C and file
   server 80296904: stream reads on the simulated drive, decoding, PLAYING;
   see the sound section below) and last the movie player, whose codec
   update waits for that retrace inside its own update. The original runs
   these servers in their own threads, woken at every retrace ahead of the
   frame (ADXM_SetupThrd 8029D0D8: ADXT server thread 8029D434 at priority
   12, file server thread 8029D3DC at 14, both after VIWaitForRetrace, main
   thread 16; Dolphin r44: the two passes 0.013 / 0.003 fields before the
   frame's world clock sample, every field), so the whole update of a frame
   - the loader's requests and bookings, a stream started or paused by a
   delivery, `start.lua` / `first.lua`, the demo or a manager, the entries'
   update and ISREADY / ISALLREADY - lies between the pass of its own
   retrace and the pass of the next one: a stream's first read comes in the
   next field whatever started it, and a PLAYING counts from the next
   update. A field a movie frame or the module reload covers runs only these
   servers and the SNDF's AX frames (`Engine::hold_update`, see the movie
   below).

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

An ACTION actor's motion combiner (`MotionState`) keeps each state's clock as
the original's igAnimationState does (`StateClock`: time = ((T >> 18) * speed
+0x38) << 18 + base +0x40 at node time T, 801D3628). A state that MOTIONSET
puts in (801D1BCC) is not evaluated yet (+0x1c, `MotionState::fresh`): every
combiner update restarts it at time 0 with its fade starting at that update
(801CFFB4 0x801D00D0..0x801D0164) until the traversal's evaluation (801D0C14)
or the motion phase's update (802F0EB0) marks it evaluated. Because the
traversal culls, a clip set while its actor is out of view or before the
stage's first picture starts - and its fade and WAITCHECK 'M' count - from
the actor's first traversal (n_leomon's wait02 in staggf03 1.95 s later;
e_rare's trnsIN in dng412b1; the g_b01base1 bases in stagBS01 up to 1.9 s).
MOTIONSET's field 3 'S' freezes a state at the world time's state time
(802F1160: the first letter the combiner's first state 0x802F12C0 - the
previous clip; a baked pose has no time - the second the new clip
0x802F1370; speed 0, the base moved to keep the time). The frozen new clip is
still fresh, so the next combiner update restarts it at time 0 on the frozen
clock: it holds its first picture. Once one state is left, the motion phase
gives it the node clock's scale again with its time kept (0x802F108C). Only
two MOTIONSETs of the game data use 'S' (all 6142 records of ndmw.afs and
area.afs checked): n_leomon's KYRO01 in staggf03 ("RS", action01 holds its
first picture through the 16 frame fade) and f_apocaly's ACTN05 in stagBS01
("SS", wait01's first picture for two frames). WAITCHECK 'M' compares the
state time against the clip's duration unsigned (0x802F03A8). Not modelled:
the speed and base a reused pool state brings along (801D1BCC resets
neither) - the port starts the activation's combine mode 0 state at time 0,
and no 'S' state goes back to the pool frozen in the data. `MotionState` is
not in the snapshot: a restore replays the stage. A fresh state that the
widened widescreen picture shows while the original's frustum still culls it
(`peek_pose`) is drawn as the combiner update would leave it now, on a copy
of the combiner: its first picture, the fade at weight 0, until the first
traversal starts the clip from there. (Drawn from its MOTIONSET's time
instead, the clip ran in the margin and jumped back to time 0 at the first
traversal: in the cheat story run's widescreen pictures Ophanimon's wait01
25 s, n_gekoa's in stag0302 8.5 s, the stagBS01 bases 0.6-1.5 s, each only
a few pixels at the picture's edge.)

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
A request takes one of the Cri SE list's 48 sfp entries (802F66C8): the first
free one, else the one with the oldest serial among those whose priority
(beSoundData [9]) does not exceed the sound's, stopped first (802F56B0); its
SE list index is the SNDF sequence it plays on (8031CCFC: SEQ_SET, SEQ_PLAY,
then SEQ volume 127). `dw4::sndf` (include/dw4/sndf.hpp, in the core) is the
SNDF the entries talk to (803C2AC4 commands into its ring; GETSTATUS at once)
and its AX frame callback (802A8F7C: the driver tick 803C325C, the AXART
service, MIXUpdateSettings): the tick frees voice records whose AX voice ran
out or whose release reached -96 dB, runs the queued messages (a request
starts one of 16 tracks, 803C5188, whose notes reach the drum note-on
803B9BFC; 0xB035 keys the sequence's voices off, 0xB037 sets its SEQ volume)
and the tracks; a note takes an AX voice at the split's priority (+0x1D + 11)
from the pool of 64 minus the CRI renderer's (AXAcquireVoice 8023C9D8: a
voice of a lower priority, such as a keyed-off one, is taken through the drop
callback 803C0B44) and the first free of 64 voice records (803C0CC0); its
pitch is fctiwz(100 * semitones + 1.5625 * fine) cents, the fine tuning
(+0x0B) and a random detune (+0x14, the C library's rand() 8009C1CC, seed 1)
in 64ths of a semitone, its SRC ratio the cents through the table 804C4A98
(8024583C, 16.16 truncated; every 24 kHz split: -498 cents, 0.750015 at the
DSP's 32028.5 Hz); its volume envelope has an attack from -96 dB (+0x0C,
stage ambience loops only), decay and sustain and a release on key off
(+0x12, 803BBA80). The Cri's per-field update frees an entry once 8031D0F0
reports its sequence gone (GETSTATUS: a one-shot track or any voice record of
the sequence; an entry the driver never reported playing stays) and stops a
fade to 0.0 at 12 or less; a stop (802F5F98) sets SEQ volume 0 and sends the
SNDF stop only while GETSTATUS still reports the sequence. SOUND ONFREEZE /
OFFREEZE (the SUBWND programs, 80315784 -> 802F68D0) set every running type 4
entry to SEQ volume 0 / 127 (an ADX one to -99.9 dB / 0 dB). The game's
SNDF runs in game time (the AX frames before each update; the update's
commands reach its ring 0.6 ms after the frame's world time, as Dolphin's
onsets step by an AX frame for requests 0.57 / 0.65 ms before a boundary);
its tick takes whole milliseconds off the tracks' waits (803C325C: round(us
/ 1000) of the CPU timer, 5 per AX frame) and a track's pass ends after any
event with a delta, also 0 (803C49F4). The audio hears a mirror of it
(`sndf::Mirror`): a copy ticked once per AX frame of the audio timeline that
gets the game's calls between the same ticks, so nothing the game sees
depends on whether audio is rendered, and a host that renders ahead of the
game hears a request later by as much as its queue runs ahead (a top-up of
1600 samples: 6 AX frames), never twice. A copy made while voices run (the
first render after a restore) moves them to the audio's frame numbering.
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
model's SE set: the sounds an ACTION2 action plays after a frame delay. The SE
sets hear the action through ACTION_BROADCAST (803130D4 sends MODELCTRL
SETSOUND), which an action start sends after its first commands ran and only
when the action changes (802F1794, 80305ED0 at 0x802F21D8), so the same action
given again plays nothing. SETSOUND only queues a sound subcommand on the
model (8030DE1C -> 80310FAC, beside its effect and hitbox subcommands); the
model's subcommand pass (803107E0 at 0x8030D008, in its update) adds the
system step and then starts it once the delay is reached (80310754: SOUND
PLAY, 803105DC). An action of the model's own update starts a delay-0 sound
in that frame and a delay of d frames d - 1 frames later; an action another
manager or model starts after the model's update (a window a menu opens, an
effect another model generates, a damage reaction) is heard a frame later -
the reference captures' sound subcommand log (append vs start counter) shows
both. A started type 4 sound keeps its subcommand with the play id, and the
subcommand cleanup of the model's next sequence start or REMOVE fades it out
(803106C0: VOLUMECTL DOWN, 1.0 -> 0.0 over 10 frames; an SFP entry steps
8031DDB8 to SEQ volumes that reach its sounding voices, 803BC708, and stops at
12 or less; the AX mixer ramps each new level linearly across the next AX
frame, MIXUpdateSettings 8025A530). Levels follow the
AX mixer (MIXInitChannel 80259334): the left side is attenuated by the pan
table at the pan and the right side by the table at 127 - pan, so effect
voices (split pan through the 8045D130 curve; every shipped split is centred)
and mono streams play at -3 dB per side while stereo streams keep each channel
at full level on its own side (802A7F34). The RNA renderer sets each ADX
voice's sample-rate converter to rate / 32000 (802A6C38, mode 0) while the DSP
outputs 54 MHz / 1686 = 32000 * 1125 / 1124 samples per second, so the beCri
streams play 1125 / 1124 fast (48042.7 Hz, the 240 samples per AX frame
CriStreams counts); only Sofdec's handle takes the corrected mode 1
(1124 (rate + 1) / 1125, 48000.2 Hz). Effect voices also feed the AX
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
split, 0 = none) relative to its main level, so a voice started by a sound
subcommand sends like any other and an entry's SEQ volume fade (the voice's
volume articulator, upstream of the MIX buses) lowers its send with it; the
streams and the movie send nothing (the RNA's AUX levels stay at -99.9 dB,
802A7B5C). `render()` mixes the whole AX output at the DSP rate the way the
AX command list of each frame has the DSP do it (`dw4::axmix`, __AXNextFrame
8023D0D8; AIInit 8023C0D4 sets the AI's DSP rate to 32 kHz, 54 MHz / 1686 =
32028.5 Hz): frame by frame (160 DSP samples, 2427840 CPU ticks, on the grid
CriStreams uses), every frame once and in order when the output first needs
it. Streams and effect voices run through the AX linear SRC (type 1 for every
split and the RNA's voices: the 16.16 ratio steps a four-sample history and
the output interpolates its two oldest samples, as Dolphin's AX emulation),
their levels ramp across the frame (a level change of a voice or a stream,
ADXT_SetOutVol -> 802A6B8C -> MIXSetInput, reaches the next AX frame,
MIXUpdateSettings 8025A420; a voice or stream that starts plays at its level
at once, as Dolphin's onsets show no fade-in), the effect voices' AUX A sends
go to the AX library's three rotating AUX buffers (8023CE40, so a send comes
back two frames later) and the return is added; then the AX ucode's
compressor (command 0x12, which __AXNextFrame sends while __AXClInit 8023D788's
flag is set, and nothing clears it: a frame whose main bus exceeds 0x8000 is
scaled by an attack ramp down to -4 dB within the frame, then released by
0.4 dB per frame over ten frames, the ramps of 804C0820) and the 16-bit
saturation. A stream's start, pause and volume change from the game's update
reach the first AX frame at or after the update's command time (the ADXT
call reaches the RNA's voice at the next frame, 8029A480 -> 802A7534; the
server pass of a field a movie frame covers counts from its retrace, and the
movie's sound starts in the first AX frame at or after Sofdec's unpause), an SFP
note sounds from the frame after the SNDF callback that started it (the
callback's PB changes reach the DSP a frame later, 802A8F7C -> 803C325C), so
a stream leads an effect of the same update by an AX frame, as in the
captures. The DSP's output is resampled to 48 kHz (562 / 375 exactly) by a
96-tap Kaiser-windowed sinc (flat to 15 kHz, the DSP's images about 80 dB
down from 16.5 kHz on): output sample s plays the DSP at CPU tick
s * 10125 plus one field minus the filter's 1.5 ms (the chunk rendered after
the update at t holds the audio from t on); the audio's AX frames map to the
game's by whole frames, a mapping that moves only when the audio runs more
than half a field off the world clock (a host topping up its queue, a clock
file's jump, a restore), so the mix itself never repeats or skips anything.
`dw4_game` queues the output on an SDL3 audio stream (`DW4_AUDIO_DUMP=FILE.wav`
records it, `DW4_AUDIO_DUMP_STREAMS=FILE.wav` the part of the same mix of the
stream channels `DW4_AUDIO_STREAMS_CHANNELS` names (default BGM and
ambience), `DW4_AUDIO_TOPUP=FRAME:SAMPLES` renders extra audio after a frame like
the host's queue top-up, `DW4_AX_COMPRESSOR=0` leaves the compressor out,
`DW4_TRACE_SOUND=1` logs the effects and sound subcommands, the SNDF voices
and where each stream's and the movie's audio begins in the dump).
MOVIE PLAY (8031EDFC) looks up the beMovieInfoData of the
file (strcmp; [4] layer, [5] end script; a name the list lacks plays nothing,
so does one behind an empty entry or one of another type, where the search
ends (8031EE8C), and a PLAY while the beMovie name is still set is refused),
splits the Sofdec program stream with `dw4::sfd`
(MPEG-1 video 0xE0, ADX sound 0xC0) and plays the sound on a movie channel
of the AX mix (at the DSP rate like the streams, no AUX sends);
the host decodes the video with pl_mpeg and draws the picture the player
took over the screen. The beMovie RAM keeps the name (0x8031F308), which the
skip and the end script need, only once the movie layer answered LAYERCTL
GETLAYERNODE for the data's layer and the movie has its scene (the movie
manager makes it after the codec's open, 802AEC7C -> 802AF5C8); a movie
without them plays on unnamed, and a file the codec cannot open plays nothing
and leaves no name. The only movie is the opening (stagIS09 first.lua,
AUS_opening.sfd on the USA disc: 1920 pictures at 29.97 Hz), played after
the logos and again whenever the title's prompt times out (attract mode).
The game hands the movie to the Alchemy movie plugin (igMovieManager
802AEC7C, igCriMovieCodec): its open (802ADC8C) reads the file's first ten
sectors through ADXF and waits for them with VIWaitForRetrace inside the
update of MOVIE PLAY, so that frame takes 8 or 9 fields, then starts the
Sofdec player (prep state; 803E3D1C makes the sound's ADXT in RNA mode 1,
paused). From the next frame on the movie manager's codec update (802AE8B8)
waits for a retrace itself before its ADX server pass and mwPlyGetCurFrm, so
every frame of a movie takes two fields: the game runs at 30 Hz until the
stage's teardown stops the movie (`Engine::hold_update`: the covered fields
run no update, keep the picture and the input, and only tick the CRI
servers and the SNDF's AX frames, `SoundManager::update_held`; `Engine::held`,
`Engine::vi_time`). Every field ticks the servers once
(`SoundManager::server_tick`, on the field's VI time): a frame that updates
after the loader's update, a covered field after the open's check. In the
drive model's clock a tick stands for the servers on the retrace after it
(`DiscDrive::kDolphinClockOffset`), so the tick of the MOVIE PLAY step (the
stage's first.lua runs in the loader's update, before it) is the open loop's
first pass (the header read) and the tick of the step before a covered field
is the pass that field's loop iteration checks: the field whose pass booked
the header runs
mwPlyStart and ends the frame. The first codec update's pass starts
Sofdec's stream (a 365-sector ring on the simulated drive, CriStreams
`movie_*`); the player leaves prep two passes after the first with data
(803E38C4 unpauses the sound 0.084 field after that pass), takes picture k
in the codec update after the next k ones (update 12 + k) and reads
PLAYEND two updates after the last picture; the XFB of a frame shows the
picture of the codec update before its own (Dolphin: the frame taking k
still shows k - 1), so the port's covered field shows that one. The codec
then stops with state 2 and the next beMovie update (8031EC10) runs the end
script and clears the name (a later PLAY is taken; the ended codec stays
and waits its retrace in every frame), while the codec keeps the 30 Hz
frames going until the teardown. A or START from player 1 (read through the
pad getter, so a press in the frame of the PADINPUT ENABLE counts; the
stage's script enables the pads 120 nominal frames of world time after MOVIE PLAY, which
the first update at or after that time takes: about 60 of the 30 Hz updates,
Dolphin V 1116 -> E 1236, and in attract mode the time falls on a covered
field and the update after it takes it) stops every movie at once (802AF560)
and runs the end script in the same update. While
Sofdec decodes, the frames start 0.011 field after their retraces
(`Engine::set_clock_lateness`), so the skip's NEXTSTAGE, posted for one
nominal frame later, arrives two updates after it; the decoder works two
pictures ahead, so the frames start on time again from the one after the
codec took the last but one picture (Dolphin's world clock samples: late in
the frame taking picture 1918 of 1920, on time from the one taking 1919), and
a skip in the movie's last frames gets its NEXTSTAGE in the next update
(Dolphin d_rskipend1 / 2). The picture the codec
took last stays on the screen through the frame that tears the stage down,
after the end and after a skip (`Engine::presenting`, `SoundManager::movie_still`):
none of the frames after the stop runs a codec update, so their XFB shows
that picture, not the one before it that a playing movie's frame shows
(Dolphin d_skipeven2). (Dolphin captures of a
boot with both movies and of skips: work/dref r40movie / r41movie runs with OSReport
hooks on these functions.)

When a stream is ready matters to the scripts: demos wait with
`DMchkSD SOUND ISALLREADY` / `ISREADY`, which test ADXT status 3 (PLAYING,
802F6FB0), and the original only gets there after the stream's first block
came off the disc. `dw4::CriStreams` (include/dw4/disc_timing.hpp, in the
core) models that on the engine clock, once per field from the CRI server
tick (also during loads), which the frame step runs after the managers and
before the movie player: the pass of the next retrace, which in the
original runs in the server threads before the next frame's update (see
step 5 above). So a stream started anywhere in a frame's update is first
read in the next field and a PLAYING counts from the next frame's update,
as in Dolphin (r44 / r46 time-base logs: ADXT_StartAfs in an update, the
first read at the next retrace, the PLAYING store at a retrace, ISALLREADY
1 in the update after it; stag0203: start.lua's AM_DESER, its second read,
the posted AM_VALLE and both PLAYINGs on Dolphin's passes; staggf03's
PLAYatPAUSE DM_LEOMN's first read on Dolphin's field against the demo).
Until r46 the pass ran after the loader's update and held a stream that a
delivery or the loader's scripts started for the next field's pass
(`CriStreams::Handle::hold`), so streams started in an update were read a
field late on the drive's clock while the demo and the managers saw each
PLAYING a field early, which cancelled in the frames ISALLREADY answers 1.
The model: the six beCri ADXT handles (two stereo 48 kHz for
BGM and ambience with a 53-sector input ring, four mono with 13 sectors),
their ADXSTM readers (one DVD read at a time through gcCi - 802A5D6C
refuses a read while any handle's read is on the drive, the exec 802A680C
at the start and the end of the file server marks it done - the lowest
handle first, the loader's ADXSTM 8051B1D0 after the six, a finished read
booked in its handle's pass before that handle asks again, 25 sectors
before the header is parsed, a refill of the free chunk up to the ring's
end while the ring holds less than 85 %, 8029B2D0 / 80299248: 45 of 53
sectors, so a stereo stream reads 8 or 9 sectors every 18 or 20 fields),
the decoder (one call per server pass, 80290074 / 80293E5C: the least of
the input's whole frames up to the ring's end and its 36-byte extra area,
the output SJ's free chunk up to its end capped at the unit of three
fields, and the frames to the output ring's end), the ARAM renderer (4096
samples, played halves given back, one DMA per pass of the least of the
ring's free chunk up to its end and the output SJ's data up to its end and
96-sample extra area, 802A6DF8; not modelled yet: at a loop's end the
decoder stops at the loop end sample and the trap 8029C970 / 8029CAFC drops
the rest of the loop end's sector from the input, 2048 - loop end byte % 2048
bytes, 8029C1E0 +0x50, while the port decodes on through it), the DSP's progress as the renderer reads it (the voice
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
(igIGBFile::readFile 800528DC through igAdxFile 802AA1B0 over a CRI file
stream that comes after the ADXT readers in each server pass, a thread below
the main thread) the loader reads every file of a load in 64-sector requests
and asks for the next chunk when the parser wants bytes beyond the last one.
The thread's wait for a chunk (802A9DC4) does not follow the retrace: with
igAdxFile +0x3C / +0x3D set it sleeps 16666666 ns (+0x58 / +0x5C) between
looks at the stream, so it takes a chunk up at the first look after the
server pass marked it done, on a grid that starts at its request and drifts
against the fields (Dolphin r46, hooks on the requests and the wait's returns
of 956 chunks: the return is the request + 0.00245 + n * 0.999309 fields
within 0.002 and never before 0.027 fields after the retrace; the main
thread and the servers take the first 0.031 fields after each retrace).
What it does with each chunk follows the file's content
(`loader_work` in engine.cpp, hooks after each step of 800528DC in 106
files): the type metadata, the directory entries, the reference index,
making every object (0.000247 fields each, 9.7 fields for a stage file's
30000), the object pass through the reader's 128 KB buffer (+0x104,
refilled by 800506CC: the thread asks for a chunk a buffer ahead of the
records it reads; 0.000108 fields per record, 3.66e-5 per string byte), the
memory blocks read in place (1.01e-6 per byte, 0.13 fields a chunk) and the
passes after the last byte (memory entries, every entry's +0x90 800527D8,
which grows with the textures' igCharMetaField bytes); walked from each
chunk's arrival around the main thread's window this puts 941 captured
chunk times within 0.14 fields (rms). The update after the work after the
last chunk routes the file, the next update asks for the next one (the
thread takes it up when that update ends: 0.036 fields after the retrace in
states 100 and 2, 0.068 in state 4, 0.249 for the loading card, the
descriptor after the reload's late sample). Routing a large member, state 5
and start.lua are long updates that cover the retraces after them
(`loader_register_cost`, `loader_start_cost`: refitted to 14 Dolphin loads,
-1.888 fields + 0.00315 per model RAM + 0.0028 per info and -4.064 +
0.00425 per model RAM + 0.0109 per generated model; dng205b3's start.lua
covers 2 retraces, dng308b3's 1). A stage change itself starts with
the application loop's module reload: NEXTSTAGE's frame ends the loop's update
/ render loop, and after its picture (the endScene retrace) the loop reloads
every module (80021944 -> 8040F570 -> 8028A87C: every module's +0x70 reset,
igMovieManager's stopping the movies, then every info of the application's
list +0x10 deleted) before the next update samples the world clock and
beSystem releases the directories and asks for the descriptor (0x8031658C /
0x803165A0). The module resets come at the reload's start
(`Engine::reset_modules`, every `Manager::clear_stage`, the messenger and the
demo): igMovieManager's 802AF560 stops the movies, beCri's 802F523C turns the
effect off (802F6F10 NONE: the reverb's delay lines go) and unloads the
stage's banks (802F6BAC; the port frees their loops), so a stage's sound ends
with the reload's first field (Dolphin d_ngsr1, a soft reset in dng101b1: the
DSP output is silent from the field after NEXTSTAGE's on, the port's on the
same field). Dolphin captures with hooks on the reload, every reset and every
delete (r44timing d_td3) put 0.45 fields into the module resets and the rest
into the deletes, growing with what the infos hold;
`Engine::loader_teardown_cost` is fitted to 16 measured reloads (boot stages,
stage changes, soft resets, game overs, a gate warp; J->U hooks or world clock
deltas): 0.448 fields + 0.753 per 1000 objects of the beDBManagerInfos + 0.656
per 1000 other memory blocks + 0.166 per 1000 other objects, within 0.41
fields of each (the update after the reload on the original's field in 15 of
16; stag0103's 2P soft reset, 17.04 fields, ends a field early; the table is
at the function). `Engine::step` runs the reload's whole fields as covered
fields (no update, the picture of NEXTSTAGE's frame stays,
`Engine::presenting`) and the update after it samples the world clock late by
the rest of a field (`StageSnapshot::clock_lateness`) and sends the descriptor
request at once. (Until r44 the loader charged 1.5 fields + 0.7 / 1000 memory
blocks + 0.18 / 1000 objects to that update: fitted to its whole world clock
delta, a field more than the reload, so every descriptor read after a teardown
came a field late.)
So a stage starts 500-900 fields after its NEXTSTAGE as in the original (a
constant 106 before). The load time depends strongly on where the BGM's
refill cycle stands when it starts: Dolphin itself gives 497 or 607 fields
for dng307b1 at two login times 155 fields apart. Against captures of 15
loads at both BGM phases (the boot's three: 234 / 61 / 118 fields against
233.8 / 62 / 121) the port's BGM refills come on the original's fields with
its sizes; what remained was the loading thread's timing (a chunk a field
early or late now and then, which can swap a chunk with a refill and shift
the rest of the load), modelled since round 46 from the thread's work and its
sleep grid. (Round 43 found loads after
the title whose BGM refills come 2-3 fields before Dolphin's from the load's
start on, +34 to +115 fields in all, and round 45 staggf03 -> stagML02 +88
to +91 fields. Round 46 traced both to the comparison, not to the CRI
model: the drive's rotational latency follows the parity of the whole
seconds of Dolphin's clock, and the port's world clock stood 240 to 420
fields behind the captures' (their boots and menus take longer), so every
seek of a load could cost half a turn (1.05 fields) more or less; the
NEXTSTAGE of a script's SEND stood 1 to 157 fields off the BGM's file
position; and Dolphin's sampled world clock delivers a one-frame post a
frame later now and then (staggf03: NEXTSTAGE an update after the
port's). With the BGM's phase, the drive's rotation and the DSP's frame
grid put on Dolphin's at the load's first read (build tools of r46:
cmpload.sh, loadcmp2.py: a world clock offset, `DW4_DISC_CLOCK`,
`DW4_AX_PHASE`) every read of a load - loader chunks and refills, times and
sizes - matches Dolphin's until the first request that falls on the other
side of a server pass; title -> stagML11 the first 25 reads to 199 fields
and the load's last read on Dolphin's field. Round 46's part B put the
loading thread on the original's work per file content and its sleep grid
(above) and measured the load set again against new captures with the
time-neutral DVD log plus hooks on the thread's requests and returns (29 us
per OSReport, 0.0017 fields; two captures that differ only in those hooks
still part at a read that slips a whole field at 1482, and Dolphin's own
lengths of one load differ by up to 47 fields between capture setups:
dng101b1 493 / 514, staggf03 575 / 622). Aligned, the load's last loader read
after its first one, Dolphin / port (part A before) in fields: stagIS08 174 /
175 (172), stagIS09 39 / 39 (39), stagIS10 107 / 107 (105), the soft reset
dng101b1 -> title 107 / 106 (104), dngQ1B1e 616 / 596 (577), stagML11 466 /
466 (466), dng101b1 514 / 514 (489), stagBS01 733 / 733 (753, every PLAYING
and ISALLREADY of its start now on Dolphin's field), stagBS04 690 / 692 (692),
stagBS03 649 / 640 (640), stagGF03 622 / 543 (625), dng205b3 743 / 744 (712),
dng308b3 616 / 652 (629), stag0301 765 / 765 (775), and four new games sent
to stages outside the fit: stagBS02 632 / 673 (632), stag0203 733 / 733
(764), dng204b1 535 / 591 (591), stag0401 765 / 775 (753) - 10 of 17 within
2 fields (part A 6). The others part at a single request a fraction of a
field off: staggf03's fourth chunk of its stage file is asked for 0.12
fields early, before the pass where the BGM refill comes first, and the load
ends 79 fields early; dngQ1B1e and dng204b1 part at a refill a field late
(the DSP grid's estimate). A gate warp stag0101 -> stagML11, natural run:
494 / 475 (436) fields from the descriptor to the stage's start.)
The port parses each file when the original asks for it.
`DW4_TRACE_SOUND=1` logs every simulated read (with the drive's clock
modulo the 2 s its rotation repeats in, to set beside a capture's DVD
command time TB * 12 + 23.8e6),
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
CRI stream, no chunk processing or post-processing fields), the module reload
and the update costs (routing, state 5, start.lua) cover no retraces, the boot's timing
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
Static geometry sits under no transform (none in the stages checked, so the
HitLand traversal's matrix stack holds only its identity and 801F6050 tests
the triangles where the file puts them); a moving child's transform is pushed onto that identity (8021B044 +
80128774), and 801F6050 then tests the child in its own vertex space: each
segment goes through the inverse (8012A92C, 801252D0), a local hit comes back
through the matrix (8012D498), its distance is measured from the original
segment's origin (801F63BC) and the normal turns with the matrix (8012D434,
not normalized again).

The ray tests, the camera and the player input round like the Gekko
(`gekko_math.hpp`, compiled without contraction): fmadds-style fused
multiply-adds round once to single, square roots are frsqrte plus Newton
steps, and sin / cos / atan2 / asin / acos are the DOL's MSL (fdlibm with
CodeWarrior's fused multiply-adds), each in the original's operation order:
the face normal from 8012CB74's cross product (801238E8), the fused facing,
plane and hit-point arithmetic, CHECKWALL's side axis through the
HitLandModel's Euler (0, pi/2, 0) matrix (its cosine is -4.37e-8, not 0) and
its cos(acos()) push ratio, the camera's path product 80128774, Euler round
trip 8012947C / 80129134 and view inverse 8012A860, the input's general
inverse 8012A92C and wanted heading (frsp(atan2) sums, 8034C65C). Fed with
Dolphin's stick reads and world clock, the stag0204 cannon scene then matches
Dolphin's wall queries and answers, stick headings and box camera bit for bit
over the whole scene, and a 2-player dng101b1 stretch matches all 1462 of
its wall query and answer lines, enemies included.

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
place relative to the ridden matrix of the last frame: last frame's rotation
inverted in place (8012A92C), the model's matrix with its offset as the
translation times that, times the current matrix (the paired-single 80128690
twice; the stag0203 train matches Dolphin's 61 ride steps bit for bit). The
ridden car follows a recorder there (MOVERL JOINT), whose matrix the original
takes from its traversal as modelview times the inverse view (801DE9C0). The
traversal therefore carries the original's modelview stack next to the world
matrix it draws with: it starts with the camera's view (+0x64), every
igTransform and beModelCtrlNode2 pushes its local matrix times the top through
the paired-single 80128690 (801DC85C, 8030FAA4), and a recorder keeps the top
times the inverse view (+0xA4, 8012A92C of the view). So the car carries the
original's rounding noise (rotation 1e-9..1e-7, translation up to 5e-5 at
the stag0203 cameras) and its riders' land and wall queries follow Dolphin
bit for bit (round 44: recorder, stack top and inverse view 326 / 326 frames,
the follower's matrix 115 / 115, 5488 land / wall answers);
`DW4_TRACE_RECORDER=1` prints them in the hooks' format. The stage scripts'
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
