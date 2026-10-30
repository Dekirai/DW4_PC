# Seamless online co-op

Design of phase 2 of the online / PC feature package (round 51), revision 2.
Base: main f915905 (round 48: save file, presence, achievements, randomizer,
the DW4Online server). main has moved on since (4785f3b, round 49,
presentation only) and round 50 runs; round S1 branches from the main of its
day. Line references in this document are to f915905.

The design rests on the groundwork journal
(`work/notes/online_understand_journal.jsonl`), the round 48 journal
(`work/notes/r48feat_journal.jsonl`), four spikes of this round and two
critiques of revision 1, none of them merged:

| Source | Where | What it proved |
| --- | --- | --- |
| cameras spike | branch `spike-r51cam` (5a16cda, b20a5d6), worktree `DW4_PC-wt/r51cam` | per-slot box cameras, one union-of-frusta simulation pass plus a picture-only pass; one camera = the original bit for bit |
| netcode spike | branch `spike-r51net` (dc769b7), worktree `DW4_PC-wt/r51net` | late join into a running lockstep session by snapshot + replay, mid-stage spawn, two missing snapshot fields |
| story spike | branch `spike-r51story` (43c69cf), worktree `DW4_PC-wt/r51story` | flag census, merge monotonicity on real saves, WinHTTP SPKI pinning |
| stage flow spike | worktree `DW4_PC-wt/r51flow` (uncommitted `work/notes/r51flow/scan_flow.py`, `build/spike`) | script scan of exits, demos, mats; leash and despawn measurements |
| netcode critique | branch `crit-r51net`, worktree `DW4_PC-wt/r51critnet` (experiments A-E under `build/exp*`, an uncommitted test patch in `game_main.cpp`) | FMA3 off costs 2.9x; the snapshot holds the audio position; extra renders are idempotent; int16 sticks compress 5x worse |
| player critique | read-only greps of the 7,168 decompiled scripts against revision 1 | quest context, split arrivals, exit prompt owner, dead-only fork sides, scenes that freeze everyone |

Numbers are measured (Ryzen 7 7800X3D, hidden runs, other rounds running in
parallel) unless they are marked as an estimate. Nothing in this document is
built on main yet. The round plan is in section 17, the questions for the user
in section 18.

## 0. Summary

- **Instances.** A room on the server holds up to ten members. Every member is
  always in exactly one *instance*: a lockstep group of one to four players
  that simulates one stage, identically on every member's machine. Members in
  the same field, dungeon or boss stage *with the same quest context* share one
  instance (enemies, drops, EXP), up to four, then a parallel instance opens.
  Hub and UI stages (lobby, briefing room, Digi-Lab, title, save room, call
  shop) are solo instances; members there see each other as name tags (3.3,
  question 1).
- **Arriving together.** Members who reach the same stage within seconds of
  each other start it together: the server holds the first group's barrier for
  up to three seconds when another member is on the way, and spawns the
  late-comer into the new instance before any stage script runs, so both get
  the original group entry. Later arrivals wait up to 15 s on the loading
  screen for the entry scene to end, then join (3.4).
- **Cameras.** Every member has its own box camera: the original 802F2BC0 logic
  centred on that member's player. Once per step one simulation traversal
  culls against the union of all members' cameras; each machine then draws its
  own camera's picture without changing any state. No leash: `limit_Circle` is
  off while an instance has two or more members. A defeated member watches the
  nearest living member (4.1).
- **Free travel.** Exits are per player. The prompt goes to the member who
  stepped on the mat; on *Yes* every other member on the mat is asked too, and
  those who said yes leave together: the instance *forks* at that frame (each
  side despawns the other side's players). If every living member goes, the
  whole instance moves as in the original. Dead members always stay with
  living ones. Gate disc, story triggers, boss starts and *give up* fork the
  same way; scenes that concern one member (talks, map recorders, camera
  tours, the status window) are scoped to that member when the others are far
  away (from round S3, section 5.3).
- **Transport.** All traffic goes through the server's relay (protocol DW4R2,
  one TCP connection per client and room). A one-member instance runs on its
  member's own clock (no input delay) and streams its inputs to the server.
  With two or more members the server *seals* every tick on its own 59.94 Hz
  schedule: a pad that is late is filled at once, so nobody waits for anybody.
  The server orders membership events, holds the stage snapshot and the input
  log, and serves late joiners, reconnects and desync repairs from them. It
  never simulates.
- **Mid-stage join** = stage snapshot + replay of the log as an observer, then a
  JOIN at a future tick. The joiner appears at its own entrance point with
  SQ_ENTR; `first.lua` never runs again. Joins are offered only when the
  joiner's measured replay rate brings it in within 30 s.
- **Room story** = the room owner's online save. The server keeps one
  `online_story` row per account that only the room service writes. Members
  send what their engine has beyond the last confirmed room story; it merges by
  fixed rules and reaches an instance at its next stage entry, never mid-stage.
- **Client.** Account and room UI in ImGui (title overlay and the F1 Online
  tab), the online character as a virtual memory card, an upload whenever the
  save room writes, cloud backup in F1 Saves, online achievements, name tags
  for remote members, Discord party size.
- **Online off** changes nothing: every new path hangs off `Engine::online()`
  (default off), the camera code with an empty camera mask is today's code,
  `Engine::fingerprint` is untouched, the floating point setup is not changed
  (no FMA3 switch). Proof as always: `det.sh` 4004f056 / audio 396cb219, the
  story runs, the save state suite, the Dolphin comparisons.
- **Plan.** Five rounds plus a buffer. The first (proposed r52) gives two or
  three players in a room over a local relay, with their own cameras, free
  travel and arrivals together.

### 0.1 Changes in revision 2

Every blocker and should-fix of the two critiques is decided below; the minor
points are taken over as well. Where an experiment decides a detail, it is the
first task of the round that needs it (17).

| Finding | Decision | Section |
| --- | --- | --- |
| JOIN keyed by the stage name only: quest and story players swap quests in stag0401 | JOIN and co-arrival need the same *session context* (quest flags and counters); personal session values (gate disc return) travel with the member | 3.4, 3.7 |
| Members who arrive seconds apart stay split for the whole stage | co-arrival (HEADING, hold up to 3 s, the late-comer spawned in the load step) and hold-until-joinable (up to 15 s) | 3.4 |
| Exit prompt falls back to slot 0 on a step-off | prompt owner = a living member that stepped on in this update; none = no prompt | 5.2 |
| A fork side with only defeated members gets a game over | side rule: dead members go with living ones; only *Give up* makes a dead-only side | 6.7 |
| FMA3 off makes the simulation 2.9x slower | no FMA3 switch; online needs an FMA3 CPU until round S4 routes the simulation's host libm calls through MSL routines | 6.12 |
| Snapshot bytes hold the audio position | hashes use a canonical form without presentation fields; a mismatch is a desync vote | 6.8 |
| Resume tick after a barrier undefined | barrier tick B, transition steps with the pads of B, barrier applied in the load step, tick 0 = first step after the load step | 6.7 |
| Sealing waits up to 150 ms on p95, the delay rule misses the own return path, loss unmeasured | server-paced sealing at 59.94 Hz with immediate fill; `d_i` from the member's own round trip at p99.9; loss matrix in S1 | 6.4 |
| int16 sticks make join states 5x larger | online pads quantized to the GameCube's stick steps (6 B per player, round S1); state transfers get their own limit | 6.4, 6.6 |
| Catch-up numbers optimistic, RESYNC unbounded | clients report their replay rate; joins and resyncs limited by the expected catch-up time; beyond it a member continues alone; S1 measures the final path | 6.6, 6.9 |
| Console reset (B+X+START) reboots every machine | off online; the local combo opens *Leave room?* | 7.4 |
| Auto-join surprises at puzzle mats and NINZU timers | auto-join stays (user decision); arrival notices, *Waiting for* hints, question 8 (play alone) | 5 |
| Talks, recorders, camera tours freeze everyone | class `local_scene` with the proximity rule (S3); an S1-S2 limit | 5.3 |
| Forks without NEXTSTAGE (boss arenas, Trailmon) undefined | in-stage forks: participants keep the instance; Trailmon does not fork (participants only) | 6.7, 5 |
| A defeated member's camera freezes at the capsule | view of the nearest living member, *Give up* in S1, capsule marker | 4.1, 5 |
| Status window freezes everyone (S1-S2), unsafe from S3 | S1-S2 original plus feedback; S3 proximity rule with a local freeze zone (question 6) | 5.3 |
| EXP to far or idle members | default: only members near the kill share (question 7) | 5 |
| Story made while the owner is away lingers or vanishes | pending set against the confirmed room story; *drop* removes refused values at the next barrier with a notice | 8.3, 8.6 |
| Boss-locked target, boss destinations | *Wait for them / Go alone*; the exit prompt names who stays | 3.4, 5.2 |
| Members on the mat taken without being asked | every other member on the mat gets the prompt too | 5.2 |
| Pits darken every screen | only the fallen member's respawn; its fade only on its machine | 5 |
| Target moves on during a catch-up | the joiner follows the instance id; a barrier in between makes it a co-arrival or a NEW | 6.6 |
| Minor points | env variables, fallback snapshot, MXCSR, fork replay sides and event routing, boss lock in the simulation, net fingerprint contents, idempotence hook, replay without port events, pacing, camera mode as a room setting, `dngq6b01`, pad of the triggering member, preferred slots, story challenge achievements, exit prompt filter in S1, stale line references | 3.1, 3.6, 4.4, 5.1, 6.5-6.12, 7.4, 11 |

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
| Same stage and same quest context = same instance, automatically (question 8 asks about a *play alone* switch) | 3.4, 3.7 |
| Arrivals within 3 s start together; later ones wait up to 15 s for the entry scene | 3.4 |
| Instance slots stay stable within a stage; a member keeps its preferred slot when free; slot 0 is refilled at a stage load | 3.6 |
| A one-member instance is led by its member (no input delay); from two members the server seals ticks on its own clock | 6.4 |
| A late pad is filled at once (last pad, then neutral); nobody waits | 6.4 |
| Exits ask every member on the mat; dead members stay with living ones | 5.2, 6.7 |
| Boss and story mats keep the original gather rule plus a *Go without them* choice | 5 |
| Scenes of one member (status window, talks, recorders, camera tours): original when everyone is near, scoped to that member with a local freeze otherwise, from round S3 (question 6) | 5.3 |
| EXP is shared by the members near the kill (question 7) | 5 |
| Story while the owner is away: server setting, *drop* until the user decides, refused progress removed at the next barrier (question 2) | 8.6 |
| The online character is uploaded when the save room writes and also automatically at stage changes and when leaving the room (question 3, answered) | 9 |
| Difficulty, camera mode, light mode, randomizer and fast loading are room settings | 3.1, 10 |
| Online needs a CPU with FMA3 until round S4 | 6.12 |
| The relay stays plain TCP with one-time HMAC tickets; UDP only if the S1 loss matrix demands it | 6.2, 6.4 |
| The server's achievement catalogue adopts the client's 72 ids | 11 |
| The direct `--host` / `--join` sessions stay as a LAN and test tool; F1 Online shows only rooms | 10 |
| No server-side arbiter engine | 6 |

## 2. Terms

| Term | Meaning |
| --- | --- |
| room | server object: name, password, 2-10 members, owner, settings (difficulty, camera mode, light mode, randomizer, fast loading). Room slots 0-9 are not instance slots |
| member | an account in a room; one machine, one player |
| instance | a lockstep group of 1-4 members simulating one stage; server id plus a *visit* number that counts its stage loads |
| instance slot | the engine player slot 0-3 (`GameSystem::players`) a member plays in that instance |
| present | a slot whose member is in the instance (joined and spawned); the others are empty or despawned |
| leader | the lowest present slot of an instance |
| tick | one entry of an instance's input stream within a visit; tick 0 drives the first step after the visit's load step, tick k the k-th step after it (6.7) |
| barrier tick | the tick whose step executed NEXTSTAGE; the last tick of a visit |
| transition steps | the steps from the barrier tick to the load step (module reload, its held fields, the load step); they consume no ticks (6.7) |
| solo-led | an instance with one member: the member produces the ticks on its own clock and streams them to the server |
| sealed | an instance with two or more members: the server fixes each tick (all pads and events) on its schedule and every machine executes only sealed ticks |
| barrier | the park after the barrier tick until the server's decision (NEW, JOIN, DISSOLVE, co-arrival) and the room story arrive |
| co-arrival | a group that enters a NEW instance together with the base group: spawned in the base's load step, then it restores the base's load snapshot |
| session context | the temporary values that shape a stage's setup beyond the room story (quest flags and counters, 3.7); instances join only with an equal context |
| fork (split) | one instance becomes two at a frame decided inside the simulation; each side despawns the other side's players |
| join | a member enters a running instance by snapshot + replay, then a JOIN event spawns it |
| scoped scene | a scene of one member that pauses and shows only for that member (5.3) |
| hub / UI stage | stagML02, stagML11-14 and every stagIS* (title, character select, save room, call shop, staff roll and the other menu stages) |
| online rules | `Engine::online()`: the switches that turn the online-only behaviour on; all off = the original game |

## 3. Instance model

### 3.1 Room and members

The room is the r48 room (`server/DW4Online/src/DW4Online.Domain/Services/RoomService.cs`):
name 1-32 characters, optional password, 2-10 players, one room per owner,
compatible protocol and data hash only. Its settings: difficulty (at most what
the owner's online story unlocked, flags 711 and 77), camera mode and light
mode (the title options `camera_mode` / `light_mode`, `src/engine/title.cpp:589-593`;
online they come from the room, so every instance of a room uses the same
camera data, aaaCamera or aaaCameraL), randomizer seed and modes (the host's
seed for everyone), fast loading. Cheats and rebalancing are refused (exists).

One player per machine: each member is one account with one online character
and plays the machine's first pad and keyboard. A second local player on the
same machine would need a second login and character (question 5).

### 3.2 Instances

An instance is the unit of real co-op: up to four members simulate one stage
in lockstep. The engine's hard limit of four (players[4], four RAMs per species
package, four points per point group, 4-bit mat masks) is why instances are
capped at four. Every member is in exactly one instance at any time; solo play
is an instance of one.

An instance lives on the server as: id, stage, visit, session context hash,
members with their instance slots, mode (solo-led or sealed), parent (the
instance it forked from, with the fork frame), flags (`departing` while a
forked group walks to its exit; `boss_lock` and `joinable` as the members
report them from the simulation, 6.5), the stage snapshot and the tick log
since that snapshot. It ends when its last member leaves.

Members in the same field, dungeon or boss stage with the same session context
are in the same instance unless it is full, boss-locked or too long to catch up
(3.4). Instances of the same stage never merge; members who end up in two
instances of one stage meet when they enter the next stage together.

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
later addition (question 1). Co-op starts at the area gate: members who leave
the lobby for the same field within a few seconds start it together as
co-arrivals (3.4).

If the user prefers shared lobbies (question 1), hub stages join like fields
and the scoped scene work of round S3 grows to cover every lobby program; the
save room and the call shop stay solo either way.

### 3.4 Entering a stage

Every stage entry of an instance passes a barrier (6.7). The arriving group is
every member of the instance that executed the NEXTSTAGE. The server decides,
in this order:

1. **DISSOLVE** if the target is a hub or UI stage: every member continues as
   its own solo instance (each machine despawns the others and moves its own
   player to slot 0 in the load step, and writes back its personal session
   values, 3.7). A game over (everyone to stagML11 point 0) dissolves this way.
2. **Co-arrival** if another group of the room with the same session context is
   parked at a barrier for the same stage, or announced with HEADING (6.3) that
   it is on its way there, and the groups together have at most four members.
   The group that parked first is the *base* and continues as NEW. The other
   groups' members are spawned by the base engine in its load step, before any
   stage script runs, as joined players (their carries come in the base's
   BARRIER_OK, 6.7); they then drop their own engines (keeping their own load
   snapshot as a fallback, 6.6) and restore the base's load snapshot with an
   empty log. Everybody starts the stage together, entry scene included,
   exactly like a local co-op group. A base group waits at most
   `Instances:CoArrivalHoldMs` (default 3000) for an announced group; its
   loading screen shows "Waiting for <name>...". HEADING is sent when a member
   confirms an exit (5.2), one to two seconds before its barrier, so members
   who take the same gate within about three seconds of each other arrive
   together.
3. **JOIN** if an instance of the target stage with the same session context
   has places for the whole group, is joinable, and the expected catch-up of
   the slowest arriving member (6.6) is at most `Instances:MaxCatchUpSeconds`
   (default 30; S1 confirms it from the measured replay rates). Among several,
   the one with the most members wins, then the shortest catch-up. Each
   arriving member runs its own transition steps and keeps that load snapshot
   as a fallback, drops its engine (keeping its player slot data aside),
   catches up and is spawned by one JOIN event at its entrance point.
   - **Hold until joinable.** A target that is not joinable only for a while
     (its entry scene, `first.lua`, a talk; the STATE reason says which) holds
     the arriving group's barrier until it is joinable, at most
     `Instances:JoinHoldSeconds` (default 30), with "Joining <name>..." on the
     loading screen. Entry scenes last 70 to about 190 frames
     (`funcStageIn`, `stag0101_m1__func.lua`), up to 630 with an enemy intro
     (`stag0201_m1__first.lua`), so friends who arrive a few seconds apart
     meet in the same instance. A hold that runs out ends in NEW with a
     notice.
     *Measured in S1 (real game clients, `tools/online_test`):* a new
     instance is not joinable for its whole stage load plus the entry scene
     (STATE reason `loading` throughout): stag0101 from the lobby gate 1,384
     ticks (23.1 s: about 750 ticks of loading, then the entry until the
     player has control), stag0001 1,294 ticks (G1). With the first default
     of 15 s a member who took the gate 8 s after its friend (just outside
     the co-arrival window) was held until 0.1 s before the target became
     joinable and then started alone (scenario S17b). The default is
     therefore 30 s: it covers the measured 23 s from a NEW plus the longest
     enemy intro (630 frames against stag0101's ~190, 7 s more); a hold never
     starts earlier than about 3 s after the target's NEW, because closer
     arrivals are co-arrivals.
   - **Boss-locked target.** The arriving member chooses on the loading
     screen: *Wait for <names>* (held until the lock clears, then JOIN; can be
     cancelled) or *Go alone* (NEW). The choice travels as BARRIER_CHOICE; the
     engine is parked, so it is no simulation input.
4. **NEW** otherwise: the group's own engine continues into the stage and
   becomes a new instance (slot 0 refilled, 3.6). Its snapshot at the load step
   is the instance's base. A NEW next to an instance of the same stage tells
   the member why ("Kim is on another quest", "Kim's group is full", "Kim has
   been there too long to catch up").

If the server does not answer within five seconds (connection lost), a solo
group continues as NEW without a story update and is not offered to joiners
until it reconnects (it uploads its snapshot and log then); a larger group
keeps waiting (its members cannot run unsealed ticks).

### 3.5 Leaving an instance

- **Through an exit, the gate disc or *give up***: a fork inside the
  simulation (5, 6.7). The leaving side becomes a `departing` child instance
  until its own barrier.
- **Through a boss start with *Go without them* or a story trigger**: an
  in-stage fork; the participants keep the instance, the others become a new
  joinable instance of the same stage (6.7).
- **Leaving the room, closing the game, a kick, a timeout**: the server stamps
  LEAVE for the slot (6.5); every machine despawns that player at the first
  membership-safe step.
- **A disconnect**: the slot is filled for up to ten seconds for a reconnect,
  then LEAVE (6.10).
- **A desync that cannot be repaired in time**: the member continues alone
  (6.9).

A despawned slot is gone for good in that instance (record kept aside first,
then PLAYER REMOVE as the original pit scripts do it, then its HUD panels,
quick panel, item label line, capsule and weapon entries, `joined = false`,
`bit_chips[slot]` cleared, 7). Nothing of the original's "released slot that
stands around" remains online: NINZU, EXP shares, MATisAllPlayer, cameras and
game over then count only the remaining members, automatically.

### 3.6 Slots

- Every member has a *preferred slot*: the slot of its first instance in the
  room. A joiner or a NEW slot map gives it when it is free, otherwise the
  lowest free slot. Slots stay stable while the instance stays in its stage,
  gaps allowed (as in local co-op with pads 1 and 3). So a member keeps its HUD
  panel position (`funcPlayerStatusGenerate`: x = -260 / -110 / 40 / 190 by
  slot), prompt colour and capsule look across stages as far as possible; the
  local player's HUD panel gets a highlight frame (presentation).
- At a stage load (NEW), slot 0 must be occupied: if it is free, the lowest
  present slot moves to slot 0 (its player slot data, bit capsule and HUD
  colour). The server authors the slot map in the barrier record and every
  member applies it, so no machine has to guess. `boot.lua` therefore never
  sees slot 0 unjoined (it would load all 16 species packages,
  `dng101b1_m1__boot.lua` function_00B04190).
- Within a stage, slot 0 can be absent after a fork. Everything that reads pad 0
  or player 1 then needs a rule (7.4): SelectCtrl windows read the pad of the
  member who triggered them, else the leader's (game over `i_messky 0`,
  Trailmon `i_messcr 0`, info windows; `src/engine/ui.cpp:538`); the film skip
  reads the leader's pad; the soft reset and the console reset are off online.
  `make_record` writes the shared flags only into slot 0's record, which online
  does not matter (the server ignores story bytes of uploads, 8.4). The card
  folder reads of the lobby start scripts use players[0]
  (`managers_core.cpp:3453-3470`); in a solo hub instance that is the member
  itself.

### 3.7 Session context

The engine-wide temporary values fall into three classes. A data file
(`data/online/session_values.txt`) lists them; round S1 completes it with a
census of the load-time reads in every stage a quest can reach (the first task
of the membership lane).

| Class | Values (census of round S1) | Online rule |
| --- | --- | --- |
| context (22) | flags 63, 64, 71, 72, 376-381, 384-389, 409, 424, 508, 571; COUNTER 2; flag folder 11 | hashed from the arriving engine at the barrier (BARRIER) and on change (STATE); JOIN and co-arrival only between equal hashes |
| personal (64) | GATESTAGE and its point (read only by `stagML11_m1__start.lua` and `mate02.lua`, set by the gate disc and the quest boards); the 63 flags only hub stages write and no joinable stage reads (lobby talks, shops and the quest boards' 572-575, the lobby's derived 900-937, 24-34, 85, 328, 336, 352, 364, 374, 375, 415-420, 475-480, 594, 611) | kept aside in the member's carry at a join, written back into the member's engine at DISSOLVE, so a gate disc return never leads to a stranger's stage and a lobby talk never comes back from another member's lobby |
| instance (290) | every other temporary flag (the dungeon flags 100-299, lever and gate flags, ...) and COUNTER 0 and 3-6 (quest dungeon timers and counters, written and read only inside dngq1-3 and dngq7) | belong to the engine line: a fork keeps them on both sides, a joiner adopts the instance's (documented, not merged) |

**Census (round S1, measured).** `tools/online/session_census.py` reads the
7,168 decompiled scripts and writes `data/online/session_values.txt` (checked
in; a ctest compares it with the engine's tables in `src/engine/online.cpp`).
It takes the temporary values (the flags `gameStart.lua` clears minus the
story set it restores, the never-merged 0, 503, 508, 611 and 900-937,
COUNTER 0 and 2-6, folder 11, GATESTAGE) and classes them by where they are
written and read: *context* = written by a hub program and read by a joinable
stage (load or run time), or written by a hub program and a joinable stage
and read back by a hub (409, 424, 571, 384, 388: the quest state the lobby's
`start.lua` and `mate01.lua` read on the return); *personal* = GATESTAGE and
every value no joinable stage writes or reads; *instance* = the rest. Against
the starting list: 63 and 64 are new (a lobby talk in stagML12 / stagML13
switches an event of stag0202 / stag0302 at its load); 71, 72 (stag0401's
gate state the quest boards reset) and 508 (the lobby's "came through the
lobby" flag every field's game over reads) are new; 572-575 move to
personal (written and read only by the quest boards); COUNTER 0 moves to
instance (only the quest dungeons that set it read it). COUNTER 2 stays
context: the dungeons' first floors compare it at load (b1 / f1
`start.lua`), and every field's game over script (`end_g_000bcaps.lua`)
reads it to clear the right quest. 64 scripts index the dungeon flags
100-299 by a computed number; all of them are instance values.

**Personal values in the engine.** Online, the engine keeps every present
slot's personal values (in the snapshot's online block): a joiner's and a
co-arrival's come from their carry; SYSsetGateStage writes the engine-wide
GATESTAGE and the personal GATESTAGE of every present slot (after a gate disc
fork that is the leaving side); `carry_player` of the only present member
reads the engine-wide values. DISSOLVE writes the own slot's personal values
back into the engine-wide ones. The context hash treats an absent flag or
counter like 0 (FLGisOn creates zero entries).

Why: stag0401 is both the area-4 story field and the start of every board quest.
`stagML11_m1__quest01..04.lua` set 409 plus 376-379, 571 and 424 and COUNTER 2;
`stag0401_m1__start.lua:128-200` builds gates, blocks and the quest teleporter
from 376-381 and 385-387; `stag0401_m1__first.lua:11-16` skips the enemy intro
with a quest flag; `stagML11_m1__mate00.lua` offers only the quest route while
409 is set. Without the context key a quest player who enters stag0401 point 3
while a story player is there would lose the quest (or carry it into the story
player's lobby). With it they play in separate instances; two players on the
same quest share one.

The temporary flags of the instance class travel with the engine line, which
is an inherent result of seamless play: a member who pulled the lever in
stag0301 (flag 56) and then joins a friend's stag0302 instance adopts that
instance's state (56 = 0). The docs say so; nothing tries to merge them.

## 4. Cameras and simulation

### 4.1 One box camera per member

Today `CameraManager` has one box state for the whole stage (centre, box, blend,
saved and live matrices; `include/dw4/engine/managers.hpp:199-222`) and
`update_box` (`src/engine/managers_core.cpp:373-504`, 802F2BC0) centres it on the
average of all generated players and writes the shared scene nodes.

Online with two or more members, every present slot gets a `PlayerBox` with the
state of 802F2BC0 (centre, moved flag, box, previous box, blend, CAMHOKAN timer,
saved eye / target, live pivot / eye / target, shake offsets). Each runs the
original logic with the centre at its own player, which is the original's
one-player case exactly. Only the shared camera writes the shared scene nodes.
Shared by the whole instance: the demo viewer (SETDEMOCAMERA, except in scoped
scenes, 5.3), the SHAKE pattern (applied per slot), SETBOXCAMERA (resets every
slot). The camera mode and light mode are room settings (3.1), so the camera
data does not depend on who arrived first. Stages without box data fall back to
the authored scene camera path; the fallback without viewer follows the slot's
own player. The original has no camera collision and no zoom, so there is
nothing else to carry. The C-stick drives the quick panel cursor in the
original (`src/engine/panel.cpp:380-386`) and stays so; a free mouse camera is
not part of online play (it would have to travel as input).

When an instance goes from one member to two, the existing member's slot takes
over the shared state (centre, box, blend, saved matrices, timer), so its
picture does not cut; the newcomer starts with the first-run rule of
SETBOXCAMERA (direct cut). The camera mask is simulation state and changes only
at the step where a JOIN, LEAVE or fork applies, identically on every machine.

Measured in round S1 (branch `feat-r52cam`, `CameraManager::set_player_cameras`,
test switch `DW4_TEST_RULES=frame:cameras:present`): one player walking in
stag0102 through a camera box change (box 1 to 2 at frame 885, a 120-frame
blend) with the mask going from 0 to {0} at frame 1000 (inside the blend),
1350 or 2150 gives the same picture camera (view and projection, bit for bit)
in all 2,600 frames as the run without the change, and the same fingerprint.

**A defeated member.** While a slot has no generated player, its `PlayerBox`
stands still at the capsule (`update_player_box` returns, spike r51cam
`managers_core.cpp:399-403`) and keeps the capsule's surroundings simulated
through the union. The defeated member's *picture* uses the camera of the
nearest living member of the instance (`Engine::set_view_slot`, presentation
only) and shows "Defeated - wait for a raise, or hold START to give up"
(*Give up* is round S1 work, 5). The partners see a marker with the name over
the capsule (overhead labels, presentation). A raise needs a partner within 30
units (`combat.cpp:593-605`); without a leash the marker is how they find it.

### 4.2 Simulation pass and render pass

The scene traversal is part of the simulation (`docs/ARCHITECTURE.md`, the
paragraphs on `Engine::render` and the cull, lines 141-179): node animation
clocks, actor combiners, LOD choices, transform recorders and ParticleTimers
advance in it, and the frustum cull of the game camera skips subtrees with
their updates. With several cameras the engine splits the work:

- **Simulation pass, once per step**: one traversal culled against the union of
  all members' frusta (a camera mask per node; tests stop for a subtree as soon
  as one camera has it completely inside). Values that depend on one view take
  the first member camera (transform recorders, billboards, beResetNode) or the
  nearest camera (igLod's hysteresis). Every machine computes every member's
  camera from the lockstep inputs, so the union and its results are the same
  everywhere. Shadow casters are traversed under each member's light (8031C1FC
  with that member alone: position + 200 up); water surfaces run while any
  member's frustum holds their box.
- **Render pass, every present**: the local member's camera (or the viewed
  member's, 4.1) only, with `simulate = false` (every node on its peek path: no
  clock, combiner, LOD cache or recorder changes), the widescreen margin
  through `peek_pose`, the local shadow light. Smooth motion captures and
  blends with the local camera, the r49 rule "the picture's cull also keeps
  what the previous step's camera saw" uses the local camera's previous view,
  and the overhead labels project with it.

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
| steering direction (camera yaw + stick, `model_ctrl.cpp:1253-1262`) | frame camera | the slot's own camera |
| item name label (ITNAMEFONT line = owner, `items.cpp:174-248`) | frame camera | the owner's camera; other owners' lines are hidden in the local picture (draw filter) |
| water surfaces (`water.cpp:122-160`) | frustum of the frame camera | union |
| shadow light (`shadow.cpp:94-104`) | player average + 200 | union in the simulation, local in the picture |
| test bots (`game_main.cpp:1655-1662, 1808-1815`, `campaign.cpp:1618-1621`) | frame camera | `frame_camera(.., slot)` |
| overhead labels, smooth motion, widescreen widening | picture camera | local (or viewed) camera |

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

Round S1 (r52, `feat-r52cam` on main 92b82f8 with round 50): `det.sh`
4004f056 / 396cb219 twice with online off, with `DW4_CAMERAS=1` (the slot
camera and the union pass with one camera; `DW4_CAM_CHECK`: 0 of 1,902
frames differ) and with `DW4_CAMERAS=1 DW4_ONLINE=1` (a solo online instance:
the online rules on, one member present).

**Idempotent pictures.** A solo-led instance presents as often as its monitor
asks (about 2.4 picture passes per step at 144 Hz), a joiner replays the same
ticks headless with none; both must end in the same state. The netcode critique
showed it at f915905 (experiment B: two extra renders per step and alternating
widening, headless, twice headless and a hidden widescreen window: 4004f056 for
newgame_dng101b1, 4fa35801 for four players in stag0102), but smoke runs present
once per step and blend only without `smoke_frames`
(`game_main.cpp:2019-2020`), so no permanent check covers it. Round S1 adds a
test hook (`DW4_EXTRA_RENDERS=<n>`, `DW4_EXTRA_WIDEN=1`, plus an emulated r49
previous-view peek) to the checks of section 15 and compares against headless
with the net fingerprint.

The hook is in `game_main.cpp` (round S1): `DW4_EXTRA_RENDERS=n` renders the
same world time n more times after every step (into a scratch draw list, also
in headless runs, plus one more `traverse_scene` there), `DW4_EXTRA_WIDEN=1`
alternates the widening 0.75 / 0.5 between them, `DW4_EXTRA_PREVIOUS=1` gives
them the previous step's picture camera view as smooth motion's previous view
(the round 49 rule). Measured with n = 2 and both options, against the same
run without the hook (md5 of the fingerprint lines every 300 frames; for
per-slot cameras also of the `PlayerBox` hashes, the third column of the
`DW4_TRACE_CAMERAS` lines):

| Run | Without the hook | With the hook, headless | With the hook, hidden 1920x1009 widescreen window |
| --- | --- | --- | --- |
| newgame_dng101b1, 6000 frames | 4004f056 | 4004f056 | 4004f056 |
| four players, stag0102, shared camera | 4fa35801 | 4fa35801 | 4fa35801 |
| four players, stag0102, `DW4_CAMERAS=15 DW4_ONLINE=15` | bd855f57 / boxes 3342acf5 | bd855f57 / 3342acf5 | bd855f57 / 3342acf5 (local camera 2) |
| four players, dng101b1, `DW4_CAMERAS=15 DW4_ONLINE=15` | 91da4c29 / boxes 22c995df | - | 91da4c29 / 22c995df (local camera 3) |

Without the hook the same four-player runs give these values headless and in
hidden windows with local camera 0, 2 and 3 alike (the done check of round S1).

The four-player stag0102 value with the shared camera is the one of the
critique's experiment B on f915905 (4fa35801): rounds 49 and 50 changed
nothing in it.

### 4.5 Cost

CPU traversal time per step (µs), simulation + picture, with the CRT's FMA3
routines (the path online uses, 6.12):

| Stage | today (one combined pass) | 1 camera split | 4 cameras | 4 cameras, no cull |
| --- | --- | --- | --- | --- |
| stag0102 (field) | 207-210 | 100-107 + 138-140 | 284-292 + 148 | 180 + 151 |
| dng101b1 (dungeon) | 178 | 82-94 + 113-120 | 216-232 + 109 | 97 |
| stag0201 | 188 | 88 + 130 | 222 + 125 | 128 |
| dng205b1 | 171 | 93 + 120 | 216 + 126 | 124 |
| stagBS02 (boss) | 111 | 56 + 80 | 80 + 78 | 64 |

Each extra camera adds about 45-65 µs per step, almost all of it frustum tests
(four identical cameras cost as much as four different ones). Worst case with
four cameras about 0.44 ms per step, about 2.6 % of a core. With smooth motion
the picture pass runs at every present, the simulation pass once per step
(stag0102 at 144 presents/s: today 30 ms/s, four cameras 39 ms/s). The
replay rate a joiner reaches depends on the whole simulation step, not only the
traversal; S1 measures it on the final path (6.6).

Round S4 adds a cheaper conservative test for the extra cameras (a
precomputed view-projection, sphere against planes), built strictly without
floating point contraction like `frustum_cull_code`; the exact original test
stays for solo instances. S4 re-measures this table after the libm routing
(6.12).

Re-measured in round S1 on the production code (`feat-r52cam`, the same
scenario: four players, `DW4_LEVEL=200`, `spread4.txt`, 2400 frames, a hidden
1920x1009 widescreen window, `DW4_TRACE_TRAVERSAL_TIME=1`; the one-camera
split with `DW4_CAMERAS=1 DW4_LEASH_OFF=1`, four cameras with
`DW4_CAMERAS=15`). Two sequential runs a / b while other rounds used about
half of the machine; the 4-camera no-cull probe of round 51 is gone (plan B
only):

| Stage | today | 1 camera split | 4 cameras |
| --- | --- | --- | --- |
| stag0102 | 215 / 200 | 123 / 116 + 155 / 152 | 309 / 282 + 175 / 152 |
| dng101b1 | 166 / 174 | 90 / 90 + 121 / 118 | 217 / 232 + 110 / 125 |
| stag0201 | 218 / 186 | 95 / 100 + 134 / 155 | 232 / 246 + 126 / 149 |
| dng205b1 | 176 / 188 | 104 / 98 + 129 / 128 | 224 / 220 + 129 / 137 |
| stagBS02 | 109 / 108 | 53 / 51 + 73 / 71 | 74 / 74 + 71 / 72 |

The better of the two runs is within 10 % of the round 51 table for 28 of the
30 values; the exceptions are stag0102's one-camera simulation pass (116
against 100-107, the two runs themselves differ by 6 %) and stagBS02's
one-camera picture pass (71 against 80, faster). The four-camera simulation
pass costs 2.0-2.6x the one-camera pass in the fields and dungeons and 1.4x in
the boss arena, as before.

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

Checked in round S1 (`DW4_LEASH_OFF=1`, the ring off in every stage, also
after each stage load): `det.sh` 4004f056 / 396cb219, and one player with the
ring on and off gives the same fingerprint every 100 frames walking in stag0102
and stag0201 (2600 frames) and with the explore bot fighting through dng101b1
(6000 frames, the player defeated on the way). Nothing besides the ring acts
on a solo player. The engine applies the rule itself: while
`OnlineRules::leash_off` is set, every step switches the child hit off once
the stage's HitLand infos are there (`Engine::apply_leash_rule`).

The local co-op spread of about 92 units is also the yardstick for "near" in
this design (`kNearUnits = 100`): what members within it experience is what a
local co-op group experienced.

### 4.7 Demo cameras

A demo viewer is instance-wide, as is PADINPUT, except in a scoped scene (5.3),
where the viewer applies only to the triggering member's camera and its
picture. The stage flow rules (5) fork non-participants away before story and
boss scenes and scope the scenes of one member, so an instance-wide scene only
ever shows to members who took part or stood near. Every camera comes from
lockstep data, so the per-slot viewer stays deterministic.

## 5. Stage flow in an instance

The scan of the 7,168 decompiled scripts (96 stages; `scan_flow.py`) and the
player critique decide every case. `funcStageChange`, `funcStageIn`,
`funcPlayerSet` and `funcPlayerStatusGenerate` are byte-identical in all 96
`func.lua`.

| Case | Original | Online, multi-member instance | Mechanism | Round |
| --- | --- | --- | --- | --- |
| Exit mat with `funcStageChange` (195 calls: 183 matz, 7 mate, 2 matf, 2 matg, 1 mats; all guarded by MATisAllPlayer and `DMgetCount()==0`) | all living players on the mat; prompt for MATgetPlayerNo; PLAYER / ENEMY -1 OFCTRL; fade; everyone goes | the member who stepped on gets the prompt, only its player is frozen; on *Yes* every other living member on the mat is asked; those who said yes leave together (fork); everyone living = the original group move; never a slot-0 fallback (5.2) | C++ port of `funcStageChange` (`stag0101_m1__func.lua:107-206`) registered over the global after `func.lua` loads (the VM does not compile Lua text, `docs/LUA4.md`); `NETsplit` | S1 |
| Exit with a direct NEXTSTAGE (20 matz: quest dungeons, stag0203) | MATisAllPlayer | same rules as the port | class table (5.1) | S3 |
| Destination of an exit | arguments of the mat script | unchanged; the randomizer's zone remap still applies in STAGECTL NEXTSTAGE and the exit text remap on TEXTCTL (the port sends the same TEXTCTL messages) | none | - |
| Gate disc (quick panel, `gatedisc.lua` in 96 stages) | SYSsetGateStage, SQ_EXIT for all, everyone to stagML11 point 3; closes a running status window even if it is another player's | the user forks off first (with any dead member who would otherwise be left without a living one, 6.7), then the original script runs in that copy; the others play on and keep their windows | wrapper at `run_script` | S1 |
| A member is defeated | capsule at the spot, RAISE after 10 updates; the dead player watches the shared camera | capsule unchanged; the dead member's picture follows the nearest living member; capsule marker for the partners (4.1) | `set_view_slot` | S1 |
| *Give up* (new, dead member only, hold START) | - | the dead member forks off alone; in that copy nobody lives, so the original `end_g_000bcaps.lua` runs (game over, lobby point 0, capsule kept as BKCHIP); the only fork that makes a dead-only side | port intent (6.5) + fork | S1 |
| Every member dead | game over, everyone to stagML11 point 0 | unchanged; the barrier dissolves the group into solo lobbies | DISSOLVE | S1 |
| Quest abort when one player falls (dngq3*, dngq4*, stag0103, stag0203 hit_p_*), quest timer end | whole group | unchanged (whole instance); only members on the same quest share it (3.7) | none | - |
| Story trigger by one player (MATisPlayer: stagGF03 math00 Leomon; event-like hit_g) | one player starts it, everyone is teleported and taken along | participants = the trigger plus the members on the same mat or hit (plus dead members per the side rule); the others fork off before the script's first command, keep the stage as a joinable instance and still have the trigger ahead | class table + wrapper, in-stage fork | S3 |
| In-stage warps without a boss (Trailmon `rid_g_dtrailmon` in dng412b3/b4 with MOVETRANS 0-3) | everyone is taken along | no fork: the choice reads the trigger's pad, MOVETRANS and the demo apply only to the participants (trigger plus members on the same hit); the others stay where they are | class `warp`, scoped commands | S3 |
| Story and boss mats with MATisAllPlayer (the 20 boss arena matg with MOVETRANS 0-3 and doorwall ONHIT, story mats) | everyone gathers | gather as in the original; a member standing on the mat while others are elsewhere sees *Go without them*: the members on the mat proceed and keep the instance (boss-locked until the boss falls), the rest fork into a new joinable instance of the same stage with the boss ahead | port intent re-runs the mat script, in-stage fork | S3 |
| Boss stages stagBS01-06 | whole arena | entered through exits; boss-locked from the boss intro (the hit_f_* scenes) to the boss's defeat; an arriving member chooses *Wait for them* or *Go alone* (3.4); the exit prompt into a boss stage names who stays (5.2) | boss lock in the simulation | S3 |
| Boss end (ded_e_*: MOVETRANS 0-3, doors, flags) | everyone | only participants are still there; the lock clears, joins open again | none | - |
| Stage entry scene (`first.lua`; 52 of them set a demo camera such as 'entry') | at the stage start | runs when the instance starts, with every co-arrival; a later joiner waits for its end (hold, 3.4), never reruns it and appears with its own SQ_ENTR at its entrance | co-arrival, join path | S1 |
| Scenes of one member: field talks (88 hit_n_* in fields and dungeons, among them the map recorder on every story dungeon floor), camera tours and demos on mats (stag0102 matf00 'Slope', stag0101 matb01 / matx01 / hit_g_lever01z01 camsp0401-0404), tutorials, shaking mats (dng310f3 matt05) | whole group frozen, HUD off, PADINPUT off, demo camera for all | S1-S2: original (a known limit). From S3: class `local_scene` with the proximity rule (5.3): everyone near = original; otherwise only the trigger is frozen and sees it, with a local freeze zone | class table, scoped commands | S3 |
| Status window (START, SUBWND) | the engine freezes, one window at a time, drawn for all | S1-S2: original instance-wide freeze; a member whose START is refused because another member's window is open sees "<name> has a menu open". From S3: the proximity rule (5.3, question 6); one window per instance at a time; another member's window no longer blocks exits (DMgetCount) | scoped SUBWND + draw filter | S1 / S3 |
| Shops, bank, Digi-Lab, card keeper, quest board, event console, NPC talks in the lobby | lobby programs | solo hub instance: single player behaviour | DISSOLVE | S1 |
| Save room stagIS12 | SAVECTRL; players no longer joined leave the group | solo; writes the virtual card; uploads the own record (9); *leave the group* hidden online | wrapper | S2 |
| Call shop stagIS13 (LOADCTRL2 without title) | adds or replaces players | off online: a port hint instead (every member is its own machine) | wrapper | S3 |
| Pits (stag0102 mati00, dng205b3 matt23-25, dng206f2, dng310f3, dng412b4, dng103f2, dngq4b2*) | ADDHP -99999 + PLAYER REMOVE; once every joined player fell and no capsule lives: ENEMY -1 OFCTRL, full-screen fade i_fadewn01 SQ_BLOUT30, MATCTRL PAUSE / RESTART, respawn, ENEMY ONCTRL 90 | each fallen member respawns alone: only the per-player part of the respawn (GENERATE, SQ_HIDE, SQ_ENTR, ADDHP, ADDMP of function_00CC4198) for that member, no enemy freeze, no mat pause, the fade drawn only on its machine; when every present member is in the pit, the original script runs | class `pit`, wrapper | S3 |
| NINZU branches (53 scripts) | player count when the script runs | count of present members when the script runs (the original meaning). A member who just arrived counts at once: the dwbutton2 window of dng103f2 (and dng204b1, dng205b3, dngq1b2e, dngq4b1* / b2*) is 75 frames alone and 15 with two or more, as in local co-op. Puzzles that fixed NINZU at setup can stall when a member leaves after the setup; entering the stage again resets them | arrival notice | S1 |
| Puzzle mats (about 120 MATisAllPlayer scripts without exit or NEXTSTAGE: breaking gates stagGF01-04 mate00, stag0202 mata00-05, stag0203 matg00-09, stag0404 matd02-12, dng310f3 matt05-16, ...; buttons per slot such as stag0101 hit_g_sbutton) | everyone on the mat | unchanged: co-op puzzles. Members on such a mat see "Waiting for <names>" while present members are elsewhere (presentation, from the mat mask); every machine shows "<name> joined" when a JOIN applies | hint overlay | S1 (notice) / S3 (hint) |
| EXP (`combat.cpp:1069-1097`) | every joined living player gets a share scaled by the joined count (two: killer 93 %, other 47 %; four: 80 % and 40 % each); no distance check, but the leash kept everyone within about 92 units | default: the count and the recipients are the members within `kNearUnits` of the defeated enemy (a lone killer far from the others gets the solo 100 %); members further away neither get a share nor lower the killer's (question 7: or the original rule) | sharing rule | S3 |
| Drops, item pickup | the label goes to the nearest player, first come | unchanged | none | - |
| Session values (quest flags, counters, folder 11, GATESTAGE) | engine-wide | by class (3.7): context must match to join, personal travels with the member, instance belongs to the engine line | context hash, carry | S1 |
| Slot 0 duties (pad 0 windows, film skip) | pad 0 | the triggering member's pad, else the leader's (7.4) | pad redirect | S1 |
| Soft reset (STAGECTL), console reset (B+X+START 180 fields on any pad), pause menu, save states, cheats, rebalancing | - | off online; the local console reset combo opens *Leave room?* (save states and pause already are off) | online rules | S1 |
| The quest maze dngq6b01 (matz01-14 with `trans.lua`: one global `g_curMapBlock`, MOVETRANS or regeneration of every joined slot, drops erased, generators reset) | everyone gathers, the world resets | unchanged: class `gather` (original rule); no free travel inside dngq6b01 (documented) | class table | S3 |

### 5.1 Script classes

A port data file (`data/online/script_classes.txt`, generated by a tool from the
scan and checked in) lists every script with a class: `exit`, `exit_direct`,
`boss_mat`, `boss_intro`, `boss_end`, `story_mat`, `story_trigger`, `warp`,
`gather`, `local_scene`, `pit`, `gatedisc`, `callshop`, `saveroom`, `puzzle`.
Online only:

- MATisAllPlayer keeps the original meaning for `puzzle`, `gather` and every
  unlisted mat; for `exit` the port decides (5.2); for `boss_mat` and
  `story_mat` it is the original plus a *Go without them* intent.
- `run_script` consults the class before the first command: `gatedisc`,
  `story_trigger` and confirmed `boss_mat` / `story_mat` runs fork the
  non-participants away first (6.7), then run the original script on the
  participant side and skip it on the other side. `warp` and `local_scene`
  runs do not fork: their instance-wide commands are scoped (5.3).
- `boss_mat` and `boss_intro` runs set the boss lock, `boss_end` runs clear it
  (6.5).
- `NETsplit(mask)` returns whether this copy continues (7.3); it is the only
  place where the local slot influences the simulation, and only at a fork.
- Every script run records its *trigger slot*: the member that stepped on (for
  mats, the rule of 5.2), the shooter (BLTgetShootRamNo) for hits, the talker
  (g_messPlayerNo) for talks; none otherwise. Pad redirects and scoped scenes
  use it.
- The class tests check that dngq6b01's matz01-14 are `gather`, never `exit`
  or `warp`.

### 5.2 Exits (the `funcStageChange` port)

The port keeps the original's structure (its demo records and their per-update
checks, the prompt colour per slot, the wait for the models of defeated
players) and changes who is asked, who is frozen and who leaves:

- **Prompt owner.** The mat script runs on every change of the 4-bit mask
  (`mat_ctrl.cpp:108`): stepping on, stepping off, a death, a despawn. The
  port's owner is the lowest living member whose bit is in `current & ~previous`
  in this update (it stepped on). A run without such a member does nothing; the
  slot-0 fallback of MATgetPlayerNo (`mat_ctrl.cpp:164-167`) is never used.
  A member who stays on the mat after answering *No* is not asked again until
  it steps off and on.
- **Freeze and drawing.** Only the owner's player is off control; enemies run
  on. The prompt's half fade (`i_fadewn01 SQ_BHLFOUT30`) and window
  (`i_zzstchgwb`) are drawn only on the owner's machine (draw filter, round
  S1). Before the prompt, the owner's screen names the members who would stay
  ("Kim and Lee stay here"), with a warning when the destination is a boss
  stage or a boss floor (presentation).
- **Others on the mat.** On *Yes*, every other living member on the mat is
  asked in turn with the same prompt in its own colour, drawn on its machine;
  each has 600 frames (10 s), no answer counts as *No*. The owner waits frozen
  ("Waiting for <name>"). Then the members who said yes leave together: one
  fork, one barrier, one arrival. A member who steps on while the departure is
  pending is not part of it.
- **Departure.** Leavers = the owner plus the members who said yes, plus dead
  members by the side rule (6.7). If they are every present living member, it
  is the original group move (no fork). HEADING goes to the server at the
  departure (3.4).
- The randomizer's exit text remap happens on TEXTCTL in `ui.cpp`; the port
  sends the same TEXTCTL messages, so it keeps working.

Built in round S1 (`src/engine/online_exits.cpp`, `feat-r52cam`):

- The port replaces the Lua global `funcStageChange` after every `func.lua`
  while the instance has two or more present members; the original function
  stays under a hidden global and comes back when the instance drops to one
  member, so a solo instance runs the original script (det with a solo online
  instance: 4004f056).
- On an exit mat (a mat script whose chunk names `funcStageChange`, found by
  `Engine::script_mentions`) `MATisAllPlayer` is true online when a living
  member stepped on in this update; every other mat keeps the original rule.
  The 20 direct-NEXTSTAGE exits keep the original until S3.
- The owner's prompt is a demo work with the original's records; after its
  answer one port record (demo record type 8, port only) steps a state
  machine: the other members' prompts one after another (600 frames each, then
  SELECTCTRL REMOVE, a No), then the departure: `exit_leavers` (the side rule),
  the group move when every living member goes, otherwise
  `Engine::online_split` and, on the leavers' copy, the original departure
  records as a new demo work; the stayers' copy fades the hidden half fade
  back.
- Draw filter: the picture pass skips another member's `i_zzstchgwb` /
  `i_zzstchgwn`, and while an exit runs `i_fadewn01` and the STCHGFONT00 line
  for members who do not see the prompt (`Engine::picture_hidden_roots`).
- Limits in S1, as in the original: the owner's own prompt has no time limit;
  a member who steps on while another member's exit runs (`DMgetCount()`
  is not 0) gets no prompt until it steps off and on again.
- Checked with two members in dng101b1 (both copies of every fork): an exit
  taken alone (the leaver arrives in stag0101 alone, the stayer keeps
  dng101b1, the fingerprints equal up to the fork), the partner on the mat
  asked (yes: the group move without a fork; no, or no answer for 600 frames:
  the fork), a defeated partner taken along by the only living member (no
  fork, no game over), the gate disc (its user forks off to stagML11 point 3;
  with a defeated partner everyone goes), give up (the defeated member's copy
  runs the original game over into stagML11 point 0, the partner's copy
  despawns it).

### 5.3 Scoped scenes (round S3)

Scenes that concern one member pause the whole engine in the original. Online
from round S3, the classes `local_scene` and the status window follow the
*proximity rule*, decided when the scene starts (simulation, the same on every
machine):

- **Everyone near.** If every other present member is within `kNearUnits` of
  the trigger, the scene runs exactly as in the original: the whole instance
  pauses and every screen shows it, as a local co-op group experienced it.
- **Someone far.** Otherwise the scene is scoped to the trigger: its
  instance-wide commands are narrowed while the script run and every demo
  record it starts are active. PLAYER -1 OFCTRL / ONCTRL and PADINPUT reach
  only the trigger's slot; ENEMY -1 OFCTRL and GENERATER ONFREEZE reach only
  the enemies and generators within `kNearUnits` of the trigger (a *local
  freeze zone*: the original's protection, applied around the member who is
  in the scene); SETDEMOCAMERA steers only the trigger's camera; windows,
  overlays (`j_map102b3`), fades and HUD panel closes are drawn only on the
  trigger's machine; SUBWND REMOVE closes only the trigger's window. Members
  near the trigger are frozen with it and see "<name> is in a scene".
- **Status window.** The same rule: near = the original freeze (MODELCTRL,
  GENERATER, SOUND, messenger, `subwnd.cpp:1042-1059`); far = the owner's
  player off control, the local freeze zone, the window, its prompts and fades
  only on the owner's machine, no messenger pause and no SOUND freeze for the
  others. The first task of the S3 scenes lane is to find out what the
  messenger pause and the SOUND freeze protect inside a window (a scripted
  run with each of them left out against the original), and the lane keeps
  them where a window needs them.
- Question 6 asks the user whether this is the wanted protection (the
  alternatives: always the instance-wide freeze; no protection when far).

## 6. Transport and lockstep groups

### 6.1 Topology

The server is the hub: every client keeps one TCP connection to the relay of
its room, across all its instances. The r48 relay (a byte splice per instance
channel with the instance host's PC as the lockstep centre, `RelayServer.cs`)
is replaced: it made guest-to-guest latency cross four links, ended the
instance when its host left (`EndInstanceLocked`, `RoomService.cs:486-510`), and
knew nothing to help a joiner, a reconnect or a desync. A peer-to-peer mesh is
rejected (NAT, IP disclosure, against "all traffic through the server").

The server orders, seals on its clock and stores; all members simulate as
equals. It never runs the game.

### 6.2 Connection

- `POST /rooms` and `POST /rooms/{code}/join` additionally return a room-scoped
  relay ticket (HMAC, one-time, 60 s; role `member`); `POST /rooms/{code}/ticket`
  issues a new one for a reconnect.
- The client connects to the hard-coded server address (88.198.52.221, or
  loopback in a development build) and the relay port from `GET /status`
  (default 52041). It never takes a host name from the server.
- First line `DW4R2 <ticket> <protocol> <dataHash> <settingsHash>\n`, answer
  `OK <memberId> <serverTimeMs>\n` or `ERR <code>\n`; then frames of type (u8)
  + length (u32 LE) + payload, the netplay framing. Frames above 1 MiB are
  refused; state transfers come in chunks. `settingsHash` covers every
  simulation-relevant setting (protocol, data hash, the room's difficulty,
  camera and light mode, randomizer seed and modes, fast loading); the server
  refuses a member whose hash differs from the room's.
- The live relay connection counts as the room heartbeat; the REST heartbeat
  is needed only while it is down.
- Plain TCP as in r48. Inputs are not secret; tickets are one-time and
  short-lived. TLS on the relay would need SChannel on a raw socket and is not
  planned. If the S1 loss matrix (6.4) shows that TCP's head-of-line stalls
  hurt too much, round S4 moves the game frames (INPUT, SEALED) to UDP on the
  same port with the ticket as the session key; everything else stays on TCP.

### 6.3 Messages

**Pinned in round S1 (integration).** One layout for the C++ client
(`src/game/online/wire.hpp`), the C# relay hub and its fake clients
(`server/DW4Online/src/DW4Online.Relay/Protocol`) and the Python test client
(`server/DW4Online/tests/e2e/relay_spike_test.py`); the byte layout of every
field is in the appendix "DW4R2 wire layout". Golden byte vectors check it on
both sides (`tests/online_wire_tests.cpp`,
`server/DW4Online/tests/DW4Online.Tests/WireGoldenTests.cs`): a change on one
side fails both until the other follows. Message types 0x01-0x13 go client to
server, 0x80-0x8E server to client; a pad is 6 bytes (6.4); `inst` is the
instance id, `visit` its stage load count, both u32.

Client to server:

| Type | Message | Fields | When |
| --- | --- | --- | --- |
| 0x01 | INPUT | inst, visit, first tick, 1-32 pads | sealed instance: every step, `d_i` ticks ahead (the first `d` together with SNAPHASH) |
| 0x02 | LOG | inst, visit, first tick, 1-1200 pads | solo-led instance: the pads it applied, batched every 100 ms, contiguous from tick 0 (hub instances send none) |
| 0x03 | HEADING | inst, stage, point, session context hash | an exit or the gate disc this copy takes (`Engine::take_heading`), one to two seconds before its barrier |
| 0x04 | BARRIER | inst, visit, barrier tick, stage, point, alive and present masks, session context hash, own carry | after the barrier tick |
| 0x05 | BARRIER_CHOICE | inst, wait or alone | the arriving member's choice at a boss-locked target (BARRIER_WAIT flag 1) |
| 0x06 | SPLIT | inst, visit, fork tick, leaver mask (the side that moves to the child: `SplitResult::child`), kind (exit, in-stage) | after the step of a fork |
| 0x07 | SNAPSHOT | inst, visit, total, offset, chunk (at most 60 KiB) | after the load step of a NEW: the base group's member in the lowest new slot only |
| 0x08 | SNAPHASH | inst, visit, canonical hash | after the load step (NEW) or the restore (co-arrival): every member |
| 0x09 | READY | inst, visit, tick (a multiple of 60), net fingerprint, replay rate, carry | a joiner within ten ticks of the newest tick |
| 0x0A | JOIN_AT | inst, visit, tick J | the solo leader's answer to JOIN_REQ |
| 0x0B | NETFP | inst, visit, tick (a multiple of 60), net fingerprint after that tick | every member of a field instance |
| 0x0C | STATE | inst, visit, tick, flags (joinable, membership-safe, boss lock), session context hash, reason | on change (the boss lock and the context at once, the rest at most once a second) |
| 0x0D | INTENT | inst, visit, kind (1 go without, 2 give up), argument | a member's port choice (sealed instances; the server stamps it) |
| 0x0E | STORY | inst, visit, confirmed revision, phase claim, persistent flags, keys, counter growth per engine line (8.3) | before every BARRIER; once a second when the pending set changed, else every ten seconds while it is not empty; never from a stagIS* stage |
| 0x0F | PRESENCE | stage (empty: hidden), inst, position, yaw, species, level, HP, max HP, MP, max MP | five times per second in a stage (HP 0: defeated) |
| 0x10 | RESUME | inst, visit, slot, last executed tick | after a reconnect that WELCOME calls resumable |
| 0x11 | RATE | replay steps per second | after WELCOME (the calibration) and after every replay |
| 0x12 / 0x13 | PONG / BYE | nonce, client time / reason | |

Server to client:

| Type | Message | Fields | When |
| --- | --- | --- | --- |
| 0x80 | WELCOME | member (the room slot 0-9), inst, visit, slot, mode (hub, solo-led, sealed), stage, last tick, delay, max catch-up seconds, flags (resumable) | the first frame after OK: a fresh member's solo hub instance (the title, stage ""), or the instance it still holds |
| 0x81 / 0x82 | SEALED / LIVE | inst, visit, first tick, count, tick records | SEALED to members at each tick's due time (batched when behind), LIVE to observers (the leader's LOG or the sealed ticks) |
| 0x83 | JOIN_REQ | inst, visit, slot, suggested delay | to the leader of a solo-led instance: choose J |
| 0x84 | BARRIER_OK | old inst and visit, decision (1 NEW, 2 JOIN, 3 DISSOLVE, 4 CO_ARRIVE), new inst and visit, stage, point, own slot, slot map, mode, delay, co-arrivals (slot, member, carry), story revision, story, dropped set, notice, names | answer to BARRIER |
| 0x85 | BARRIER_WAIT | inst, reason (co-arrival, not joinable, boss lock, snapshot), seconds left, names, flags (choice needed) | while a barrier is held (the loading screen's line) |
| 0x86 | STATE_DATA | inst, visit, total, offset, chunk of the state blob (purpose, slot, snapshot, forks with their sides, first tick, tick records) | join, co-arrival, resync, resume |
| 0x87 | PRESENCE | member, the fields above | members in the same stage outside the own instance |
| 0x88 | RESYNC | inst, visit | the member's fingerprint lost the vote (a STATE_DATA follows) |
| 0x89 | STORY_ACK | revision, the room story (1,100 bytes), flags (answer), refused keys with reasons (8.3) | after WELCOME (the confirmed story to start from), to every other member on each new revision, to a STORY's sender as its answer |
| 0x8A | ROOM | notice (joined, left, owner present, owner away, lagging, lag over, connected, disconnected), member, account, name, flags (owner) | in-band room changes |
| 0x8B | PING | nonce, server time, the member's arrival margin in ms, suggested delay | once per second |
| 0x8C | CLOSE | reason (left, kicked, room closed, replaced, protocol, abuse, timeout, stopping), text | before the server closes |
| 0x8D | ROSTER | every member: stage, inst, level, HP, max HP, flags (connected, lagging, owner) | once per second (the room UI) |
| 0x8E | REASSIGN | old inst, visit, new inst, from tick, mode, flags (departing) | after a fork: this member's copy is the child |

Events inside a tick record: JOIN (slot, entrance point, carry), LEAVE (slot),
INTENT (slot, kind, argument), LEAD_FROM (tick: the remaining member leads
alone from it; stamped on the tick before), DELAY (slot, d). A client takes
LEAD_FROM and DELAY only live and only for itself (never from a replayed
history or while it observes before its own JOIN) and ignores events for
slots that are not on its side of a fork.

What the integration changed against the lanes: the client lane's layout
(server types 0x41-0x4E, 7-byte pads with a fill flag, the member id in JOIN
and in BARRIER_OK, REKEY, instance 0 for the title, decisions 0-3, a decimal
settings hash) gave way to the relay lane's, which the hub, the fake clients
and the Python client already shared: WELCOME names the title's hub
instance, REASSIGN replaces REKEY, a side of one member after a fork stays
sealed until the server's LEAD_FROM, the lag icons come from ROOM / ROSTER,
a hidden or defeated member's tag comes from PRESENCE's empty stage or HP 0,
and the settings hash is the relay's canonical text (appendix) sent as 16 hex
digits. Two client-side facts stay: a solo group whose barrier gets no answer
for five seconds goes on as NEW under instance 0 (not offered to joiners),
and the wire's numbering of decisions (1-4) and intents (1-2) maps onto the
engine's (`Decision` 0-3, `IntentKind` 0-1) in `src/game/online/engine_adapter.cpp`.

### 6.4 Ticks, sealing, delay

**Solo-led** (one member). The member steps on its own 59.94 Hz clock
(`kFieldSeconds`, `engine.hpp:46`) with input delay 0, exactly like offline
play, and streams the pads it applied (LOG). A network hiccup does not stall
it. Hub instances are not logged at all (nobody joins them); field instances
are, so that a joiner can replay them.

**Sealed** (two or more members). The server runs a schedule per instance at
59.94 Hz: tick t is due at `S(t) = S0 + t · kFieldSeconds` on its monotonic
clock. At `S(t)` it seals tick t with every present member's pad and sends
SEALED; a pad that is not there by then is filled at once with that member's
last sealed pad (at most six ticks in a row), then neutral. A pad that arrives
after its tick was sealed is dropped and counted. Nobody waits for anybody:
one member's late uplink costs that member its inputs for those ticks, never
the others a hitch. Machines execute only sealed ticks, their own slot's pad
included as sealed. A member that has filled pads for 30 ticks in a row is
marked as lagging (icon on its name tag); after `Relay:LagLeaveSeconds`
(default 10) of continuous filling the server stamps LEAVE.

**Delay.** Member i executes tick t when SEALED(t) arrives, about one one-way
time after `S(t)`, and sends its pad for `t + d_i` then; it arrives about one
round trip after `S(t)` and is due at `S(t + d_i)`. So
`d_i = ceil((RTT_i + J_i) / 16.683 ms)`, where `J_i` covers the member's
round-trip spread up to its 99.9th percentile. Only the member's own round
trip counts: the server's schedule is the common clock, so the other members'
latencies do not enter (they did in the waiting star of revision 1, where the
formula also lacked the member's own return path). The server measures each
member's arrival margin (due time minus arrival) directly; it raises `d_i` by
one when more than 0.1 % of the last 3,600 pads were late and lowers it by one
when the 0.1 % quantile of the margin stayed above one tick for 30 s; a change
is a DELAY event at a sealed tick (the member repeats or skips one sample;
deterministic, because the sealed pads are what every machine executes).
Bounds 1..15 (`Sealing:MaxDelayTicks`; revision 2 said 10, which the S1 loss
matrix below showed too small above about 110 ms of round trip). The starting
value is `ceil((RTT + spread) / 16.683 ms) + 1`, the +1 for the client's
one-tick playout buffer; measured `d` without loss: 3 at 21 ms RTT, 5 at 61 ms,
7 at 71 ms with 10 ms jitter, 11 at 121 ms. Inputs are
indexed by tick, so every machine has its own `d_i`, as the direct sessions
already allow (`game_main.cpp:1853-1856`).

**Measured in round S1 (client lane): the formula misses the playout tick.**
A member does not execute tick t when SEALED(t) arrives but one playout tick
later (the pacing below keeps one tick in reserve), plus up to one pass of its
host loop. With the built client (`src/game/online/session.cpp`, the pacing of
`pacing.cpp`) against the fake hub of `tests/online_fake_hub.hpp` (server-paced
sealing, immediate fill, a simulated clock, TCP order kept), the smallest delay
without a single late pad in 20 s of two-member play
(`dw4_online_session_tests --delay-table`) was:

| one way | jitter (each way) | needed `d` | `ceil((RTT + J) / f)` | `ceil((RTT + J) / f + 1.5)` |
| --- | --- | --- | --- | --- |
| 5 ms | 0 | 3 | 1 | 3 |
| 10 ms | 0 | 3 | 2 | 3 |
| 20 ms | 0 | 4 | 3 | 4 |
| 30 ms | 10 ms | 7 | 5 | 7 |
| 50 ms | 0 | 8 | 6 | 8 |
| 50 ms | 20 ms | 10 | 9 | 10 |

So the starting value is `d_i = ceil((RTT_i + J_i) / 16.683 ms + 1.5)`
(J = the round-trip spread, both directions' jitter), one to two ticks more than
the formula above; the server's adaptation then works from there. The
estimate inside Europe becomes 3-4, +1-2 on WLAN. The client takes the
suggested delay from PING before a sealed start (NEW with two or more members,
JOIN_AT) and changes it in a sealed instance only through DELAY events.

**Pacing.** A client executes sealed ticks at its own 59.94 Hz pace with a
playout buffer of one tick. Its pace follows the server's by at most ±0.1 %
(clock drift of the two crystals is far below that), and the audio device is
fed at the simulation's pace through `SDL_SetAudioStreamFrequencyRatio`, so the
sound manager's mapping to the AX frames (`audio_delta_`,
`managers_core.cpp:2555-2559`) does not move. Revision 1's ±2 % nudge would
have shifted the audio by one AX frame every 25 fields. A backlog of more than
three ticks (after a stall on the member's downlink) runs at once without
pictures, which moves the audio once, as an offline hitch does.

**Loss.** Over TCP a lost segment holds back everything behind it: about one
round trip plus 50 ms with fast retransmit, more for a tail loss. With
server-paced sealing a loss on a member's uplink drops that member's inputs for
that time; a loss on its downlink hitches only its own picture. How often this
happens at real loss rates was never measured (the delay proxy of `r51net`
loses nothing). The relay lane of round S1 measures it first: a matrix of loss
0.1 / 0.5 / 1 / 2 % and jitter 0-30 ms at one-way times of 10-60 ms, with the
fill rate per member and the hitches of the others; a head-of-line proxy model
on Windows loopback, and real TCP loss on loopback where a netem-capable
environment exists. The result decides whether round S4 carries a UDP
transport for the game frames.

*Measured in S1 (relay lane; scenario S12 of 16):* `DW4Online.FakeClient matrix` (also
`tools/online_test/run.py matrix`). Per cell an in-process DW4R2 relay on
loopback and three fake clients that arrive together in one sealed instance,
each behind its own impairment proxy with the same one-way time, jitter and
loss in both directions. Windows loopback has no netem and the development PC
no WSL, so the head-of-line model is the measurement: every read from the
sender is one segment, delivered after one-way + U(0, jitter) and never before
an earlier one (TCP keeps the order, so jitter adds latency); a lost segment
arrives one fast retransmission later (RTT + three segment gaps, about RTT +
50 ms at 60 segments per second), a lost retransmission adds a 300 ms RTO
(doubling), and everything behind it waits. 20 s warm-up (the delay rule
settles; for the matrix its bound is 20 and nobody leaves for lag), then 40 s
measured, machine about 25 % busy with other rounds. Each cell: the mean of the
three members' fill rate (pads the relay filled), final `d` (range), hitches
per minute (ticks that reached the client after their playout time; one tick
of playout buffer above the 99th percentile arrival, moved by at most 0.1 % of
the pace) and stall seconds per minute:

| one-way / jitter (RTT) | 0 % loss | 0.1 % loss | 0.5 % loss | 1 % loss | 2 % loss |
| --- | --- | --- | --- | --- | --- |
| 0 / 0 (0.3 ms), proxy only | 0 %, d 2, 0/min, 0 s | | | | |
| 10 / 0 ms (21 ms) | 0 %, d 3, 0/min, 0 s | 0.33 %, d 5-6, 2.0/min, 0.10 s | 0.32 %, d 9, 18.5/min, 0.39 s | 0.35 %, d 10, 27.5/min, 0.46 s | 1.40 %, d 11, 52.0/min, 1.19 s |
| 10 / 10 ms (32 ms) | 0 %, d 4, 0/min, 0 s | 0.28 %, d 7-8, 4.0/min, 0.21 s | 0.51 %, d 10-11, 20.5/min, 0.65 s | 0.76 %, d 11, 34.0/min, 0.98 s | 1.26 %, d 12-17, 44.0/min, 1.46 s |
| 10 / 30 ms (57 ms) | 0.11 %, d 8-9, 1.0/min, 0 s | 0.42 %, d 9-11, 1.5/min, 0.10 s | 0.44 %, d 13, 15.0/min, 0.83 s | 1.07 %, d 14, 31.0/min, 1.45 s | 1.24 %, d 14-20, 49.5/min, 3.04 s |
| 30 / 0 ms (61 ms) | 0 %, d 5, 0/min, 0 s | 0.50 %, d 9-10, 3.5/min, 0.30 s | 0.50 %, d 14, 18.5/min, 1.03 s | 0.39 %, d 15, 31.5/min, 1.81 s | 1.50 %, d 15-16, 61.0/min, 4.05 s |
| 30 / 10 ms (71 ms) | 0 %, d 7, 0/min, 0 s | 0.61 %, d 11-13, 3.5/min, 0.31 s | 0.79 %, d 15-16, 17.5/min, 1.19 s | 0.93 %, d 16, 35.5/min, 2.55 s | 1.47 %, d 16-18, 59.0/min, 4.45 s |
| 30 / 30 ms (95 ms) | 0 %, d 9, 0/min, 0 s | 0.65 %, d 13-15, 2.5/min, 0.26 s | 0.85 %, d 18, 19.5/min, 1.77 s | 1.01 %, d 18-19, 29.0/min, 2.84 s | 1.47 %, d 18-20, 61.5/min, 6.06 s |
| 60 / 0 ms (121 ms) | 0.40 %, d 11, 1.5/min, 0 s | 0.99 %, d 14-17, 4.5/min, 0.56 s | 1.03 %, d 20, 15.0/min, 1.78 s | 1.28 %, d 20, 31.5/min, 3.75 s | 4.66 %, d 20, 60.5/min, 7.73 s |
| 60 / 10 ms (132 ms) | 0 %, d 10-11, 0/min, 0 s | 1.14 %, d 15-18, 4.5/min, 0.54 s | 1.86 %, d 20, 16.5/min, 2.12 s | 2.91 %, d 20, 36.0/min, 4.84 s | 6.52 %, d 20, 61.0/min, 8.42 s |
| 60 / 30 ms (155 ms) | 0.22 %, d 13, 0/min, 0 s | 1.14 %, d 19-20, 5.0/min, 0.58 s | 3.84 %, d 20, 20.5/min, 2.86 s | 37.5 %, d 20, 27.5/min, 3.77 s | 13.8 %, d 20, 58.0/min, 10.27 s |

Only the impaired member pays: with 2 % loss on one member's link only (30 /
10 ms), that member had 0.54 % fills, d 16, 63 hitches and 4.3 s of stalls per
minute while the two clean members had 0.04 % fills, d 7-8, 0-1.5 hitches and no
stall (60 / 30 ms: 13.6 % against 0-0.17 %, the clean ones at d 13 and without
hitches). Server-paced sealing keeps a loss local, as designed.

What TCP costs: without loss nothing (no fill, no hitch). With loss the delay
rule keeps the fill rate low (at most 1.5 % up to 2 % loss at 30 ms) by raising
`d` far above the clean value (0.5 % loss: 9-18 instead of 3-9 ticks, 100 to
150 ms more input delay), and every lost segment on a member's downlink stalls
its picture for about one round trip plus 50 ms: 15-20 hitches and 0.4-1.8 s of
stall per minute at 0.5 % loss, 28-36 and 0.5-3.8 s at 1 %, 44-62 and 1.2-8 s at
2 %. A first run on a fully loaded machine (every core busy with other rounds,
not used for the table) showed the same picture plus a few scheduling
hitches.

**Decision: round S4 carries UDP for INPUT and SEALED**, with redundancy (each
INPUT datagram repeats the member's pads not yet sealed, each SEALED datagram
the last few ticks, so an isolated loss costs nothing and only a burst longer
than the redundancy waits), the ticket as the session key, the TCP connection
kept for everything else and as the fallback when UDP does not get through.
The loss rates of WLAN and busy home links (0.5-2 %) would otherwise cost the
affected player 100-150 ms of extra input delay and 15-60 picture stalls per
minute. Until then TCP works: the relay's delay rule and fill keep everyone
else unaffected.

**1 → 2 members.** The server sends JOIN_REQ to the solo leader; the leader
answers JOIN_AT(J) with `J = current tick + d`, switches to delayed sampling at
once and repeats its last pad for the ticks before `J` it has no sample for
(logged, so deterministic). From J the instance is sealed; the server places
`S(J)` at its estimate of when the leader reaches J (from the LOG timestamps).

**2 → 1.** When a LEAVE or fork leaves one member, the server sends
LEAD_FROM(L): the member executes the sealed ticks before L and leads alone
from L, resampling with delay 0.

**Pads on the wire.** Online, a pad is quantized before it is sent: each stick
axis to the GameCube's steps, `round(v · 74)` as an int8 in -74..74 (the values
a GameCube stick can give the game are k/74, `pad.cpp:85-121`), so every
machine executes `k / 74.0f`. The PC host otherwise sends the SDL int16 axes
as floats (`controls.cpp:36`, `pad.cpp:100`), whose noise in the low bits made
the logs compress five times worse (critique experiment C: 3.7 % against
19 %). Quantizing is deterministic (every machine receives the same sealed
values), leaves the response curve as it is and is closer to the original.
Offline input is unchanged. A pad is then 7 B: buttons as u16 (if the PC's
masks fit in 16 bits, which S1 checks; else u32 and 9 B) and four int8 sticks;
`connected` comes from the present mask. About 0.4 KB/s per player and
direction.

*Checked in S1 (relay lane):* the PC's button masks fit in 16 bits. The
logical bits are 0x0008-0x8000 (`include/dw4/pad.hpp`, `pad_button`),
`gamecube_buttons` maps raw PADStatus bits only onto them, and the keyboard
and controller bindings (`controls.cpp`, `press(PadKey)`) set only those bits;
the recorded logs of 6.6 use 0x0010-0xFC50. So a pad is buttons u16 LE, then
the main stick x, y and the C-stick x, y as int8 `round(v * 74)` (main stick
-72..72 after the host's 72/74 scale, C-stick -59..59): 6 B, not the 7 B
counted above, since `connected` comes from the present mask and nothing else
needs a byte (the u32 case would have been 8 B). The relay lane's independent
Python client found the miscount: the wire layout in the appendix is 6 B.

### 6.5 Events and when they apply

The server stamps JOIN, LEAVE and INTENT at a future tick J (sealed: the
largest member horizon + a margin; solo-led: the leader's JOIN_AT). They apply
before the first step at or after J in which the instance allows them, a
deterministic test every machine evaluates the same way:

- **membership-safe**: pad input enabled, no demo record running, no SUBWND,
  not loading, presenting, no fork pending. LEAVE and INTENT wait for it.
- **joinable**: membership-safe, not a hub or UI stage, `boss_lock` clear.
  JOIN waits for it.
- **boss_lock** is simulation state in `OnlineRules` (7.2): set when a
  `boss_mat` or `boss_intro` run starts a boss fight, cleared when the boss's
  `boss_end` (ded_e_*) runs and at every barrier. Every machine computes it,
  STATE reports it at once, and the server's flag only mirrors it.

**Absent and leaving slots.** Inside the engine, the input of slot s is the
neutral pad with `connected = false` unless s is present, and the neutral pad
with `connected = true` while s is leaving. It is an engine rule applied in
every step and every replay, whatever the tick contains (ticks sealed before a
SPLIT still carry the leavers' pads). So a window waiting for a leaving slot
cannot hang the instance (the pad redirect, 7.4, does the rest), and no remote
pad reaches the console reset (which is off online anyway, 7.4).

A slot is *leaving* from the tick of its LEAVE until the LEAVE applies: when
the LEAVE's tick comes in a step that is not membership-safe, every machine
marks the slot (`Engine::online_mark_leaving`, before that step; the client's
`apply_events`), so its pad is neutral and connected and its readers take the
leader's pad (`pad_reader_slot`: an exit prompt it owns, which has no time
limit, and its status window, whose page turns and digit cursors read
`window.player` through `pad_reader_slot` too). The step the LEAVE waits for
then comes. (Round S1 review: nothing set the mark, and a member that left
with its own exit prompt or status window open froze the instance for the
rest of the stage; the session test `leave_with_open_prompt`.)

Members report `STATE` so that the server knows when an instance is joinable
and why not; a JOIN into an instance that turns unjoinable simply waits (the
joiner sees "Waiting for the scene to end").

**Record limits.** A tick record holds at most 255 events (a u8 count) and an
event at most 65,535 bytes (a u16 length). The relay keeps one INTENT of a
member open at a time (more are dropped and counted, `intent_dropped`), seals
at most 200 events into one record (the rest follow in the next ticks;
LEAD_FROM is never deferred) and refuses a BARRIER or READY carry above
65,533 bytes (it becomes a JOIN event: slot, point, carry). A record past
these limits broke every member's SEALED frame and the log's compressed block
(round S1 review: 300 INTENT in one burst, a 64 KiB carry).

### 6.6 Mid-stage join

1. At the barrier the server answers JOIN with a slot and STATE_DATA: the
   snapshot of the instance's visit, every tick since, the events and the
   forks in its history with the side that leads to this instance.
2. Before it drops its engine, the client runs its own transition steps (6.7)
   and keeps its own load snapshot's bytes as the *fallback*. Then it keeps its
   player data aside (player slot, bit capsule, record, personal session
   values), restores the snapshot into a fresh engine and replays as an
   observer (`Engine::set_replay(true)`: no pictures, no audio rendering, no
   port events), taking the listed side at every fork; LIVE keeps the newest
   ticks coming. A loading overlay shows the progress and the expected
   duration.
3. Within ten ticks of the newest tick it sends READY with its net fingerprint
   at the last tick that is a multiple of 60 (compared with the members' NETFP
   of that tick), its carry data and its replay rate.
4. The server stamps JOIN(J) (6.4, 6.5). Every machine, the joiner included,
   applies `online_join_player(slot, carry, entrance point)` at the first
   joinable step at or after J: species and weapon packages loaded as for a
   direct start, PLAYER GENERATE + PLAYER2 GENESTAGE at the slot's point of the
   entrance group (`point_matrix(point, slot)`), SQ_ENTR, HUD panels
   i_stpnl01-05, camera mask and leash update, the arrival notice. `first.lua`
   does not run again.
5. From J the joiner sends pads; the server fills them until they arrive.
6. The joiner's music: the snapshot holds the leader's stream position at the
   load step; after the catch-up the joiner seeks every playing stream to the
   position the replayed time implies (presentation only, 6.8).

**When the target moves on.** If the target forks during the catch-up, the
joiner stays with the side that keeps the instance id (6.7). If it passes a
barrier into the same stage again, the joiner becomes a co-arrival of that
visit (new snapshot, empty log). If it leaves for another stage, dissolves or
ends, the joiner restores its fallback and enters as NEW (with its group, if it
arrived as one). A READY mismatch restarts the catch-up once; a second one
falls back the same way. A client never constructs a second engine while one
runs: `Engine::Engine` reseeds the process-wide system RNG
(`engine.cpp:544`, `fight::generator().seed(kBootSeed)`), which would change a
running engine's stream; a debug assertion guards it, and the fallback is
therefore bytes, never a parked engine.

**Catch-up time and limits.** A replay runs the whole simulation step without
pictures. Measured: 4,100-5,400 steps per second for one or two players in
light stages (spike), 2,000-3,600 in field and boss stages (groundwork), about
6,700 for four players in stag0102 without the union pass (critique experiment
E: 0.149 ms per step). The union pass with four cameras adds about
0.15-0.18 ms per step headless (r51cam: 81 µs today, 257-261 µs with four
cameras), so the final path should give about 3,000 steps per second for four
players in a field on the reference machine and 1,000-1,500 on a weaker PC
(estimate; the first task of the S1 cameras lane measures it). Every client
reports its rate (RATE: a hidden 0.5 s calibration replay of a bundled
four-player log at the room join, then every real replay). The server computes
the expected catch-up as `remaining ticks / (rate - 59.94)` (the instance keeps
running) and offers a JOIN only below `Instances:MaxCatchUpSeconds` (default
30). On the reference machine that admits about 25 minutes of a four-player
field log, on a weaker PC about 8-12 minutes; longer stays open a parallel
instance with a notice (3.4). The minute limit of revision 1 goes. There is no
mid-stage state to shorten this: a save state is a load snapshot plus its input
log (`game_main.cpp:2016`), so every catch-up starts at the visit's load.

**Measured on the final path (round S1).** The replay of a save state
(`DW4_STATE_TEST=5400`: a new engine restores the stage snapshot and replays
the 5,399 logged steps with their traversals, headless, as a catch-up does),
four players (`DW4_PLAYERS=4 DW4_LEVEL=200`, `spread4x.txt`: they walk apart
for 6000 frames), the CRT's FMA3 routines, the production union pass
(`DW4_CAMERAS=15 DW4_ONLINE=15`) against the shared camera; the state test
found every replay identical. The save state replay also mixes the music for
its stream positions, which a catch-up leaves out (`set_replay`), so the rate
without the mix is the catch-up rate. Steps per second, the range of two
sequential runs (three with the mix; the machine shared with other rounds,
about half loaded):

| Stage | shared camera, without the mix | 4 cameras, without the mix | 4 cameras, with the mix |
| --- | --- | --- | --- |
| stag0102 (field) | 5,710-5,760 | 2,370-2,390 | 1,790-1,810 |
| dng101b1 (dungeon) | 7,320-7,480 | 2,870-3,330 | 1,980-2,320 |
| stagBS02 (boss) | 10,230-10,650 | 6,640-8,290 | 3,280-4,040 |

A live four-member step costs the same (0.42 ms in stag0102, about 2.5 % of a
core at 60 Hz). The field with four cameras is the slowest case: about 2,400
steps per second on the reference machine, a fifth below the estimate above.
Decisions from it:

- `Instances:MaxCatchUpSeconds` stays **30**. With 2,400 steps per second a
  30-second catch-up admits `30 x (2,400 - 59.94)`, about 70,000 ticks or 19
  minutes of a four-player field log on the reference machine (dungeon about
  23, boss arena about 55); a PC at half that speed about 9 minutes. Longer
  stays open a parallel instance with a notice (3.4); the loading screen never
  waits more than half a minute for a join.
- The client's **initial rate estimate**, before its calibration replay has
  reported (RATE), is **1,200 steps per second**: half the reference machine's
  slowest case, so a fresh client on a weaker PC is not offered a join it
  cannot finish in time; the calibration replaces it within the first second
  in the room.

**The client's calibration and the music position (measured in round S1,
client lane).** There is no bundled log (it would be game-derived data): the
calibration direct-starts a field with four joined players (fast loading),
lets every pad walk a slow circle and attack now and then, and times the
steps after the load, each step as a catch-up step runs it (the step, the
picture's traversal, the music position). `dw4_game --online-calibrate STEPS`
prints it; the room join runs it for 0.5 s before the online engine starts.
On the reference machine (hidden, other rounds running), main's path without
the union pass, 6,000 steps:

| Stage | steps / s | without the music position (`DW4_ONLINE_CALIBRATION=nomusic`) |
| --- | --- | --- |
| stag0102 | 2,731-2,805 | 4,561 |
| dng101b1 | 3,066-3,254 | 5,726 |
| stagBS02 | 4,519-4,646 | 11,598 |

The music position is kept by rendering each replayed step's sound and
dropping it, as the save state replay does (`load_state`); a seek of the
streams afterwards would need a stream seek in the sound manager, which does
not exist. It costs 0.14 ms per step in stag0102, about 40 % of the catch-up
rate. With the union pass's 0.15-0.18 ms on top, a four-player field would
replay at about 1,900 steps per second on the reference machine, not 3,000
(the cameras lane measures the final path). The client reports what it
measured (RATE after the calibration and after every replay), so the
server's offers follow the real path either way; a position-only advance of
the streams (no mixing) is a candidate for round S4's performance lane. Under
load (two clients calibrating at once while four story segments ran) the
0.5 s calibration gave 1,372-2,916 steps per second, so the first estimate
is rough and the replays' own rates replace it.

**Integration (round S1).** The client keeps the music position by rendering
and dropping each replayed step's sound, so a catch-up runs at the cameras
lane's "with the mix" column (about 1,800 steps per second in a four-player
field on the reference machine), not the "without the mix" one. The decisions
above stand: 30 s still admit about 14 minutes of a four-player field log on
the reference machine, and the client's RATE reports what its replays really
reach, so the server's offers follow the real path. A position-only advance
of the streams (round S4) would give the "without the mix" rates back.

**State size.** Measured (spike `r51net`, two and three processes, dng101b1,
stag0101, stagml11): join state 271,387 B after 2,999 steps raw (snapshot
16.4-17.7 KB + 85 B per step with 21-byte pads + events), fingerprints
identical after the replay every time (after the snapshot fix of 7.1); 8,970
steps identical after a join, including a common stage change; with three
processes the second joiner's replay contained the first join. With int8-like
synthetic sticks a four-player log of 20 minutes compressed to about 364 KB
(zstd 3); with the SDL int16 noise the same log compresses about five times
worse (critique experiment C: 20 minutes about 1.1-1.2 MB). Quantized pads
remove that noise, so the int8 figure is the expectation: about 0.4-0.6 MB for
the longest joinable log. The relay lane measures it with recorded real input
first. State transfers have their own limit (`Relay:StateBytesPerSecond`,
default 4 MB/s per connection with a 16 MiB burst), outside the 64 KB/s bucket
of the game frames.

*Measured in S1 (relay lane), `tools/online_test/log_sizes.py`.* Recorded
input from save state files (the snapshot plus every step's pads since the
stage load): the user's own keyboard game (1,492 steps), a campaign bot
driver run with continuous analog steering (30,743 steps, 8.5 min, 32,793
distinct stick values) and a shorter bot run (1,349 steps). Player k of n
replays the recording from offset k·len/n; the log is the relay's tick record
(present mask, event count, 6 B per present slot); sizes per 600-tick block,
each compressed on its own (as the relay stores it), scaled to the duration.
The driver run is the representative one:

| Log | raw | zstd 3 | zlib 6 |
| --- | --- | --- | --- |
| 4 players, 20 min, r48 21-byte float pads | 5.90 MB | 1.18 MB (20.0 %) | 1.22 MB |
| 4 players, 20 min, quantized 6-byte pads | 1.83 MB | 227 KB (12.4 %) | 234 KB |
| same, with +-24 LSB stick noise before the quantization | 1.83 MB | 239 KB (13.1 %) | 244 KB |
| 4 players, 30 min, quantized (with noise) | 2.74 MB | 341 KB (359 KB) | 351 KB |
| 2 players, 20 min, quantized | 0.98 MB | 111 KB | 112 KB |
| 1 player, 20 min, quantized | 0.56 MB | 54 KB | 53 KB |

The keyboard game compresses to 0.6-0.9 % (three stick values); the short
bot run to 6-8 %. Quantized pads make the log 5.2 times smaller than the r48
pads (the critique's 5x), and the controller noise of an int16 axis barely
survives the GameCube's steps (+5 %). The longest joinable log (30 min, four
players) is about 0.35 MB compressed and 2.7 MB raw; with the snapshot
(19.4-20.2 KB raw in these files) a join state stays below 3 MB raw.
Consequences built on in S1: the relay keeps its log in memory as 600-tick
blocks compressed with Deflate (zlib 6 is as good as zstd 3 on these blocks
and is in .NET; about 240 KB per 20-minute four-player visit); STATE_DATA
sends the tick records raw (codec 0; the client has only a zstd decoder and
.NET no zstd encoder), which at the default `Relay:StateBytesPerSecond`
(4 MB/s, 16 MiB burst) is under one second for the longest log and well
inside the burst; the snapshot travels as the leader uploaded it.

The spike proved spawning only for a joiner of the same species with its
package already loaded; other species (packages mid-stage, the scheduler's
"next frame" registration), the `q_` packages from three players up, the HUD's
SQ_OPEN, the quick panel and the item label line are round S1 work.

### 6.7 Barrier and fork

**Barrier tick.** The tick B whose step executed NEXTSTAGE (`pending_stage()`
not empty afterwards; STAGECTL NEXTSTAGE `managers_core.cpp:837-866`) is the
last tick of the visit. Every member parks after B and sends BARRIER. NEXTSTAGE
is simulation, so all members of a sealed instance park at the same B. The
server stops sealing the old visit at the first BARRIER it receives; ticks it
had already sealed after B are discarded by every member and by the log.

A single BARRIER (a desync, or a forged one) therefore stops the sealing for
every member of the instance until `Instances:BarrierReportSeconds` (5 s)
ends the wait: their pictures freeze that long (measured in the round S1
review: 5.17 s), then the reporters leave as a group of their own (NEW) and
the others play on (LEAVE, LEAD_FROM). A BARRIER that names an instance the
server does not have the member in (a straggler whose group was decided
without it, a client whose own five-second timeout went on as NEW, a forged
one) arrives alone and first takes the member out of the instance the server
still had it in (LEAVE when sealed); a BARRIER while the server still parks
the member, or while it catches up on a target the BARRIER does not name,
ends that wait. (Before: the old slot stayed filled, one member made 20,001
instances in 83 s, an honest arrival was sent to such a zombie to join a
leader that played elsewhere, and the housekeeping's index -1 stopped every
room's pings.)

**Transition steps.** The steps from B to the load step (the module reload's
step with `reset_modules`, its held fields with `console_reset_.hold`, the load
step with `pads_.update` and `begin_stage_load`; `engine.cpp:1843-1914`) take,
for every present slot, the pad that slot had at B, and the neutral pad for
absent slots. They consume no ticks and are the same on every machine of the
group without any message, for solo groups too. The load step's pads go into
the snapshot (`engine.cpp:698-699`), so the rule makes the snapshots of all
members equal. (In the original the held fields do not read the controllers at
all; a button held through the load stays held, as here.)

**Application point.** BARRIER_OK's contents are handed to the engine while it
is parked (`Engine::set_barrier`), and the engine applies them in the load
step right before `begin_stage_load`, after `reset_modules` and the held
fields, in this order: the story merge and the removal of dropped values
(8.5, 8.6), the membership (DISSOLVE: despawn the others, move the own player
to slot 0, write back the personal session values; NEW: the slot map and the
co-arrivals' carries as joined players), the online rules for the new stage.
(At the park point itself `reset_modules` has not run yet; it runs in the step
after B.) The fade before NEXTSTAGE has already darkened the screen, so the
park (about one round trip, or a hold, 3.4) shows as the loading screen.

**Tick 0.** The new visit's snapshot is the one `begin_stage_load` takes in the
load step. Tick 0 of the new visit drives the first step after the load step;
for NEW the leader uploads the snapshot and every member sends its canonical
hash right after its load step (6.8); co-arrivals restore the uploaded snapshot
and then send the hash of what they restored. Each member of a sealed NEW
submits its pads for ticks 0..d_i-1 at once when its hash is out (as the
direct sessions do at their start). The server places `S(0)` when every
member's hash is in, plus the largest round trip and two ticks, so a slow load
on one machine does not cost the others their first inputs. The net
fingerprints at tick 0 are compared as well. S1 checks the indexing
against the save state's input log (`write_state_file` stores the snapshot
with the log of the steps after it); whichever way it falls, the load step's
pads are the pads of B.

**Fork.** Decided inside the simulation by the online overrides (exit, gate
disc, story or boss participants, give up) at step F, so every machine knows the
two sides without a message. Each copy despawns the other side's players at the
end of step F and from F+1 needs only its own side's pads. Every member sends
SPLIT(F, mask). The server forks when every connected member of the instance
reported the same fork, or after `Instances:ForkConfirmMs` (2 s) when members
of both sides did; F must be a sealed tick at most 600 ticks back. A single
member's SPLIT moves nobody (round S1 review: one forged SPLIT moved its
partner into a child its engine never forked); a disagreement is a desync.
Until the fork, both sides still execute the instance's sealed ticks (each
copy already despawned the other side's players, so it plays only its own
side's pads), and the fingerprints from F on are not compared.

- **Side rule.** A side without a living member does not stand alone: its
  members go with the other side (the dead travel with the living, as
  `funcStageChange` and the gate disc take defeated players along). When both
  sides have living members, the dead stay with the stayers. Only *Give up*
  makes a side of dead members on purpose. Without this rule a dead member
  left alone would get the game over at once (`model_ctrl.cpp:2246-2262` runs
  `end_g_000bcaps.lua` on every update while no player is generated).
- **Which side keeps the instance.** Exit-type forks (exit, gate disc, give
  up): the stayers keep it; the leavers become a `departing` child (same stage
  and visit, never offered for joins) until their barrier. In-stage forks
  (*Go without them* at a boss or story mat, a story trigger): the
  participants keep it, boss-locked while their boss lives; the others become
  a new child instance of the same stage that is joinable at once.
- **Replays.** Every replay ends in one instance. STATE_DATA lists the forks
  in that instance's history since its snapshot (F, leaver mask, the side that
  leads to the instance), and the engine takes that side at each F
  (`Engine::set_fork_sides`), for observers, joiners, resyncs and resumes
  alike. A child's history is its parent's snapshot and log up to F plus its
  own ticks; the server keeps the shared part once.
- **Pending events.** Events stamped for the instance before the server knew
  of the fork: JOIN goes with the side that keeps the instance id; LEAVE and
  INTENT go with the side of their slot; every copy ignores events for slots
  that are not on its side. The server re-stamps what belongs to a child into
  the child's stream.

### 6.8 Snapshot and log on the server

Per logged instance, in memory: the stage snapshot (about 17 KB raw, 3.3-4.8 KB
compressed), the ticks since (pads of present slots plus events), compressed in
blocks of 600 ticks, the fork list, and the members' fingerprints. A new visit
replaces them; the instance's end drops them (a child keeps the parent's part
it needs). A restart of the server ends every room (the r48 sweeper), so
nothing is persisted.

**Canonical form.** The snapshot is not pure simulation state: a stream's
`frames_read` (`managers.hpp:453`, "audio only, never consulted by the game")
advances only in the audio mixer (`managers_core.cpp:1745`), which the audio
device's queue paces (`game_main.cpp:2733-2736`) and which never runs without a
device. A BGM plays on into the next stage, so two machines with the same
simulation state write different snapshot bytes at almost every group stage
load (critique experiment A: three bytes, 1,348,800 against 0). Hashes and
comparisons therefore use a canonical form: `Engine::snapshot_bytes(canonical)`
writes zero for every field marked presentation-only (today `frames_read`;
the S1 check below finds any other). The uploaded snapshot keeps the
leader's real values, which a restoring machine uses only for its music
position (6.6).

**What a mismatch triggers.** The leader uploads the snapshot after the load
step, the others send the canonical hash. A canonical mismatch (or a net
fingerprint mismatch at tick 0) is a desync vote like any other (6.9). The S1
membership lane checks the canonical form first: headless, with audio dump and
in a hidden window, the canonical bytes are equal at every stage load of the
r25c story run and the raw bytes differ only in presentation fields.

**Measured (round S1).** All ten segments of the r25c story run, each
headless and with the audio mixer running (`DW4_AUDIO_MIX=1`: the mixer
renders every step's samples and drops them, as with a device), and the
segments `new` and `k8` also in a hidden window that draws every picture
(`DW4_RENDER=1` with the mixer), each process dumping every stage load's
snapshot (`DW4_SNAPSHOT_DUMP`; `tools/online/canonical_check.py`): every
segment reached its checkpoint at the reference frame in every mode; the
canonical bytes are equal in all modes at all 193 stage loads (54 of them in
three modes); the raw bytes are equal at 35 loads and differ at 158 in 2-4
bytes each (502 in all), every one of them inside a stream's `frames_read`,
the field the canonical form zeroes. No other presentation-only field
exists. The mixer and the hidden window give byte-equal raw snapshots (the
smoke runs feed the mixer at the simulation's pace), so only a machine
without audio output differs. The SNAPHASH is
`online_hash(snapshot_bytes(SnapshotForm::canonical))` (FNV-1a, 64 bit).

A solo group that went on without the server (3.4) uploads snapshot and log
when it reconnects.

**Limits of the store (round S1 review).** A solo member steps at the game's
pace, so its LOG may run at most 2 % plus `Instances:LogLeadTicks` (600) ahead
of the wall clock since its visit's BARRIER_OK; more is a protocol fault (a
forged LOG wrote 860,400 ticks in 60 s). Only the member that leads the solo
stretch logs it, also the ticks before J after JOIN_AT (a joiner's LOG wrote
its own pads and slot into the leader's history). A solo log stops at
`Instances:MaxVisitHours` (12 h): further LOG is dropped and the instance
takes no joins. RATE and READY's rate are capped at `Instances:MaxReplayRate`
(20,000 steps/s), so a forged rate cannot buy a join on a long log. A fork's
child shares the parent's snapshot also while it still uploads: whichever side
the uploader went to completes it for both (before, a child forked before the
upload finished never became joinable). A sealed visit's member whose
canonical hash has not come after `Instances:SnapHashSeconds` (60 s) leaves
it by LEAVE, and sealing starts without it; so does a member that goes before
tick 0 (a co-arrival every engine spawned in its load step), and a lost one
no longer holds the others' start (the reconnect window runs from tick 0).

**State transfers outside the room lock.** A STATE_DATA (join, co-arrival,
resync) is captured under the room lock (the compressed blocks are never
changed once made, the open block is copied, `TickLog.Capture`) and built on
the thread pool; nothing of the stream is pumped to the member until it went
out, so the ticks follow it in order. The sealing thread does not wait for a
busy room (`Monitor.TryEnter`; that room seals its due ticks a millisecond
later in one batch), and the room API answers a busy room with the last
housekeeping pass's view. (Round S1 review: a join of a 523k-tick log held a
sealed pair in another room up to 53 ms, a join every 2 s up to 153 ms.)

### 6.9 Fingerprints and desync

Every member sends a *net fingerprint* every 60 ticks: `Engine::fingerprint`
plus the RNG state, rotations, the sound manager's random value, `OnlineRules`
(present, leaving, camera mask, boss lock), every slot's `PlayerBox` state and
the session context hash. It is a separate function, so
`Engine::fingerprint` (and with it the det reference) stays unchanged. (Equal
coarse fingerprints prove little on their own: in r51cam every cull variant gave
the same 47549cdd for stag0102.) The server compares per tick:

- A majority (two or more equal) wins. With two members the longer member wins.
- The others get RESYNC: they rebuild by replay from snapshot and log (6.6
  rules, with the fork sides), keep their slot, and the server fills their
  pads meanwhile; the member sees the progress and the expected duration.
- **Limit.** A resync whose expected catch-up exceeds
  `Instances:MaxCatchUpSeconds` is not attempted: the server stamps LEAVE for
  the member's slot and its machine continues the stage alone from its own
  state (it despawns the others, a solo instance from then on) with "Lost sync
  with the group - you continue alone; you meet again at the next stage". The
  same happens after three resyncs of one member within ten minutes. The
  server keeps both fingerprint lines and the last ticks for diagnosis.
- A mismatch at READY restarts the catch-up once, then falls back (6.6).
- **One vote per tick.** Only sealed ticks count, every tick is voted on once,
  a member is resynced at most once at a time and once per
  `Instances:ResyncCooldownSeconds` (10 s; further lost votes meanwhile are
  only counted), and the relay does not lag-remove a member while it catches
  up. A resync ends with the member's READY: no JOIN follows (it never left
  the instance), the client goes back to sending its pads from the tick it
  reached (`Session::finish_resync`; a replay that reaches the group's stage
  change parks with the group). (Round S1 review: the client waited for a JOIN
  as an observer for good and was lag-removed; 20 NETFP per second for one
  tick sent 12 states of 2.4 MB in a second.)

### 6.10 Lag, disconnects, reconnect

- A lagging member (6.4) gets a connection icon on its name tag on the other
  machines; after `Relay:LagLeaveSeconds` of continuous filling the server
  stamps LEAVE.
- A closed connection: the slot is filled for `Relay:ReconnectSeconds`
  (default 10), then LEAVE.
- The disconnected client freezes its picture with "Connection lost,
  reconnecting..." (a sealed member cannot run unsealed ticks; a solo-led
  member plays on until its next barrier). It fetches a new ticket and sends
  RESUME with its last executed tick: still in the instance, its engine is
  still the one it had, so it gets only the missing ticks (at most the
  reconnect window plus the time since, a few seconds of replay). Already
  removed, it continues alone from its engine as a new solo instance, like a
  member whose resync is too long (6.9); its progress after the disconnect
  never reached the shared engine, so nothing is lost or doubled. A client
  that lost its engine (a crash) enters the stage again through the title.
- After the room's member timeout (35 s) the server removes it from the room;
  the client returns to the title with a message. The online character keeps
  its last upload.
- RESUME is taken once per connection, only after a resumable WELCOME, and
  only for a reconnect's gap (at most `Instances:MaxResumeTicks`, 3,600 ticks,
  before what the member was sent); the ticks go out under the state budget.
  (Round S1 review: 20 RESUME per second resent a 523k-tick log, 272 MB in
  15 s.) A connection that ends without BYE releases its address's slot at
  once (before, the relay's writer waited for good: after
  `Relay:MaxConnectionsPerIp` crashes or lost networks the address could not
  connect until a restart).
- A solo leader that does not answer JOIN_REQ is asked again every five
  seconds, `Instances:JoinRequestTries` (3) times; then the joiner plays the
  stage as NEW ("Joining ... failed").

### 6.11 Instance host and room owner

There is no instance host any more. The leader is the lowest present slot,
which matters only for the pad redirect fallback and for who uploads the
snapshot; leaving is an ordinary LEAVE. `EndInstanceLocked` on host loss goes
away. The room owner leaving the room keeps the r48 behaviour (the room runs
on, the owner's place stays reserved, the story follows the owner-away setting,
8.6).

### 6.12 Versions and determinism across machines

- Protocol and snapshot version change with every simulation change (they go
  to 43 with the first round). `GET /status` gives the minimum and current
  protocol; an old client shows "Update required" before the room list.
- The data hash covers archive counts, logical paths and mod file names and
  sizes, not mod contents (`game_main.cpp:486-516`); round S4 adds contents.
- **No FMA3 switch.** Revision 1 planned `_set_FMA3_enable(0)` for online
  sessions. That turns `std::fma` in the static CRT into a software routine
  (critique experiment D: 2.57 ns against 26.59 ns per call, bit-identical
  results), and the simulation uses `std::fma` throughout (about 110 per node
  test in `frustum_cull_code`, the Gekko math, 33 sites in the animation math,
  collision, wall slides): stag0102 with four players went from 0.149 to
  0.436 ms per step (experiment E), with no fingerprint change on this CPU.
  `std::fma` is correctly rounded on every path and stays.
- **What does differ** between the CRT's FMA3 and non-FMA3 paths are the libm
  routines (experiment D2, one million inputs: acosf 2,451, double sin 11,090,
  cos 10,900, acos 5,443, exp 1,511, pow 370, atan2 44 different results).
  About 25 host libm call sites are on the simulation path:
  `math.cpp:51/62` tan / atan (the projection, and through it the cull),
  `enbaya.cpp:144` acos, `skinning.cpp:316-318` double sin / cos,
  `model_ctrl.cpp` (11), `lua_host.cpp` atan2, `particles.cpp`,
  `managers_core.cpp` sin / cos / pow, the Lua 4 VM's pow.
  - **Until round S4**: online needs a CPU on which the static CRT takes its
    FMA3 routines. The client checks it at the room join and refuses with
    "Online play needs a CPU with FMA3 (Intel 2013 / AMD 2015 or later)".
    With a static CRT (/MT) every machine then runs the same code.
  - **The exact condition (measured in round S1).** The source of the math
    library is not part of the UCRT source drop (`ucrt/initializers/
    fma3_initializer.cpp` only registers `__acrt_initialize_fma3`), so the
    client lane disassembled `fma3_available.obj` and the dispatchers from the
    static `libucrt.lib` of the Windows SDK 10.0.26100.0 that the build links
    (`lib /extract`, `dumpbin /disasm`). `__acrt_initialize_fma3` stores
    `__use_fma3_lib` = 0 unless CPUID.1:ECX has FMA (bit 12), OSXSAVE (27)
    and AVX (28) and XGETBV(0) has the XMM and YMM bits (`& 6 == 6`); then 1,
    and 3 when CPUID.0 reports leaf 7 and CPUID.(7,0):EBX has AVX2 (bit 5).
    The routines test it in two ways: the assembler routines (`sin`, `cos`,
    `tan`, `exp`, `log`, `fmod`, `sinf`, `cosf`, `tanf`, `expf`, `logf`,
    `powf`) take their FMA3 path when it is nonzero, the C routines compiled
    twice (`acos`, `asin`, `atan`, `atan2`, their float versions, and `pow`
    from the ARM optimized routines) only when `(value & 3) == 3`. A CPU with
    FMA3 but without AVX2 (AMD Piledriver / Steamroller) therefore runs a
    mixed set that matches neither path. The gate wants exactly 3 (FMA3, AVX,
    OSXSAVE, the OS's YMM state and AVX2) and `_get_FMA3_enable() != 0`;
    the static build reads the CRT's own `__use_fma3_lib`, and a test checks
    that the cpuid replication (`src/game/online/cpu_gate.cpp`) gives the same
    value (3 on the reference Ryzen 7 7800X3D).
  - **Round S4** routes those call sites through `dw4::sim` wrappers that call
    the MSL routines in online sessions (`include/dw4/msl_math.hpp` has sin,
    cos, asin, acos, atan, atan2; tan, pow and exp are added; float callers go
    through double as the original's MSL did) and the host libm offline, so the
    offline det reference stays. The switch is process-wide and set at the
    online session start. Then the FMA3 gate goes; S4 re-measures cost and
    catch-up on that path and gives the user a two-machine test kit.
- **Floating point environment.** Nothing pins MXCSR today (no `_controlfp`,
  `_mm_setcsr` or `fesetround` in `src` or `include`), and the simulation runs
  on the main thread between GL calls, SwapBuffers and the hooks of injected
  overlays (Discord, Steam, RTSS, OBS). Every simulation entry (Engine::step,
  the simulation traversal, the replay loop) saves MXCSR, sets 0x1F80 (round
  to nearest, no FTZ / DAZ, exceptions masked) and restores it afterwards, a
  RAII guard of a few nanoseconds. It runs offline too: with the default MXCSR
  it is a no-op (det proves it), with a changed one it restores the original
  behaviour. A counter of entries that found another value goes into the net
  fingerprint diagnostics.
- **Environment variables.** The engine constructor reads DW4_GOD,
  DW4_ONE_HIT and DW4_INFINITE_MP into `cheats_` (`engine.cpp:538-540`); the
  engine also reads DW4_FIXED_RANDOM (`combat.cpp:61`), DW4_CLOCK_FILE
  (`engine.cpp:1801`), DW4_DISC_CLOCK and DW4_AX_PHASE
  (`managers_core.cpp:2520-2525`), DW4_LEVEL, DW4_BITS, DW4_DEVICES, DW4_DISKS,
  DW4_FORMS, DW4_XDATA, DW4_FLAG_FOLDER and DW4_CARD_RECORD. Online, such a
  value would desync silently or ride along in a new instance's snapshot (a
  restore copies `cheats_`, `engine.cpp:729`). Online: the session start
  refuses with a message naming the variable if any simulation variable is
  set (one list, `online_forbidden_env`; a test greps `getenv("DW4_` in `src`
  and fails for a variable that is neither on it nor marked presentation-only),
  and after every online engine construction and restore `cheats_` is set from
  the room settings. The settings hash (6.2) covers the rest.
- One deterministic engine per process (the system RNG is a static,
  `combat.cpp:36-39`, reseeded by every engine construction): a client never
  constructs a second while one runs (6.6). Moving the RNG into the engine
  stays optional.

## 7. Engine interface

Everything below exists only with `Engine::online().enabled` (or a nonzero
camera mask). The signatures below are the final ones of round S1 (pinned by
the integration; `include/dw4/engine/online.hpp`, `online_membership.hpp`,
`engine.hpp`, `managers.hpp`); later rounds extend them.

### 7.1 Snapshot completeness (also an offline fix)

`serialize_system` (`src/engine/save_state.cpp:141-153`) misses
`GameSystem::direct_start` and `text_call`; a state restored from bytes
(file, network) instead of in-process diverges: main "differs from frame
2001", the spike "identical after loading". Add both, bump `kSnapshotVersion`,
add a test "restore from bytes == in-process restore". The CLI guest of a
`--host --stage` session must set `direct_start` like the host
(`game_main.cpp:657-658` vs 721ff.); the spike desynced at step 720 without it.
Both also repair offline save state files and the direct test sessions; neither
changes `det.sh`. The netcode critique checked the rest of `serialize_system`
(PlayerSlot, Stats, BitChip, Cheats, ItemDevice, OriginalPads, ConsoleReset and
CriStreams complete) and found no other mutable engine statics; it did not
audit every manager's state across long multi-stage sessions, so the S1
membership lane runs a cross-process join after a long campaign session (bytes
equal to the in-process restore after at least 20 stage loads, fingerprints
equal for 10,000 steps after it).

**Built and measured (round S1).** `serialize_system` writes both fields,
`kSnapshotVersion` is 43, the ctest `online` checks the bytes, and the CLI
guest of a `--stage` host starts directly. The state test through a file on a
direct start with attacks after the save point (`DW4_STATE_TEST=2000`,
`DW4_STATE_FILE`, `--stage dng101b1`, the spike's input): main of the day
"differs from frame 2001", round S1 "identical after loading".

The cross-process join after a long session: the k8 segment of the r25c
story run (28 stage loads, 192,599 frames, the campaign driver) wrote a
state file at every stage load (`DW4_STAGE_STATES`), the pads of every step
(`DW4_PAD_LOG`) and the fingerprint with the generator's hash at every frame.
A second process loaded the state of load 21 (dng310f3, frame 154,813) and
of load 22 (stagbs03, frame 168,823), replayed the first process's pads
(`DW4_PAD_REPLAY`) to the end of the session and traced the same: the
fingerprints and generator hashes are equal at every one of the 37,787 and
23,778 frames, and the canonical and the raw bytes of all seven (six) later
stage loads - the boss stage, the lobby, the save room - are equal to the
first process's. The memory card each process wrote in the save room differs
in one byte, the GCI header's modification time from the host clock (offset
0x2B), which the game never reads back; online every member has its own
virtual card anyway.

### 7.2 Online rules

Built in round S1 (membership and cameras-exits lanes, joined by the
integration into one implementation: `src/engine/online.cpp`,
`online_cameras.cpp`, `online_exits.cpp`).

```cpp
// include/dw4/engine/online.hpp (included by engine.hpp)
struct OnlineRules {
    bool enabled = false;            // overrides, despawn, pad redirect, input masking
    std::uint8_t player_cameras = 0; // bit per slot with its own box camera; 0 = shared camera (original)
    bool leash_off = false;          // limit_Circle child hit off
    bool scoped_scenes = false;      // proximity rule for local scenes and the status window (round S3)
    bool near_share = false;         // EXP shared by members near the kill (round S3, question 7)
    std::uint8_t present = 0;        // instance members (bit per slot)
    std::uint8_t leaving = 0;        // members with a pending LEAVE
    bool boss_lock = false;          // a boss fight runs (set by boss_mat / boss_intro, cleared by boss_end and barriers)
    bool operator==(const OnlineRules&) const = default;
};
struct PersonalSession { std::optional<std::string> gate_stage_name; std::int32_t gate_stage_no; std::map<std::int32_t, std::uint8_t> flags; };
enum class ForkKind : std::uint8_t { exit = 0, in_stage = 1, give_up = 2 };
struct ForkSide { std::uint64_t frame; std::uint8_t leavers; bool take_leavers; };  // `leavers` names either side (below)
struct SplitResult { std::uint64_t frame; std::uint8_t leavers, kept; bool in_stage, local_leaves; std::uint8_t child; };
enum class IntentKind : std::uint8_t { go_without = 0, give_up = 1 };
enum class Decision : std::uint8_t { new_instance = 0, join = 1, dissolve = 2, co_arrive = 3 };
enum class SnapshotForm : std::uint8_t { raw = 0, canonical = 1 };
struct ExitHeading { std::uint64_t frame; std::string stage; int point; std::uint8_t leavers; };   // HEADING's data
struct ExitPromptView { bool active; int owner = -1, asking = -1; std::uint8_t yes, stays, sees_fade; std::string stage; bool boss_destination; };
constexpr int exit_prompt_owner(std::uint32_t current, std::uint32_t previous, std::uint32_t living, std::uint8_t present);  // 5.2, -1 none
constexpr std::uint8_t exit_leavers(std::uint8_t present, std::uint8_t living, std::uint8_t going);  // the side rule of 6.7
Cheats online_cheats(const Cheats& room);              // no cheats, no rebalancing; randomizer, drops, fast loading kept
const SessionValueTables& session_value_tables();     // 3.7, equal to data/online/session_values.txt (ctest)
const std::vector<std::string_view>& online_forbidden_env();    // 6.12; the one list (the client's env check delegates to it)
const std::vector<std::string_view>& online_presentation_env(); // the rest of the program's DW4_* names ('*' = prefix)
std::vector<std::string> online_forbidden_env_set();
std::uint64_t online_hash(std::span<const std::uint8_t>); // FNV-1a 64; SNAPHASH = online_hash(snapshot_bytes(canonical))
std::uint64_t net_fingerprint_cameras(const Engine&);  // CameraManager::player_box_hash()

const OnlineRules& Engine::online() const;
void Engine::set_online(const OnlineRules&);   // only between steps; on: room cheats, console reset off; then the camera, leash and Lua side
std::uint8_t Engine::player_cameras() const;   // OnlineRules::player_cameras
bool Engine::online_shared() const;            // online with two or more present members
void Engine::set_view_slot(int slot);          // presentation only: the local member's slot (0..3)
int Engine::view_slot() const;
int Engine::picture_slot() const;              // the camera render() draws: the view slot, the nearest living member's while defeated
void Engine::set_fork_sides(std::span<const ForkSide>);  // replays: the side to take at each fork
void Engine::set_split_identity(int slot);     // live play: the local slot for forks (-1: the side that keeps the instance id)
int Engine::split_identity() const;
static int Engine::live_engines();             // an engine constructed while an online one lives throws std::logic_error
static std::uint64_t Engine::mxcsr_repairs();  // simulation entries that found another MXCSR control word
// managers.hpp, CameraManager (per-slot box cameras, 4.1)
void CameraManager::frame_camera(Engine&, FrameCamera&, int slot);
void CameraManager::set_player_cameras(Engine&, std::uint8_t before, std::uint8_t after, std::uint8_t heirs);
std::uint64_t CameraManager::player_box_hash() const;
const Vec3f* CameraManager::player_box_centre(int slot) const;
```

**One implementation (integration).** `set_online` and every membership
step (join, remove, fork, barrier: `membership_changed`) end in
`online_rules_changed(before)`: `apply_online_cameras(before)` hands the
camera mask over (`set_player_cameras(before.player_cameras,
after.player_cameras, heirs = before.present)`: members that played with
the shared camera keep its box state, new ones start with SETBOXCAMERA's
first run; at a barrier the boxes were cleared with the module reset, so
every member starts fresh), `apply_leash_rule()` switches `limit_Circle` off
while `leash_off` (it also runs in every step, after the demo update, for
the ring a stage load brings back), and the online Lua globals are
reinstalled when online or the sharing changes (the `funcStageChange` port
only while `online_shared()`).

`OnlineRules` and the present slots' `PersonalSession` values are part of
the StageSnapshot (a joiner restores them) and of the net fingerprint, not of
`Engine::fingerprint`. Defaults are all off. The membership calls maintain
`present`, `player_cameras` (every present slot from two members on; back to
one keeps `{its slot}` until the next load; a visit that starts with one
member has 0) and `leash_off` (from two members on until the next load; the
engine switches HitLand's `limit_Circle` off at once and again after every
load), so every machine changes them at the same step; the cameras lane
reads them. Every online step switches the console reset off again, the
STAGECTL soft reset does not arm online, and `restore` of an online snapshot
goes through `set_online` as well. The MXCSR guard compares the control bits
(6-15) only: bits 0-5 are the sticky exception flags any arithmetic sets.

### 7.3 Membership, barriers, forks

```cpp
std::optional<std::string_view> Engine::pending_stage() const;  // read-only: a NEXTSTAGE waits for its load step
bool Engine::membership_safe() const;   // pad input on, no demo record, no SUBWND program, not loading, presenting, no fork pending
bool Engine::joinable() const;          // membership_safe, not a hub or UI stage, no boss lock
static bool Engine::hub_stage(std::string_view);
std::uint64_t Engine::session_context_hash() const;  // FNV-1a over the context values, absent = 0
PersonalSession Engine::personal_session(int slot) const;
// online_membership.hpp
struct PlayerCarry { GameSystem::PlayerSlot slot; GameSystem::BitChip chip; PersonalSession personal; std::vector<std::uint8_t> record; };
struct RoomStory { std::array<std::uint8_t, 1024> flags; std::array<std::uint8_t, 12> folder; std::array<std::int32_t, 16> counters; std::uint32_t revision; };
struct StoryKeys { std::vector<std::int32_t> flags, folder; };
struct BarrierResult { Decision decision; std::array<int, 4> slot_map; std::vector<std::pair<int, PlayerCarry>> co_arrivals;
                       int own_slot; std::optional<RoomStory> story; StoryKeys dropped; OnlineRules rules; };
std::vector<std::uint8_t> encode_carry(const PlayerCarry&);     PlayerCarry decode_carry(std::span<const std::uint8_t>);
std::vector<std::uint8_t> encode_barrier(const BarrierResult&); BarrierResult decode_barrier(std::span<const std::uint8_t>);

PlayerCarry Engine::carry_player(int slot) const;
void Engine::online_join_player(int slot, const PlayerCarry&, int entrance_point);  // before a step, when joinable()
void Engine::online_remove_player(int slot);                                        // before a step, when membership_safe()
void Engine::online_intent(int slot, IntentKind, int argument);                     // before a step (give_up forks at once)
bool Engine::online_split(std::uint8_t mask, ForkKind kind);  // inside a step (NETsplit, the exit port, the gate disc)
std::optional<SplitResult> Engine::take_split();   // after a step; the copy's side is already applied
std::optional<ExitHeading> Engine::take_heading(); // after a step: an exit this copy takes (the client's HEADING)
const ExitPromptView& Engine::exit_prompt() const; // the exit port's state (texts, the picture filter)
void Engine::picture_hidden_roots(std::vector<const Node*>&);  // presentation: other members' item lines and exit prompts
void Engine::set_barrier(const BarrierResult&);    // while parked after the barrier tick; applied in the load step before begin_stage_load
bool Engine::barrier_set() const;
void Engine::apply_room_story(const RoomStory&);   // at game_started only (barriers go through set_barrier)
std::vector<std::uint8_t> Engine::snapshot_bytes(SnapshotForm) const;  // raw or canonical (presentation fields zeroed)
void Engine::set_replay(bool);                     // catch-up: no port events (the host renders no audio)
std::uint64_t Engine::net_fingerprint();           // fingerprint + generator + rotations + sound random + rules + personal + PlayerBoxes + context
void Engine::set_boss_lock(bool);                  // round S3 wires boss_mat / boss_intro / boss_end
int Engine::leader() const;                        // the lowest present slot that is not leaving
int Engine::trigger_slot() const;  class Engine::TriggerScope;  // 5.1: mats, hits, the gate disc; demo works and posts keep it
int Engine::pad_reader_slot(int index, int trigger) const;      // 7.4
void Engine::online_gate_stage_set();              // SYSsetGateStage ran: the present slots' personal GATESTAGE follows
```

**Fork sides and the relay (integration).** `SplitResult::child` is the side
that leaves the instance id (exit and give up: the leavers; in-stage: the
others): the relay's SPLIT and its fork lists name that side. A
`ForkSide::leavers` may name either side; the engine matches it against the
continuing side or the child side of the fork at that frame and takes the
named side when `take_leavers`. `ForkSide::frame` is the engine's frame after
the fork's step: tick t of a visit runs in the step after the load step's
frame plus t, so the client converts a fork list's tick with `load frame + t
+ 1` (`GameEngineLink::set_fork_sides`). The exit port and the gate disc call
`online_split(mask, ForkKind::exit)`: `present` narrows at once (the
departure's records cover only this copy's side) and the other side's
players go at the end of the step.

**The client's side.** The DW4R2 session drives the engine through
`dw4::game::online::EngineLink` (`src/game/online/engine_link.hpp`; the game's
`GameEngineLink` in `engine_adapter.cpp`, the tests' fake engine in
`tests/online_fake_hub.hpp`): the facts after a step (stage, pending stage
and point, load serial, snapshot bytes, net fingerprint, membership-safe,
joinable with the STATE reason, boss lock, context hash, alive and present
masks, carry, `take_split` as the relay's child side, `take_heading`,
presence) and the changes between steps (replay steps with the music
position, restore into a fresh engine, replay mode, `start_online`, split
identity, fork sides, `set_barrier`, join, remove, intent, view slot).

How they behave (proven by `tests/online_tests.cpp`, ctest `online`):

- **Input.** An online step masks the pads (absent slots neutral and
  disconnected, leaving slots neutral and connected) and, from the barrier
  tick to the load step (`pending_stage()` set), gives every present slot the
  pad it had at the barrier tick (the session's first stage: neutral,
  connected). The barrier tick's pads are latched at the end of the step
  that executed NEXTSTAGE.
- **Join.** `online_join_player` writes the carry into the slot (joined),
  its capsule and personal values, loads `_common/<species>.igb` (the `q_`
  variant when the stage loaded `q_` packages) and the weapon packages unless
  the stage has them, then (in a `Messenger::PortOnly` scope) PLAYER GENERATE
  with the matrix of `point_matrix(entrance_point, slot)`, PLAYER2 GENESTAGE
  (its capsule left in this stage), SQ_ENTR, the HUD panels i_stpnl01-05
  GENERATE at the slot's x and their SQ_OPEN / name / SQ_OPENWN / SQ_WAIT.
  `first.lua` does not run again.
- **Remove.** PLAYER REMOVE, PLAYERARMS REMOVE, NDMWPANEL REMOVE (quick
  panel), the HUD panels' REMOVE, the slot's capsule REMOVE, joined off,
  `bit_chips[slot]` cleared, NINZU recounted; the item label goes by itself
  (its owner no longer answers). Calls for slots that are not present are
  ignored (events for the other side of a fork).
- **Fork.** `online_split(mask, kind)` applies the side rule (6.7: a side
  without a living member goes with the other; with living members on both
  sides the defeated stay with the stayers; `give_up` keeps only defeated
  members in the mask), returns false when nobody goes on, true without a
  fork when everyone does, else decides this copy's side (the fork side list
  for that frame and mask, else the split identity, else the side that keeps
  the instance: the stayers for `exit` and `give_up`, the mask for
  `in_stage`), narrows `present` to that side at once (a SYSsetGateStage
  later in the same script run sees only that side) and despawns the other
  side's players at the end of the step. `ForkSide.frame` is
  `SplitResult.frame`, the engine's frame after that step.
- **Barrier.** `set_barrier` while parked; the load step applies it right
  before `begin_stage_load`: NEW remaps the slots (`slot_map[old] = new`,
  -1 not in the group), writes the co-arrivals' carries as joined players
  (so `boot.lua`, `funcPlayerSet` and the entry scene see them, before any
  stage script), moves the lowest joined slot to 0 if 0 is free, sets the
  split identity to `own_slot`; DISSOLVE keeps only `own_slot`, moves it to
  slot 0 and writes its personal values back; JOIN and CO_ARRIVE leave the
  load step alone (the client drops the engine after it). `rules` supplies
  the room switches; present, cameras, leash and boss lock follow from the
  membership. The room story merge of round S2 goes first (S1 carries
  `story` and `dropped` but does not apply them).
- **Personal values.** From two present members on every present slot has its
  own `PersonalSession` (the base member's taken from the engine-wide values
  at the first join), so a member's gate disc return and lobby flags never
  come from another member (3.7).

Lua host functions registered only online: `NETsplit(mask [, kind])` (kind
0 exit, 1 in-stage, 2 give up; returns 1 when this copy goes on with the
script run), `NETisOnline()`. The `funcStageChange` port and the
`run_script` wrappers (5.1) call `Engine::online_split` directly.

### 7.4 Pads: redirect, resets

- **Redirect.** SelectCtrl reads the pad of its RAM index
  (`src/engine/ui.cpp:538`). Online in a multi-member instance, a window with
  index 0 that a script run with a trigger slot opened (5.1) reads the
  trigger's pad; a window whose index is absent or leaving, or that has no
  trigger, reads the leader's pad; other indexes read their own slot as in the
  original. The film skip (`managers_core.cpp:2703-2711`) reads the leader's
  pad.
- **Soft reset.** The STAGECTL soft reset (`managers_core.cpp:759-834`) is off
  online.
- **Console reset.** `ConsoleReset` reboots the console when B+X+START are held
  for 180 fields on any connected pad (`include/dw4/console_reset.hpp:53,
  79-91`; `engine.cpp:1905-1909`), and the host then starts a new engine on the
  boot stage (`game_main.cpp:1927`). In a sealed instance the remote pads are
  connected, so one member (by accident or on purpose) would reboot every
  machine. Online, every engine runs with `ConsoleReset::set_enabled(false)`
  (set again after every restore). The client watches the local pad for the
  combo itself and opens *Leave room?* (presentation). A test drives B+X+START
  for 4 s on slot 1 with an input script: no reboot, equal fingerprints, the
  leave prompt on slot 1's machine.

### 7.5 Observe-only additions

A port event `card_written {slot, card, box}` when WRITE_MEDIA or FILEMAKE
completes (for the upload, 9). Like the existing port events it is collected
only on request and is in neither the fingerprint nor the snapshot. In replay
mode (catch-up, resync) no port events are collected at all, so achievements
and story "was there" credits never come twice (the existing suppression covers
only the save state replay, `game_main.cpp:1254`).

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
| Counters | COUNTER 1 (junk shop donations), COUNTER 7 (training attempts) | additive (8.3) |
| Never merged | the 404 flags `gameStart.lua` clears, 0, 508, 900-937 (derived at each lobby load), the non-monotone 503, 611, 905, folder 11, COUNTER 0 and 2-6 (session values, 3.7) | - |

After every merge the server sets the copy of the room difficulty to the story
working set and the key copies to K, as the save room does
(`stagIS12_m1__start.lua`); otherwise a later `gameStart.lua` would turn the
story back. Checked against real saves (four campaign checkpoint sets, 32
consecutive pairs): no phase step back, no key lost, no COUNTER 1 decrease, no
record with two phase flags, nothing lost with the phase group as above (without
the copies in the group, 701, 703 and 704 would be lost four times each). A
delta per checkpoint step is 10 B median, 79 B at most.

### 8.3 Pending sets

Each machine keeps the *confirmed room story*: the last room story the server
sent it (BARRIER_OK, STORY_ACK, ROOM). Its *pending set* is what a merge of its
engine's persistent shared values into the confirmed story would add: a higher
phase, persistent flags and key items the confirmed story lacks (read with
`find`, never creating keys). It sends STORY with the pending set at every
barrier and every ten seconds while the set is not empty, and the server
answers STORY_ACK with the new room story and what it refused, with the
reason. Phase, flags and keys are sets, so resends and duplicates from several
members of one instance do no harm, and nothing depends on the state at the
last barrier: progress a member's engine adopted through a join is pending
until the server confirms or refuses it. This catches FLGset, FLGflip,
COUNTERset and the flag folder alike (the `story_flag` port event covers only
FLGset).

The two additive counters travel as the cumulative growth of the engine line:
per (instance, visit), the growth since the visit's start, applied once by the
server; a child counts from its fork. The story census checks in round S2 where
COUNTER 1 and 7 change; if only in hub stages (solo instances, no joins or
forks), the line key never matters.

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
  room story and sets the room difficulty, camera mode and light mode;
  `gameStart.lua` then clears the temporaries as in the original.
- **At every barrier** (6.7): the load step merges the barrier's room story
  into the engine with the same rules, touching only keys whose value changes
  (FLGisOn creates zero entries and the fingerprint hashes every key,
  `lua_host.cpp:400`, `engine.cpp:774-781`), and removes dropped values (8.6).
  It runs before the snapshot, so joiners get the same state. Every member of
  the group applies the same bytes at the same step.
- **Never mid-stage**: the stage is built from the flags by its load scripts
  (621 story flag reads at load time), so a mid-stage change would contradict
  gates and NPC scenes.

### 8.6 Owner away

r48 freezes the story while the owner is not in the room
(`SetStoryFrozenAsync`). Because "progress by any player counts" is a user
decision and the freeze was an r48 default, the server gets a setting
`Rooms:StoryWhileOwnerAway = drop | hold | apply`:

- **drop** (r48): the server refuses pending values while the owner is away and
  records them in the room's *dropped set* for the session. BARRIER_OK carries
  the dropped set, and the load step removes those values from the engine (the
  phase falls back one-hot to the room's); the player sees "Story progress was
  not saved: the room owner is away". This is the only case in which the story
  in an engine goes back, and only online. So nothing made while the owner was
  away lingers in engines, spreads through joins, or vanishes silently in the
  next session; a dropped value counts again only when a member sets it anew in
  a stage that sets it while the owner is present.
- **hold**: kept per room on the server and merged when the owner returns; the
  engines keep the values meanwhile (they stay in the pending sets).
- **apply**: merged at once.

The default stays `drop` until the user decides (question 2).

### 8.7 Built in round S2 (story lane)

**Census (measured).** `tools/online/story_census.py` reads the 7,168
decompiled scripts and writes `data/online/story_sets.json` (shared with the
server, which embeds it) and the engine's tables
`src/engine/online_story_sets.inc`; the ctest `online_story` compares them.
Phase: flags 1-6 and 86, one-hot (stagML02 and stagML11 step it, clearing the
step before; stagBS06 and stagML11 set 86). Persistent: **109** flags (the
non-phase working set and every flag a script sets to 1 for good; none of them
is ever cleared by a script outside the two restore scripts, the census
stops otherwise); 99 of the 116 phase and persistent flags have exactly one
setter stage, flag 99 none (a working set flag no script sets), 406-408 come
from `FLGset(SYSgetDifficulty() + 406, 1)` (stagBS06). Keys: flag folder 0-10.
Never merged: 445 flags (gameStart.lua's temporaries, 0, 503, 508, 611,
900-937, the copies 529 / 541 / 553 of the quest item), folder 11, COUNTER 0
and 2-6.

**Counters.** COUNTER 1 changes only in the engine's junk shop program
(`subwnd_shop.cpp` ShopJunk), which only `stagML13_m1__junkshop.lua` opens:
a hub stage, solo. COUNTER 7 changes in `stag0001_m1__gene03.lua` (each
attempt, read by `matz00.lua` at 10): a **joinable field**, so the line key
matters. A line is the engine line (instance, visit) the growth was made in;
every member of an instance reports the same line (a joiner's and a
co-arrival's base is the counter after the restore: the visit's load), the
server takes each line's cumulative growth once. A fork's child counts from
its fork under the id REASSIGN brings; the copy that becomes the child sends
the parent line's growth up to the fork until the server took it. Lines of
instance 0 (a solo group that went on without the server's answer) are the
sender's own on the server.

**Copies are derived.** The design's table above lists the difficulty copies
as OR-merged. Built: the server never takes a copy from a member; after every
merge it derives the room difficulty's copies (phase one-hot, working set,
keys 0-10 into 518 / 530 / 542 + k) from the working set, as stagIS12's
`start.lua` does, and when a room opens it *rebases* the owner's story on the
room's difficulty (working set and keys from that difficulty's copies, as
`gameStart.lua` does). Without the rebase a hard room would merge the normal
story's working set into the hard copies. The copies are read only by the
title (`stagIS10` hostLoad and gameStart), so the engine's copies are not
touched at barriers.

**Pending sets on the client** (`src/game/online/session.cpp`). The pending set
is computed once a second and sent when it changed, else every ten seconds
while it is not empty, and always right before BARRIER (the server merges it
before it decides, so BARRIER_OK brings it back). Never from a stagIS* stage:
the character select holds a record's own story until the room story replaces
it at `game_started`. Refused keys (not for the rate) wait for the next
barrier; held ones are sent again (the server keeps them). The confirmed
story is `Session::confirmed_story()` (the title's application at
`game_started`, 8.5).

**Plausibility and policy on the server** (`DW4Online.Domain.Story.
RoomStoryKeeper`, one per room hub, under the room lock). The sender's
instance *now* decides: no instance or a stagIS* stage refuses everything
(not_member); a flag or a phase step only from a stage whose scripts set it
(stage); the phase rises at most one step beyond the room's (with *hold*: the
held phase); a counter only from a stage that changes it, growth 0..100,000
(COUNTER 7) or 0..100,000,000 (COUNTER 1) per line (limit); never-merged
values refused (not_story); a member's STORY rate is a burst of 10, then 60
per minute (rate; the client sends again). Then the owner-away policy:
*apply* merges; *hold* keeps phase, flags, keys and line growth and merges
them when the owner returns (the phase step by step); *drop* refuses
(owner_away) and records the keys in the room session's dropped set with a
drop epoch. Every instance remembers the epoch its visit began in (a fork's
child its parent's): a dropped value counts again only from a visit that
began after its drop, that is from an engine whose barrier removed it and that
set it anew; from older lines it stays refused even with the owner back. The
dropped set lives for the room session and goes with every BARRIER_OK
(removal is a no-op where the room has the value by then).

**At the barrier** (`Engine::apply_barrier`, `story::apply`). The phase rises
one-hot to the room's (a pending phase above it stays unless dropped, then it
falls back), persistent flags, keys and counters the room has come in,
dropped values the room lacks are cleared (a dropped counter takes the room's
value), only keys whose value changes are touched; then the membership. A
damaged story blob merges nothing, alike on every member. The client shows
"Story progress was not saved: the room owner is away." when the dropped set
removes something from its own engine. JOIN and CO_ARRIVE merge into the
member's own fallback load; the restored target snapshot has the story of the
target's visit start.

**Golden vectors.** `tools/online/story_golden.py` (a third, independent
implementation in Python) writes `data/online/story_golden.json`: 14 merge,
3 pending, 3 rebase and 6 application cases; the C++ ctest `online_story` and
the C# `StoryGoldenTests` give the same results. STORY / STORY_ACK have golden
bytes on both sides (`online_wire_tests.cpp`, `WireGoldenTests.cs`).

**Storage.** Migration 0008 adds `online_story (account_id, revision, flags
VARBINARY(1024), folder BINARY(12), counters BINARY(64) as 16 x i32 LE,
updated_at)`; only newer revisions replace a row (the relay saves each new
revision off the room lock). `GET /online-character` composes the record's
BASE_FLAG / FLAGFOLDER / COUNTER (big endian) from the account's own story,
`PUT` replaces the uploaded ones with it before validation, summary and state
achievements. `GET /rooms/{code}/story` (members) shows revision, phase,
flags, keys, counters, policy, owner presence, the dropped set and the held
count.

**Reconnects.** STORY_ACK answers come in order on one connection, so a new
connection (`Session::attach`) forgets the STORY frames still waiting for an
answer on the old one and sends the pending set again at once.

**Relay notes.** The membership log notes the owner's presence (`owner
present=... held=n`) and what the owner-away policy took from a STORY
(`story_dropped` / `story_held member=n keys=flag:501,folder:2,...`), beside
`story revision=n phase=p` for every new revision.

**Tests.** ctest `online_story` (census tables, 26 golden cases, blobs),
`online` (`story_at_the_barrier`: engine and link), `online_session`
(`room_story`: the title rule, the one-second path, the other member's
confirmed story and barrier merge, one counter line for a shared instance,
the dropped removal with the notice, a STORY lost with its connection sent
again at once after the reconnect), `online_wire` (golden STORY /
STORY_ACK); server unit tests `StoryGoldenTests`, `RoomStoryKeeperTests`,
`HubStoryTests` (S14 at the hub: drop with the same story bytes and dropped
set in a group's BARRIER_OKs, hold merged at the owner's return, apply) and
integration `OnlineStoryTests` (composition; a guest's lobby flag in the
owner's `online_story` through the real relay within ten seconds).

**S14 on real game clients** (`tools/online_test/scenarios/S14`, `S14h`,
`S14a`; drop, hold, apply). The owner (k2 without flag 501; `seed_story`, so
the room story is that card at revision 1) stays at the title, where no STORY
goes, and leaves the room when its run ends; the guest (k2 with 501, which
the lobby's `first.lua` sets at the first visit) waits at the title until
the owner is away, then enters the lobby: its pending set is 501 alone.
Drop: `story_dropped keys=flag:501`, 501 stays in the engine for the rest of
the lobby visit, the lobby gate's barrier into stag0101 removes it and the
guest sees "Story progress was not saved: the room owner is away." Hold:
`story_held keys=flag:501`, the guest is told once, no revision, 501 stays
after the barrier. Apply: revision 2 at once, 501 stays. All three pass
(about 140 s each, side by side). Without the seed the guest's whole card
story would be pending against the owner's empty online story, and drop
would take its key items too.

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
- **When.** When the save room writes, as in the original, and also
  automatically at every stage change (at the barrier, from the state the
  member carries into the next stage) and when the member leaves the room
  (question 3, answered: automatic saves wanted). A crash or quit loses at most
  the current stage. The room story does not depend on it (8.3).
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
  to what the owner's online story unlocked, camera mode, light mode,
  randomizer seed and modes, fast loading), Join (password prompt). In a room:
  members with their area (from presence), Leave room, Kick (owner).
- **Session start.** Joining or creating checks the CPU (6.12) and the
  simulation environment variables (6.12), asks to end the offline game
  ("unsaved progress is lost"), then restarts the engine at the title in online
  mode: virtual card, room settings (cheats and rebalancing off, difficulty,
  camera and light mode, randomizer and fast loading from the room), online
  rules on, the console reset off, the replay rate calibration in the
  background. Leaving the room returns to the offline title.
- **Loading screen texts** during a held barrier (BARRIER_WAIT): "Waiting for
  <name>...", "Joining <name>... (catching up, about N s)", the boss choice,
  and the reason when a parallel instance opens (3.4).
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
- **Online unlocks** count only this machine's player (its local slot, never
  p1): event unlocks with `POST /achievements/unlock` and the room context
  (ranked rules exist), stats every 60 s with `POST /stats` from the existing
  queue (the r48 token buckets start at account creation, so no big first
  batch). State achievements come only from the server reading uploaded
  characters in ranked rooms.
- **Story achievements** count for every member of the instance in which the
  story flag rose ("was there"): each machine sees the same port event in its
  engine during live ticks (never in a replay, 7.5) and awards its own player.
  Members elsewhere in the room do not get them.
- **Story challenges.** `story_clear` and its challenges (speed_fast under
  4 h, speed_normal, rookie_clear, deathless_clear) read slot 0's character and
  its own play time today (`achievements.cpp:325-338`). Online they go only to
  the room owner's machine (the story is the owner's), computed from the
  owner's online character; a guest who happens to be slot 0 when the last
  boss falls gets the plain "was there" credit, never "under 4 hours".
- Offline unlocks stay local in the F1 Achievements tab (r48 default).

## 12. Name tags and presence

- Members of the own instance are engine players: the existing overhead labels
  draw them from GameSystem (name converted from the full-width PLAYERNAME,
  level, HP and MP bars), projected with the local camera. A defeated member's
  capsule gets a name marker (4.1).
- Room members in the same stage but another instance (hub stages, a fifth
  player in a parallel instance, a member on another quest) come from PRESENCE
  packets through `OverheadLabels::set_remote_players` (exists): interpolated
  between packets, hidden by the same rules (loads, demos, menus, a defeated
  player). They are presentation only and never touch the scene graph.
- A lagging member's tag shows a connection icon.
- Notices (presentation, a few seconds at the top): "<name> joined", "<name>
  left for <stage>", "<name> is in a scene", "<name> has a menu open".

## 13. Discord

The existing IPC client gets party fields in a room: `party {id: "dw4-" + a hash
of the room code, size [n, max]}` and the state `Online room (n/max)`, never the
room name. Details stay `<Form> Lv <level>` and the area. The party size counts
the room (up to ten), not the instance. No invites or join secrets for now.

## 14. Server changes

| Area | Change |
| --- | --- |
| Relay | `DW4Online.Relay`: DW4R2 hub replacing the splice: one connection per member, frames, instance groups (solo-led and sealed), server-paced sealing at 59.94 Hz on a dedicated timer thread with immediate fill, delay suggestions from the arrival margins, event stamping, snapshot and log store with canonical hashes and fork lists (6.8), state delivery for join / co-arrival / resync / resume with its own rate limit, fingerprint vote, presence fan-out, pings, the 64 KB/s bucket for game frames |
| Rooms | `RoomService`: instances as lockstep groups (id, stage, visit, context hash, slots, parent and fork frame, departing, boss lock, joinable), barrier decisions DISSOLVE / co-arrival / JOIN / NEW with holds and the boss choice (3.4), catch-up estimates from the members' replay rates, forks of both kinds, in-band JOIN and LEAVE, preferred slots, no instance end when its first member leaves (`EndInstanceLocked` goes), room-scoped relay tickets, the settings hash, the relay connection as heartbeat. `POST /rooms/{code}/stage` and `/stage/leave` are retired; the room state lists instances with stage and members. Room settings gain camera mode and light mode |
| Story | migration with `online_story`; `StoryMerge` in the domain with the golden vectors; plausibility table from the census; pending sets with STORY_ACK; the owner-away setting with the session's dropped set; `GET /online-character` composes, `PUT` ignores story bytes; `GET /rooms/{code}/story` for the website and debugging |
| Achievements | catalogue migration to the client's ids and thresholds |
| Config | `Sealing:MaxDelayTicks` (15, measured in S1, 6.4), `Sealing:LateTarget` (0.001), `Relay:ReconnectSeconds` (10), `Relay:LagLeaveSeconds` (10), `Relay:StateBytesPerSecond`, `Instances:CoArrivalHoldMs` (3000), `Instances:JoinHoldSeconds` (30, measured in S1, 3.4), `Instances:MaxCatchUpSeconds` (30), `Rooms:StoryWhileOwnerAway` (drop, built in S2), `Instances:StoryBurst` (10) and `Instances:StoryMessagesPerMinute` (60, S2); `Play:CurrentProtocol` follows the client. `Sealing:StallMs` and `Instances:MaxJoinLogMinutes` of revision 1 are gone |
| Website | the room page shows instances per stage |
| Deploy | fixed-key self-signed certificate and the pin script (10); port 52041 as before |
| Tests | unit tests for sealing schedules and fills, events, forks, barrier decisions (holds, co-arrival, context key) and merge vectors; integration tests with scripted fake clients; end-to-end tests with the real game client (16) |

## 15. Online off stays bit-identical

- `OnlineRules` default off; offline games, save states, story runs, the
  campaign tools and the direct `--host` / `--join` sessions never turn it on.
- The camera mask 0 is today's camera and traversal code, unchanged.
- The Lua overrides and wrappers are registered only online.
- Draw filters (item label lines, exit prompts, scoped scenes) act only online.
- New port events are observe-only (not in fingerprint or snapshot).
- The net fingerprint is a separate function; `Engine::fingerprint` stays.
- No FMA3 switch; the libm routing of round S4 selects the MSL routines only in
  online sessions. Input quantization, the console reset switch and the
  environment check act only online.
- The MXCSR guard runs offline too and is a no-op with the default MXCSR.
- The snapshot version rises (new `OnlineRules` block, the two missing fields);
  as with every bump, older save state files no longer load.
- Proof in every round: `det.sh` twice (4004f056 / audio 396cb219), every ctest
  suite, the save state suite (17/17 plus a test from a file), and in the
  rounds that change engine code the story runs r25c and nc1 frame-equal with
  main. A union test hook with one camera must also give 4004f056, and the
  extra-renders hook (4.4) must equal headless.
- The commands since round S1: `det.sh` with `DW4_CAMERAS=1 DW4_CAM_CHECK=1`
  (4004f056 and "0 of 1902 frames"), with `DW4_LEASH_OFF=1` and with
  `DW4_EXTRA_RENDERS=2 DW4_EXTRA_WIDEN=1 DW4_EXTRA_PREVIOUS=1` (4004f056 each),
  and the four-player runs of 4.4 (stag0102 and dng101b1 with
  `DW4_CAMERAS=15 DW4_ONLINE=15`: the same fingerprint and `DW4_TRACE_CAMERAS`
  lines headless and with local cameras 0, 2 and 3).

## 16. Testing

- **Local stack** `tools/online_test/run.py`: starts a private mysqld
  (`C:/xampp/mysql/bin/mysqld.exe` with its own data directory and port, as the
  server's `local-db.ps1`), the DW4Online server on 127.0.0.1 (HTTPS with a
  throwaway certificate, relay on a free port), registers accounts through the
  API, starts N clients built with `DW4_ONLINE_DEV` (hidden: `--smoke-test`,
  `DW4_NO_ERROR_BOX=1`, `DW4_RENDER=1` only where a picture pass is wanted;
  `nice -n 10`) with input scripts, collects every client's net fingerprints per
  60 ticks and its membership log, compares them per instance and checks the
  expected events; it stops only the processes it started. A latency proxy
  between clients and relay adds delay, jitter and (modelled) loss.
- **Scenarios** (each a script plus expected log):

| Id | Scenario | Round |
| --- | --- | --- |
| S1 | two members in one stage walk apart (> 200 units), own cameras, enemies on both sides | S1 |
| S2 | one member leaves by an exit (fork), the other follows later (join by catch-up), both change stage together | S1 |
| S3 | three clients: two joins mid-stage, one at a time, then a common stage change | S1 |
| S4 | gate disc while a partner fights | S3 |
| S5 | everybody falls: game over, dissolve into solo lobbies | S1 |
| S6 | boss mat: gather, and *Go without them* (participants boss-locked, the others a joinable instance) | S3 |
| S7 | story trigger by one member (stagGF03 math00) while the other is elsewhere | S3 |
| S8 | status window of one member while the other fights far away (scoped) and near (original) | S3 |
| S9 | pit: one member falls and respawns alone, no fade on the other screen | S3 |
| S10 | disconnect and reconnect within ten seconds; and after the timeout | S4 |
| S11 | injected desync (test hook) and resync; a resync beyond the limit continues alone | S4 |
| S12 | latency, jitter and loss proxy matrix | S1 (measure), S4 (tune) |
| S13 | room of ten: instances 4 + 4 + 2 plus solo members, 30 minutes | S4 |
| S14 | owner-away policies drop (removal at the barrier, notice) / hold / apply | S2 |
| S15 | save room upload, 409 from a second machine | S2 |
| S16 | quest and story in stag0401: a quest-1 member arrives at point 3 while a story member plays there: separate instances, both keep their state; two members on quest 1 share one | S1 |
| S17 | two members take the area gate 2 s apart (co-arrival, one entry scene) and 8 s apart (hold, then join) | S1 |
| S18 | exit mat: A answers *No* and stays, B steps off, dies or despawns on the mat: no prompt; A answers *Yes* while B stands on the mat: B is asked; B *No*: only A leaves | S1 |
| S19 | dead partner: A dead, B (the only living member) takes an exit or the gate disc: A goes with B, no game over; A gives up: A alone to the lobby | S1 (exit, gate disc), S3 (story trigger) |
| S20 | dng103f2 dwbutton2 with a member who just arrived (15-frame window as in local co-op); a puzzle mat shows *Waiting for* | S3 |
| S21 | B+X+START held for 4 s on slot 1: no reboot anywhere, *Leave room?* on slot 1's machine | S1 |
| S22 | a joiner whose target leaves the stage during the catch-up (fallback NEW) or forks (follows the instance id) | S1 |
| S23 | boss-locked target: *Wait for them* (joins after the boss falls) and *Go alone* | S3 |
| S24 | map recorder (dng102b3) read far from the partner (scoped, local freeze) and near (original) | S3 |
| S25 | Trailmon ride (dng412b3) with one member not aboard: no fork, the rider moves alone | S3 |

- **Real game clients** (round S1 integration): `run.py G1 --game-exe EXE
  --data DATA` starts two hidden `DW4_ONLINE_DEV` game clients instead of fake
  ones. Both log in over the pinned HTTPS API (`DW4_ONLINE_SERVER`,
  `DW4_ONLINE_PIN`, `DW4_ONLINE_PASSWORD`), the first creates the room, the
  second joins it `--offset` seconds later; both play a new game with the
  campaign bot from the title (every hub stage a DISSOLVE), meet in the first
  field (the second joins the first's instance by catch-up) and must keep equal
  net fingerprints (`DW4_ONLINE_TRACE` netfp lines) for `--sealed-ticks` ticks;
  the relay's membership log must show no desync, mismatch or resync.
- **Scenarios on real game clients** (round S1): `run.py [ID ...] --game-exe
  EXE --data DATA --cards CHECKPOINTS` runs `tools/online_test/scenarios/<ID>`
  (a `scenario.json`, one script per client, an `expected.txt`). Every client
  is a hidden `DW4_ONLINE_DEV` game that calls a saved character of a campaign
  checkpoint set at the title (`--online-card`, the r25c set: k2 = level 7
  for deaths, k10 = level 21 for the long walks) and plays its pad from a
  script (`--online-script`, `src/game/online_script.hpp`): steps with
  `until` groups and `fail` checks over the instance (stage, present members,
  sealed ticks, exit prompts, mats, deaths, flags, the wall clock for timed
  departures), actions such as `route` (known waypoints, `routes/*.txt`),
  `mat` (answers exit prompts), `fight`, `die`, `giveup`, `disk`, `gate`,
  `hold`, `errand` (one step of the campaign bot, such as
  `talk:n_digilfm#1`: up to the model's talk box, the messages moved on,
  choices answered with their first entry). A client's `card_flags` in
  `scenario.json` sets story flags in a copy of its card (`run.py`
  `derive_card`, both copies of the save with their checksums) for a
  precondition the r25c campaign never met and that has nothing to do with
  the online play (S16: Digi-Elf 0 freed in stag0103's boat ride, flag 89,
  stored for the normal difficulty as 714). `"seed_story": true` starts the
  room story from the room creator's card (`run.py` `seed_story` writes the
  owner account's `online_story` row from the card's first record before the
  room opens), `"server": {...}` gives the scenario its own server with those
  settings and its own database (S14h / S14a: `Rooms:StoryWhileOwnerAway`),
  `"min_shared_ticks"` lowers the shared ticks below (S14: one member plays).
  Checked per scenario: the net
  fingerprints of every instance equal on every client every 60 ticks, at
  least 10,000 ticks in shared instances,
  the canonical snapshot hashes of every NEW and co-arrival equal, no desync,
  mismatch or resync in the relay log, no client closed, every script done,
  and the scenario's expected log in order (`!` lines must not appear).
- **Checks outside the stack**: the extra-renders hook (4.4), the canonical
  snapshot check (6.8), the cross-process join after a long campaign session
  (7.1), the console reset test (7.4).
- **For the user**: `scripts/online-dev.ps1` starts the local stack and two
  visible game windows (agents run only the hidden variant, `-Hidden`, which
  plays both with the campaign bot; `run.py dev`).

## 17. Round plan

Each round is one workflow: at most four parallel lanes, then integrate, review
(game and server), fix, as in round 48. Lanes build against sections 6 and 7;
the integration pins the final layouts. Where a lane depends on a measurement,
that measurement is its first task and its result goes into this document
before the lane builds on it. Every round ends with the checks of section 15.
Round numbers are proposals (r49 and r50 exist, r51 is this design).

### Round S1 (r52): seamless core, first playable over a local relay

Goal: two or three players in a room over a local relay, each with their own
camera, no leash, free travel between stages with forks, arrivals together and
mid-stage joins. New characters start from the title (nothing is saved online
yet).

First tasks (measurements):

- cameras-exits: the replay and step rate on the final path (union pass, four
  players, the CRT's FMA3 routines) in stag0102, dng101b1 and stagBS02,
  headless; it sets the default of `Instances:MaxCatchUpSeconds` and the
  client's initial rate estimate. The extra-renders hook (4.4) on the main of
  the day (with the r49 previous-view peek) against headless.
- membership: the session-value census (3.7); the canonical snapshot check
  (6.8); the cross-process join after a long campaign session (7.1).
- relay: the loss and jitter matrix for server-paced sealing (6.4), which
  decides whether S4 carries UDP; join log sizes with quantized pads from
  recorded input (6.6).
- client: the static CRT's condition for its FMA3 routines (6.12).

| Lane | Scope |
| --- | --- |
| cameras-exits | per-slot box cameras, union-cull simulation pass and render pass (spike r51cam brought to production, the r49 previous-view rule for the local camera), camera mask changes at membership steps, leash rule, per-slot consumers and the item label line filter, bots per slot; the `funcStageChange` port per 5.2 (prompt owner, the other members on the mat, owner-only fade and prompt, the stays list), the gate disc wrapper, the side rule for dead members; the defeated member's view, *Give up*, the capsule marker; the extra-renders hook as a permanent check |
| membership | snapshot completeness and version 43 with the bytes test, the canonical form, the CLI guest fix, `OnlineRules` (input masking for absent and leaving slots, the boss lock field), `online_join_player` for any species (packages mid-stage), `online_remove_player`, forks of both kinds with `take_split`, fork sides and event routing, `pending_stage`, the barrier (transition steps with the pads of B, `set_barrier` in the load step, co-arrivals' spawn, dissolve with personal values), predicates, the session context hash, pad redirect by trigger slot, soft and console reset off, cheats from the room settings, the MXCSR guard, the net fingerprint (with `OnlineRules`, `PlayerBox`, context), replay mode without audio and port events, the second-engine assertion |
| relay | DW4R2 hub in DW4Online: solo-led and sealed groups, server-paced sealing at 59.94 Hz with immediate fill and delay suggestions, events, barrier decisions (context key, co-arrival with HEADING, hold until joinable, boss choice, catch-up limit from the reported rates), forks of both kinds with fork lists, snapshot and log store with canonical hashes, state delivery with its own limit, presence, pings; RoomService instance groups, preferred slots, room tickets, the settings hash; `tools/online_test` local stack with the latency proxy and fake-client tests |
| client | `src/game/online/` WinHTTP worker with pinning and DPAPI token, development login and room create / join from the command line, the DW4R2 client (solo-led and sealed execution with ±0.1 % pacing and the audio frequency ratio, catch-up join with the fallback snapshot and the follow rules, barrier with the loading screen texts, fork, lag and reconnect overlay basics, notices, the music position fix-up), pad quantization, presence send and receive into the remote labels, online session start with the room settings (camera and light mode), the CPU gate, the environment check, the local leave combo, the replay rate calibration |

Done when:
- Online off: `det.sh` twice 4004f056 / 396cb219; ctest all green; save state
  suite 17/17 and a state loaded from a file identical; r25c 10/10 frame-equal
  with main; a one-camera union run gives 4004f056; the extra-renders hook
  equals headless.
- Cameras: four players in stag0102 and dng101b1 give the same net fingerprint
  with local camera 0, 2 and 3 and without pictures (hidden windows); the cost
  table of 4.5 re-measured within 10 %; the replay rate table measured and
  written into 6.6; a solo run with the ring off checked against the reference
  (4.6).
- Local stack (private mysqld, DW4Online, relay on loopback, HTTPS with a
  throwaway certificate): S1, S2, S3, S5, S16, S17, S18, S19 (exit, gate disc),
  S21 and S22 pass with hidden clients, every instance's net fingerprints
  identical every 60 ticks for at least 10,000 ticks; the canonical snapshot
  hashes agree at every NEW; server unit, integration and end-to-end tests
  green; the loss matrix is recorded with its transport recommendation.
- `scripts/online-dev.ps1` exists, its hidden variant runs, and the README has
  an "Online co-op (preview)" note with the known limits: talks, recorders,
  camera tours and the status window still pause the whole instance; pits
  still darken every screen; nothing is saved online; online needs an FMA3
  CPU.

Depends on: this design; main with rounds 49 and 50 joined.

**Integration (round S1, branch `dref-r52int`).** The four lanes are joined
on main with round 50 (merge order membership, cameras-exits, client, relay)
and fitted to one engine API (7.2, 7.3) and one wire layout (6.3 and the
appendix, golden bytes on both sides). Checked on the integrated code:

- Online off: `det.sh` twice 4004f056 / 396cb219, also with `DW4_CAMERAS=1`
  (the one-camera union) and with the extra-renders hook (`DW4_EXTRA_RENDERS=2`,
  widen, previous view); the save state suite 17/17 and states through a file
  (direct start, new game frame 4500) identical after loading; r25c 10/10 with
  every segment's frame count equal to main's run (1,758,666 frames in all);
  ctest green, the online suite with the game data included.
- Server: unit tests (with the golden wire and settings-hash tests),
  integration and end-to-end tests (the C++ WinHTTP client and the Python
  DW4R2 client) green against a private MariaDB; the fake-client scenarios
  S1, S2, S3, S5, S16, S17a, S17b, S22a and S22b pass again on the integrated
  hub (`run.py`).
- Real game clients (`run.py G1`, offset 10 s): both clients reach stag0001
  (frame 10,870) from the title through four DISSOLVEs; the first gets NEW (its
  hub instance with the next visit), the second's barrier is held 12.8 s until
  the first's instance is joinable (1,294 ticks after its load), then JOIN:
  STATE_DATA with the snapshot and 1,294 ticks, a catch-up at 5,970 steps per
  second, READY at tick 1,260 with the members' fingerprint, JOIN_REQ /
  JOIN_AT 1,330, the JOIN event at tick 1,330 on both machines, sealed from
  there: equal net fingerprints at all 163 compared ticks from 1,380 to 11,100
  (9,720 sealed ticks); when the first left, LEAVE and LEAD_FROM let the second
  lead alone. The first two runs found two integration faults that the fake
  clients could not: BARRIER_OK of a NEW named the reused instance's next visit
  as the old one (the hub, fixed), and the client's five-second barrier
  timeout ran out during the relay's join hold (a BARRIER_WAIT now restarts
  it).

**Scenarios on real game clients (round S1, `tools/online_test/scenarios`).**
Every scenario of the round passes with hidden game clients (section 16,
`run.py` with the r25c checkpoint cards): S1, S2, S3, S5, S16, S17a, S17b,
S18, S19a (gate disc), S19b (exit), S19c (give up), S21, S22a and S22b in one
run, 14 of 14 (five at a time),
each with 10,080 to 13,920 ticks of equal net fingerprints in shared
instances (every 60 ticks, no difference anywhere), the canonical snapshot
hashes of every NEW and co-arrival equal, no desync, mismatch or resync in
the relay log. Earlier runs kept two instances sealed and equal for 54,060
ticks (S2, S5 while their scripts were stuck). What they show:

- S1 / S17a: two members who leave the lobby gate 1 or 2 s apart start the
  field together (HEADING, `hold_co_arrival`, one entry scene, sealed from
  tick 0), then walk 300 units apart with enemies on both sides.
- S17b: 8 s apart, the later member's barrier is held while the first's
  instance loads and runs its entry (23.1 s, 3.4), then it joins by catch-up.
- S2: a fork at stag0101's matz02 (the leaver's copy leaves, the stayer's
  stays), the stayer follows 15 s later and joins the leaver's stag0102
  instance by catch-up, then both go back through matz01 as one group.
- S3: two joins into one instance one after the other (slots 1 and 2), then
  one barrier for all three.
- S5: both fall, the game over window (the leader's pad) is one barrier of
  the group, dissolved into two lobbies.
- S16: a story member enters stag0401 at point 0 and plays there; a member
  who took quest 1 at the lobby Digi-Elf (`errand talk:n_digilfm#1`, its
  "yes": `quest01.lua` sets the context flags) leaves the lobby gate 40 s
  later, which offers only point 3, and gets a NEW beside the story
  member's instance (notice OtherContext); a second quest-1 member 40 s
  after it joins the first's instance by catch-up at point 3 and they share
  10,620 ticks. Flag 409 stays set on the quest side and off on the story
  side throughout (the scripts' fail checks). The quest members' card is
  k10 with Digi-Elf 0 freed (flag 89 / 714, `card_flags`): the quest's
  precondition, which the r25c campaign never met.
- S18: the exit port's owner and asked members exactly as 5.2 (No stays on
  the mat and is not asked again; another member stepping on and off asks
  nobody else; Yes asks a member who stepped on during the prompt; its No
  forks off only the owner).
- S19a / S19b / S19c: a defeated member goes along with the only living
  member through the gate disc (lobby point 210,120, not the game over's
  60,-30) and through an exit (the group move, no fork); giving up forks it
  off alone into its own game over while the partner plays on.
- S21: B+X+START for 4 s asks "Leave room?" on the holder's machine only, no
  reboot anywhere.
- S22a / S22b: a slow joiner (`DW4_ONLINE_RATE=300`) falls back to NEW when
  its target leaves the stage during the catch-up, and follows the instance
  id when its target forks.

Faults the scenarios found and fixed (the session tests' fake hub now shows
two of them): a solo instance never announced its departure (HEADING), so no
two members ever co-arrived from the lobby (`Engine::note_departure`); a
co-arrival believed it was alone (its BARRIER_OK names only its own slot; the
present slots now come from the restored engine); `JoinHoldSeconds` 15 was
shorter than a new instance's load and entry (now 30, 3.4); a joiner dropped
the target's LIVE ticks that came during its own transition steps and waited
for good (`live_during_transition`); a fork on a multiple of 60 sent both
sides' fingerprints as the instance's and made the relay resync the stayer
(no NETFP at a fork tick); a LEAVE that had to wait for a membership-safe
step held back the LEAD_FROM behind it, so the remaining member waited for
ticks nobody sealed; a BARRIER named the point the departing stage was
entered at instead of the one its exit leads to (`GameEngineLink::
pending_point` read the stage snapshot; it now reads PLAYERPOINT's point,
which SETGENERATEPOS sets before NEXTSTAGE), so every joiner came in at the
point its *previous* stage was entered at: S16's second quest member
appeared at stag0401 point 0, walked back across the story field's
generators and fell with its partner (the online engine test now checks
the barrier's point; S2's return through stag0102 matz01 and the gate disc
now report points 4 and 3).

Not covered yet: the S12 matrix with real clients (the relay lane's
fake-client matrix, 6.4, stands), visual checks of the online presentation
(hidden runs only). Running ten scenarios at once overloaded the PC: joiners that needed
more than `Rooms:MemberTimeoutSeconds` (35) between the API join and the relay
connection were dropped from their rooms; five at a time pass.
`scripts/online-dev.ps1` and the README note "Online co-op (preview)" exist;
the hidden variant passes (two clients from the title, sealed together in
stag0001, equal final fingerprints).

**Review and fix round (round S1).** The review (game and server, live
attacks on a private stack) found, and this round fixed with a test each:

- *DISSOLVE kept the wrong character*: the relay names the new slot 0 in
  BARRIER_OK's own slot, the engine read it as the old slot, so every member
  not in slot 0 kept slot 0's player (name, level, items, bits, capsule) and
  personal values after each game over or return to a lobby (S19a on real
  clients: the k10 member showed the k2 partner's level 7). The adapter now
  hands the engine the slot map's source of 0 (`online` test
  `dissolve_through_the_link`; S19a's b fails below level 21).
- *Relay*: zombie instances from BARRIER with an unknown instance; a
  housekeeping task that died silently for every room on a slot -1; leaked
  connections without BYE; records past their u8 / u16 limits (64 KiB carry,
  300 INTENT); LOG faster than the game; RESUME and fingerprint floods; heavy
  state transfers under the room lock (6.5, 6.7-6.10). Plus a co-arrival that
  goes before tick 0, a joiner's LOG in the leader's history, a single
  member's SPLIT, JOIN_REQ without end, a fork child that never got its
  snapshot, a hub removed under a handshake, a bare `"randomizer": true`.
- *Client*: no leaving mark (6.5), RESYNC that never ended (6.9), a damaged
  carry ending the game, "Defeated" during every load and departure (now HP 0
  or the capsule out), the leave prompt that the combo could not close and
  that hid every loading line, notices without names, the login token
  forgotten on any network error, the MXCSR guard and the leash rule missing
  in restore, the load step's traversal missing after a restore,
  DW4_PAD_REPLAY allowed online, a give-up's fork side not found in a replay;
  S18's asked member now answers itself (not the prompt's timeout).

Checked on the fixed code: online off `det.sh` twice 4004f056 / 396cb219
(also with `DW4_CAMERAS=1` and the extra renders), the save state suite
17/17 and states through a file identical, r25c 10/10 with every segment's
frame count unchanged (1,758,666); ctest 40/40; server unit tests 130,
integration and end-to-end 67 (one skipped, as before); the fake-client
scenarios 9/9; the scenarios on real game clients 14/14 (S18 in a second run
after its scripts were refitted: b's own No comes 92 ticks after it is asked,
so the fork comes earlier and both play 600 ticks longer for 10,500 shared
ticks). S19a's b reaches the lobby with its own 880 HP (the bug showed its
partner's 240 and level 7).

Not fixed (minor, listed): a joiner's catch-up gets about 12 ms of each host
pass, so on a 60 Hz window it reaches about 70 % of its reported rate; a full
instance (four) still leaves the first arrival alone when five arrive at once
(the base holds only when every announced member fits).

### Round S2 (r53): accounts, online character, room story

Goal: the real loop from the title: log in, pick or create a room, play the
online character, save, and see the room story grow from every member's
progress.

| Lane | Scope |
| --- | --- |
| ui | title panel and F1 Online (account screens, room list, create / join with camera and light mode, in-room view), version gate, session start and end from the title, Discord party, member areas from presence |
| character | virtual card from the online character, `card_written` and the upload with If-Match and 409 handling, cloud backup in F1 Saves, room difficulty, camera mode and story at `game_started` |
| story | merge sets and golden vectors (C++ and C#), pending sets against the confirmed room story with STORY / STORY_ACK, the counter line key (after the census of where COUNTER 1 and 7 change), real story application and dropped-value removal at barriers, server `online_story` with composition, plausibility and the owner-away setting with the dropped set |
| achievements-deploy | catalogue alignment and the client's online sync (unlocks by the local slot, stats, "was there" from live ticks only, story challenges for the owner only), room page with instances, DEPLOY.md fixed key and pin script, pin constants |

Done when: on the local stack, driven through the client's own code paths in a
headless test mode: two accounts register, one creates and one joins a room,
both make new characters, play, save in the save room, and `GET
/online-character` returns the saved records; a story flag set by the guest in
its own instance reaches the owner's `online_story` within ten seconds and
appears in another instance at its next barrier; S14 passes for all three
policies (under *drop* the refused progress is gone from every engine after
the next barrier, with the notice); the golden vectors pass in both languages;
online achievements unlock with the ranked context; S15 passes; the checks of
section 15 hold.

Depends on: S1.

**Story lane (round S2, branch `feat-r56story`).** Built as 8.7 describes:
the census and its sets, the merge rules in C++ (`include/dw4/engine/
online_story.hpp`) and C# (`DW4Online.Domain.Story`) with golden vectors,
STORY / STORY_ACK, pending sets and counter lines on the client, the barrier
merge and dropped-value removal in the engine, the room story keeper on the
relay (plausibility, drop / hold / apply, the dropped set with drop epochs),
`online_story` with the record composition and `GET /rooms/{code}/story`.
Checked: the golden vectors in C++, C# and Python; the session test
`room_story` and the hub's S14 for all three policies; the guest-to-owner path
through the real relay and MariaDB in under ten seconds; S14, S14h and S14a
with hidden game clients (8.7); S2, S3, S5 and S17a with hidden game clients
and the nine fake-client scenarios still pass with the room story in every
BARRIER_OK; ctest 41/41, server unit tests 147, integration and end-to-end 69
(one skipped); online off `det.sh` twice 4004f056 / 396cb219, the save state
suite 17/17, r25c 10/10 (1,748,011 frames, every segment's count unchanged).
Not done in the lane: the title's application of
`Session::confirmed_story()` at `game_started` (character lane; when it is
wired, every card scenario needs `seed_story` or its members lose their card
story at game start, and S14's guest needs a value it makes in the lobby: k2
without 501 stalls in the first visit's talk at n_digilff #2's `ISSEQNAME
SQ_WALK02`), the FakeClient's story (the fake clients ignore STORY_ACK).

### Round S3 (r54): stage flow in shared instances

Goal: every case of the table in section 5 behaves as decided.

| Lane | Scope |
| --- | --- |
| classes | the script class table and its generator (with `local_scene`, `gather`, `warp`, `boss_intro`, `boss_end`), MATisAllPlayer per class, in-stage forks before story triggers, boss and story mats with *Go without them*, boss locks set and cleared in the simulation, the boss choice at arrival, Trailmon without a fork (participants only), the 20 direct-NEXTSTAGE exits, dngq6b01 as `gather`, joiners' entry without `first.lua` checked across stage types |
| scenes | first task: what the messenger pause and the SOUND freeze protect inside a window (scripted runs with each of them left out, against the original); then the proximity rule (5.3) for `local_scene` and the status window: command scoping that sticks to the script run and its demo records, the local freeze zone, the per-slot demo viewer, draw filters for windows, overlays and fades, DMgetCount online, one window per instance; save room and call shop rules |
| world | per-member pits without the instance-wide steps, *Give up* across stage types, game over and quest rules confirmed, session values across forks and joins (the census classes), the EXP rule chosen in question 7, puzzle mat *Waiting for* hints, NINZU behaviour documented, the local HUD highlight |
| bots-scenarios | FollowBot and the campaign bot online, scenarios S4, S6-S9, S19 (story trigger), S20, S23-S25, an online two-player story segment (leader and follower) through one area including a dungeon boss |

Done when: the scenarios of the lanes pass on the local stack with hidden
clients; the two-player segment reaches the boss's defeat and the area's exit
with identical fingerprints; r25c and nc1 frame-equal with main; the checks of
section 15 hold. If the user chose shared lobbies (question 1), the scenes lane
also covers the lobby programs and the done-when adds a lobby scenario.

Depends on: S1 (S2 for the save room rules), the answers to questions 6 and 7
(defaults otherwise).

### Round S4 (r55): robustness, latency, determinism across machines

| Lane | Scope |
| --- | --- |
| resync | the server's vote with diagnostics, resync by replay with the catch-up limit (beyond it: continue alone), RESUME with missing ticks, LEAVE timeouts, catch-up progress with expected duration |
| latency | delay tuning from the S1 matrix and S12; a UDP transport for INPUT / SEALED if the matrix showed TCP's loss stalls unacceptable; lag indicators |
| determinism | the `dw4::sim` libm routing to MSL routines in online sessions (6.12) and the removal of the FMA3 gate, mod contents in the data hash, version gate, a two-machine test kit and instructions for the user |
| performance | the cheaper conservative frustum test for extra cameras (strict, no contraction), the picture pass with smooth motion at 144 Hz, cost and replay rate re-measured on the final path (libm routing), a server load test with fake clients (200 rooms) |

Done when: S10-S13 pass (the room of ten for 30 minutes without a desync, with
bounded server memory); an injected desync returns to identical fingerprints;
the four-camera simulation pass costs at most 1.6x the one-camera simulation
pass in stag0102 and dng101b1 (measured 2.3-2.9x today), with the same
fingerprint in two runs and with local cameras 0, 2 and 3; the replay rate on
the final path is measured and `Instances:MaxCatchUpSeconds` follows it; the
libm routing changes no offline fingerprint; the checks of section 15 hold.

Depends on: S1-S3.

### Round S5 (r56): presence, comfort, release readiness

| Lane | Scope (shaped by the answers in section 18) |
| --- | --- |
| presence | ghost models in hub stages if wanted (question 1), roster polish |
| travel | warp to a friend's stage from the lobby if wanted (question 4); a *play alone* switch if wanted (question 8) |
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
   the scoped treatment, and joins into a lobby instance replay everything
   since its start. Default: solo with name tags; friends who leave the lobby
   for the same field within a few seconds still start it together.
2. **Story while the owner is away.** Progress the guests make while the room
   owner is not in the room: drop it (as r48 does; the game then takes it back
   at the next stage change and says so), keep it and add it when the owner
   returns, or add it at once? Default: drop, switchable on the server.
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
6. **Menus and talks far from the others.** When one member opens the status
   window or talks to an NPC / reads a map recorder: if everyone is near, the
   whole group pauses as in local co-op. If someone is far away, should (a)
   only that member pause, with the enemies near it frozen too (default: the
   original protection, but only around that member), (b) the whole group
   pause anyway, or (c) only that member pause with no protection?
7. **EXP sharing.** The original gives every player in the group a share of
   every kill, however far away (the leash kept everyone close). Online
   without a leash: (a) only members near the kill share it, a lone fighter
   gets the full solo amount (default), or (b) the original rule for the whole
   instance?
8. **Play alone in a room.** Members in the same stage play together
   automatically (your decision). Should a member also be able to switch to
   "play alone" (never joined into others' instances, still in the room and
   counting for its story)? Default: no switch.

### Answers (user, 2026-10-29)

1. Lobby: solo (the default).
2. Story while the owner is away: drop (the default).
3. Saving the online character: **also automatically** at stage changes and
   when leaving the room, in addition to the save room (not the default;
   section 9 and round S2 / S5 follow it).
4. Warp to a friend: no, walking only (the default).
5. Two players on one PC online: no, one player per PC (the default).
6. Menus and talks far from the others: (a) only that member pauses, the
   enemies near it freeze too (the default).
7. EXP: (a) only members near the kill share it (the default).
8. Play alone in a room: no switch (the default).

## 19. Risks

- **Multi-camera semantics** have no Dolphin reference: an actor animates when
  any member sees it, recorders carry the first member's rounding noise, LOD
  follows the nearest camera. Deterministic and identical to the original for
  one member, but new behaviour for several.
- **Spawning and despawning mid-stage** have no model in the original: other
  species, `q_` packages, HUD, quick panel, item lines, capsules, scripts that
  assume player 1. The spike proved only the same species. Co-arrivals avoid
  it for arrivals together (they are spawned before the stage scripts run).
- **Snapshot completeness**: every field that survives a stage load and is not
  serialized breaks joins, reconnects and resyncs (two were found, plus the
  presentation field that needs the canonical form). The bytes test, the
  long-session cross-process join and a bump discipline guard it.
- **Catch-up time** still grows with an instance's time in the stage: there is
  no mid-stage state (save states are load snapshots plus input logs). Joins
  beyond about 25 minutes of a four-player field (8-12 on a weaker PC) open a
  parallel instance, and a resync beyond the limit leaves a member alone. A
  full mid-stage state would be a large engine project and is not planned.
- **Determinism across CPUs** is unproven (all runs on one CPU). Until S4 the
  FMA3 gate excludes CPUs without FMA3 (older than about 2013-2015); the MSL
  routing then has to prove itself on two machines the user owns.
- **Latency over TCP**: losses cause head-of-line delays. Server-paced sealing
  keeps them local to the member they hit (measured in S1), but that member
  pays with a much larger input delay and picture stalls (0.5 % loss: about
  +100-150 ms and 15-20 stalls per minute). The S1 matrix decided: round S4
  carries UDP with redundancy for INPUT and SEALED (6.4).
- **Server timing**: the 59.94 Hz schedule depends on the server's timer and
  garbage collector; a pause of the server hitches every member of every
  sealed instance equally. A dedicated timer thread and the server GC settings
  keep it small; the S4 load test measures it.
- **Holds at arrival** lengthen the loading screen by up to 3 s (co-arrival)
  or 15 s (entry scene), only when another member is involved.
- **Scoped scenes** are new behaviour: the local freeze zone protects the
  member in the scene from nearby enemies only; enemies walking in from
  outside it are not frozen. Question 6 settles the trade.
- **Forks** discard the other side's script run; state changes before the fork
  exist on both sides (deliberate, instance-class values travel with the
  line). Two instances of one stage do not merge again before the next stage.
- **Story trust**: a modified client can push story flags into the owner's
  save; plausibility rules limit it, the setter table needs hand entries.
- **Hub stages solo** reads "same stage = real co-op" as "nothing to share in
  the lobby"; question 1 settles it.
- **Cost** of a fourth camera on weak CPUs (about +45-65 µs per extra camera on
  a 7800X3D, mostly frustum tests); a slow member only loses inputs to the
  fill, it never slows the others, but its own catch-ups take longer.
- **Plain TCP relay**: tickets and inputs travel unencrypted; tickets are
  one-time and short-lived.
- **Scope**: five rounds plus a buffer; S1 is the largest (the blockers of
  revision 1 all land there). The first rounds compete with the
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
- Netcode critique: `DW4_PC-wt/r51critnet/build` (r48int exe = f915905 plus
  the test switches `DW4_EXTRA_RENDERS`, `DW4_EXTRA_WIDEN`, `DW4_FMA3_OFF`):
  `expA` snapshot with and without audio output (4004f056 both, `frames_read`
  1,348,800 against 0 at offset 15322-15324); `expB` extra renders
  (4004f056 / 4fa35801 everywhere); `expC/stick_entropy.py` (int8 3.7 %, int16
  with noise 19.0 %); `expD/fma_bench.cpp`, `libm_paths.cpp` (std::fma 2.57 /
  26.59 ns; libm path differences); `expE` FMA3 off (stag0102 four players
  0.149 / 0.436 ms per step, fingerprints identical).
- Player critique: greps of `work/port-research/dec`; the 141 MATisAllPlayer
  scripts without an exit or NEXTSTAGE were listed in a scratch file of that
  review.

## Appendix: byte layouts of round S1 (membership lane)

All in the save state archive (`include/dw4/engine/state_archive.hpp`):
little endian, `bool` and `u8` one byte, `int` / `i32` / `u32` four, `u64`
eight, a string or vector as a u32 count followed by its elements, a
`std::optional` as a bool then the value, a `std::map` as a u32 count then
key / value pairs in key order, a `std::array` without a count, an enum as
its underlying type. The integration copies the final form into
`server/DW4Online/README.md`.

**Stage snapshot, version 43** (`encode_snapshot`, `src/engine/save_state.cpp`):
version 42's fields, with the GameSystem block extended by `direct_start`
(bool) and `text_call` (i32) after `gate_stage_no`, and two blocks after the
sound state:

| Field | Type |
| --- | --- |
| OnlineRules | enabled bool, player_cameras u8, leash_off bool, scoped_scenes bool, near_share bool, present u8, leaving u8, boss_lock bool (8 B) |
| personal | 4 x optional PersonalSession: gate_stage_name optional string, gate_stage_no i32, flags map i32 -> u8 |

The canonical form (`SnapshotForm::canonical`) writes the same bytes with 0
for every stream's `frames_read` (u64). An offline snapshot carries 8 B of
rules (all 0) and 4 B of empty personal values more than version 42.

**PlayerCarry** (`encode_carry` / `decode_carry`): u32 magic 0x59524143
("CARY"), u32 layout version (43), the PlayerSlot as the snapshot writes it
(joined, unique, name, digimon, weapons, HP / MP, stats, devices, bank,
disks, bits, bank bits, XDATA, UPCNT, UPDATA, DOWNDATA, the equipment lists,
WAZALIST, the BASE_* tables, cards, base flags / folder / counters), the
BitChip (stage string, position 3 x f32, bits i32, EXP i32, digimon string),
the PersonalSession, the record (vector u8). Decoding throws on another magic
or version, a truncated buffer or trailing bytes. Measured: 2,120 B for the
level-1 Agumon of the dng101b1 test scenario (ctest `online`).

**BarrierResult** (`encode_barrier` / `decode_barrier`): u32 magic 0x52524142
("BARR"), u32 layout version, decision u8 (0 NEW, 1 JOIN, 2 DISSOLVE,
3 CO_ARRIVE), slot_map 4 x i32, u32 co-arrival count (at most 4) then per
co-arrival slot i32 and a carry body (without magic and version), own_slot
i32, optional RoomStory (flags 1024 x u8, folder 12 x u8, counters 16 x i32,
revision u32), dropped flags (vector i32), dropped folder (vector i32),
dropped counters (vector i32, round S2; nothing persists these bytes, so the
layout version still follows the snapshot's),
OnlineRules (8 B).

**Pad log of the test tools** (`DW4_PAD_LOG` / `DW4_PAD_REPLAY`,
`src/game/game_main.cpp`, test runs only): per step u32 host frame before
the step, then 4 pads of connected u8, left x / y, right x / y as f32, buttons
u32 (88 B per step). It is not the relay's pad format (6 B, 6.4).

**Evidence of the membership lane** (worktree `DW4_PC-wt/r52mem`,
`build/`, not committed): `canon/<mode>/<segment>/` (the r25c segments with
`DW4_SNAPSHOT_DUMP`; k8 headless also with `DW4_STAGE_STATES`,
`DW4_PAD_LOG` and `DW4_TRACE_FINGERPRINT=1`), `xjoin/s21`, `xjoin/s22` (the
cross-process joins), `runs/std_*` (the state test through a file on a
direct start), `online_data.log` (the ctest scenarios' output).

## Appendix: DW4R2 wire layout (round S1, pinned)

The one layout of round S1, built by the relay lane (`server/DW4Online/src/DW4Online.Relay/Protocol`) and pinned by
the integration: the game client (`src/game/online/wire.hpp`), the fake clients (`tests/DW4Online.FakeClient`) and the
independent Python client (`tests/e2e/relay_spike_test.py`) speak it; golden byte vectors in
`tests/online_wire_tests.cpp` and `server/DW4Online/tests/DW4Online.Tests/WireGoldenTests.cs` hold both sides to it. All integers little endian; `str` = u8
length + printable ASCII; `blob` = u32 length + bytes; `pad` = 6 B (6.4); masks use bits 0-3 (slots 0-3).

**Room REST.** `POST /rooms` and `POST /rooms/{code}/join` answer with `relayTicket` (one-time, 60 s, role
`member`) and `settingsHash` (16 hex digits); `POST /rooms/{code}/ticket` gives a fresh ticket (a reconnect). `GET
/status` and the room state's `relay` are `{host, port, protocol: "DW4R2"}`. The room state's `instances` are the
relay's groups `{id, stage, visit, mode: hub|solo|sealed, members: [{accountId, username, slot, connected, lagging}],
departing, bossLock, joinable, ticks, parent}`; `members[].instance` names each member's. Room settings: `difficulty`
0-2, `cameraMode` 0-1 (default 0), `lightMode` 0-1 (default 1), `fastLoading`, `randomizer: {enabled, seed (u32),
drops, enemies 0-2, zones 0-2}`.

**Settings hash.** FNV-1a 64 (offset basis 0xcbf29ce484222325, prime 0x100000001b3) of the ASCII text
`DW4R2-settings/1;protocol=P;data=D;difficulty=N;camera=N;light=N;rand=SEED,DROPS,ENEMIES,ZONES;fast=N` (decimal
numbers; DROPS and fast 0/1; `rand=0,0,0,0` while the randomizer is not active, that is seed 0 or no mode on).

**Handshake.** `DW4R2 <ticket> <protocol> <dataHash decimal> <settingsHash hex>\n` -> `OK <member> <serverMs>\n`
(member = the room slot 0-9) or `ERR <code>\n` with code `protocol` (bad line or protocol differs), `ticket`, `data`,
`settings`, `full`. Then frames: type u8, payload length u32, payload; at most 1 MiB; WELCOME is the first frame.

**Tick record** (SEALED, LIVE, STATE_DATA): mask u8 (slots whose pads follow), event count u8, pads (slot order),
events: type u8, length u16, payload. Events: JOIN 1 (slot u8, entrance point u8, carry bytes), LEAVE 2 (slot u8),
INTENT 3 (slot u8, kind u8, argument i32), LEAD_FROM 4 (tick u32: the last sealed tick; the remaining member leads
alone from that tick), DELAY 5 (slot u8, d u8: only that member's sampler uses it). The mask only says which pads
follow; the engine's own present / leaving masks (from JOIN / LEAVE) decide what a pad does (6.5).

Client to server:

| Type | Name | Payload |
| --- | --- | --- |
| 0x01 | INPUT | inst u32, visit u32, first tick u32, count u8 (1-32), pads |
| 0x02 | LOG | inst u32, visit u32, first tick u32, count u16 (1-1200), pads (contiguous from the log's end; overlap is ignored, a gap closes the connection) |
| 0x03 | HEADING | inst u32, stage str, point u8, context hash u64 |
| 0x04 | BARRIER | inst u32, visit u32, barrier tick u32, stage str, point u8, alive mask u8, present mask u8, context hash u64, own carry blob (at most 65,533 bytes: it becomes a JOIN event, whose length is a u16) |
| 0x05 | BARRIER_CHOICE | inst u32, choice u8 (0 wait, 1 alone) |
| 0x06 | SPLIT | inst u32, visit u32, fork tick u32, leaver mask u8 (the side that moves to the child), kind u8 (0 exit, 1 in-stage) |
| 0x07 | SNAPSHOT | inst u32, visit u32, total u32 (at most 4 MiB), offset u32, chunk (the rest; chunks in order from 0) |
| 0x08 | SNAPHASH | inst u32, visit u32, canonical hash u64 |
| 0x09 | READY | inst u32, visit u32, tick u32 (a multiple of 60), net fingerprint u64, replay rate u32, carry blob (at most 65,533 bytes); also a resync's end (no JOIN follows) |
| 0x0A | JOIN_AT | inst u32, visit u32, tick J u32 |
| 0x0B | NETFP | inst u32, visit u32, tick u32 (tick % 60 == 0), hash u64 |
| 0x0C | STATE | inst u32, visit u32, tick u32, flags u8 (1 joinable, 2 membership-safe, 4 boss lock), context hash u64, reason u8 (0 none, 1 entry scene, 2 scene, 3 menu, 4 loading, 5 boss, 6 fork pending, 7 hub) |
| 0x0D | INTENT | inst u32, visit u32, kind u8 (1 go without, 2 give up), argument i32 (sealed instances only) |
| 0x0E | STORY | inst u32, visit u32, confirmed revision u32, phase u8 (0 no claim, else 1-7: flags 1-6, 86), flag count u16 + u16 flags (< 1024), key count u8 + u8 keys (< 12), line count u8 (at most 32) + (counter u8 (< 16), line inst u32, line visit u32, growth i32) each (round S2, 8.7) |
| 0x0F | PRESENCE | stage str (may be empty), inst u32, x f32, y f32, z f32, yaw f32, species u8, level u16, hp u16, max hp u16, mp u16, max mp u16 (at most every 150 ms) |
| 0x10 | RESUME | inst u32, visit u32, slot u8, last executed tick u32 |
| 0x11 | RATE | replay rate u32 (steps per second) |
| 0x12 | PONG | nonce u32, client ms u64 |
| 0x13 | BYE | reason u8 (leaves the relay at once; the room membership stays) |

Server to client:

| Type | Name | Payload |
| --- | --- | --- |
| 0x80 | WELCOME | member u8, inst u32, visit u32, slot u8, mode u8 (0 hub, 1 solo-led, 2 sealed), stage str, last tick u32 (0xFFFFFFFF none), delay u8, max catch-up seconds u16, flags u8 (1 resumable: the member still holds that slot; send RESUME) |
| 0x81 / 0x82 | SEALED / LIVE | inst u32, visit u32, first tick u32, count u16, tick records (SEALED to members, LIVE to observers catching up) |
| 0x83 | JOIN_REQ | inst u32, visit u32, slot u8, leader's delay u8 |
| 0x84 | BARRIER_OK | old inst u32, old visit u32, decision u8 (1 NEW, 2 JOIN, 3 DISSOLVE, 4 CO_ARRIVE), new inst u32, new visit u32, stage str, point u8, own slot u8, slot map 4 x i8 (old slot -> new slot or -1), mode u8, delay u8, co-arrival count u8 + (slot u8, member u8, carry blob) each, story revision u32, story blob (1,100 bytes: flags 1024, folder 12, counters 16 x i32; the same bytes for every member of a group; empty: none), dropped blob (u16 flag count + u16 flags, u8 key count + u8 keys, u8 counter count + u8 counters; empty: none), notice u8 (0 none, 1 other context, 2 full, 3 too long to catch up, 4 hold expired, 5 went alone, 6 join failed), name count u8 + names |
| 0x85 | BARRIER_WAIT | inst u32, reason u8 (1 co-arrival, 2 not joinable, 3 boss lock, 4 snapshot), seconds left u16, name count u8 + names, flags u8 (1 = send BARRIER_CHOICE) |
| 0x86 | STATE_DATA | inst u32, visit u32, total u32, offset u32, chunk (at most 60 KiB): the state blob |
| 0x87 | PRESENCE | member u8, then the PRESENCE fields (members in the same stage outside the own instance) |
| 0x88 | RESYNC | inst u32, visit u32 (a STATE_DATA with purpose resync follows) |
| 0x89 | STORY_ACK | revision u32, story blob (1,100 bytes or empty), flags u8 (1 the answer to this member's STORY), refused count u16 + (kind u8: 0 phase, 1 flag, 2 key, 3 counter; index u16; reason u8: 1 owner away (dropped), 2 held, 3 stage, 4 phase jump, 5 rate, 6 not a member, 7 not story, 8 limit) each (round S2, 8.7) |
| 0x8A | ROOM | notice u8 (1 joined, 2 left, 3 owner present, 4 owner away, 5 lagging, 6 lag over, 7 connected, 8 disconnected), member u8, account u64, name str, flags u8 (1 owner) |
| 0x8B | PING | nonce u32, server ms u64, margin ms i16 (the member's 0.1 % arrival margin), suggested delay u8 (once per second) |
| 0x8C | CLOSE | reason u8 (1 left, 2 kicked, 3 room closed, 4 replaced, 5 protocol, 6 abuse, 7 timeout, 8 server stopping), text str |
| 0x8D | ROSTER | count u8 + (member u8, stage str, inst u32, level u16, hp u16, max hp u16, flags u8: 1 connected, 2 lagging, 4 owner) (once per second) |
| 0x8E | REASSIGN | old inst u32, visit u32, new inst u32, from tick u32, mode u8, flags u8 (1 departing): after a fork the receiver's copy is the child; its stream comes with the new id from that tick |

**State blob** (reassembled STATE_DATA): purpose u8 (1 join, 2 co-arrival, 3 resync, 4 resume), slot u8, codec u8
(0 = raw records), snapshot blob (as the leader uploaded it), fork count u8 + (tick u32, leaver mask u8, kind u8,
side u8: 0 stayers, 1 leavers = the side that leads to this instance), first tick u32 (0), tick count u32, tick
records. LIVE frames continue with the next tick; they come after the blob on the same ordered queue.

**Rules the client must follow** (found while building the relay):

- Pads of a sealed NEW for ticks 0..d-1 may (and should) go out together with SNAPHASH; the relay accepts INPUT
  before it starts sealing (S(0) = the last hash + the largest round trip + two ticks).
- A joiner replays LEAVE, LEAD_FROM and DELAY of the history like any record, but it takes LEAD_FROM and DELAY only
  for itself and only live (a LEAD_FROM in the replayed history is for whoever played then; taking it desynced the
  fake joiner of S22b).
- Every copy ignores events for slots not on its side of a fork (6.7); a JOIN goes with the side that keeps the id.
- The barrier: send BARRIER after the barrier tick; ticks of the old visit that still arrive are discarded. A
  member whose report does not arrive within `Instances:BarrierReportSeconds` (5) is left out of the group and
  arrives alone with its next BARRIER (it is told by its decision).
- After REASSIGN, frames in flight that name the old id are still taken for the new instance.
- BARRIER_OK names the instance and visit the member parked in (old inst and visit, also when a NEW reuses the instance
  with the next visit); a client takes only an answer to its own barrier.
- A held barrier is an answer: a solo client's five seconds without a BARRIER_OK (then NEW as instance 0) count from
  the last BARRIER_WAIT; a boss choice waits without a limit.
- The solo leader answers JOIN_REQ with JOIN_AT(J), samples ahead from then on, repeats its last pad for the solo
  ticks before J, logs them and flushes the LOG up to J-1; from J it executes only SEALED ticks.
- HEADING carries the session context hash: co-arrivals need equal contexts.
