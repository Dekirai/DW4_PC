# Native animation timing and layer validation

Version 0.18 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. The portable core implements recovered numeric animation
clocks, source wrapping and ordered layer arithmetic. Complete ACTION/combiner
state management remains pending; the preview retains its provisional controller.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed without compiler warnings; 30/30 CTest suites and 6 Python verifier tests passed |
| Animation timing fixtures | 62 asset-free checks cover signed/unsigned coarse clocks, original metadata cache defaults, shared caches, speed-origin continuity, saturation, blend rounding, exceptional durations, endpoint branches and runtime modes |
| Animation graph and layer fixtures | 66 asset-free checks, including timed sampling, cache publication on success, layer order, original quaternion behavior, fused cancellation and invalid inputs |
| Original Agumon integration | 5,659 additional checks pass for all 82 clips; five modes at six local-time fractions add 2,460 complete timed poses and finite layer evaluations |
| Independent timing instruction oracle | 30,000 operations in 1,000 shared-cache programs match at prepared-object and duration-service boundaries; NaN payloads excluded |
| Independent layer instruction oracle | 10,000 tuples / 70,000 float32 quaternion and translation components match; half use decoded original rotations, half synthetic rotations |
| Packaged walking regression | 120-frame SDL replay passes with 198 steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads and final (-292,40,431); pause/restart/focus-loss checks pass |
| Packaged visual inspection | 1280x720 overview and walking captures inspected; version 0.18 visible, original Agumon/map render in the 1236x600 walking target with 8x MSAA |

The instruction probes cover numeric operations and bound channel mixing, not
the complete ACTION state machine, source construction, snapshots, events or
actor lifecycle. Timed original-data checks use the production codec and pose
path; they are not independent codec or original gameplay-frame comparisons.
Original bytes, local probes and captures remain ignored. See
[ANIMATION_TIMING.md](ANIMATION_TIMING.md) for ownership and exact boundaries.

## Native model sequence validation

Version 0.17 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. The portable command runner retains original model sequence
state, weapon sections, supported control/motion effects and required ACTION
requests. Complete outer player callbacks and ACTION execution remain pending;
the walking preview continues using its provisional host controller.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed without compiler warnings; 29/29 CTest suites and 6 Python verifier tests passed |
| Sequence fixtures | 65 asset-free checks cover original list slots, aliases, retained state, label caches/barriers, command-name precedence, nested jumps, movement stop rules, failed/reentrant services and malformed/bounded input |
| Original Agumon sequence integration | 193 additional checks pass across both entry groups, all four slots and all four weapon sections; original lists request the matching WAIT/RUN/WAIT actions and travel approximately 8.275 local units at explicit cached delta 1 |
| Complete SEQ corpus decode | 9,060 programs, 80,621 sequence occurrences and 447,274 command occurrences in 2,273 archive IGBs; zero rejections; unsupported runtime commands are retained, not executed |
| Independent command instruction oracle | 6,000 bounded command programs match at the supplied list/string/channel allocation and ACTION service boundaries; NaN sign/payload excluded |
| Independent label instruction oracle | 3,000 programs with 24,539 set/lookup pairs match original label tables and cached offsets; missing original labels produce an explicit native guard |
| Packaged walking regression | 120-frame SDL replay passes with 198 steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads and final (-292,40,431); pause/restart/focus-loss checks pass |
| Packaged visual inspection | 1280x720 overview and walking captures inspected; version 0.17 visible, Agumon and original map render in the 1236x600 walking target with 8x MSAA |

The original-data fixture supplies a recording ACTION service to verify requests;
it does not execute the animation manager, full sequence entry or player creation.
The instruction probes use synthetic prepared work and supplied external service
boundaries. They do not establish allocator behavior, full callbacks, original
gameplay or physical multiplayer. Original bytes/probes/captures remain ignored.
See [MODEL_SEQUENCE.md](MODEL_SEQUENCE.md) for exact scope and source addresses.

## Native model control and preparation validation

Version 0.16 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. Original CTRL/PDMOVE effects, movement preparation, player
turn rates and post-input sequence requests now run in the portable core.
Full original player construction, input and sequence/action execution remain
pending. The walking preview retains its provisional host controller.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed without compiler warnings; 28/28 CTest suites and 6 Python verifier tests passed; installed executable matches the build by SHA-256 |
| Model control fixtures | 64 asset-free checks: original rates, FMA signed-zero heading, timer/position-lock preparation, exact CTRL/PDMOVE fields and names, byte/phase branches, request priority, independent state, dependency guards and atomic failures |
| Complete CTRL/PDMOVE scan | All 668 records in 2,273 recognized archive IGBs decode: 526 CTRL and 142 PDMOVE; zero rejections |
| Independent control instruction oracle | 10,000 cases, 2,500 each for CTRL, PDMOVE, post-input control and MOVE; zero mismatches at the supported service boundaries |
| Original command/placement integration | 49 additional checks pass using Agumon Move 130 / Stop 1540, CTRL 6499 / 622 and all eight PLAYERPOINT 115 matrices; explicit command effects request WAIT/WALK and travel approximately 8.275 local units over 18 Move / six Stop updates at cached delta 1 |
| Packaged walking regression | 120-frame SDL replay passed: unchanged 198 host steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads and final (-292,40,431); pause/restart/focus-loss checks passed |
| Packaged visual inspection | 1280x720 overview and walking captures inspected; version 0.16 visible; Agumon and original map render in the 1236x600 walking target with 8x MSAA |

The oracle executes a bounded instruction profile with synthetic named objects
and preallocated channel/list capacity. Host atan2, timer creation and sequence
dispatch are supplied boundaries. It does not establish original libm parity,
allocator behavior, full sequence execution or gameplay equivalence. The optional
data checks explicitly apply commands at stored matrices; they are not original
player creation, a scheduler run or physical multiplayer. Original bytes,
instruction probes and captures remain ignored. See [MODEL_CONTROL.md](MODEL_CONTROL.md).

## Native model turning and movement validation

Version 0.15 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. It adds the recovered model turn interpolation and combined
rotation/translation order to the portable core. The walking preview still uses
its provisional host controller; original player preparation and dispatch remain
required before that core can drive the original game.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed; 27/27 CTest suites and 6 Python verifier tests passed |
| Model motion fixtures | 65 asset-free checks: shortest-arc ties, signed steps, exact-zero/force branches, timer side effects, Euler basis and position, negative zero, update order, independent ownership and atomic failure |
| Accumulated angles | Accelerated wrapping matches a separate literal repeated-float loop across both signs and exponent boundaries through magnitude 2^27; stagnant original loops stop explicitly |
| Independent instruction oracle | 9,000 cases: 6,000 standalone turns and 3,000 combined ticks; all 8,434 published states/matrices match by bits, with 566 original non-finite poses rejected at the native boundary and zero mismatches |
| Original Agumon commands and placements | 22 additional checks pass using Move 130 / Stop 1540 and all four PLAYERPOINT 115/group-0 matrices; 18 Move and six Stop updates at explicit cached delta 1 travel approximately 8.275 units along each source basis and stop |
| Packaged walking regression | 120-frame SDL replay passed: unchanged 198 host steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads and final (-292,40,431); pause/restart/focus-loss checks passed |
| Packaged visual inspection | 1280x720 overview and walking frames inspected; version 0.15 is visible, and the walking target is 1236x600 with driver-supplied 8x MSAA |

The instruction oracle shares host double sine/cosine and IEEE float FMA with
the native boundary. Its bitwise matches establish surrounding arithmetic and
control flow, not equivalence to the original PPC math library. NaN payloads,
FPSCR exception flags and guest subnormal modes remain outside this profile.
The original-data fixture exercises four stored placements, not original spawn
initialization, physical multiplayer or the game's scheduler. Original data,
instruction probes and captures remain ignored. See [MODEL_MOTION.md](MODEL_MOTION.md)
for source addresses, supported effects and remaining integration work.

## Native motion and timer validation

Version 0.14 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. The original motion arithmetic and timer are portable core
implementations; the walking preview still uses its provisional host controller.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed; 26/26 CTest suites, 6 verifier tests, 9 boot CLI, 11 app CLI and 10 play CLI tests passed |
| Motion fixtures | 168 checks: six modes, preserved/impulse speed, strict crossings, expiry before clamp, exact-zero sentinel, signed deltas, valid zero-frame infinity/NaN, malformed schema and atomic input failure |
| Channels, matrix and timer | Independent owned channels, shared cached delta, initial cached frame 1, coarse clock/float rounding/wrap, signed scale and elapsed time, saturation, strict expiry, preserved restart fields, local scale/rotation and explicit fused translation |
| Complete original command scan | All 3,796 MOVE/MOVEEX records in 2,273 recognized archive IGBs decoded; 197 MOVEEX and 131 zero-frame records; zero rejections |
| Independent scalar instruction oracle | 18,000 setups and 54,000 ticks agree with a bounded interpreter of the supplied PPC routines, including expiry/stop, overflow and unordered comparisons; finite/infinite results checked by bits, NaN payloads excluded |
| Agumon original commands | All 16 MOVE records decode; original Move 130 reaches speed 0.7 and travels approximately 6.65 over 18 delta-1 updates; Stop 1540 travels approximately 1.625 over six updates and becomes inactive |
| Packaged walking regression | 120-frame SDL replay passed: unchanged 198 steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads and final (-292,40,431); pause/restart/focus-loss checks passed |
| Packaged visual inspection | 1280x720 overview and walking frames inspected, driver supplied 8x MSAA; version 0.14 and existing preview scope remain visible |

Original instructions/data and local oracle probes remain ignored. These checks
establish the supported arithmetic/data profile, not a full original gameplay
comparison. NaN payloads, FP exception flags and guest subnormal modes are not
modeled. Original actor/sequence state, turn interpolation, collision response,
timer ownership and scheduling are required before integrating this core into
the original game. See [MOTION.md](MOTION.md).

## Native initialization and pad validation

Version 0.13 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. This increment reconstructs preliminary loading, supported
native callback effects and original physical-pad state. It does not complete
the original startup, audio backend or multiplayer game.

| Check | Evidence |
| --- | --- |
| Release build and package | Passed; 25/25 CTest suites, 6 verifier tests, 9 boot CLI and 11 app CLI tests passed |
| Native controller fixtures | 50 checks: exact exports/schema, owned state, source-qualified aliases, captured recipient keys, preserved RAM fields, ordered unknown-handler stop, atomic malformed steps and budgets |
| Startup loading fixtures | 81 checks including the prior aggregate path and new preliminary-only reader order, descriptor retention, empty `/s` subset, repeated loads, partial ownership, malformed routes and byte caps |
| Original preliminary trace | Only `ndmw.afs[80]` descriptor and `[178]` member requested; one retained file, 222,408 bytes and four exports; no ordinary/common read |
| Original callback effects | Preliminary Static info 499 / RAM 496 registers `STATICINFO / dng101b1 / 0`, then Sound info 3 stops the prefix; separate explicit Switch inspection initializes info 3471 / RAM 3443 without changing its target |
| Packaged preliminary CLI | Reports preserved native RAM/recipient state and returns the expected exit 3 at `beSoundInfo`; func/boot do not execute |
| Original pad fixtures | 2,130 checks: radius and angular boundaries, strict/inclusive comparisons, chord edges, independent stick/button repeat timers, four physical slots, freeze/disconnect/reconnect, invalid-input atomicity and raw GC conversion |
| Independent pad conversion oracle | Every 65,536 raw button mask agrees with interpreted original conversion instructions; all 131,072 signed-byte stick pairs across main/C-stick agree with the SDK branch formulas |
| Aggregate inspection regression | One-player profile retains nine files, 4,006,731 bytes, 454 exports, one Lua info, 17 held scenes and 436 aggregate deferred infos; actual Lua state remains alive |
| Startup UI | Finite packaged 1280x720 run passed; inspected screenshot shows explicit preliminary/audio bypass scope and readable owned-file results |
| Walking regression | Packaged 120-frame replay passed with unchanged 198 simulation steps, 101.333 travel, 46 idle/152 run steps, 102 pose uploads, final (-292,40,431), and pause/restart/focus-loss checks |

The native pad library has no device enumeration or original player-join state;
four independent synthetic slots do not establish physical multiplayer gameplay.
Host libm results at angular boundaries are not claimed bitwise identical to the
original PPC math library. Preliminary callback dependencies are explicit and do
not establish the original complete manager registry or sound-transfer services.
The existing aggregate inspection queue spans phases and must not be replayed as
the original later registration queue. See [CONTROLLERS.md](CONTROLLERS.md),
[PLAYER_MOVEMENT_RESEARCH.md](PLAYER_MOVEMENT_RESEARCH.md) and
[AUDIO_STARTUP_RESEARCH.md](AUDIO_STARTUP_RESEARCH.md).

## Native walking preview validation

Version 0.12 was validated locally on 2026-09-09 with MSVC 19.51 and an NVIDIA
GeForce RTX 5070 Ti. This establishes an interactive native original-map walking
state, not completion of the original game or its startup sequence.

| Check | Evidence |
| --- | --- |
| Release build | Native executable, portable core and package built successfully |
| Asset-free tests | 23/23 CTest suites; 6 verifier, 8 inspect CLI, 5 catalog CLI, 8 boot CLI, 11 app CLI and 10 play CLI tests passed |
| Walking solver | 683 synthetic checks: slopes, shared edges, tiny gaps, stepped/stacked floors, bounded hierarchy, swept wall radius, sliding, overhead walls, malformed input and equal travel at 30/60/144 Hz |
| Collision reader | 40 synthetic checks plus 5 original checks; 102 original floor triangles and 228 original wall/door triangles preserve their source coordinates and stored inner switch selection |
| Original assets | Map package dng101b1 member 1 / BG02, player model 1386, original entry 115/group 0/player 0, waitS/runD clips; 236 textured GPU batches and six images |
| SDL keyboard replay | 120-frame run: 198 host simulation steps, 101.333 units of total travel, 46 idle and 152 running steps, 102 pose uploads; final position (-292,40,431) after restart and further movement |
| Pause, restart, focus loss | Production replay asserts frozen position/animation, cleared held inputs and exact grounded spawn/facing/idle restoration; all three checks passed |
| Original-data lifecycle | 37 local harness checks passed: movement in all four directions, stable map batches/textures, pause, held-key clearing, restart, return to workbench, fresh start, remount, missing-resource failure and recovery |
| Rendering | 1280x720, 1920x1080 and 3840x2160 windows passed the same replay with identical simulation results; scene targets 1236x600, 1876x960 and 3796x2040; driver supplied 0/8/8 MSAA samples |
| Visibility and GPU state | Original GPU suite plus partial-occlusion silhouette, unchanged visible texture, no self-highlighting, invalid batch rejection, resize, MSAA and state restoration passed |
| Visual inspection | App-rendered walking, spawn, pause and overview frames inspected; original Agumon is visible in the corridor, with gold hidden-surface indication when needed |

The 120 Hz solver, speed, body dimensions and follow camera are provisional
native behavior. These finite runs are not a sustained FPS benchmark. Physical
Xbox/PlayStation/GameCube device compatibility, original collision parity,
enemies, combat, doors, audio, progression, saves and multiplayer remain open.
Original-data captures and harnesses are ignored local files; CI needs no game
assets. [PLAY_PREVIEW.md](PLAY_PREVIEW.md) describes launch and controls.

## Earlier viewer and startup validation

Local validation completed 2026-09-08 on Windows x64, MSVC 19.51, NVIDIA
GeForce RTX 5070 Ti. This records version 0.11 owned startup loading and routing,
not gameplay compatibility or a finished port. Earlier asset, animation,
placement and renderer measurements are retained from their respective
milestones. Retained Lua runtime ownership, ordered common-file loading,
source-qualified info routing and startup UI/CLI lifecycle are the new
acceptance work.

| Check | Result |
| --- | --- |
| Release core and native app build | Passed |
| CTest: existing asset/viewer suites plus scripts, Lua 4, startup and startup loading | 21/21 suites passed in Release |
| Embedded scripts and scheduling | 67 synthetic checks plus 66 original checks passed in Release and fully instrumented AddressSanitizer; exported identity, shared records, first-match shadowing, independent native barriers and aggregate budgets |
| Original embedded scripts | dng101b1 member 1 exports 55 supported script records; func/boot/start/first references and byte sizes match the recovered chains |
| Native Lua 4 | 286 synthetic checks plus 10 original checks passed in Release and fully instrumented AddressSanitizer; typed stack/call/closure/table/vararg behavior, malformed chunks, invalid operands, poison-on-failure and cumulative limits |
| Boot host/resolver/runtime fixtures | 69 checks passed in Release and fully instrumented AddressSanitizer; prior host/resolver checks plus actual string/table/function/number globals after source destruction, move construction/assignment, stable report identity, failed-state rejection, one-attempt execution and resolution after moves |
| Startup loading/routing fixtures | 59 checks passed in Release and fully instrumented AddressSanitizer; bounded callback reads, stage/FIFO order, source-qualified aliases, common Lua chunks not rerun, exact scene rules, valid reference zero, malformed/null branches, cumulative camera/name/export limits, failed-prefix counters and independent loading/routing success flags |
| Original owned startup resources | Zero/one/three profiles retain 96/9/11 files and 14,535,397 / 4,006,731 / 4,747,791 serialized bytes; every document equals its direct AFS payload, including repeated occurrences across profiles |
| Independent original info routing | All 1,617 qualified routing tuples, including the separate late-scene list, match an independent raw-IGB oracle across three profiles; final deferred counts 583/436/446 and held-scene counts 104/17/19; every ordered source/stage/ordinal/location/size also matches |
| Retained original boot runtime | All 17 original helper globals survive source/chunk destruction and runtime moves; the actual production Host path still matches official Lua 4.0.1 for all 188 calls/arguments and 110 queue entries, with unchanged instruction counts |
| Packaged startup CLI | All original ordered queue and qualified Lua/held/deferred entries match the independent oracle for zero/one/three profiles; actual retained Lua state reported after loading |
| Version 0.11 app lifecycle | 32 isolated native checks passed: successful owned-session counts, invalid input/package/unresolved-resource replacement, clear/remount, fresh-run recovery and byte-identical original 236-batch animated-player framebuffer before/after success, failure and result-view rendering |
| Version 0.11 startup UI | Packaged finite 1280×720, 1920×1080 and 3840×2160 runs passed; Resources, Loaded files, Deferred and Late views inspected at 720p; difficulty/camera variants, missing/unresolved resources, slot-1-only state, empty startup and all nine pages return expected results |
| Independent original execution | Native VM and unmodified official Lua 4.0.1 match all 188 ordered host calls/arguments and all 110 total queue entries across zero/one/three-player input profiles |
| Original zero-player boot | func: 35 instructions / 0 calls; boot: 505 instructions / 105 calls; 94 distinct requests, all catalog-resolved |
| Original one-player boot | Slot 0 Agumon with sword01/gun01/nil: boot 310 instructions / 34 calls / 7 requests, all catalog-resolved |
| Original three-player boot | Agumon/Vmon/Dorumon with shared weapons: boot 432 instructions / 49 calls / 9 requests, including original q_ substitution and exact dedup; all resolved |
| Boot CLI syntax | 8 table-driven test methods cover explicit state, malformed/range/duplicate selectors, nil weapon configuration, fixed USA getters, missing values and mixed app sources; no graphics/data required |
| Version 0.10 boot app lifecycle | 26 ignored native checks passed: mixed-source guards, input order, zero/one queues, success replaced by failed rerun, recovery, remount/missing mount and exact existing animated-scene pixels before/after boot success/failure; 9 additional tool CLI cases passed |
| Version 0.10 packaged startup UI | Original zero/one/three-player queues match all reference entries; finite 1280×720, 1920×1080 and 3840×2160 runs passed and screenshots were inspected; difficulty/camera variants, empty startup without data, missing/unresolved resources and slot-1-only state return the expected results |
| Version 0.10 viewer regression | All nine pages and the existing textured, animated player scene passed finite runs with the final packaged executable |
| Python ISO verifier tests | 6 synthetic tests passed |
| Native CLI end-to-end tests | 8 synthetic tests passed |
| Catalog/package CLI arguments | 5 tests passed, including missing/duplicate/conflicting selectors, empty paths, signed/partial/overflow indices and index-zero handling |
| Native app CLI without graphics | 11 test methods with table-driven subcases passed; explicit player selectors, duplicates, ranges, source conflicts, finite clip time and playback prerequisites; runs in Windows app CI |
| Player placement fixtures | 66 synthetic checks passed in Release and fully instrumented AddressSanitizer; exported identity, exact groups/slots, affine rotation/scale/reflection, immutable source, invalid links, overflow and aggregate serialized-name budgets |
| Original entry matrices | Both groups and all four slots in ndmw entry 179 decoded; all 128 matrix components compare bit-for-bit with eight original stored matrices |
| Player visual fixtures | 118 synthetic checks passed in Release and fully instrumented AddressSanitizer; model instance/ONE/LOD/actor/appearance/skin provenance, malformed or ambiguous ownership, attachments, modifiers, visibility and allocation guards |
| Original player visual | 12 additional checks passed in Release and AddressSanitizer: Agumon's three explicit branches and fourth model instance select the expected original geometry |
| Dynamic scene GPU and focus | Release and AddressSanitizer passed isolated actor-batch updates, every static pixel unchanged, UV/sampler retention, occlusion, exact reset, MSAA, stable normalization, source-bounds focus and invalid-input state preservation |
| App lifecycle integration | 37 ignored local harness checks passed: original shadow Solid to rejected Textured to Solid restores exact pixels; failed clip index/time/post-decode placement retains prior clip/time/vertices/pixels; member changes remove player; invalid resource leaves 235 static batches |
| Combined original map and player | BG02 plus high Agumon: 236 textured batches, six images, 31,886 static vertices plus 1,371 player vertices; full entry matrix replaces authored model-node transform |
| Original player native variants | Package and direct-AFS sources; high/low appearance, Solid shadow, both entry groups, slots 0 and 3, waitS/runD and finite playback; unsupported shadow material and invalid resource/model/point/group/branch/clip/time fail explicitly |
| Compact playback and 4K | 1280×720 and 3840×2160 host windows render combined scenes; actual targets 650×243 at MSAA 0 and 3210×1683 at MSAA 8; elevated inspection camera shows group 0, while nearby geometry can obscure group 1 |
| Version 0.9 startup research | Original file-list manager dispatch order func/boot, then start, then first traced; start calls funcPlayerSet; embedded filename lookup and outer ONE-model matrix replacement documented; script execution was added in version 0.10 as recorded above |
| Package synthetic fixtures | 109 checks passed in Release and AddressSanitizer: export/member order, catalog identity, exact `/s` handling, malformed input, forged metadata, NUL-padded repeated names, boundary compatibility and allocation limits |
| Original package integration | 8 checks passed in Release and AddressSanitizer; 97 descriptors / 195 members, including 98 preliminary `/s` members; both dng101b1 members match their AFS bytes exactly |
| Named map export identity | `dng101b1.igb` -> ndmw entry 80 -> member 1 `[0.179]` -> explicit export 7 / scene index 1 / info 3372 / BG02 root 2877 |
| Original switch command evidence | `first.lua` submits `SWITCHCTRL / ALLON`; original dispatcher, controller registration callback and target-mask effect traced; controller 3471 is explicit export 9 and targets root 2877 |
| Switch-controller and scene fixtures | 127 combined scene checks pass in Release and AddressSanitizer, including all-on/off, cross-word and nested masks, unchanged bytes, wrong/exported targets, malformed state and cumulative child-validation budget |
| Original native command replay | ALLON produces 235 instances / 26,352 triangles; ALLOF is complete and empty; every instance path, geometry reference and world transform agrees with explicit child-zero inspection |
| Package/control UI regression | 36 finite native checks rerun on version 0.9 passed: descriptor-only inspection, original commands, source/selector guards, invalid controller/target, empty/no-scene results, all eight pages, font, raw scene, Agumon animation and manual rig |
| Final compact scene controls | 1280×720 screenshot inspected after tab-fitting adjustment: Instances, Details, State and Status visible; ALLON, ALLOF and Stored selection fit |
| Named package at 1080p and 4K | All 235 textured batches / 5 images retained; 1290×603 and 3210×1683 scene targets inside 1920×1080 and 3840×2160 host windows, actual 8-sample MSAA |
| USA DOL identity | SHA-1 matches `e409a88a7379ed1a536f93b0a303a0ce7cd5d877` |
| ISO versus extracted data | Boot header, DOL, filesystem table and all four filesystem files match |
| Native boot/DOL inspection | `GDJEB2`, revision byte 0, 10 loaded sections, entry `0x80005240` |
| Native archive index | 1,382 `area.afs` entries; 973 `ndmw.afs` entries |
| Native IGB metadata integration | 2,274 inputs, zero failures (details in IGB_FORMAT.md) |
| Native IGB object integration | All 2,274 inputs and their object lists passed (details in IGB_OBJECTS.md) |
| Logical resource catalog | 2,080 paths, 2,079 available; archive counts match; 20 logical/direct reads identical |
| Native texture integration | 25,906 base images decoded, zero failures: 22,254 indexed8/RGBA8 and 3,652 BC1 |
| Independent BC1 sample | Original card payload decoded by Pillow's DDS reader matches native RGBA byte-for-byte |
| Texture viewer | Original font atlas and card rendered on the native GPU; screenshots inspected |
| Logical texture loading | `--resource bectext.igb` resolves and displays `ndmw.afs[79]` |
| Invalid explicit texture source | Missing resource reports an error and exits with code 3 in a finite smoke run |
| Native mesh corpus integration | 198,068 geometry instances decoded; 3,793,429 emitted triangles; zero unsupported profiles or failures |
| Original UV corpus | All 198,068 geometries checked: 46,988 valid UV streams, 151,080 absent; zero size or non-finite failures |
| UV wrapping evidence | 48,360 UV pairs on 3,825 geometries lie outside [0,1]; source coordinates are preserved |
| Stored base-texture binding corpus | 3,208 supported bindings; all have valid UVs and decodable bound images; remaining material profiles explicitly unsupported |
| Mesh synthetic fixtures | Triangle lists, grouped strips, winding, degenerates, counts, references, finite values and allocation bounds passed |
| Original topology identification | DOL enum table confirms triangle list 3 and triangle strip 4; all source index/length constraints checked |
| Portable orbit camera | Projection/frustum, orbit, pan, zoom bounds, reset and invalid-input tests passed |
| Native GPU renderer test | Pixel coverage, orbit, wireframe, resize, MSAA resolve, textured colors/UVs/wrapping/filtering/alpha, state restoration and cleanup passed |
| GPU pose updates | Position changes retain texture, UVs, indices, normalization and frame counter; reset reproduces pixels, invalid updates preserve the prior pose |
| Joint overlay projection | Uses the same fixed normalization/camera; fitted center, clipping and unchanged reference projection passed |
| Original skin reference mapping | Agumon geometry 5665 -> selector 5481 -> skin 404 -> database 810 -> skeleton 701; 25 bones, 20 joints, 24 palette entries |
| Original CPU manual pose | All three Agumon geometries reset with zero measured position error; bone 8 Z+30 degrees changes only vertices mapped to its joint descendants |
| Original rig corpus | All 894 `ndmw.afs` IGBs screened: 476 geometries with skin buffers, 290 supported with 222,694 vertices; no supported reset/manual-pose failures |
| Rig reset accuracy | Maximum coordinate error 1.97209e-15 across supported corpus; remaining 186 skinned profiles explicitly unsupported |
| Skinning synthetic fixtures | 153 checks passed, including full helper ancestry, original quaternion signs/norm, noncommuting child rotation, reset and malformed graph/matrix/palette/cache rejection |
| Native manual Agumon pose | Textured high geometry, bone 8 (`LArm01`) Z+45 degrees: 252 changed vertices, maximum displacement 4.47705; inverse-bind reset changes zero vertices |
| Original animation graph and poses | All 82 Agumon clips decoded and evaluated at zero, quarter, three-quarter and endpoint times: 8,200 bone transforms with complete mesh/joint outputs |
| Independent pose composition | 328 high-detail Agumon poses / 449,688 vertices compared with separate float32 composition using the same resolved channels; maximum vertex component difference 2.38419e-6, below 1e-5 |
| Reference codec compatibility | 55,950 quaternion/translation tuples over every half-sample interval and endpoint match the pinned enbrip implementation at nine significant digits; separate original engine interpolation is documented in ENBAYA.md |
| Original interpolation math | 49 synthetic checks; 83,400 quaternion comparisons against a bounded scalar interpreter of the original DOL routine match exactly on MSVC; the comparison does not emulate floating exception/status flags |
| Animation graph fixtures | 50 synthetic checks cover binding identity/ambiguity, masks including bits 31/32, duration, unsupported channels, owned data and original pose guards |
| Bounded Enbaya fixtures | 201 checks passed in Release and AddressSanitizer, covering packed widths, state changes, malformed/truncated streams, allocation limits and both sampling paths |
| Native clip inspection | Original textured waitS/runD at zero and quarter time produce distinct inspected frames; 720p controls fit and 1080p uses actual 8-sample MSAA |
| Native live playback | 32 frames upload 33 evaluated poses and cross the runD endpoint with Loop enabled while retaining texture and UVs |
| Animation error handling | Missing/case-mismatched names, out-of-duration time, unsupported rig and missing source exit 3; malformed, non-finite, negative or mixed manual/animation CLI inputs exit 1 |
| Original 3D sample | `ndmw.afs[179]`, reference 3: 24 vertices and 8 triangles rendered |
| Agumon stored-space preview | `_common/p_agumon.igb`, mesh 2, reference 5665: 1,371 vertices and 1,966 triangles; solid and wireframe screenshots inspected |
| Agumon bound-image verification | Image 5609, `P_agumon01.dds`, 256×256 BC1: all 65,536 texels opaque; independent Pillow DDS decode matches native RGBA exactly |
| Agumon textured GPU preview | Original high-detail mesh, 1,371 UV pairs and bound image rendered; yellow skin, green eye, mouth/teeth and white claws visually inspected |
| Low Agumon textured preview | Mesh 0, image 5597 (`P_agumon02.dds`, 64×64), 570 UV pairs rendered |
| Unsupported original material | Shadow mesh 1 in Textured mode reports its incomplete/ambiguous scope and exits 3; its Solid preview still succeeds |
| Mesh MSAA target | 1920×1080 window, 1290×554 textured offscreen mesh target with rig inspection and actual 8 samples |
| Compact rig layout | 1280×720 window: scrollable left Rig tab, visible manual controls and complete posed model in a 636×211 viewport |
| Manual rig source routing | Logical resource and direct AFS entry both work; low-detail default, high-detail textured/wireframe and combined head-axis deltas render successfully |
| Explicit rig failures | Helper bone 0, out-of-range bone 25, unsupported box and missing resource exit 3; malformed angles and CLI combinations exit 1 |
| Seven UI pages | Finite native smoke runs passed, including the existing texture CLI |
| Textured source routing | Both logical resource and direct AFS entry select the verified original image; missing source exits 3 |
| Invalid explicit mesh source | Missing resource exits 3; `--mesh` without a source exits 1 |
| Native BNR decode | 96×32 RGB5A3 banner displayed correctly |
| Initial five UI pages | Version 0.1 hidden rendering tests passed; overview, graphics and archive screenshots inspected |
| 4K/MSAA context | 3840×2160 framebuffer, actual 8 samples, VSync Off reported by native graphics page |
| Start without game data | Passed; data can be selected later |
| Static scene fixtures | 80 active checks passed in Release and AddressSanitizer: transforms, DAG instances, ordered geometry emission, masks, explicit inspection, malformed data, incomplete diagnostics and limits |
| Instance-path materials | 96 synthetic checks passed in Release and AddressSanitizer, including independently inherited bind/enable scopes and full-path texture-modifier rejection |
| Original BG02 stored state | `ndmw.afs[179]`, scene index 6 / info 3372 / root 2877: stored root mask is zero, so default traversal remains empty |
| Original BG02 explicit inspection | Root child 0: 462 node visits, 219 transforms, 235 instances from 29 geometry attributes; 31,886 placed vertices and 26,352 triangles; complete static traversal |
| Independent scene placement | Separate raw-graph traversal and original instruction arithmetic match all 235 owner/geometry/path tuples; matrix component error 0 and maximum world-position component error 9.5367431640625e-7 |
| Original scene texture coverage | All 235 instances have verified path bindings and correctly sized UVs; 5 unique images decode to 528,384 RGBA bytes |
| Native scene GPU | Shared placement, differently textured batches, opaque occlusion independent of draw order, texture sharing, one MSAA target, state restoration and malformed/budget rejection passed in Release and AddressSanitizer |
| Native BG02 scene | Solid, Wireframe and Textured finite renders passed; 720p controls fit and the 1080p scene target uses actual 8-sample MSAA |
| Native scene at 4K | 3840×2160 host window, 3210×1683 textured scene target, actual 8-sample MSAA, all 235 batches and 5 images retained |
| Eight-page and viewer regression | All eight pages, original font, manual Agumon pose and textured runD inspection passed finite native runs |
| Scene error handling | Empty stored selection, invalid root/child, non-switch override, missing source and unsupported dynamic branch exit 3; invalid/mixed CLI options exit 1 |

Run `scripts/build.ps1 -Package` to reproduce the Windows build and CTest.
The additional synthetic checks are:

```powershell
python -m unittest discover -s tests -p 'test_*.py' -v
python tests/cli_tests.py build/Release/dw4_inspect.exe
python tests/catalog_cli_tests.py build/Release/dw4_catalog.exe
python tests/app_cli_tests.py build/Release/dw4_pc.exe
python tests/boot_cli_tests.py build/Release/dw4_boot.exe build/Release/dw4_pc.exe
```

Example finite rendering check (output is local and ignored by Git):

```powershell
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 3 --page graphics --screenshot work/graphics.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource bectext.igb --screenshot work/font.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --archive area.afs --entry 621 --screenshot work/card.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource _common/p_agumon.igb --mesh 2 --screenshot work/agumon.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --screenshot work/agumon-textured.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --rig --bone 8 --rotate-z 45 --screenshot work/agumon-manual-pose.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --animation runD --animation-time 0.2 --screenshot work/agumon-animation.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --archive ndmw.afs --entry 179 --scene-root 6 --scene-inspect-child 0 --scene-mode textured --screenshot work/bg02-scene.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --package dng101b1.igb --package-member 1 --scene-root 1 --scene-switch-control 3471 --scene-switch-command all-on --scene-mode textured --screenshot work/bg02-package.bmp
.\build\Release\dw4_renderer_tests.exe
```

`--smoke-test` deliberately renders finite hidden frames and skips normal frame
limiting. It is not a benchmark or proof of 144 FPS game simulation. The 4K
check used a local settings file requesting 3840×2160 and MSAA 8x. Borderless
desktop mode was implemented but not exercised in the hidden-window test.

No physical controller was connected during validation; Xbox, PlayStation and
GameCube hardware behavior remains unverified. Gameplay scene activation, actor and
animation-combiner behavior, gameplay, audio, saves, online co-op and modding remain
unimplemented. Original clip inspection includes helper transforms; skinning
still requires single influences and identity selector transforms. See
[SKINNING.md](SKINNING.md) and [ANIMATIONS.md](ANIMATIONS.md).
Static scene inspection now reconstructs a bounded export/subtree profile with
original placements and explicit root selection. The inspected BG02 switch is
stored disabled. An explicit original ALLON command can now enable it through
its exported controller, alongside the separate manual-child inspection option.
The original func/boot script order now executes in a bounded native VM and
builds a resolved resource queue. Version 0.11 retains that live VM, loads the
common payloads and reconstructs the original info-routing order. The original
loading-screen gate, active manager callbacks and later layer/text initialization
remain unimplemented. Nested masks and source data remain unchanged. See
[STARTUP_LOADING.md](STARTUP_LOADING.md) and [STARTUP_VIEWER.md](STARTUP_VIEWER.md).
Explicit player visual placement now replaces the verified outer model matrix
and joins original single-clip playback to the scene. It does not execute player
generation, choose startup state, compose weapons/shadows or simulate movement.
The host inspection camera can be occluded by map geometry and does not reproduce
the original camera. See [PLAYER_VIEWER.md](PLAYER_VIEWER.md),
[PLAYER_PLACEMENT_RESEARCH.md](PLAYER_PLACEMENT_RESEARCH.md) and
[STARTUP_RESEARCH.md](STARTUP_RESEARCH.md).
See [PACKAGES.md](PACKAGES.md) and [SCENE_ACTIVATION.md](SCENE_ACTIVATION.md).
The scene renderer applies base
images with inspection shading; original lighting, render passes, culling and
transparent-surface ordering are still outstanding. See [SCENES.md](SCENES.md),
[SCENE_RESEARCH.md](SCENE_RESEARCH.md) and [SCENE_RENDERER.md](SCENE_RENDERER.md).
The mesh viewer displays stored or posed positions with inspection shading or a
verified base-texture binding. Full original materials and comparison against
recorded gameplay frames remain outstanding. The 3,208 supported bindings are a limited profile, not a claim that all
198,068 meshes have fully supported materials. See [MATERIALS.md](MATERIALS.md)
and the corpus diagnostics in [MATERIAL_RESEARCH.md](MATERIAL_RESEARCH.md).
Base texture decoding does not establish final appearance with the original renderer.
No exhaustive visual comparison of the 25,906 image instances was performed.
See [TEXTURES.md](TEXTURES.md) for layout constraints and sample observations.
Big-endian IGB metadata and documents were tested synthetically; all recognized
local game IGB metadata was little-endian. The original data remains outside
the repository.

GitHub Actions independently builds/tests the portable core on Windows/Linux
and builds/packages the Windows app. The workflow does not require game files
and does not perform an original-data or physical-GPU test. The renderer GPU
test is built there but is run explicitly on a machine with an OpenGL driver.
