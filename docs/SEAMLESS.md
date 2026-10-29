# Seamless online co-op

Design of phase 2 of the online / PC feature package (round 51). Base: main
f915905 (round 48: save file, presence, achievements, randomizer, the DW4Online
server). main has moved on since (round 49, presentation only); round 50 runs.
The design rests on the groundwork journal
(`work/notes/online_understand_journal.jsonl`), the round 48 journal
(`work/notes/r48feat_journal.jsonl`) and four spikes of this round, none of them
merged:

| Spike | Where | What it proved |
| --- | --- | --- |
| cameras | branch `spike-r51cam` (5a16cda, b20a5d6), worktree `DW4_PC-wt/r51cam` | per-slot box cameras, one union-of-frusta simulation pass plus a picture-only pass; one camera = the original bit for bit |
| netcode | branch `spike-r51net` (dc769b7), worktree `DW4_PC-wt/r51net` | late join into a running lockstep session by snapshot + replay, mid-stage spawn, two missing snapshot fields |
| story | branch `spike-r51story` (43c69cf), worktree `DW4_PC-wt/r51story` | flag census, merge monotonicity on real saves, WinHTTP SPKI pinning |
| stage flow | worktree `DW4_PC-wt/r51flow` (uncommitted `work/notes/r51flow/scan_flow.py`, `build/spike`) | script scan of exits, demos, mats; leash and despawn measurements |

Numbers are measured in those spikes (Ryzen 7 7800X3D, hidden runs, other
rounds running in parallel) unless they are marked as an estimate. Nothing in
this document is built on main yet. The round plan is in section 17, the
questions for the user in section 18.

## 0. Summary

- **Instances.** A room on the server holds up to ten members. Every member is
  always in exactly one *instance*: a lockstep group of one to four players
  that simulates one stage, identically on every member's machine. Members in
  the same field, dungeon or boss stage share one instance (enemies, drops,
  EXP), up to four, then a parallel instance opens. Hub and UI stages (lobby,
  briefing room, Digi-Lab, title, save room, call shop) are solo instances;
  members there see each other as name tags (section 3.3, question 1).
- **Cameras.** Every member has its own box camera: the original 802F2BC0 logic
  centred on that member's player. Once per step one simulation traversal
  culls against the union of all members' cameras, so whatever any member
  sees behaves as in the original and whatever nobody sees freezes as in the
  original. Each machine then draws its own camera's picture without changing
  any state. No leash: `limit_Circle` is switched off while an instance has
  two or more members.
- **Free travel.** Exits are per player. The original exit prompt goes to the
  member who stepped on the mat; on *Yes* the living members on the mat leave
  together and the others stay: the instance *forks* at that frame (each side
  despawns the other side's players, which costs nothing because every
  machine has the same state). If every living member is on the mat, the
  whole instance moves as in the original. Gate disc, story triggers, boss
  starts and *give up* fork the same way. What concerns only the instance
  (game over, quest aborts, field talks) keeps the original behaviour.
- **Transport.** All traffic goes through the server's relay (protocol DW4R2,
  one TCP connection per client and room). A one-member instance runs on its
  member's own clock (no input delay) and streams its inputs to the server.
  With two or more members the server *seals* every tick (all pads plus
  events) and every machine executes only sealed ticks. The server orders
  membership events, holds the stage snapshot and the input log, and serves
  late joiners, reconnects and desync repairs from them. It never simulates.
- **Mid-stage join** = stage snapshot + replay of the log (4,100-5,400 steps per
  second measured), as an observer, then a JOIN at a future tick. The joiner
  appears at its own entrance point with SQ_ENTR; `first.lua` never runs again.
- **Room story** = the room owner's online save. The server keeps one
  `online_story` row per account that only the room service writes. Members
  send story deltas (persistent flags, key items, two counters) that merge by
  fixed rules; an instance applies the current room story at its next stage
  entry (between the NEXTSTAGE step and the load step), never mid-stage.
- **Client.** Account and room UI in ImGui (title overlay and the F1 Online
  tab), the online character as a virtual memory card, an upload whenever the
  save room writes, cloud backup in F1 Saves, online achievements, name tags
  for remote members, Discord party size.
- **Online off** changes nothing: every new path hangs off `Engine::online()`
  (default off), the camera code with an empty camera mask is today's code,
  `Engine::fingerprint` is untouched. Proof as always: `det.sh` 4004f056 /
  audio 396cb219, the story runs, the save state suite, the Dolphin
  comparisons.
- **Plan.** Five rounds. The first (proposed r52) gives two players in a room
  over a local relay, with their own cameras and free travel.

## 1. Decisions

### 1.1 Fixed by the user (not reopened)

- Seamless co-op on the user's own server. Rooms with a name, an optional
  password, up to ten players and a room list.
- Every player travels freely and makes progress in their own area. Players in
  the same stage play real co-op (shared enemies and drops) in one stage
  instance of at most four; a room has as many instances as needed.
- No shared camera and no leash, also in the same stage.
- Story progress by any player counts for the room's story, which is the
  host's (room owner's) online save.
- All game traffic goes through the user's server (relay); nobody forwards a
  port.
- The online save is a separate online character per account (starts from
  zero, stored on the server); one cloud backup of the local save per account.
- The client talks to the hard-coded address 88.198.52.221 (not changeable; a
  loopback override only in development builds) with a pinned self-signed
  certificate for now, later `https://dekirai.crygod.de/dw4online`.
- Name, level, HP and MP above each player's Digimon.
- Online: no cheats, no rebalancing, no save states; the randomizer is a host
  room setting. Discord application ID 1560921673354453093.
- With online off the original game stays exactly as it is.

### 1.2 Defaults this design chooses

Each is explained in its section; the ones that change the design noticeably
and that the decisions above do not settle are questions in section 18.

| Default | Section |
| --- | --- |
| Hub and UI stages are solo instances with presence (question 1) | 3.3 |
| One player per machine online (question 5) | 3.1 |
| Instance slots stay stable within a stage; slot 0 is refilled at a stage load | 3.6 |
| A one-member instance is led by its member (no input delay); from two members the server seals ticks | 6.4 |
| Boss and story mats keep the original gather rule plus a *Go without them* choice | 5 |
| Status window: owner-only freeze and drawing (from round S3; the original instance-wide freeze before) | 5 |
| A lagging member is filled (last pad, then neutral) instead of stalling the others | 6.4 |
| Story while the owner is away: server setting, r48 behaviour (drop) until the user decides (question 2) | 8.6 |
| The online character is uploaded when the save room writes, as in the original (question 3) | 9 |
| Fast loading is a room setting; difficulty and randomizer are room settings | 10 |
| The relay stays plain TCP with one-time HMAC tickets | 6.2 |
| The server's achievement catalogue adopts the client's 72 ids | 11 |
| The direct `--host` / `--join` sessions stay as a LAN and test tool; F1 Online shows only rooms | 10 |
| No server-side arbiter engine, no UDP for now | 6, 19 |

## 2. Terms

| Term | Meaning |
| --- | --- |
| room | server object: name, password, 2-10 members, owner, settings (difficulty, randomizer, fast loading). Room slots 0-9 are not instance slots |
| member | an account in a room; one machine, one player |
| instance | a lockstep group of 1-4 members simulating one stage; server id plus a *visit* number that counts its stage loads |
| instance slot | the engine player slot 0-3 (`GameSystem::players`) a member plays in that instance |
| present | a slot whose member is in the instance (joined and spawned); the others are empty or despawned |
| leader | the lowest present slot of an instance |
| tick | one entry of an instance's input stream; tick k drives the k-th engine step after the instance's stage-load snapshot (except while parked at a barrier, 6.7) |
| solo-led | an instance with one member: the member produces the ticks on its own clock and streams them to the server |
| sealed | an instance with two or more members: the server fixes each tick (all pads and events) and every machine executes only sealed ticks |
| barrier | the point after a step that executed NEXTSTAGE: the instance parks until the server's decision (NEW, JOIN, DISSOLVE) and the room story arrive |
| fork (split) | one instance becomes two at a frame decided inside the simulation; each side despawns the other side's players |
| join | a member enters a running instance by snapshot + replay, then a JOIN event spawns it |
| hub / UI stage | stagML02, stagML11-14 and every stagIS* (title, character select, save room, call shop, staff roll and the other menu stages) |
| online rules | `Engine::online()`: the switches that turn the online-only behaviour on; all off = the original game |

## 3. Instance model

### 3.1 Room and members

The room is the r48 room (`server/DW4Online/src/DW4Online.Domain/Services/RoomService.cs`):
name 1-32 characters, optional password, 2-10 players, one room per owner,
compatible protocol and data hash only. Its settings: difficulty (at most what
the owner's online story unlocked, flags 711 and 77), randomizer seed and
modes (the host's seed for everyone), fast loading. Cheats and rebalancing are
refused (exists).

One player per machine: each member is one account with one online character
and plays the machine's first pad and keyboard. A second local player on the
same machine would need a second login and character (question 5).

### 3.2 Instances

An instance is the unit of real co-op: up to four members simulate one stage
in lockstep. The engine's hard limit of four (players[4], four RAMs per species
package, four points per point group, 4-bit mat masks) is why instances are
capped at four. Every member is in exactly one instance at any time; solo play
is an instance of one.

An instance lives on the server as: id, stage, visit, members with their
instance slots, mode (solo-led or sealed), parent (the instance it forked from),
flags (`departing` while a forked group walks to its exit, `boss_lock`,
`joinable` as reported by the members, 6.5), the stage snapshot and the tick
log since that snapshot. It ends when its last member leaves.

Two members in the same field, dungeon or boss stage are in the same instance
unless it is full, locked or too old to join cheaply (3.4). Instances of the
same stage never merge; members meet when one of them enters the stage again.

### 3.3 Hub and UI stages are solo

Hub and UI stages (2) are always solo instances, never joined:

- Nearly everything in them is a menu program that freezes the whole engine
  (SUBWND GENERATE freezes MODELCTRL, GENERATER and SOUND and pauses the
  messenger, `src/engine/subwnd.cpp:1042-1059`) or pulls the group along (the
  event console to stagML02, the call shop, the Digi-Lab scene). Solo, every
  shop, talk, quest board and console behaves exactly as in single player.
- There are no enemies and no drops to share.
- Lobby instances would live longest of all; a join replays everything since
  the stage load (6.6).
- The save room and character select touch the memory card, which is part of
  the simulation (`src/engine/save_menu.cpp:320-349, 686-741`); solo, each
  machine has its own card and needs no shared instance card.

Members in the same hub stage see each other through *presence* (12): a name
tag with level, HP and MP at the other member's position, updated about five
times per second, drawn outside the simulation. Ghost models are a possible
later addition (question 1). Co-op starts at the area gate: whoever picks the
same field enters the same instance.

If the user prefers shared lobbies (question 1), hub stages join like fields
and the scoped window work of round S3 grows to cover every lobby program; the
save room and the call shop stay solo either way.

### 3.4 Entering a stage

Every stage entry of an instance passes a barrier (6.7). The server decides
for the group that arrives (all members of the instance that executed the
NEXTSTAGE):

1. **DISSOLVE** if the target is a hub or UI stage: every member continues as
   its own solo instance (each machine despawns the others and moves its own
   player to slot 0 before the load step). A game over (everyone to stagML11
   point 0) dissolves this way.
2. **JOIN** if an instance of the target stage has free places for the whole
   group, is joinable, not boss-locked, and its log is shorter than
   `Instances:MaxJoinLogMinutes` (default 30 minutes; catch-up then stays under
   about 40 s on the reference machine, 6.6). Among several, the one with the
   most members wins, then the shortest log. Each arriving member drops its own
   engine (keeping its player slot data aside), catches up and is spawned by
   one JOIN event at its entrance point.
3. **NEW** otherwise: the group's own engine continues into the stage and
   becomes a new instance (slot 0 refilled, 3.6). Its snapshot at the load step
   is the instance's base.

If the server does not answer within five seconds (connection lost), a solo
group continues as NEW without a story update and is not offered to joiners
until it reconnects (it uploads its snapshot and log then); a larger group keeps waiting (its members cannot run
unsealed ticks).

### 3.5 Leaving an instance

- **Through an exit, gate disc, story trigger or boss start**: a fork inside the
  simulation (5, 6.7). The leaving side becomes a `departing` child instance
  until its own barrier.
- **Leaving the room, closing the game, a kick, a timeout**: the server stamps
  LEAVE for the slot (6.5); every machine despawns that player at the first
  membership-safe step.
- **A disconnect**: the slot is filled for up to ten seconds for a reconnect,
  then LEAVE (6.10).

A despawned slot is gone for good in that instance (record kept aside first,
then PLAYER REMOVE as the original pit scripts do it, then its HUD panels,
quick panel, item label line, capsule and weapon entries, `joined = false`,
`bit_chips[slot]` cleared, 7). Nothing of the original's "released slot that
stands around" remains online: NINZU, EXP shares, MATisAllPlayer, cameras and
game over then count only the remaining members, automatically.

### 3.6 Slots

- A joiner takes the lowest free slot. Slots stay stable while the instance
  stays in its stage, gaps allowed (as in local co-op with pads 1 and 3).
- At a stage load (NEW), slot 0 must be occupied: if it is free, the lowest
  present slot moves to slot 0 (its player slot data, bit capsule and HUD
  colour). The server authors the slot map in the barrier record and every
  member applies it, so no machine has to guess. `boot.lua` therefore never
  sees slot 0 unjoined (it would load all 16 species packages,
  `dng101b1_m1__boot.lua` function_00B04190).
- Within a stage, slot 0 can be absent after a fork. Everything that reads pad 0
  or player 1 then needs a rule (7.4): SelectCtrl windows of an absent or
  leaving slot read the leader's pad (game over `i_messky 0`, Trailmon
  `i_messcr 0`, info windows; `src/engine/ui.cpp:538`), the film skip reads the
  leader's pad, the soft reset is off online. `make_record` writes the shared
  flags only into slot 0's record, which online does not matter (the server
  ignores story bytes of uploads, 8.4). The card folder reads of the lobby
  start scripts use players[0] (`managers_core.cpp:3453-3470`); in a solo hub
  instance that is the member itself.

## 4. Cameras and simulation

### 4.1 One box camera per member

Today `CameraManager` has one box state for the whole stage (centre, box, blend,
saved and live matrices; `include/dw4/engine/managers.hpp:208-222`) and
`update_box` (`src/engine/managers_core.cpp:373-504`, 802F2BC0) centres it on the
average of all generated players and writes the shared scene nodes.

Online with two or more members, every present slot gets a `PlayerBox` with the
state of 802F2BC0 (centre, moved flag, box, previous box, blend, CAMHOKAN timer,
saved eye / target, live pivot / eye / target, shake offsets). Each runs the
original logic with the centre at its own player, which is the original's
one-player case exactly. Only the shared camera writes the shared scene nodes.
Shared by the whole instance: the demo viewer (SETDEMOCAMERA), the SHAKE pattern
(applied per slot), SETBOXCAMERA (resets every slot), the camera mode (aaaCamera
or aaaCameraL, one per instance, the room's). Stages without box data fall back
to the authored scene camera path; the fallback without viewer follows the
slot's own player. The original has no camera collision and no zoom, so there
is nothing else to carry. The C-stick drives the quick panel cursor in the
original (`src/engine/panel.cpp:380-386`) and stays so; a free mouse camera is
not part of online play (it would have to travel as input).

When an instance goes from one member to two, the existing member's slot takes
over the shared state (centre, box, blend, saved matrices, timer), so its
picture does not cut; the newcomer starts with the first-run rule of
SETBOXCAMERA (direct cut). The camera mask is simulation state and changes only
at the step where a JOIN, LEAVE or fork applies, identically on every machine.

### 4.2 Simulation pass and render pass

The scene traversal is part of the simulation (`docs/ARCHITECTURE.md`, the
paragraphs on `Engine::render` and the cull, lines 141-179): node animation clocks, actor combiners, LOD choices, transform
recorders and ParticleTimers advance in it, and the frustum cull of the game
camera skips subtrees with their updates. With several cameras the engine splits
the work:

- **Simulation pass, once per step**: one traversal culled against the union of
  all members' frusta (a camera mask per node; tests stop for a subtree as soon
  as one camera has it completely inside). Values that depend on one view take
  the first member camera (transform recorders, billboards, beResetNode) or the
  nearest camera (igLod's hysteresis). Every machine computes every member's
  camera from the lockstep inputs, so the union and its results are the same
  everywhere. Shadow casters are traversed under each member's light (8031C1FC
  with that member alone: position + 200 up); water surfaces run while any
  member's frustum holds their box.
- **Render pass, every present**: the local member's camera only, with
  `simulate = false` (every node on its peek path: no clock, combiner, LOD cache
  or recorder changes), the widescreen margin through `peek_pose`, the local
  shadow light. Smooth motion captures and blends with the local camera, the
  r49 rule "the picture's cull also keeps what the previous step's camera saw"
  uses the local camera's previous view, and the overhead labels project with
  it.

Semantics in a multi-member instance: an actor animates as soon as any member
sees it; a recorder carries the rounding noise of the first member's camera
(it changes when the first member leaves); LOD follows the nearest camera. All
of it is deterministic. It has no Dolphin reference (the original never had two
cameras); the solo case is the reference.

Rejected: simulating with the shared centre camera and drawing per player
(enemies freeze visibly outside the centre camera, steering uses the wrong
camera); culling with the local camera (every machine would simulate something
else: desync); one full traversal per camera (order-dependent LOD and recorder
results, 15-25 % slower). No culling at all in multi-member instances is a valid
fallback (measured cheaper with four cameras) but changes the original
behaviour for unseen actors (dng101b1's fingerprint changes); it stays the plan
B if the cheaper test of 4.5 is not enough.

### 4.3 Per-slot consumers

| Consumer | Today | Multi-member instance |
| --- | --- | --- |
| steering direction (camera yaw + stick, `model_ctrl.cpp:1236-1262`) | frame camera | the slot's own camera |
| item name label (ITNAMEFONT line = owner, `items.cpp:221-248`) | frame camera | the owner's camera; other owners' lines are hidden in the local picture (draw filter) |
| water surfaces (`water.cpp:122-160`) | frustum of the frame camera | union |
| shadow light (`shadow.cpp:94-104`) | player average + 200 | union in the simulation, local in the picture |
| test bots (`game_main.cpp:1655-1662, 1808-1815`, `campaign.cpp:1618-1621`) | frame camera | `frame_camera(.., slot)` |
| overhead labels, smooth motion, widescreen widening | picture camera | local camera |

The HUD at the bottom (four status panels) stays shared, as in local co-op.

### 4.4 Solo and offline

A solo instance and every offline or direct (`--host` / `--join`) game run with
the camera mask 0: today's code path verbatim. The spike also shows that the
generalised path with one camera is the original: det 4004f056 / 396cb219 for
the union code with one camera and for a slot's own camera with one player, also
with pictures (1920x1009, widescreen), and slot 0's own camera equal to the
shared one in all 1,902 frames of the check. An instance that drops back to one
member keeps the mask `{its slot}` until its next stage load (equal to the
original by the same proof); a new solo instance starts with 0.

### 4.5 Cost

CPU traversal time per step (µs), simulation + picture:

| Stage | today (one combined pass) | 1 camera split | 4 cameras | 4 cameras, no cull |
| --- | --- | --- | --- | --- |
| stag0102 (field) | 207-210 | 100-107 + 138-140 | 284-292 + 148 | 180 + 151 |
| dng101b1 (dungeon) | 178 | 82-94 + 113-120 | 216-232 + 109 | 97 |
| stag0201 | 188 | 88 + 130 | 222 + 125 | 128 |
| dng205b1 | 171 | 93 + 120 | 216 + 126 | 124 |
| stagBS02 (boss) | 111 | 56 + 80 | 80 + 78 | 64 |

Each extra camera adds about 45-65 µs per step, almost all of it frustum tests
(four identical cameras cost as much as four different ones). Worst case with
four cameras about 0.44 ms per 60 Hz step, about 2.6 % of a core. With smooth
motion the picture pass runs at every present, the simulation pass once per
step (stag0102 at 144 presents/s: today 30 ms/s, four cameras 39 ms/s).

Round S4 adds a cheaper conservative test for the extra cameras (a
precomputed view-projection, sphere against planes), built strictly without
floating point contraction like `frustum_cull_code`; the exact original test
stays for solo instances.

### 4.6 No leash

The leash is `beChangePosTransform` (`src/engine/change_pos.cpp:24-58`), which
follows the player centre only while every player is within 40 units, and the
HitLand ring `limit_Circle` under it, which never stops a single player
(`src/game/test_path.hpp:32-48`). Measured: a standing partner holds the other
player at about 92 units (M1, `r51flow/build/spike`); with the ring off, four
players spread to 316 units. 83 of 89 stage packages have the ring.

Online: the step where an instance first has two members switches the ring's
child hit off (`HitLandManager::set_child_hit("limit_Circle", false)`, as
`WithoutLimitCircle` does for tests); it stays off until the next stage load,
where it is on again exactly when the instance starts with one member.
ChangePos itself is unchanged. Whether anything besides the ring hangs under
the ChangePos transform (or whether enemies query the ring) is unchecked; round
S1 checks it with a solo run with the ring off against the reference.

### 4.7 Demo cameras

A demo viewer is instance-wide, as is PADINPUT. The stage flow rules (5) fork
non-participants away before story and boss scenes, so an instance-wide scene
only ever shows to the members who took part. A later refinement can keep far
away members on their own box camera during a scene; the simulation stays
deterministic because every camera comes from lockstep data.

## 5. Stage flow in an instance

The scan of the 7,168 decompiled scripts (96 stages; `scan_flow.py`) decides
every case. `funcStageChange`, `funcStageIn`, `funcPlayerSet` and
`funcPlayerStatusGenerate` are byte-identical in all 96 `func.lua`.

| Case | Original | Online, multi-member instance | Mechanism | Round |
| --- | --- | --- | --- | --- |
| Exit mat with `funcStageChange` (195 calls: 183 matz, 7 mate, 2 matf, 2 matg, 1 mats; all guarded by MATisAllPlayer and `DMgetCount()==0`) | all living players on the mat; prompt for MATgetPlayerNo; PLAYER / ENEMY -1 OFCTRL; fade; everyone goes | the mat fires for any living member on it; the prompt goes to MATgetPlayerNo as before but only that member's player is frozen (enemies run on); on *Yes* the living members on the mat leave together and the instance forks; all living on the mat = the original group move; the dead go with the leavers only if no living member stays | C++ port of `funcStageChange` (`stag0101_m1__func.lua:107-206`) registered over the global after `func.lua` loads (the VM does not compile Lua text, `docs/LUA4.md`); `NETsplit` | S1 |
| Exit with a direct NEXTSTAGE (20 matz: quest dungeons, stag0203) | MATisAllPlayer | same subset rule | class table (5.1) | S3 |
| Destination of an exit | arguments of the mat script | unchanged; the randomizer's zone remap still applies in STAGECTL NEXTSTAGE | none | - |
| Gate disc (quick panel, `gatedisc.lua` in 96 stages) | SYSsetGateStage, SQ_EXIT for all, everyone to stagML11 point 3; closes a running status window even if it is another player's | the user forks off alone first, then the original script runs in the user's copy; the others play on and keep their windows | wrapper at `run_script` | S1 |
| A member is defeated | capsule at the spot, RAISE after 10 updates | unchanged | none | - |
| *Give up* (new, dead member only) | - | the dead member forks off alone; in that copy nobody lives, so the original `end_g_000bcaps.lua` runs (game over, lobby point 0, capsule kept as BKCHIP) | port intent (6.5) + fork | S3 |
| Every member dead | game over, everyone to stagML11 point 0 | unchanged; the barrier dissolves the group into solo lobbies | DISSOLVE | S1 |
| Quest abort when one player falls (dngq3*, dngq4*, stag0103, stag0203 hit_p_*), quest timer end | whole group | unchanged (whole instance) | none | - |
| Story trigger by one player (MATisPlayer: stagGF03 math00 Leomon; event-like hit_g; Trailmon `rid_g_dtrailmon` with MOVETRANS 0-3) | one player starts it, everyone is teleported and taken along | participants = the trigger plus the members on the same mat or hit; the others fork off before the script's first command and still have the trigger ahead in their copy | class table + wrapper | S3 |
| Story and boss mats with MATisAllPlayer (the 20 boss arena matg with MOVETRANS 0-3, story mats) | everyone gathers | gather as in the original; a member standing on the mat while others are elsewhere sees *Go without them*: the members on the mat proceed, the rest fork off and have the scene or boss ahead | port intent re-runs the mat script, wrapper forks | S3 |
| Boss stages stagBS01-06 | whole arena | entered through exits (subset rule); boss-locked from the stage start to the boss's defeat: late arrivals get a parallel instance | join lock | S3 |
| Boss end (ded_e_*: MOVETRANS 0-3, doors, flags) | everyone | only participants are still there; joins open again | none | - |
| Stage entry scene (`first.lua`; 52 of them set a demo camera such as 'entry') | at the stage start | runs when the instance starts; a joiner never reruns it and appears with its own SQ_ENTR at its entrance | join path | S1 |
| Field talks (hit_n_*), recorder map, messages | whole group frozen, HUD off, PADINPUT off | unchanged (rare in fields); joins wait until it ends | none | - |
| Status window (START, SUBWND) | the engine freezes, one window at a time, drawn for all | S1-S2: original. From S3: no global freeze online, only the owner's player off control; the window, prompts and fades of that member are drawn only on the owner's machine (filter on draw items, the traversal stays the same); one window per instance at a time; another member's window no longer blocks exits (DMgetCount) | scoped SUBWND + draw filter | S3 |
| Shops, bank, Digi-Lab, card keeper, quest board, event console, NPC talks in the lobby | lobby programs | solo hub instance: single player behaviour | DISSOLVE | S1 |
| Save room stagIS12 | SAVECTRL; players no longer joined leave the group | solo; writes the virtual card; uploads the own record (9); *leave the group* hidden online | wrapper | S2 |
| Call shop stagIS13 (LOADCTRL2 without title) | adds or replaces players | off online: a port hint instead (every member is its own machine) | wrapper | S3 |
| Pits (stag0102 mati00, dng205b3 matt23-25, dng206f2, dng310f3, dng412b4, dng103f2, dngq4b2*) | ADDHP -99999 + PLAYER REMOVE; respawn at a table position once every joined player fell and no capsule lives | each fallen member respawns alone at the table position with SQ_ENTR | rule in the pit wrapper | S3 |
| NINZU branches (53 scripts) | player count when the script runs | count of present members when the script runs (the original meaning). Puzzles that fixed NINZU at setup can stall when a member leaves after the setup; entering the stage again resets them | none | - |
| Puzzle mats (MATisAllPlayer for switches and doors) | everyone on the mat | unchanged: co-op puzzles; members elsewhere come over or leave the stage | none | - |
| EXP, drops, item pickup | all joined living share (scaled by the count), the label owner picks up | unchanged | none | - |
| Session variables (quest flags 409, 376-381, 384-389, 424, 571-575, folder 11, COUNTER 0 and 2-6, BKCHIP, GATESTAGE) | engine-wide | belong to the engine copy: a fork keeps them on both sides, a joiner adopts the instance's | none | - |
| Slot 0 duties (pad 0 windows, film skip) | pad 0 | an absent or leaving slot reads the leader's pad | pad redirect | S1 |
| Soft reset, pause menu, save states, cheats, rebalancing | - | off online (save states and pause already are) | online rules | S1 |

The temporary flags travel with the engine line, which is an inherent result of
seamless play: a member who pulled the lever in stag0301 (flag 56) and then joins
a friend's stag0302 instance adopts that instance's state (56 = 0). The docs
say so; nothing tries to merge them.

### 5.1 Script classes

A port data file (`data/online/script_classes.txt`, generated by a tool from the
scan and checked in) lists every script with a class: `exit`, `exit_direct`,
`boss_mat`, `story_mat`, `story_trigger`, `warp`, `pit`, `gatedisc`,
`callshop`, `saveroom`, `talk`, `puzzle`. Online only:

- MATisAllPlayer keeps the original meaning for `puzzle` and every unlisted
  mat; for `exit` it is "a living member is on the mat" (the port prompt
  decides who goes); for `boss_mat` and `story_mat` it is the original plus a
  *Go without them* intent.
- `run_script` consults the class before the first command: `gatedisc`,
  `story_trigger`, `warp` and confirmed `boss_mat` / `story_mat` runs fork the
  non-participants away first, then run the original script on the participant
  side and skip it on the other side.
- `NETsplit(mask)` returns whether this copy continues (7.3); it is the only
  place where the local slot influences the simulation, and only at a fork.
- The `funcStageChange` port keeps the original's structure (its demo records
  and their per-update checks, the prompt colour per slot, the wait for the
  models of defeated players); it differs only in who is frozen, who answers
  and who leaves.

## 6. Transport and lockstep groups

### 6.1 Topology

The server is the hub: every client keeps one TCP connection to the relay of
its room, across all its instances. The r48 relay (a byte splice per instance
channel with the instance host's PC as the lockstep centre, `RelayServer.cs`)
is replaced: it made guest-to-guest latency cross four links, ended the
instance when its host left (`EndInstanceLocked`, `RoomService.cs:486-510`), and
knew nothing to help a joiner, a reconnect or a desync. A peer-to-peer mesh is
rejected (NAT, IP disclosure, against "all traffic through the server").

The server orders and stores; all members simulate as equals. It never runs
the game.

### 6.2 Connection

- `POST /rooms` and `POST /rooms/{code}/join` additionally return a room-scoped
  relay ticket (HMAC, one-time, 60 s; role `member`); `POST /rooms/{code}/ticket`
  issues a new one for a reconnect.
- The client connects to the hard-coded server address (88.198.52.221, or
  loopback in a development build) and the relay port from `GET /status`
  (default 52041). It never takes a host name from the server.
- First line `DW4R2 <ticket> <protocol> <dataHash>\n`, answer
  `OK <memberId> <serverTimeMs>\n` or `ERR <code>\n`; then frames of type (u8)
  + length (u32 LE) + payload, the netplay framing. Frames above 1 MiB are
  refused; state transfers come in chunks.
- The live relay connection counts as the room heartbeat; the REST heartbeat
  is needed only while it is down.
- Plain TCP as in r48. Inputs are not secret; tickets are one-time and
  short-lived. TLS on the relay would need SChannel on a raw socket and is not
  planned.

### 6.3 Messages

Client to server (`inst` = instance id, `visit` = its stage load count):

| Message | Fields | When |
| --- | --- | --- |
| INPUT | inst, visit, tick u32, pad (21 B: connected, 4 f32 sticks, u32 buttons) | sealed instance: every step, `d` ticks ahead |
| LOG | inst, visit, first tick, count, pads | solo-led instance: the pads it applied, batched every 100 ms |
| BARRIER | inst, visit, tick, stage, point, alive and present masks | after a step that executed NEXTSTAGE |
| SPLIT | inst, visit, tick, leaver mask | after the step of a fork (verification and bookkeeping) |
| SNAPSHOT / SNAPHASH | inst, visit, chunked bytes / hash | after the load step: the leader uploads, the others send the hash |
| READY | inst, visit, tick, net fingerprint, carry (player slot bytes, bit capsule, record) | a joiner that caught up |
| JOIN_AT | inst, visit, tick | the leader of a solo-led instance answering JOIN_REQ |
| NETFP | inst, visit, tick, hash | every 60 ticks, every member |
| STATE | inst, visit, joinable, membership-safe, boss lock, reason | on change, debounced to one second |
| INTENT | inst, visit, kind (go_without, give_up), argument | a member's port choice |
| STORY | inst, visit, base revision, cumulative delta since the barrier | at every barrier and every 10 s |
| PRESENCE | stage, inst, position, yaw, species, level, HP, max HP, MP, max MP | five times per second while in a stage |
| RESUME | inst, visit, slot, last executed tick | after a reconnect |
| PONG / BYE | nonce, client time / reason | |

Server to client:

| Message | Fields | When |
| --- | --- | --- |
| SEALED | inst, visit, tick, present mask, pads, events | every tick of a sealed instance (batched when behind) |
| LIVE | the leader's LOG batches or the SEALED ticks | to observers (joiners catching up) |
| JOIN_REQ | inst, visit, slot | to the leader of a solo-led instance: choose J |
| BARRIER_OK | inst, visit, decision (NEW, JOIN, DISSOLVE), new inst and visit, slot map, story revision and bytes, rules | answer to BARRIER |
| STATE_DATA | inst, visit, slot, snapshot, log, events (chunked) | join, resync, resume |
| RESYNC | inst, visit | the member's fingerprint lost the vote |
| PRESENCE | member, the fields above | others in the same stage outside the own instance; a one-per-second roster for the room UI |
| ROOM | member joined or left, owner present, story frozen, story revision | in-band room changes |
| PING | nonce, server time, input margin, suggested delay | once per second |
| KICK / CLOSE | reason | |

Events inside SEALED (and inside a solo leader's LOG stream for its own JOIN):
JOIN (slot, carry, entrance point), LEAVE (slot), INTENT (slot, kind, argument),
LEAD_FROM (tick: from here the remaining member leads alone), DELAY (slot, d).

The lanes may refine field layouts; the integration step writes the final
layout into an appendix of this document and into
`server/DW4Online/README.md`.

### 6.4 Ticks, sealing, delay

**Solo-led** (one member). The member steps on its own 60 Hz clock with input
delay 0, exactly like offline play, and streams the pads it applied (LOG). A
network hiccup does not stall it. Hub instances are not logged at all (nobody
joins them); field instances are, so that a joiner can replay them.

**Sealed** (two or more members). Every member sends its pad for tick
`t + d_i` while executing tick `t`. The server seals tick `t` when every
present member's pad for `t` is there and sends SEALED; machines execute only
sealed ticks, including their own slot's pad as sealed. If a member's pad for
`t` is overdue by more than `Sealing:StallMs` (default 150 ms on the server's
60 Hz schedule), the server seals with that member's last pad (at most six
ticks), then neutral, and marks the member as lagging. Nobody waits more than
the stall time for anyone; the lagging member sees its inputs dropped and
catches up in simulation (steps run far faster than real time without
pictures). Clients nudge their pacing (about ±2 %) to stay a few ticks behind
the sealed horizon.

**Delay** per client: `d_i = ceil((own one-way to the server + largest one-way
of the other members + jitter p95) / 16.7 ms)`, bounded 2..8, recomputed from
the server's one-per-second pings and the margins it reports. Inputs are
indexed by tick, so every machine may use its own `d_i` (`game_main.cpp:1504-1507`
already allows it). Estimated `d` = 2-4 inside Europe, +1-2 on WLAN. Measured on
loopback with a delay proxy: throughput R ≈ 1000·d / one-way latency for a
waiting lockstep (30 ms one-way: d=3 gives 100.8 steps/s; 50 ms: d=3 60.6, d=4
80.7); jitter acts like extra latency (30 ms + 0-20 ms behaved like 41 ms).

**1 → 2 members.** The server sends JOIN_REQ to the solo leader; the leader
answers JOIN_AT(J) with `J = current tick + d`, switches to delayed sampling at
once and repeats its last pad for the ticks before `J` it has no sample for
(logged, so deterministic). From J the instance is sealed.

**2 → 1.** When a LEAVE or fork leaves one member, the server sends
LEAD_FROM(L): the member executes the sealed ticks before L and leads alone
from L, resampling with delay 0.

Wire size today: 35 B per player and step (about 2.1 KB/s per peer direction).
A compact bundle (buttons u16 + four int8 sticks, about 29 B for four players)
is optional once every PadSample is shown to come from int8 values.

### 6.5 Events and when they apply

The server stamps JOIN, LEAVE and INTENT at a future tick J (sealed: the
largest member horizon + a margin; solo-led: the leader's JOIN_AT). They apply
before the first step at or after J in which the instance allows them, a
deterministic test every machine evaluates the same way:

- **membership-safe**: pad input enabled, no demo record running, no SUBWND,
  not loading, presenting, no fork pending. LEAVE and INTENT wait for it.
- **joinable**: membership-safe, not a hub or UI stage, not boss-locked. JOIN
  waits for it.

Until a LEAVE applies, the leaving slot gets neutral pads and counts as
leaving for the pad redirect, so a window waiting for it cannot hang the
instance. Members report `STATE` so that the server offers only instances that
are joinable for a while; a JOIN into an instance that turns unjoinable simply
waits (the joiner sees "Waiting for the scene to end").

### 6.6 Mid-stage join

1. At the barrier the server answers JOIN with a slot and STATE_DATA: the
   snapshot of the instance's stage load, every tick since, and the events.
2. The client keeps its own player data aside (player slot, bit capsule,
   record), restores the snapshot into a fresh engine and replays as an
   observer without pictures, while LIVE keeps the newest ticks coming. A
   loading overlay shows the progress.
3. Within ten ticks of the newest tick it sends READY with its net fingerprint
   (compared with the members') and its carry data.
4. The server stamps JOIN(J) (6.4, 6.5). Every machine, the joiner included,
   applies `online_join_player(slot, carry, entrance point)` at the first
   joinable step at or after J: species and weapon packages loaded as for a
   direct start, PLAYER GENERATE + PLAYER2 GENESTAGE at the slot's point of the
   entrance group (`point_matrix(point, slot)`), SQ_ENTR, HUD panels
   i_stpnl01-05, camera mask and leash update. `first.lua` does not run again.
5. From J the joiner sends pads; the server fills them neutral until they
   arrive.

Measured (spike `r51net`, two and three processes, dng101b1, stag0101,
stagml11): join state 271,387 B after 2,999 steps raw (snapshot 16.4-17.7 KB +
85 B per step + events), replay 4,100-5,400 steps per second, fingerprints
identical after the replay every time (after the snapshot fix of 7.1); 8,970
steps identical after a join, including a common stage change; with three
processes the second joiner's replay contained the first join. zstd level 3: a
four-player log of 20 minutes, human-like, about 364 KB, so a join after 20
minutes moves about 0.37 MB (within the relay's 1 MiB burst). Catch-up time
grows linearly with the instance's time in the stage: after 20 minutes in a
field about 13-36 s on the reference machine, two to three times that on
weaker PCs. Instances older than `Instances:MaxJoinLogMinutes` are not offered
(3.4).

The spike proved spawning only for a joiner of the same species with its
package already loaded; other species (packages mid-stage, the scheduler's
"next frame" registration), the `q_` packages from three players up, the HUD's
SQ_OPEN, the quick panel and the item label line are round S1 work.

### 6.7 Barrier and fork

**Barrier.** After a step that executed NEXTSTAGE (`pending_stage()` not empty;
STAGECTL NEXTSTAGE `managers_core.cpp:837-866`), the client does not execute the
next tick: it parks and sends BARRIER. A sealed instance consumes the sealed
ticks that arrive meanwhile without stepping (all members park at the same
tick, so all consume the same ones); a solo instance simply waits. BARRIER_OK
carries the decision, the slot map and the room story. Every member applies,
before the load step and in this order: the story merge (8.5), the membership
(DISSOLVE: despawn the others and move the own player to slot 0; NEW: the slot
map), the online rules for the new stage. Then the load step runs
(`reset_modules`, `begin_stage_load` and its snapshot). The fade before
NEXTSTAGE has already darkened the screen, so the park (about one round trip)
is not visible. For NEW the leader uploads the snapshot, the others its hash.

**Fork.** Decided inside the simulation by the online overrides (exit, gate
disc, story or boss participants, give up) at step F, so every machine knows the
two sides without a message. Each copy despawns the other side's players at the
end of step F and from F+1 needs only its own side's pads. Every member sends
SPLIT(F, mask); the server checks that they agree (a disagreement is a desync),
keeps the stayers in the instance and moves the leavers into a `departing`
child instance (same stage and visit, never offered for joins) until its
barrier. Ticks the server sealed for the whole group after F are harmless: each
side ignores the other side's pads. Observers and later joiners replay with the
stayer side.

### 6.8 Snapshot and log on the server

Per logged instance, in memory: the stage snapshot (about 17 KB raw, 3.3-4.8 KB
compressed), the ticks since (pads of present slots plus events), compressed in
blocks of 600 ticks, and the members' fingerprints. A new visit replaces them;
the instance's end drops them. A restart of the server ends every room (the r48
sweeper), so nothing is persisted. The leader uploads the snapshot after the
load step; the others send its hash, so a mismatch shows at once. A solo
group that went on without the server (3.4) uploads snapshot and log when it
reconnects.

### 6.9 Fingerprints and desync

Every member sends a *net fingerprint* every 60 ticks: `Engine::fingerprint`
plus the RNG state, rotations and the sound manager's random value. It is a
separate function, so `Engine::fingerprint` (and with it the det reference)
stays unchanged. The server compares per tick:

- A majority (two or more equal) wins. With two members the longer member wins.
- The others get RESYNC: they rebuild by replay from snapshot and log, keep
  their slot, and the server fills their pads meanwhile.
- Three resyncs of one member within ten minutes move that member out of the
  instance into a solo instance with a notice; the server keeps both
  fingerprint lines and the last ticks for diagnosis.
- A fingerprint mismatch at READY (a broken catch-up) restarts the join once,
  then gives up with a NEW instance.

### 6.10 Lag, disconnects, reconnect

- A lagging member (6.4) gets a connection icon on its name tag on the other
  machines; after ten seconds of continuous filling the server stamps LEAVE.
- A closed connection: the slot is filled for `Relay:ReconnectSeconds`
  (default 10), then LEAVE.
- The disconnected client freezes its picture with "Connection lost,
  reconnecting..." (a sealed member cannot run unsealed ticks; a solo-led
  member plays on until its next barrier). It fetches a new ticket and sends
  RESUME with its last executed tick: still in the instance, it gets only the
  missing ticks; already removed, it joins again as a new member with its own
  player data (progress after the disconnect never reached the shared engine,
  so nothing is lost or doubled).
- After the room's member timeout (35 s) the server removes it from the room;
  the client returns to the title with a message. The online character keeps
  its last upload.

### 6.11 Instance host and room owner

There is no instance host any more. The leader is the lowest present slot,
which matters only for the pad redirect and for who uploads the snapshot;
leaving is an ordinary LEAVE. `EndInstanceLocked` on host loss goes away. The
room owner leaving the room keeps the r48 behaviour (the room runs on, the
owner's place stays reserved, the story is frozen or not according to the
owner-away setting, 8.6).

### 6.12 Versions and determinism across machines

- Protocol and snapshot version change with every simulation change (they go
  to 43 with the first round). `GET /status` gives the minimum and current
  protocol; an old client shows "Update required" before the room list.
- The data hash covers archive counts, logical paths and mod file names and
  sizes, not mod contents (`game_main.cpp:465-495`); round S4 adds contents.
- The simulation still calls host libm in places (`model_ctrl.cpp`,
  `lua_host.cpp`, `enbaya.cpp`), and the UCRT picks FMA3 variants at run time.
  Online sessions call `_set_FMA3_enable(0)` before the online engine is created
  and restore the setting when the room is left, so every online machine takes
  the same code path; offline is unchanged. Round S4 checks whether the
  remaining calls need the MSL routines and gives the user a two-machine test
  kit. All spike runs were on one CPU.
- One deterministic engine per process (the system RNG is a static,
  `combat.cpp:36-39`): a client never needs two (a joiner has left its old
  stage). Moving the RNG into the engine stays optional.

## 7. Engine interface

Everything below exists only with `Engine::online().enabled` (or a nonzero
camera mask). The rounds build against these names; the integration of round S1
fixes the final signatures in this section.

### 7.1 Snapshot completeness (also an offline fix)

`serialize_system` (`src/engine/save_state.cpp:141-153`) misses
`GameSystem::direct_start` and `text_call`; a state restored from bytes
(file, network) instead of in-process diverges: main "differs from frame
2001", the spike "identical after loading". Add both, bump `kSnapshotVersion`,
add a test "restore from bytes == in-process restore". The CLI guest of a
`--host --stage` session must set `direct_start` like the host
(`game_main.cpp:657-658` vs 721ff.); the spike desynced at step 720 without it.
Both also repair offline save state files and the direct test sessions; neither
changes `det.sh`.

### 7.2 Online rules

```cpp
// include/dw4/engine/online.hpp
struct OnlineRules {
    bool enabled = false;            // overrides, despawn, pad redirect, scoped windows
    std::uint8_t player_cameras = 0; // bit per slot with its own box camera; 0 = shared camera (original)
    bool leash_off = false;          // limit_Circle child hit off
    bool scoped_windows = false;     // status window owner-only (round S3)
    std::uint8_t present = 0;        // instance members
    std::uint8_t leaving = 0;        // members with a pending LEAVE
};
const OnlineRules& Engine::online() const;
void Engine::set_online(const OnlineRules&);   // only between steps, at a tick every member applies
void Engine::set_view_slot(int slot);          // presentation only: whose camera the picture uses
void Engine::set_split_identity(int slot);     // the local slot for NETsplit (-1 observer = stayer side)
```

`OnlineRules` is part of the StageSnapshot (a joiner restores it) but not of
`Engine::fingerprint`. Defaults are all off.

### 7.3 Membership and forks

```cpp
std::optional<std::string_view> Engine::pending_stage() const;  // read-only: a NEXTSTAGE waits for its load step
bool Engine::membership_safe() const;
bool Engine::joinable() const;
struct PlayerCarry { PlayerSlot slot; BitChip chip; std::array<std::uint8_t, 3316> record; };
PlayerCarry Engine::carry_player(int slot) const;
void Engine::online_join_player(int slot, const PlayerCarry&, int entrance_point);  // before a step
void Engine::online_remove_player(int slot);                                        // before a step
void Engine::online_intent(int slot, IntentKind, int argument);                     // before a step
struct SplitResult { std::uint64_t frame; std::uint8_t leavers; bool local_leaves; };
std::optional<SplitResult> Engine::take_split();   // after a step; the copy's side is already applied
void Engine::apply_slot_map(const std::array<int, 4>&);   // at a barrier only
void Engine::dissolve_to_solo(int own_slot);              // at a barrier only
void Engine::apply_room_story(const RoomStory&);          // at a barrier or at game_started only
std::uint64_t Engine::net_fingerprint() const;
```

Lua host functions registered only online: `NETsplit(mask)` (returns whether
this copy continues), `NETisOnline()`. The `funcStageChange` port and the
`run_script` wrappers (5.1) call into the same functions.

### 7.4 Pad redirect

SelectCtrl reads the pad of its RAM index (`src/engine/ui.cpp:538`); online, an
index whose slot is not present or is leaving reads the leader's pad. Same for
the film skip (`managers_core.cpp:2703-2711`). The soft reset
(`managers_core.cpp:759-834`) is off online.

### 7.5 Observe-only additions

A port event `card_written {slot, card, box}` when WRITE_MEDIA or FILEMAKE
completes (for the upload, 9). Like the existing port events it is collected
only on request and is in neither the fingerprint nor the snapshot.

## 8. Room story

### 8.1 What the story is

In local co-op the story is the shared FLAG / FLAGFOLDER / COUNTER of player 1
(`link_common`, `save_game.cpp:37-43`; `make_record` writes them only into slot
0's record, 80-86). The room story is exactly that, minus the instance-local
part. Everything personal (level, EXP, forms, skills, items, bank, bits, disks,
cards, XDATA, equipment) belongs to the character. In the 3,316-byte record only
BASE_FLAG (1024 B at 792), BASE_FLAGFOLDER (12 B at 1816) and BASE_COUNTER (16 x
i32 at 1828) are story.

### 8.2 Which flags merge, and how

The census (`spikes/r51story/flag_census.py`) writes the exact sets into one data
file shared by C++ and C# (`data/online/story_sets.json`); the round that builds
the merge checks it in with golden vectors.

| Set | Contents | Merge |
| --- | --- | --- |
| Phase | flags 1-6 and 86 (game cleared) with their save room copies 700-705/713 (Normal), 7-12/87 (Hard), 13-18/88 (Very Hard) | the furthest phase wins, rebuilt one-hot (the copies are one-hot too; OR would set several phases) |
| Persistent | every other flag a script sets for good (145 with a constant FLGset(..., 1); 49, 82, 334, 383, 406-408 through computed indexes) and the difficulty copies of the story working set | OR |
| Key items | flag folder 0-10 (K0-K10) | OR |
| Counters | COUNTER 1 (junk shop donations), COUNTER 7 (training attempts) | additive (cumulative delta per instance visit, applied once) |
| Instance-local, never merged | the 404 flags `gameStart.lua` clears, 0, 508, 900-937 (derived at each lobby load), the non-monotone 503, 611, 905, folder 11, COUNTER 0 and 2-6 | - |

After every merge the server sets the copy of the room difficulty to the story
working set and the key copies to K, as the save room does
(`stagIS12_m1__start.lua`); otherwise a later `gameStart.lua` would turn the
story back. Checked against real saves (four campaign checkpoint sets, 32
consecutive pairs): no phase step back, no key lost, no COUNTER 1 decrease, no
record with two phase flags, nothing lost with the phase group as above (without
the copies in the group, 701, 703 and 704 would be lost four times each). A
delta per checkpoint step is 10 B median, 79 B at most.

### 8.3 Deltas

Each machine diffs the persistent shared values of its engine against the
state at the instance's last barrier (read with `find`, never creating keys)
and sends STORY with the cumulative delta at every barrier and every ten
seconds. The server keeps per (instance, visit) the last cumulative delta and
applies only what is new, so any member may send and duplicates do no harm.
This catches FLGset, FLGflip, COUNTERset and the flag folder alike (the
`story_flag` port event covers only FLGset).

### 8.4 Ownership on the server

A table `online_story (account_id, revision, flags 1024 B, folder 12 B,
counters 64 B)` that only the room service writes: the room story is the
owner's row. `GET /online-character` composes the record from the character
and the account's own story; `PUT` ignores the story bytes, for owners and
guests alike, which avoids revision conflicts between story merges and
character uploads. A guest's own story is untouched by playing in someone
else's room; the server's state achievements (clear flags 406-408,
`CharacterSummary.cs:26`) therefore never come from a borrowed story.

Plausibility before a merge: the sender is a member of the room and in an
instance; the phase rises by at most one per delta; each flag only from a stage
that sets it (the census's setter table: 100 of 145 persistent flags have
exactly one setter stage; hand entries for the six computed ones) against the
sender's instance stage; a rate limit.

### 8.5 When the story reaches an engine

- **At the online title start**: when `GameSystem::game_started` is set (before
  `gameStart.lua`), the client replaces flags, flag folder and counters with the
  room story and sets the room difficulty; `gameStart.lua` then clears the
  temporaries as in the original.
- **At every barrier** (6.7): `apply_room_story` merges the barrier's room
  story into the engine with the same rules, touching only keys whose value
  changes (FLGisOn creates zero entries and the fingerprint hashes every key,
  `lua_host.cpp:400`, `engine.cpp:774-781`). It runs before the snapshot, so
  joiners get the same state. Every member of the group applies the same bytes
  at the same tick.
- **Never mid-stage**: the stage is built from the flags by its load scripts
  (621 story flag reads at load time), so a mid-stage change would contradict
  gates and NPC scenes.

### 8.6 Owner away

r48 freezes the story while the owner is not in the room
(`SetStoryFrozenAsync`). Because "progress by any player counts" is a user
decision and the freeze was an r48 default, the server gets a setting
`Rooms:StoryWhileOwnerAway = drop | hold | apply` (drop = r48: deltas are
refused with `story_frozen`; hold = kept and merged when the owner returns;
apply = merged at once). The default stays `drop` until the user decides
(question 2). The members' own engines keep their progress until they leave the
stage either way.

## 9. Online character and cloud backup

- **Virtual card.** Entering a room, the client builds memory card A as a
  single-file card store in `saves/online/<account>/` from `GET
  /online-character` (box 0 = the record, header from the save utility); card B
  is absent. 404 gives an empty card: the original new game flow. The original
  character select runs unchanged. The local save store is put back when the
  room is left (as `end_session` does today).
- **Upload.** On `card_written` for the own slot (in the solo save room that is
  slot 0): `make_record` on a copy of GameSystem (it changes state through
  `store_species`), then `PUT /online-character` with If-Match, play seconds and
  the room context. A 409 (the same account saved from another machine) shows a
  message and reloads; it never overwrites silently. The card file is only a
  cache; the server is the truth.
- **When.** As in the original only when the save room writes (question 3 asks
  whether to upload also at stage changes or when leaving the room). The room
  story does not depend on it (8.3).
- **Cloud backup** of the local save (F1 > Saves, outside rooms only): upload
  with a preview (`summarize_save`) and `X-Sha256`, download with a backup of
  the current file first (`.bak`), restore of the five earlier revisions; the
  endpoints exist (`/cloud-save`, `/cloud-save/meta`, `/cloud-save/restore/{n}`).

## 10. Client: account and room UI

All program UI in English, ImGui, ASCII (account names are ASCII lower case on
the server).

- **Where.** On the title screen a small *Online* panel (bottom right, only
  while the title presents and no session runs); the same screens in the F1
  Online tab, which replaces the direct host / join UI (the `--host` / `--join`
  command line stays for LAN play and tests).
- **Account.** Log in (name, password, remember me), Register (name, password
  twice, optional e-mail; the recovery code is shown once, confirmed with "I
  saved it"), Recover (name, code, new password; a new code is shown), Log out,
  Log out everywhere, Change password.
- **Rooms.** List of compatible rooms (`GET /rooms?protocol&dataHash`: name,
  owner and whether present, players n/max, lock, difficulty, randomizer, story
  frozen), refresh, Create (name, optional password, 2-10 players, difficulty up
  to what the owner's online story unlocked, randomizer seed and modes, fast
  loading), Join (password prompt). In a room: members with their area (from
  presence), Leave room, Kick (owner).
- **Session start.** Joining or creating asks to end the offline game
  ("unsaved progress is lost"), then restarts the engine at the title in online
  mode: virtual card, room settings (cheats and rebalancing off, randomizer and
  fast loading from the room, FMA3 off), online rules on. Leaving the room
  returns to the offline title.
- **Transport.** `src/game/online/`: a WinHTTP worker thread with a queue
  (synchronous requests, the game loop never blocks), HTTPS to
  `88.198.52.221:443` with base path `/api/v1` (later the domain with base path
  `/dw4online/api/v1` and ordinary chain validation, a build constant).
  Pinning: `SECURITY_FLAG_IGNORE_UNKNOWN_CA` plus a SPKI SHA-256 pin checked in
  the SENDING_REQUEST callback, closing the request on a mismatch before any
  byte of the body leaves (verified in the spike: a wrong pin gives 12017 and
  the server logs no request; a right pin 200 in 3.8 ms). Two pins (current and
  reserve) in `src/game/online/server_pins.hpp`. The token is kept with DPAPI
  (user scope, entropy = server + account) in `saves/online/`.
- **Development builds** (`-DDW4_ONLINE_DEV=ON`, never in release builds):
  `DW4_ONLINE_SERVER=127.0.0.1:<port>` and `DW4_ONLINE_PIN=<pin>` for a loopback
  server only. The test stack runs HTTPS with a throwaway certificate, so the
  pinning path is always exercised.
- **Deployment detail.** The self-signed command in `deploy/DEPLOY.md`
  (`New-SelfSignedCertificate ... NonExportable`) makes a new key, hence a new
  pin, on every renewal. DEPLOY.md switches to a key made once with openssl and
  kept, and gets a script that prints the pin the client build needs.

## 11. Achievements

- **One catalogue.** Only 52 of the 72 client ids (`src/game/achievements.cpp`)
  match the server's (`server/DW4Online/sql/0002_achievements.sql`); thresholds
  differ (client 8 h / 4 h, server 6 h / 3 h). The server is not deployed, the
  client's local unlocks exist, so the server adopts the client's ids and
  thresholds (a migration). A checked-in JSON exported by the client is
  compared by a client test and a server test.
- **Online unlocks** count only this machine's player: event unlocks with
  `POST /achievements/unlock` and the room context (ranked rules exist), stats
  every 60 s with `POST /stats` from the existing queue (the r48 token buckets
  start at account creation, so no big first batch). State achievements come
  only from the server reading uploaded characters in ranked rooms.
- **Story achievements** count for every member of the instance in which the
  story flag rose ("was there"): each machine sees the same port event in its
  engine and awards its own player. Members elsewhere in the room do not get
  them.
- Offline unlocks stay local in the F1 Achievements tab (r48 default).

## 12. Name tags and presence

- Members of the own instance are engine players: the existing overhead labels
  draw them from GameSystem (name converted from the full-width PLAYERNAME,
  level, HP and MP bars), projected with the local camera.
- Room members in the same stage but another instance (hub stages, a fifth
  player in a parallel instance) come from PRESENCE packets through
  `OverheadLabels::set_remote_players` (exists): interpolated between packets,
  hidden by the same rules (loads, demos, menus, a defeated player). They are
  presentation only and never touch the scene graph.
- A lagging member's tag shows a connection icon.

## 13. Discord

The existing IPC client gets party fields in a room: `party {id: "dw4-" + a hash
of the room code, size [n, max]}` and the state `Online room (n/max)`, never the
room name. Details stay `<Form> Lv <level>` and the area. No invites or join
secrets for now.

## 14. Server changes

| Area | Change |
| --- | --- |
| Relay | `DW4Online.Relay`: DW4R2 hub replacing the splice: one connection per member, frames, instance groups (solo-led and sealed), sealing with stall deadlines and fill rules, event stamping, snapshot and log store (6.8), state delivery for join / resync / resume, fingerprint vote, presence fan-out, pings and delay hints, per-connection limits (the 64 KB/s bucket with the 1 MiB burst fits a join state) |
| Rooms | `RoomService`: instances as lockstep groups (id, stage, visit, slots, parent, departing, boss lock, joinable), barrier decisions NEW / JOIN / DISSOLVE (3.4), forks, in-band JOIN and LEAVE, no instance end when its first member leaves (`EndInstanceLocked` goes), room-scoped relay tickets, the relay connection as heartbeat. `POST /rooms/{code}/stage` and `/stage/leave` are retired; the room state lists instances with stage and members |
| Story | migration with `online_story`; `StoryMerge` in the domain with the golden vectors; plausibility table from the census; the owner-away setting; `GET /online-character` composes, `PUT` ignores story bytes; `GET /rooms/{code}/story` for the website and debugging |
| Achievements | catalogue migration to the client's ids and thresholds |
| Config | `Sealing:StallMs`, `Relay:ReconnectSeconds`, `Relay:LagLeaveSeconds`, `Instances:MaxJoinLogMinutes`, `Rooms:StoryWhileOwnerAway`; `Play:CurrentProtocol` follows the client |
| Website | the room page shows instances per stage |
| Deploy | fixed-key self-signed certificate and the pin script (10); port 52041 as before |
| Tests | unit tests for sealing, events, forks, barrier decisions and merge vectors; integration tests with scripted fake clients; end-to-end tests with the real game client (16) |

## 15. Online off stays bit-identical

- `OnlineRules` default off; offline games, save states, story runs, the
  campaign tools and the direct `--host` / `--join` sessions never turn it on.
- The camera mask 0 is today's camera and traversal code, unchanged.
- The Lua overrides and wrappers are registered only online.
- Draw filters (item label lines, scoped windows) act only online.
- New port events are observe-only (not in fingerprint or snapshot).
- The net fingerprint is a separate function; `Engine::fingerprint` stays.
- FMA3 is switched off only for online sessions.
- The snapshot version rises (new `OnlineRules` block, the two missing fields);
  as with every bump, older save state files no longer load.
- Proof in every round: `det.sh` twice (4004f056 / audio 396cb219), every ctest
  suite, the save state suite (17/17 plus a test from a file), and in the
  rounds that change engine code the story runs r25c and nc1 frame-equal with
  main. A union test hook with one camera must also give 4004f056.

## 16. Testing

- **Local stack** `tools/online_test/run.py`: starts a private mysqld
  (`C:/xampp/mysql/bin/mysqld.exe` with its own data directory and port, as the
  server's `local-db.ps1`), the DW4Online server on 127.0.0.1 (HTTPS with a
  throwaway certificate, relay on a free port), registers accounts through the
  API, starts N clients built with `DW4_ONLINE_DEV` (hidden: `--smoke-test`,
  `DW4_NO_ERROR_BOX=1`, `DW4_RENDER=1` only where a picture pass is wanted;
  `nice -n 10`) with input scripts, collects every client's net fingerprints per
  60 ticks and its membership log, compares them per instance and checks the
  expected events; it stops only the processes it started.
- **Scenarios** (each a script plus expected log):

| Id | Scenario |
| --- | --- |
| S1 | two members in one stage walk apart (> 200 units), own cameras, enemies on both sides |
| S2 | one member leaves by an exit (fork), the other follows later (join by catch-up), both change stage together |
| S3 | three clients: two joins mid-stage, one at a time, then a common stage change |
| S4 | gate disc while a partner fights |
| S5 | everybody falls: game over, dissolve into solo lobbies |
| S6 | boss mat: gather, and *Go without them* |
| S7 | story trigger by one member (stagGF03 math00) while the other is elsewhere |
| S8 | status window of one member while the other fights (scoped) |
| S9 | pit: one member falls and respawns alone |
| S10 | disconnect and reconnect within ten seconds; and after the timeout |
| S11 | injected desync (test hook) and resync |
| S12 | latency, jitter and loss proxy |
| S13 | room of ten: instances 4 + 4 + 2 plus solo members, 30 minutes |
| S14 | owner-away policies drop / hold / apply |
| S15 | save room upload, 409 from a second machine |

- **For the user**: `scripts/online-dev.ps1` starts the local stack and two
  visible game windows (agents run only the hidden variant).

## 17. Round plan

Each round is one workflow: at most four parallel lanes, then integrate, review
(game and server), fix, as in round 48. Lanes build against sections 6 and 7;
the integration pins the final layouts. Every round ends with the checks of
section 15. Round numbers are proposals (r49 and r50 exist, r51 is this design).

### Round S1 (r52): seamless core, first playable over a local relay

Goal: two or three players in a room over a local relay, each with their own
camera, no leash, free travel between stages with forks and mid-stage joins.
New characters start from the title (nothing is saved online yet).

| Lane | Scope |
| --- | --- |
| cameras-exits | per-slot box cameras, union-cull simulation pass and render pass (spike r51cam brought to production, the r49 previous-view rule for the local camera), camera mask changes at membership steps, leash rule, per-slot consumers and the item label line filter, bots per slot; the `funcStageChange` port and the gate disc wrapper on top of `NETsplit` |
| membership | snapshot completeness and version 43 with the bytes test, the CLI guest fix, `OnlineRules`, `online_join_player` for any species (packages mid-stage), `online_remove_player`, forks and `take_split`, `pending_stage`, barrier application (slot map, dissolve, room story bytes as given), predicates, pad redirect, soft reset off, net fingerprint |
| relay | DW4R2 hub in DW4Online (solo-led and sealed groups, sealing, events, barrier decisions, forks, snapshot and log store, state delivery, presence, pings), RoomService instance groups, room tickets; `tools/online_test` local stack and fake-client tests |
| client | `src/game/online/` WinHTTP worker with pinning and DPAPI token, development login and room create / join from the command line, the DW4R2 client (solo-led and sealed execution, catch-up join, barrier, fork, lag and reconnect overlay basics), presence send and receive into the remote labels, online session start with room settings and FMA3 off |

Done when:
- Online off: `det.sh` twice 4004f056 / 396cb219; ctest all green; save state
  suite 17/17 and a state loaded from a file identical; r25c 10/10 frame-equal
  with main; a one-camera union run gives 4004f056.
- Cameras: four players in stag0102 and dng101b1 give the same fingerprint with
  local camera 0, 2 and 3 and without pictures (hidden windows); the cost table
  of 4.5 re-measured within 10 %; a solo run with the ring off checked against
  the reference (4.6).
- Local stack (private mysqld, DW4Online, relay on loopback, HTTPS with a
  throwaway certificate): S1, S2, S3 and S5 pass with hidden clients, every
  instance's net fingerprints identical every 60 ticks for at least 10,000
  ticks; server unit, integration and end-to-end tests green.
- `scripts/online-dev.ps1` exists, its hidden variant runs, and the README has
  an "Online co-op (preview)" note with the known limits (scenes still pull the
  whole instance, status window freezes everyone, nothing saved online).

Depends on: this design; main with rounds 49 and 50 joined.

### Round S2 (r53): accounts, online character, room story

Goal: the real loop from the title: log in, pick or create a room, play the
online character, save, and see the room story grow from every member's
progress.

| Lane | Scope |
| --- | --- |
| ui | title panel and F1 Online (account screens, room list, create / join, in-room view), version gate, session start and end from the title, Discord party, member areas from presence |
| character | virtual card from the online character, `card_written` and the upload with If-Match and 409 handling, cloud backup in F1 Saves, room difficulty and story at `game_started` |
| story | merge sets and golden vectors (C++ and C#), client diff and STORY messages, real `apply_room_story` at barriers, server `online_story` with composition, plausibility and the owner-away setting |
| achievements-deploy | catalogue alignment and the client's online sync (unlocks, stats, "was there"), room page with instances, DEPLOY.md fixed key and pin script, pin constants |

Done when: on the local stack, driven through the client's own code paths in a
headless test mode: two accounts register, one creates and one joins a room,
both make new characters, play, save in the save room, and `GET
/online-character` returns the saved records; a story flag set by the guest in
its own instance reaches the owner's `online_story` within ten seconds and
appears in another instance at its next barrier (S14 for all three policies);
the golden vectors pass in both languages; online achievements unlock with the
ranked context; S15 passes; the checks of section 15 hold.

Depends on: S1.

### Round S3 (r54): stage flow in shared instances

Goal: every case of the table in section 5 behaves as decided.

| Lane | Scope |
| --- | --- |
| classes | the script class table and its generator, MATisAllPlayer per class, forks before story triggers and warps, boss and story mats with *Go without them*, boss locks, joiners' entry without `first.lua` checked across stage types |
| windows | scoped status window (owner-only freeze, draw filter for its layers, prompts and fades by owner, DMgetCount online, one window at a time), the exit prompt drawn only for its member, save room and call shop rules |
| world | per-member pits, *Give up*, game over and quest rules confirmed, capsules and session variables across forks and joins, EXP and item rules confirmed |
| bots-scenarios | FollowBot and the campaign bot online, scenarios S4-S9, an online two-player story segment (leader and follower) through one area including a dungeon boss |

Done when: S4-S9 pass on the local stack with hidden clients; the two-player
segment reaches the boss's defeat and the area's exit with identical
fingerprints; r25c and nc1 frame-equal with main; the checks of section 15
hold. If the user chose shared lobbies (question 1), the windows lane also
covers the lobby programs and the done-when adds a lobby scenario.

Depends on: S1 (S2 for the save room rules).

### Round S4 (r55): robustness, latency, determinism across machines

| Lane | Scope |
| --- | --- |
| resync | extended net fingerprint, the server's vote, resync by replay, RESUME with missing ticks, LEAVE timeouts, catch-up progress and the long-log fallback |
| latency | adaptive delay, stall deadline tuning, lag indicators, a TCP delay / jitter / loss proxy and a measured matrix; UDP only if the matrix shows stalls TCP cannot avoid |
| determinism | FMA3 and libm audit for online sessions (MSL routines where needed), mod contents in the data hash, version gate, a two-machine test kit and instructions for the user |
| performance | the cheaper conservative frustum test for extra cameras (strict, no contraction), the picture pass with smooth motion at 144 Hz, a server load test with fake clients (200 rooms) |

Done when: S10-S13 pass (the room of ten for 30 minutes without a desync, with
bounded server memory); an injected desync returns to identical fingerprints;
the four-camera simulation pass costs at most 1.6x the one-camera simulation
pass in stag0102 and dng101b1 (measured 2.3-2.9x today), with the same
fingerprint in two runs and with local cameras 0, 2 and 3; the checks of
section 15 hold.

Depends on: S1-S3.

### Round S5 (r56): presence, comfort, release readiness

| Lane | Scope (shaped by the answers in section 18) |
| --- | --- |
| presence | ghost models in hub stages if wanted (question 1), roster polish |
| travel | warp to a friend's stage from the lobby if wanted (question 4) |
| saves | the upload policy chosen in question 3; second local player if wanted (question 5) |
| release | README "Online co-op" rewritten, this document updated to what was built, field test checklist for the user (deploy, pin, firewall), domain switch prepared as a build constant |

Done when: the chosen features have scenarios that pass on the local stack; the
docs describe the built system; the user has a checklist for the first test
over the deployed server (deployment stays the user's step). A buffer round
for what that field test finds is expected.

Depends on: S4 and the answers.

## 18. Questions for the user

1. **Lobby.** Hub and UI stages (lobby, briefing room, Digi-Lab) as solo
   instances where you see the other members only as name tags (later
   possibly as ghost figures), or shared lobby instances of up to four like
   the fields? Shared lobbies mean that shops, talks and the event console of
   one member freeze or pull along the others unless every lobby program gets
   the owner-only treatment, and joins into a lobby instance replay everything
   since its start. Default: solo with name tags.
2. **Story while the owner is away.** Progress the guests make while the room
   owner is not in the room: drop it (as r48 does), keep it and add it when the
   owner returns, or add it at once? Default: drop, switchable on the server.
3. **Saving the online character.** Only when the save room saves, as in the
   original, or also automatically (at stage changes or when leaving the room)?
   Without it, unsaved levels and items are lost on a crash or quit, while the
   story progress is kept anyway. Default: save room only.
4. **Warp to a friend.** Should a member in the lobby be able to jump straight
   into a friend's current field or dungeon (entering its instance at the
   entrance), or only by walking there as in the original? Default: walking.
5. **Two players on one PC online.** Is couch co-op inside an online room
   wanted (a second account logged in on the same PC, its own character)?
   Default: one player per PC online.

## 19. Risks

- **Multi-camera semantics** have no Dolphin reference: an actor animates when
  any member sees it, recorders carry the first member's rounding noise, LOD
  follows the nearest camera. Deterministic and identical to the original for
  one member, but new behaviour for several.
- **Spawning and despawning mid-stage** have no model in the original: other
  species, `q_` packages, HUD, quick panel, item lines, capsules, scripts that
  assume player 1. The spike proved only the same species.
- **Snapshot completeness**: every field that survives a stage load and is not
  serialized breaks joins, reconnects and resyncs (two were found). The bytes
  test and a bump discipline guard it.
- **Catch-up time** grows with an instance's time in the stage (about 13-36 s
  after 20 minutes on the reference machine, two to three times that on weaker
  PCs); the log limit opens a parallel instance instead.
- **Determinism across CPUs** is unproven (all runs on one CPU; host libm, FMA3).
- **Latency over TCP**: losses cause head-of-line stalls; the stall fill keeps
  the others running but drops the lagging member's inputs.
- **Forks** discard the other side's script run; state changes before the fork
  exist on both sides (deliberate, temporary flags travel with the line).
- **Story trust**: a modified client can push story flags into the owner's
  save; plausibility rules limit it, the setter table needs hand entries.
- **Hub stages solo** reads "same stage = real co-op" as "nothing to share in
  the lobby"; question 1 settles it.
- **Cost** of a fourth camera on weak CPUs (about +45-65 µs per extra camera on
  a 7800X3D, mostly frustum tests); the lockstep paces an instance to its
  slowest member only through lag filling, not through stalls.
- **Plain TCP relay**: tickets and inputs travel unencrypted; tickets are
  one-time and short-lived.
- **Scope**: five rounds plus a buffer; the first rounds compete with the
  player-noticeable 1:1 work on main.

## Appendix: evidence

- Cameras: `DW4_PC-wt/r51cam/build/runs` (matrix scripts, `m_*.log`, `ng_*.log`,
  `shot_local2.png`, `shot_local3.png`), det folders `build/det_base`,
  `det_s5*`, `det_s7*`; switches of the spike: `DW4_CAMERAS`, `DW4_CAM_LOCAL`,
  `DW4_CAM_UNION`, `DW4_CAM_NAIVE`, `DW4_CAM_NOLEASH`, `DW4_CAM_CHECK`,
  `DW4_CAM_FULLMASK`, `DW4_CAM_DUP`, `DW4_CAM_NOCULL`. Four cameras: stag0102
  47549cdd with local camera 0, 2, 3 and headless; dng101b1 e5eda7bc; naive
  29f41b86 / 61da996c; dng101b1 without simulation cull fda21c3d.
- Netcode: `DW4_PC-wt/r51net/tools/spike_r51net` (run.sh, run3.sh, lat.sh with
  delayproxy.py, st.sh, sizes.py, measure.c). Runs j3a / j3b (two processes),
  t3a / t3b (three); the state test from a file: main "differs from frame
  2001", spike "identical after loading".
- Story: `DW4_PC-wt/r51story/spikes/r51story` (flag_census.py,
  story_monotone.py, pin_spike.cpp, tls_server.py).
- Stage flow: `DW4_PC-wt/r51flow/work/notes/r51flow/scan_flow.py`,
  `build/spike` (m1_leash, m2_remove: REMOVE deterministic, md5 4f4e23a7 in two
  runs).
