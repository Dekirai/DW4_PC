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
  same way. A member's status window, NPC talks and map recorders are its
  own: they open on its screen only, several at once, and pause nothing
  (round 63, user decision of 2026-10-31, section 5.3).
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
- **Instant joins and rollback** (round 65 design, 6.13): a member's *tick
  state* (the whole simulation state at a confirmed tick, portable, hashed)
  lets a joiner skip the replay of the visit: in play within about half a
  second at any visit length, and resyncs restore a checkpoint the relay
  keeps. A *state arena* (the engine's heap, saved per tick as written pages)
  lets each member execute its own input at once and roll back when the
  sealed ticks disagree: the own character without input delay, every
  confirmed tick still the same on every machine.
- **Room story** = the room owner's online save. The server keeps one
  `online_story` row per account that only the room service writes. Members
  send what their engine has beyond the last confirmed room story; it merges by
  fixed rules and reaches an instance at its next stage entry, never mid-stage.
- **Guests on the host's story** (round 61 design, 8.8): in a room every
  member plays the owner's story (phase, story flags, key items; flag 503
  becomes story, so nobody replays a training the owner played); its own
  online character, its card file and its saves keep its personal values
  and its own story, and outside the room it is back on its own. The call
  shop, which would reload a card's story into a running online engine, is
  closed online.
- **Client.** Account and room UI in ImGui (title overlay and the F1 Online
  tab), the online character as a virtual memory card, an upload whenever the
  save room writes, cloud backup in F1 Saves, online achievements, name tags
  for remote members, Discord party size.
- **The server's character** (round 64 design, 9.1): an account has up to
  four online characters (the data select's four boxes), kept on the server
  in readable tables named as the game names the values; the server composes
  the record the game loads and checks every upload against the game's rules
  (impossible changes: refused, the member loads the server's character
  again; improbable: kept and flagged; a new character against the game's
  own new-character record); staff edit in HeidiSQL or on the website, in
  the player's game at his next load, merged with the saves of a player who
  is online meanwhile; the database stores hashes and addresses as text; the
  website shows player profiles with
  every Digimon, form, stat and piece of gear in the game's colours, pictures
  from the user's own game data.
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
| Talks, recorders, camera tours freeze everyone | talks and map recorders are the member's own scene (round 63, the user's decision of 2026-10-31); camera tours, tutorials and shaking mats still freeze everyone | 5.3 |
| Forks without NEXTSTAGE (boss arenas, Trailmon) undefined | in-stage forks: participants keep the instance; Trailmon does not fork (participants only) | 6.7, 5 |
| A defeated member's camera freezes at the capsule | view of the nearest living member, *Give up* in S1, capsule marker | 4.1, 5 |
| Status window freezes everyone (S1-S2), unsafe from S3 | the member's own window: only on its screen, nothing pauses, no protection (round 63, question 6 decided again) | 5.3 |
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
- (31.10) The online character's values (Bits, EXP, level, stats, X Data,
  forms, items, equipment) live on the server, readable and editable in the
  database, and the game loads them from there; the database shows no binary
  garbage; the website gets player profiles with all four save slots, every
  Digimon and form with its picture, level, stats, Bits, the current weapon,
  armor, chips and mods in the game's colours, and the current Digimon as the
  profile picture (9.1).
- The client talks to the hard-coded address 88.198.52.221 (not changeable; a
  loopback override only in development builds) with a pinned self-signed
  certificate for now, later `https://dekirai.crygod.de/dw4online`.
- Name, level, HP and MP above each player's Digimon.
- Online: no cheats, no rebalancing, no save states; the randomizer is a host
  room setting. Discord application ID 1560921673354453093.
- With online off the original game stays exactly as it is.
- A member who joins a room plays on the room owner's story while it is there
  (every story flag, the key items, everything story progress depends on; what
  the owner did, such as the tutorial, is done for it too); that story never
  lands on its own online character or save, and its personal progress is its
  own (31.10; section 8.8).

### 1.2 Defaults this design chooses

Each is explained in its section; the ones that change the design noticeably
and that the decisions above do not settle are questions in section 18.

| Default | Section |
| --- | --- |
| Hub and UI stages are solo instances with presence: name tags and, since round 60, ghost figures (question 1) | 3.3, 12 |
| One player per machine online (question 5) | 3.1 |
| Same stage and same quest context = same instance, automatically (question 8 asks about a *play alone* switch) | 3.4, 3.7 |
| Arrivals within 3 s start together; later ones wait up to 15 s for the entry scene | 3.4 |
| Instance slots stay stable within a stage; a member keeps its preferred slot when free; slot 0 is refilled at a stage load | 3.6 |
| A one-member instance is led by its member (no input delay); from two members the server seals ticks on its own clock | 6.4 |
| A late pad is filled at once (last pad, then neutral); nobody waits | 6.4 |
| Exits ask every member on the mat; dead members stay with living ones | 5.2, 6.7 |
| Boss and story mats keep the original gather rule plus a *Go without them* choice | 5 |
| A member's status window, NPC talks and map recorders are its own: only on its screen, several at once, nothing pauses, no protection (question 6, decided again 2026-10-31; built in round 63) | 5.3 |
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
| own window / own scene | a member's status window or local scene (NPC talk, map recorder): it runs in the simulation for that member alone, pauses nothing and shows only on its machine (5.3) |
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

Members in the same hub stage see each other through *presence* (12): since
round 60 (the user's decision of 31.10, after a first test over the real
server: "you only see yourself") as *ghost figures*, their Digimon in their
current form with the weapon they hold, walking, running and turning where
they really are, standing in their wait motion, with their name tag (level,
HP, MP) above it; they fade in on arrival and out on leaving. Look only: no
collision, no hits, no talks, no effect on anything; the PC host draws them
from PRESENCE2 packets and they never enter the simulation (12.1). A member in
a status or shop window or a talk keeps standing there (its tag hides); one
that loads, watches a scene or is in a menu stage has no figure. Co-op starts
at the area gate: members who leave the lobby for the same field within a few
seconds start it together as co-arrivals (3.4).

The hub and UI stages are the relay's `StageNames.IsHub` list: stagML02,
stagML11-14 and every stagIS* (title, character select, save room, call
shop, staff roll, the menu stages); figures need a picture of the stage, so
in practice the lobby rooms stagML11-14 and stagML02 show them.

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
   together. When more groups are announced than fit, the base holds for
   those that fit, the earliest announced first; a group another base already
   waits for is that base's. (Round S1, second review: the base held only when
   every announced member fit, so with five arrivals at once the first one
   played alone.)
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
100-299 by a computed number; all of them are instance values. (Round 61's
design makes 503 a story value, 8.8: it leaves the instance class.)

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
Shared by the whole instance: the demo viewer (SETDEMOCAMERA; a member's own
camera tour needs a per-slot viewer, later work, 5.3), the SHAKE pattern
(applied per slot), SETBOXCAMERA (resets every slot). The camera mode and light mode are room settings (3.1), so the camera
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

A demo viewer is instance-wide, as is PADINPUT, except in a member's own
scene (5.3), where PADINPUT reaches that member's pad alone (a viewer for its
camera alone is later work: camera tours keep the original). The stage flow
rules (5) fork non-participants away before story and boss scenes, and a
member's talks and map recorders set no demo camera, so an instance-wide scene
only ever shows to members who took part or stood near. Every camera comes
from lockstep data, so a later per-slot viewer stays deterministic.

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
| Scenes of one member: field talks (88 hit_n_* in fields and dungeons, among them the map recorder on every story dungeon floor), camera tours and demos on mats (stag0102 matf00 'Slope', stag0101 matb01 / matx01 / hit_g_lever01z01 camsp0401-0404), tutorials, shaking mats (dng310f3 matt05) | whole group frozen, HUD off, PADINPUT off, demo camera for all | the talks and map recorders (hit_n_*) are the talking member's own scene (5.3): nothing pauses, only that member's player stops, the window, the floor map and the HUD switch show on its screen only; one talk at a time in the instance, as in local co-op (a second talker's strike only hits until the first talk ended). Camera tours, tutorials and shaking mats: original for now | own scene (scope), copies, picture filter | r63 (talks) / later (tours) |
| Status window (START, SUBWND) | the engine freezes, one window at a time, drawn for all | the member's own window (5.3): opens at once, also while others have theirs open, pauses nothing (only the member's player stops taking its pad and can be hit), drawn and heard on its machine only; another member's window blocks neither exits (DMgetCount) nor joins | own window per slot, copies, picture filter | r63 |
| Shops, bank, Digi-Lab, card keeper, quest board, event console, NPC talks in the lobby | lobby programs | solo hub instance: single player behaviour | DISSOLVE | S1 |
| Save room stagIS12 | SAVECTRL; players no longer joined leave the group | solo; writes the virtual card; uploads the own record (9); *leave the group* hidden online | wrapper | S2 |
| Call shop stagIS13 (LOADCTRL2 without title) | adds or replaces players | off online: `callshop.lua` runs its refusal path with a port hint instead of NEXTSTAGE stagIS13 (every member is its own machine; a call would also reload the card's story into the engine and set the modes to 0, 8.8) | wrapper | G (planned for S3, not built) |
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
  runs do not fork: their instance-wide commands are scoped (5.3; round 63
  builds the `local_scene` part for the hit_n_* scripts by their name, before
  the class table exists).
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

### 5.3 Own windows and scenes (round 63)

**Decision (the user, 2026-10-31).** "Everybody plays for themselves, as in a
real online game": a member's START menu does not open on the other members'
screens and does not pause their game; the game runs on and the players can
still be hurt by the enemies. This replaces the first answer to question 6
(the proximity rule with a local freeze zone): online, a member's status
window - and by the same principle its NPC talks, map recorders and the other
single-member windows and scenes of the class `local_scene` -

1. open only on that member's own screen,
2. never pause the instance: enemies, generators, the other members, timers,
   sounds and music run on,
3. give no protection: the member's Digimon stands without its input and can
   be hit,
4. always, whether the others are near or far, and also while the member is
   alone in its instance (a member may join at any time).

Story scenes the instance shares (the classes `gather`, `boss_intro`,
`boss_end`, 5.1) keep the original. Offline, in local co-op and in the hub and
UI stages (solo instances, 3.3) nothing changes (online off is bit-identical,
section 15).

**What the original's pause protects inside a window.** A scripted hidden run
(round 63, `build/ana` in the round's worktree: dng101b1, the status window
opened with START among the enemies and its three pages walked, the switch
`DW4_TEST_SUBWND_SKIP` leaving out the MODELCTRL freeze, the GENERATER
freeze, the messenger pause and the SOUND freeze one at a time and all
together) gives the window's own course - every SELECTCTRL state change and
every text set with its frame, 147 lines - unchanged in all five variants
(equal md5 with the original). The messenger pause, the SOUND and the
GENERATER freeze change nothing inside the window (the window posts nothing,
its sounds start after the freeze); without the MODELCTRL freeze the world
moves on, and the member's own Digimon takes the window's buttons (A attacks,
B guards, the stick walks: `SQ_ATCK1`, `SQ_GUARD`, `SQ_WALK` while the
window's selects read the same presses). That is the one thing an own window
keeps: the member's player alone stands without its input while the window
is open. Not by OFCTRL: a model whose `_isOnCtrl` is clear takes no hit at
all (`hit_check`, 8030E56C), which is how the original's talks protect
everyone. The player keeps its control and reads the neutral pad instead
(`Engine::hold_own_input`, `Engine::player_pad`), so the enemies hit it - a
hidden run in dng101b1 with the menu opened among the e_nume: 18-19 damage
every second or so while the window stays open - and the window's own
cursors still read the member's pad.

**How it is built** (`src/engine/online_scenes.cpp`, `subwnd.cpp`, `ui.cpp`,
`model_ctrl.cpp`, the picture filter in `online_cameras.cpp` / `engine.cpp`):

- *Scope.* Online outside the hub and UI stages (`Engine::scenes_scoped`), a
  member's status window runs its program in that member's scope
  (`Engine::SceneScope`): the START of a present member opens its own window
  (`SubWindowManager`, one program per slot besides the original one, all of
  them in the simulation, each fed by its member's sealed pads). An NPC talk
  or map recorder (a `hit_n_*` script, the talker = the player that struck
  it) runs in the talker's scope, and so do the demo works it starts
  (`DemoWork::scope`) and the messages they post (`DeferredMessage::scope`).
- *Talks wait for each other.* A talk keeps its state in stage globals from
  its hit script to the scripts its DMsetLua records load later (g_npcRamNo,
  g_messPlayerNo, func.lua's g_talk, an exchange's g_SItem* / g_TItem*), and
  what a talk changes is often the instance's (the common FLAGFOLDER an
  exchange takes its key item from, a quest a Digi-Elf starts, story flags).
  The original never runs two talks at once. So online a talk's hit script
  sees DMgetCount 1 while another member's own talk runs (its busy path: the
  NPC only takes the hit; `Engine::run_own_talk`), as in local co-op, and
  the member talks once the other talk ended; status windows neither wait
  for talks nor make them wait. Besides, every member's own scenes keep
  those talk globals for themselves and the rest of the instance keeps its
  own (`Engine::talk_globals_`, switched around every script run of a
  scope), so an instance-wide script that sets g_messPlayerNo or g_talk while
  a talk runs (a game over, a quest message) leaves the talk alone. The
  round 63 review found two talks at once sharing them: one member's close
  sent the other's recorder back, its own scene never ended (no exit prompt
  and no talk for it any more), and an exchange gave the other talk's item.
- *Exchanges.* The field NPCs' exchanges (`n_*.lua`, `itemchange*.lua`)
  name player 0's device and disk folders (SYSctlDeviceFolder /
  SYSctlDiskFolder) whoever talks; inside a scope player 0 is the talking
  member, so it trades with its own folders.
- *Narrowed commands.* Inside a scope the messenger rewrites or drops what
  would reach the instance: PLAYER -1 OFCTRL / ONCTRL and MODELCTRL -1
  OFCTRL / ONCTRL become the member's input hold (its player stands without
  its input and stays hittable; a scene that ends without its ONCTRL
  releases it; another member frozen in its exit prompt stays frozen), any
  other PLAYER -1 command reaches the member's own player alone (stag0302's
  Gekomon demo, `demo00.lua`: PLAYER -1 SQ_WAIT);
  ENEMY -1 OFCTRL / ONCTRL, every category freeze (ONFREEZE / OFFREEZE to all
  RAMs, GENERATER), SOUND BGMSTOP / ENVSTOP / SESTOP, a BGM or ambience start
  and the quest timer's SQ_STOP / SQ_RESTART (`i_timenm*`) stay out;
  STAGECTL PADINPUT switches the member's own pad (`Engine::pads`), LAYERCTL
  SWITCH the layers of its own picture (the status window's HUD and world
  layers, a talk's HUD). No messenger pause.
- *Own copies.* The window models, cursors and NPC portraits the scope
  addresses (`i_*`, `j_*` and `n_*ic`, but not the HUD and the shared
  overlays: status panels and their icons, damage and item numbers, get-item
  panels, boss HP, quest timer and name, exit prompts, evolution overlays) are
  the member's own copies, `name@slot` with the original's RAM index: the
  model's subtree (its action switch's child) cloned under the same switch,
  hidden until its action shows it, with fresh MODELCTRL, ACTION and
  SELECTCTRL RAMs (`ModelCtrlManager::add_copy`, `ActionManager::add_copy`,
  `SelectCtrlManager::add_copy`). Text lines are copied the same way
  (`TextCtlManager::make_copies`, with their own colour attributes). Copies
  are made on first use, at the same step on every machine (simulation
  state), and go with the stage. So two, three or four members can have
  windows open at once, and an instance-wide scene keeps the originals.
- *Picture and sound.* Every machine's picture leaves out the copies of the
  other members and applies only its own member's layer filter
  (`Engine::picture_hidden_roots`; a one-member instance draws its picture in
  a separate peek pass after the step's simulation traversal when it has
  something to leave out). The sound effects a scope starts play only on its
  member's machine (`SoundManager::set_listener_slot`; the simulation state
  stays the same everywhere).
- *Membership and exits.* An own window or scene holds up neither LEAVE nor
  JOIN (`membership_safe` counts only the unscoped demo works and the
  original program): a member joins while another has its menu open, and the
  joiner's catch-up replays the open window. A member who leaves (or who
  forks to the other side) loses its window and scenes at once
  (`Engine::end_own_scenes`). DMgetCount counts only the unscoped works and
  the asking member's own (another member's talk blocks no exit; only a
  talk's hit script waits for it, above), the exit port neither makes a
  member in its own window or scene the prompt's owner nor asks it, and the
  quick panel and START refuse only for the member's own panel and window.
- *Status window actions.* The window's pages are Equipment (the sockets,
  pockets and slots), Folder (devices: discard, sort, give; key items
  to look at; disks: move or give; bits: send) and Techniques (to look at).
  Everything the window does stays deterministic (the simulation runs it on
  every machine) and touches the member's own slot only, a gift also the
  partner's. Equipment changes recalculate the member's own player; its
  preview's HP / MP restore (OFPREVIEW) keeps what the running world did to
  them meanwhile (a hit taken in the menu stays), and the window's HP / MP
  lines follow the world at once (the original's frozen world never moved
  them), so a member sees the hits it takes in its menu; a member defeated
  in its window leaves it. Gifts to a partner (devices, disks, bits) reach
  only a partner still in the instance (the others are no partners any more;
  nothing is lost), and they look at the partner's room again when they are
  given, not only when the partner list opened: the partner's folder can
  fill meanwhile (another member's window gives too, its player picks items
  up). A device for a full folder stays ("Receiver's device folder is
  full.", text 149), disks give what still fits below 9 (none fits: text
  143), bits what fits below 9,999,999 (none: back to the amount). The
  review had two members give a partner with 35 devices one each at once:
  37 devices, one more than the save keeps. The status window has no item
  use, map, retire or quick panel entries.
- *Presentation.* Name tags show "Kim is in the menu" / "Kim is talking" over
  that member's tag for the others; the member's own screen hides the tags as
  the original's window does. A map recorder's floor map (`j_map*`) is the
  reader's own, and stays on its screen for the floor as in the original.

Not yet own: camera tours and demos on mats, tutorials and the shaking mats
(they still pause the instance, as the original); the per-slot demo viewer
they need is later work.

**Proof (round 63).** ctest `online` scenario "dng101b1 own menus" (two
members, slot 0's window, slot 1's while slot 0's is open, a third member
joins while both are open: nothing frozen, both close and give their players
the pad back, a second engine and a joiner restored with both windows open
stay net-fingerprint-equal); the scenarios after the review: "stag0101 two
recorders" (two members strike their recorders in the same frame: one talk,
the other after it, both recorders back in SQ_WAIT, no hold left, a second
engine equal), "dngq5b1v slot 1 trades alone" (the Mighty Talon in slot 1's
folder), "dngq5b1v Agumon and Guilmon at once" (one figurine, one exchange)
and "stag0101 two gifts" (three members with 35 devices, two gifts at once:
36, nothing lost), and the unit check of the narrowed commands; real hidden
clients on the local stack (`tools/online_test` scenarios M1, M2, below in
section 16) with screenshots from both machines; offline unchanged (section
15).

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
side fails both until the other follows. Message types 0x01-0x14 go client to
server, 0x80-0x90 server to client; a pad is 6 bytes (6.4); `inst` is the
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
| 0x14 | PRESENCE2 | version (1), the PRESENCE fields, motion clip, flags (clip loops, busy), warp counter, sender clock (ms), weapon | instead of PRESENCE once WELCOME announced it: ten times per second while the figure moves, turns or changes its motion, five while it stands (round 60, 12.1) |

Server to client:

| Type | Message | Fields | When |
| --- | --- | --- | --- |
| 0x80 | WELCOME | member (the room slot 0-9), inst, visit, slot, mode (hub, solo-led, sealed), stage, last tick, delay, max catch-up seconds, flags (resumable; presence v2: the hub takes PRESENCE2, round 60) | the first frame after OK: a fresh member's solo hub instance (the title, stage ""), or the instance it still holds |
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
| 0x8F | NETSTAT | round trip median and spread, d, arrival margin, the stream's sealed / filled / late pads, flags (in a stream) | once per second after PING (round 62: the network indicator and online.log; older clients skip it) |
| 0x90 | PRESENCE2 | member, the PRESENCE2 fields | instead of PRESENCE to members that send PRESENCE2 themselves (round 60, 12.1) |

**Message types (the registry).** A number belongs to one message on both
sides and is never given twice, not even across rounds that run in parallel:
a client that cannot decode a frame of a type it knows drops the connection,
so two builds that give one number to two messages at the same protocol
reconnect each other every second. The review of round 62 found exactly that:
round 60's PRESENCE2 to clients took 0x8F at protocol 45, the number NETSTAT
holds since DW4Online 0.1.2, which the user runs (NETSTAT goes to every member
once a second). 0x8F stays NETSTAT; round 60 moved PRESENCE2 to clients to
0x90 (its PRESENCE2 to the relay is 0x14); a new message takes the next free
number of its direction (client to server 0x15, server to client 0x91) and is
entered in the registry checks of both sides
(`WireGoldenTests.Message_types_are_unique_and_registered`,
`tests/online_wire_tests.cpp` "type registry"), which fail until it is; a
number given twice does not compile on either side (the name switches). Since the review the client skips (and logs once) a NETSTAT,
PRESENCE, PRESENCE2, ROSTER or ROOM frame that does not decode: they only feed the
indicator, the name tags and figures and the room list.

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
newest pad, sealed in time or late (at most six ticks after it came), then
neutral. A pad that arrives after its tick was sealed is counted, fills the
slot's next ticks and its new presses go with the slot's next sealed pad
(round 62 and its review: late by a tick or a few, not lost; a held button
stays held). Nobody waits for anybody:
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
member's arrival margin (due time minus arrival) directly. Since round 62 it
raises `d_i` by one when more than 2 % of the last ten seconds of pads (600)
came late, in at least three different seconds of them (`Sealing:LateTarget`;
round 62 had 0.1 %, any one late pad, see its review below), and at once by
up to three when most pads of the last second were late (by the median margin
of that second and half a tick); it lowers `d_i` once thirty seconds of pads came since the
last change (1800: more than one beat of a 60.000 Hz display against the
59.94 Hz ticks, see the review below) by as many ticks as the 0.5 % quantile
of their margin allows with 0.15 of a tick kept (at most three at once). A
fall that has to be undone (d rises again from the value it fell to) bars
that value: one minute, twice as long at each further failure, at most ten
minutes; d then goes back to the value it fell from unless the lateness asks
for more, and the member's next stream (another stage, a fork, a join) keeps
the bar. The rule sees only paced pads: one that came within
4 ms of the member's previous pad (a burst after a stall of the client, of its
downlink or of the relay's sealing, or behind one lost segment) or more than
eight ticks after it (the first after such a stall) says nothing about the
link and is left out; a link too slow for `d` still raises it (its pads come
one by one and late, or in bursts behind gaps of up to eight ticks from a game
running only 8-20 passes a second), and a member whose pads were filled for
half a second in a row (lagging) feeds the rule with every pad. A button
pressed in a pad that came after its tick was sealed, and not held in the
slot's last sealed pad, is sealed with the slot's next pad: a press inside a
burst comes a few ticks late instead of being lost, and the late pad fills the
slot's next ticks. The first `d + 2` ticks of a stream (a sealed start, a
resync's end) do not count: the client sends those pads at once when it learns
of the stream, so the first of them are late by design; a JOIN gives every
stream of the instance `d + 120` ticks (the joiner's spawn loads its packages
in that step on every machine). (Round S1's
rule raised `d_i` by one on four late pads in a minute and lowered it by one
per 30 clean seconds: it turned every hiccup into lasting delay, see
"Measured in round 62" below.) A change is a DELAY event at a sealed tick (the
member repeats one sample, or skips samples and sends their buttons with the
next pad; deterministic, because the sealed pads are what every machine
executes).
Bounds 1..15 (`Sealing:MaxDelayTicks`; revision 2 said 10, which the S1 loss
matrix below showed too small above about 110 ms of round trip). The starting
value is `ceil((RTT + spread) / 16.683 ms + 1.5)` (round 62; S1:
`ceil((RTT + spread) / 16.683 ms) + 1` with the spread up to the 99.9th
percentile of 120 round trips), RTT the median and spread the 90th percentile
above it of the newest 30 PONG round trips, leaving out answers above twice the
median plus 50 ms: a client answers PING in its host loop, and a stage load, a
snapshot restore or a catch-up pass delays single answers by hundreds of ms
(S1 took the largest of the 120 and started a joiner at 6 at 20 ms of round
trip). Measured `d` without loss in S1: 3 at 21 ms RTT, 5 at 61 ms, 7 at 71 ms
with 10 ms jitter, 11 at 121 ms. Inputs are
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
pictures, which moves the audio once, as an offline hitch does. A smaller
excess that lasts (the smoothed level 0.75 ticks above its target of 1.5 for
half a second) runs one extra step (round 62: after a JOIN's long step the
joiner's level stayed near 2.5 for a minute, draining at +0.1 %, and every tick
of it made its pads a tick later at the relay).

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

**Measured in round 62: the guest's input lag (field report of 31.10).** A
guest felt massive, constant input lag on the real server ("everything comes
late"): DW4Online 0.1.0 (the hub of main 890e802, `Play` protocol 43) with a
client of round 56 (protocol 45), first with a friend, then as his own guest,
two games on one Windows 11 PC on wired LAN, one window in front and one in
the background, 20-30 ms of round trip.

*How it was measured.* `tools/online_test` scenario L1: the local stack, every
hidden game client behind its own impairment proxy (`L1@UP[~DOWN]`, each
direction `MS,JITTER,LOSS`; `DW4_ONLINE_RELAY_PORT` in development builds),
the host a into stag0101 first (solo-led), the guest b through the lobby gate
15 s later, into a's instance by a JOIN with catch-up (JOIN_REQ / JOIN_AT, as
in the field), then 100 s of sealed play (wander and fight). L2 is L1 with the
host's game throttled like a background window (`DW4_ONLINE_TEST_STALL`: 30
passes per second and a 150 ms stall every 3 s). The client measures every own
pad (`src/game/online/net_stats.hpp`): when it was sampled, the tick it went
out for (sealed: the executed tick + d), when that tick ran here, when its
picture came, and whether the relay sealed it as sent (else a fill replaced
it: the input was lost); `online-trace: net` every 5 s, the delay changes with
their reason, `latency.json` per scenario. `--server-root` runs the server of
another checkout (0.1.0 = main 890e802), `--play-protocol 43` its field
configuration. The clients run hidden at below normal priority on a machine
busy with other rounds, which is what a background window on a loaded PC
gets. The latency is sample to tick run here; the picture follows in the same
host pass (presented within 0.5 ms in these runs without pictures, one
present later with them).

Every row is one L1 run (two members, 100 s sealed after the join; b is the
guest that joined mid-stage). "Machine" says how busy the PC was: quiet (few
other processes), loaded (the hidden clients of up to three other rounds on
the same 16 threads; stalls of 0.1-0.5 s in every process). d is the input
delay in ticks (16.7 ms): in the first 5 s of sealed play, its largest value
and at the end; latency is the second half's average from sampling a pad to
its tick running here; lost is the share of own pads the relay replaced by a
fill (whole sealed play). The rule's value is `ceil((RTT + J) / f + 1.5)`: 3 at
20 ms, 4 at 40 ms (5 with jitter), 7 at 80 ms, 11-12 at 150 ms.

| Server | Link each way (RTT) | Machine | Member | d first / max / end | Latency | Lost |
| --- | --- | --- | --- | --- | --- | --- |
| DW4Online 0.1.0 (the field's) | 10 ms (20) | loaded | a host | 5 / 12 / 12 | 98 ms | 3.0 % |
| | | | b guest | 5 / 12 / 12 | 115 ms | 2.9 % |
| main before round 62 (the same rule) | 10 ms (20) | loaded | a host | 3 / 8 / 8 | 134 ms | 0.3 % |
| | | | b guest | 7 / 9 / 9 | 150 ms | 0.3 % |
| 0.1.0 | 20 ms (40) | quiet | a / b | 6 / 6 / 6, 8 / 8 / 6 | 90 / 104 ms | 0.1 / 0 % |
| 0.1.0 | 40 ms (80) | quiet | a / b | 8 / 8 / 8, 9 / 9 / 8 | 124 / 134 ms | 0.1 / 0 % |
| 0.1.0 | 75 ms (150) | quiet | a / b | 13 / 13 / 12, 14 / 14 / 12 | 200 / 204 ms | 0 / 0 % |
| 0.1.0 | up 5, down 35 (40) | loaded | a / b | 9 / 10 / 8, 9 / 10 / 8 | 146 / 144 ms | 0.1 / 0.1 % |
| 0.1.0, L2 (host throttled) | 10 ms (20) | loaded | a host / b guest | 13 / 14 / 14, 5 / 8 / 7 | 227 / 127 ms | 0.2 / 0.5 % |
| round 62 | 10 ms (20) | quiet | a / b | 3 / 3 / 3, 5 / 5 / 3 | 50 / 50 ms | 0 / 1.1 % |
| round 62 | 20 ms (40) | quiet | a / b | 5 / 5 / 4, 8 / 8 / 5 (before the JOIN's grace) | 67 / 83 ms | 0.1 / 1.6 % |
| round 62 | 40 ms (80) | quiet | a / b | 7 / 7 / 7, 7 / 7 / 7 | 117 / 117 ms | 0 / 0 % |
| round 62, L2 (host throttled) | 10 ms (20) | quiet | a host / b guest | 5 / 13 / 13, 3 / 4 / 4 | 193 / 52 ms | 1.8 / 0.1 % |
| round 62 (paced pads) | 10 ms (20) | loaded | a / b | 3 / 6 / 4, 3 / 6 / 4 | 71 / 77 ms | 1.8 / 1.7 % |
| round 62 (paced pads) | 20 ms (40) | loaded | a / b | 5 / 7 / 7, 5 / 8 / 6 | 98 / 108 ms | 2.1 / 2.1 % |
| round 62 (paced pads) | 75 ms (150) | loaded | a / b | 11 / 13 / 13, 11 / 13 / 13 | 201 / 205 ms | 6.4 / 1.7 % |
| round 62 (paced pads) | 20 ms + 0-5 ms jitter (47) | loaded | a / b | 5 / 7 / 6, 5 / 7 / 6 | 94 / 95 ms | 1.9 / 3.8 % |
| round 62 (paced pads) | 20 ms, 1 % loss (42) | loaded | a / b | 5 / 6 / 6, 5 / 6 / 5 | 89 / 91 ms | 7.0 / 7.0 % |
| round 62 (paced pads) | up 5, down 35 (41) | loaded | a / b | 5 / 6 / 5, 5 / 6 / 5 | 87 / 87 ms | 1.2 / 1.1 % |

With the rule of round 62 but before the paced pads (every late pad counted),
the loaded machine still ratcheted: 3 to 8 / 9 at 20 ms, 5 to 9 at 40 ms, 12
to 15 at 150 ms, 9-11 at 1 % loss (losing 1.3 %), each rise right after a
second of backlog runs (the clients' downlink, the relay's sealing or the
clients themselves stalled). That is what led to the paced pads. A run while
four rounds' clients shared the PC (stalls of 1-4 s in every process) is no
measurement of the rule: members were taken out for lag and half the pads
were filled; it is left out. The rows "paced pads" ran before the last two
changes of the round: a press in a late pad now goes with the next sealed pad
(at 1 % loss the 7 % filled pads repeat the previous pad, the presses among
them come a few ticks late), and a burst's first pad after a gap of up to
eight ticks (a game running 8-20 passes a second) or any pad of a member that
lags counts again (two real-client scenarios lost a member to the lag rule on
the four-round machine with the narrower window).

Offline the same input runs in the step of its pass (0 ms); the picture
follows as offline. "Lost" is the share of own pads the relay replaced by a
fill.

*Causes, each shown by these runs.*

1. **The starting d took the largest of 120 round trips.** A client answers
   PING in its host loop: a stage load, the snapshot restore of a join or a
   catch-up pass delays single PONGs by hundreds of ms, and
   `FromRoundTrips` took the 99.9th percentile of 120 samples (the largest).
   The joiner's JOIN came with d 6 at 20 ms of round trip (main, `delay 3 -> 6`
   at J); on 0.1.0 the solo suggestion was 4-5 before any join.
2. **Every join raised the joiner's d by one more.** The joiner learns J when
   it executes J and then sends its first d pads at once; the first of them
   are late by design, more than three late pads raised d at the next
   evaluation (main: 6 -> 7 at J + 33).
3. **The rule ratcheted.** Four late pads in a minute raised d by one, thirty
   clean seconds lowered it by one. Any hiccup of a client (a load, a
   background window's stall, a busy CPU) raised it, and before thirty
   seconds passed the next one came: both members went from 3 to 8 / 9 (main)
   and 12 / 12 (0.1.0) within about 15 s of load, then stayed for the rest of
   the stage (200 ms of input delay at 20 ms of round trip). This is the
   "massive and constant" lag; the protocol mismatch was not involved.
4. **The joiner's playout buffer stayed a tick too full.** The JOIN step loads
   the joiner's packages; afterwards its buffer stood near 2.5 ticks (target
   1.5) and drained at +0.1 %, a tick per 17 s. Every tick of it made its pads
   a tick later at the relay, so the rule kept its d a tick above the host's.
5. **A JOIN's step made every member's next pads late.** The step that applies
   a JOIN spawns the joiner on every machine (its species and weapon
   packages load in it): at 40 ms of round trip the joiner rose from 5 to 8
   in the second after its JOIN and needed 20 s to come back to 5.
6. **Bursts after stalls raised d although no network was slow.** On a busy
   PC (other games, other rounds' clients) the clients, their downlink and
   the relay's sealing thread stalled for 0.1-0.5 s now and then; each stall
   let a member's pads out late in one burst. Counting them, even the rule of
   causes 1-3 fixed went from 3 to 8 / 9 at 20 ms within two minutes of load
   (every rise right after a second of backlog runs). Two games on one PC are
   such a busy PC.
7. **Not causes:** the protocol-45 client works with the 0.1.0 hub (one wire
   layout since S1; 0.1.0 ignores STORY, so the room has no story, `story_ignored`),
   and its delay rule is main's (SlotStream.cs did not change after 890e802);
   the relay's sealing timer is high resolution (its log says so) and TCP_NODELAY
   is set on both ends; the catch-up of the join finished in about one second
   (3,300-4,200 steps per second). A slow member never slowed the other: in L2
   the throttled host went to d 13-14, the guest stayed at its own value.

*Fixes (round 62).* Hub: the starting d from the median and the p90 spread of
the newest 30 round trips with stalls left out, `ceil((RTT + J) / f + 1.5)`;
no rise from a stream's first `d + 2` ticks, nor from anyone's pads in the
`d + 120` ticks after a JOIN; the rule above (rise on paced late pads in three
different seconds of ten, at once on a mostly late second; fall after ten
seconds by the margin, up to three ticks); presses of late pads go with the
next sealed pad; NETSTAT for the client. Client:
a lasting playout excess runs an extra step; skipped samples keep their
buttons; an online session opts out of Windows' background throttling and a
minimized window's loop sleeps instead of spinning; the measurements above,
`saves/online.log` and the network indicator.

*What remains inherent.* Lockstep without prediction delays every input by d
ticks: `ceil((RTT + J) / 16.683 ms + 1.5)`, the 1.5 for the playout tick and
the host pass. Measured after the fixes: 3 ticks (50 ms) at 20 ms of round
trip, 4-5 (67-83 ms) at 40 ms, 7 (117 ms) at 80 ms, 11-13 (183-217 ms) at
150 ms. The review of round 62 (below) corrected the rest of this paragraph:
a game presenting at 60 Hz with vsync did not settle at 3-4 at 20-30 ms of
ping but swung between 3, 4 and 5 every 33 s (since the review it holds 4),
and on a busy PC d did not come back within 10-20 s (since the review it rises
less often and falls after 30 s). A member whose PC stalls regularly (a
throttled background game) raises only its own d. TCP loss still costs a lost
segment's round trip plus about 50 ms (the S1 matrix); the UDP transport of
round S4 remains the answer to that.

*Prediction or rollback for the own player: assessed, not built.* The engine
is deterministic across machines (the net fingerprints agree every 60 ticks
for 10,000+ ticks in every scenario) and steps without pictures or audio
(`Engine::set_replay`, 3,300-4,200 steps per second in a catch-up, about
0.25-0.3 ms per step), so re-simulating d ticks per picture would cost about
1-2 ms at d 3-6. What it lacks is a state to go back to: a snapshot is the
stage's load snapshot (21 KB in stag0101) and a state is that snapshot plus
the pads since the load, restored by loading the stage and replaying every
step (6,000 ticks into a stage: 1.5-2 s). Rollback needs a copy of the whole
mutable engine state per tick (the Lua 4 VM heap, every manager, the scene
graph's matrices, motion and particle state, the sound positions), which the
engine keeps in pointer-linked heap objects; copying it means either an
arena for all mutable engine state that is copied and restored as a block
(its size per stage was not measured here; at memory speed a few MB cost
about a millisecond) with the read-only resources kept outside, or a
serializer per manager. Both are a project of several rounds, and the
presentation side effects inside steps (sounds started, effects spawned) would
need the replay mode's suppression during every re-simulation. Until then the
honest number is d.

*Checks of round 62.* ctest 44 of 44; `det` 4004f056 / 396cb219 in four
runs with online off; the server's unit tests (185) and integration and
end-to-end tests (90, one skipped: the deploy kit's certificate store); the
real-client scenarios S2, S3, S18b, S19b and S22a pass with the paced rule's
first window, S1, S11a and S17a lost a member to the lag rule on the
four-round machine with it and pass with the final rule (S11a's resync
included), every instance's net fingerprints equal; L1 and L2 pass in every
cell above. Hidden windows only: what a visible window in the background does
to its own loop (vsync of an occluded window, two GL windows on one GPU) was
not measured here; its effect on the others is L2's, and its own numbers come
with `saves/online.log` from the field.

**Review of round 62 (04.11): the 60 Hz beat, held buttons, the message
numbers.** DW4Online 0.1.2 (round 62 as the user got it) had three faults the
review found and measured; DW4Online 0.1.3 and the client of this branch fix
them.

1. *A game with vsync at 60 Hz swung its own d.* A game presenting with vsync
   at 60.000 Hz (the default: `settings.ini` vsync on, a 60 Hz monitor) runs
   its passes, and so sends its pads, on a 60.000 Hz grid; the relay seals at
   59.94 Hz. The pad for tick t + d leaves at the first pass after tick t ran,
   so its arrival margin slides by one pass (16.7 ms) over a beat of 16.7 s and
   jumps back. Round 62's fall looked at ten seconds with 0.15 of a tick kept:
   it saw only the high part of the sawtooth, fell, and in the low part most
   pads of a second came late, which raised d by two: 4 -> 3 -> 5 -> 4 every
   33 s at 24 ms of round trip (the review's real clients on a 60.000 Hz pass
   grid: 75-193 of about 5,100 inputs not sealed as sent, about 120 late pads
   in a row in each cycle; the hub of 0.1.0 held 4). The hidden test clients
   have no vsync, which is why L1 never showed it. *Fix:* the fall looks at
   thirty seconds of pads since the last change (more than one beat); a fall
   that has to be undone bars that value (one minute, twice as long at each
   further failure, at most ten minutes), d goes back to the value it fell
   from, and the member's next stream keeps the bar (a display at 59.95 Hz
   beats every 100 s, longer than any window).
2. *A held button toggled at 30 Hz.* Round 62 carried a press of a late pad
   into the next sealed pad, but filled the missing ticks with the last pad
   sealed in time. Where seals and late pads alternate (d a tick too low: in
   the low part of the beat above, after a fall, at a join) a held A was sealed
   0010101... (the carried press on, the old fill off), and after six fills
   every held button toggled: phantom repeated attacks and confirms. *Fix:*
   the newest pad, sealed in time or late, is the fill source; the same
   stretch now seals 0011111... (held, one tick late, released one tick late).
   A late pad now costs that input a tick or two, not the input.
3. *One number, two messages.* Round 60's PRESENCE2 to clients took 0x8F at
   protocol 45, NETSTAT's number since 0.1.2 (6.3 "Message types"); a client
   that cannot decode a frame of a known type drops the connection, so a
   round 60 client on a 0.1.2 or later server would reconnect every second.
   0x8F stays NETSTAT; registry checks on both sides, and the client skips an
   informational frame (NETSTAT, PRESENCE, ROSTER, ROOM) that does not decode.
   Round 60 has to move PRESENCE2's server-to-client number to 0x90 before it
   merges (done in round 60's fix round, with `main` merged in: PRESENCE2 to
   clients is 0x90 and informational too).

And one consequence of 2: since a late pad costs only a tick of that input,
the rise no longer reacts to any one paced late pad in three different seconds
of ten (`Sealing:LateTarget` 0.001), only to more than 2 % (0.02). On a busy PC
the first pad after each stall of the client, of its downlink or of the relay
(up to eight ticks of gap: the rule must count a game that runs only 8-20
passes a second) and the first pad behind each lost TCP segment raised d by a
tick every few such seconds, and the thirty seconds of fall took long to give
it back. A link too slow for d still rises at once (most of a second late) or
by the 2 % (a slow game: the first pad of every burst late). A mostly late
second raises d by its median lateness and half a tick, no longer a whole
tick more: a joiner 2 ms late at 20 ms of round trip went from 3 to 5 and,
with the thirty seconds of fall, needed a minute back to 3 (now 3 -> 4).

*Closed loop* (`server/DW4Online/tests/DW4Online.Tests/DelayLoopTests.cs`, the
review's model as tests: the real `SlotStream` and `DelayController`, a port
of `pacing.cpp`, the client's passes on a display's grid, the start value from
PING round trips that include the wait for the next pass; ten minutes, three
seeds, late pads counted after the first minute; the same seeds for both
rules):

| Passes | Round trip | Round 62: late, d changes after the first minute | Review fix |
| --- | --- | --- | --- |
| 60.000 Hz | 24 ms | 0.71 %, none (d 3 too low in a part of the beat) | 0.00 %, none, d 4 |
| 60.000 Hz | 26 ms | 3.43 %, 50 (3 / 4 / 5 every 33 s) | 0.00 %, none, d 4 |
| 60.000 Hz | 40 ms | 2.52 %, 10 | 0.00 %, none, d 5 |
| 59.95 Hz (100 s beat) | 24 ms | 0.86 %, 16 | 0.32 %, 4 (tries at about 0.5, 2.5, 5.5, 10.5, 20, 32 minutes; 0.13-0.21 % after 15 minutes) |
| 59.95 Hz | 40 ms | 1.06 %, 16 | 0.61 %, 6 (0.15-0.17 % after 15 minutes) |
| 59.94 Hz, 144 Hz, no vsync | 20-150 ms | 0.00 % | 0.00 % (d 3 at 20 ms, 4 at 24-30 ms, 5 at 40-50, 7 at 80, 11 at 150) |

With client stalls of 20-150 ms at random (`LoadTable`; 60 Hz, 24 ms; the
time from a pad's sample to the seal that carries it, late pads by the seal
after they came): quiet d 4, 38 ms; 0.2 / 0.5 / 1 stalls a second with 0.1 %:
mean d 6.1 / 8.4 / 9.9, 64 / 94 / 112 ms, 1.0-1.2 % late; with 2 %: 5.0 / 5.3 /
7.5, 44 / 43 / 73 ms, 1.6-3.3 % late (without vsync at 0.2 and 0.5 a second it
stays at 4).

*Real clients* (hidden, loopback, every client behind its own proxy; L3 is L1
with both games' passes on a 60.000 Hz grid, `DW4_ONLINE_TEST_PASS_HZ=60`, the
review's case; the machine loaded by other rounds' clients and builds, 50-85 %
of its CPU):

"Backlogs" is how often a client ran a backlog of sealed ticks at once
(stalls of the client or its downlink, per client and run), a measure of the
machine's load; "late input" is the share of own pads not sealed as sent (since
the review mostly sealed a tick or a few later, not lost); latency as in the
table above.

| Link each way (RTT) | Hub | Backlogs | d first / max / end (a, b) | Latency a / b | Late input a / b (of ~5,100) |
| --- | --- | --- | --- | --- | --- |
| L3: 12 ms + 1 ms jitter (24), 60 Hz grid | 0.1.2, the review's runs | 0-2 | 4 -> 3 -> 5 -> 4 every 33 s | 71 / 71 ms | 75 / 193 and 85 / 91 |
| | 0.1.2, this branch | 98-109 | 5 / 12 / 6, 5 / 12 / 8 | 157 / 164 ms | 244 / 182 |
| | fix with 0.1 % | 3-4 | 4 / 6 / 4, 4 / 6 / 4 | 89 / 88 ms | 22 / 3 |
| | fix (0.1.3), two runs | 0-2 | 4 / 4 / 4, 4 / 4 / 4 | 67 / 67 ms | 0 / 0, 0 / 0 |
| L3: 13 ms + 1 ms (26), 60 Hz grid | fix with 0.1 % | 0-3 | 4 / 4 / 4, 5 / 5 / 4 | 67 / 70 ms | 0 / 0 |
| | fix (0.1.3) | 11-14 (stalls to 0.8 s) | 5 / 8 / 8, 5 / 6 / 6 | 92 / 87 ms | 151 / 167 |
| L3: 15 ms + 1 ms (30), 60 Hz grid | fix (0.1.3) | 0-3 | 5 / 5 / 4, 5 / 5 / 4 | 71 / 70 ms | 0 / 0 |
| L1: 10 ms (20), no vsync | fix (0.1.3), three runs | 0-2 | 3 / 3 / 3; joiner 5 / 5 / 3, 4 / 4 / 3, 3 / 3 / 3 | 50 / 50-64 ms | 0-2 / 0 |
| L1: 20 ms (40) | fix with 0.1 % | 0-3 | 5 / 5 / 5, 5 / 5 / 5 | 83 / 83 ms | 0 / 0 |
| | fix (0.1.3) | 0-3 | 5 / 5 / 5, 5 / 5 / 5 | 83 / 83 ms | 2 / 2 |
| | fix (0.1.3) | 8-13 (stalls to 0.56 s) | 5 / 9 / 5, 5 / 9 / 5 | 124 / 123 ms | 41 / 46 |
| L1: 20 ms + 0-5 ms jitter (47) | fix with 0.1 % | 0-3 | 5 / 6 / 6, 5 / 6 / 6 | 87 / 88 ms | 32 / 34 |
| | fix (0.1.3) | 0-3 | 5 / 5 / 5, 5 / 5 / 5 | 83 / 83 ms | 0 / 0 |
| L1: 20 ms, 1 % loss (42) | fix with 0.1 % | 56-62 | 5 / 10 / 10, 5 / 11 / 11 | 163 / 165 ms | 35 / 23 |
| | fix (0.1.3) | 55-66 (the loss's own stalls) | 5 / 8 / 8, 5 / 9 / 9 | 132 / 142 ms | 130 / 119 |
| L2: 10 ms, the host throttled (30 pictures a second, a 150 ms stall every 3 s) | fix with 0.1 % | 56 / 10 | host 5 / 8 / 8, guest 5 / 7 / 7 | 110 / 98 ms | 230 / 33 |
| | fix (0.1.3) | 32 / 3 | host 5 / 6 / 6, guest 3 / 4 / 3 | 95 / 59 ms | 199 / 6 |
| L1: 75 ms (150) | fix with 0.1 % | 16-19 | 11 / 15 / 15, 11 / 15 / 15 | 221 / 224 ms | 66 / 55 |

On a quiet machine the fix holds d where the network puts it, with no late
input: 3 at 20 ms, 4 at 24-30 ms with the 60 Hz grid, 5 at 40 ms. On the
loaded machine of this round (other rounds' scenario clients and four 600,000
frame campaign runs, 75-85 % of the CPU, the test clients at below normal
priority) the runs differ more by the load of the moment than by the rule;
stalls of half a second and more make a client's pads late in whole seconds,
which raises its d by the "most late" rule as it must (a client that cannot
keep up is filled otherwise), and the thirty-second fall brings it back. Where
the load was alike, 2 % kept d lower than 0.1 %: jitter 5 instead of 6, 1 %
loss 8-9 instead of 10-11 (30 ms less input delay, its late pads a tick late
instead), and L2's guest at 3-4 next to a throttled host (0.1 %: 5 -> 7). TCP
loss still costs the UDP transport's absence: at 1 % the input delay roughly
doubles.

*What the round 62 table promised and did not hold.* The review could not
reproduce two rows: at 1 % loss it measured d 5 -> 10 (155-163 ms, 40 ms of
round trip), 5 -> 11 with 5 ms of jitter, 4 -> 8 at 0.5 % and 25 ms, not
"5 / 6 / 5-6 (90 ms)"; under full load (13 compilers, 8-11 hidden clients)
3 -> 12 at 20 ms and 35 s until 3-4 again, 4 -> 8 at 40 ms and staying, not
"one or two more, back within 10-20 s". This branch's runs with the 0.1 % rule
on a loaded machine agree: 5 -> 10 / 11 at 1 % loss (163 ms), 11 -> 15 at
150 ms and 3 -> 7 for L2's guest. The rows above with 2 % replace them.

*Minors of the review.* A JOIN's grace (`d + 120` ticks of every stream) hid
the joiner's lateness but did not prevent it: under load the review saw about
200 of the joiner's 580 pads in the first ten seconds after J late and not
sealed as sent. With the newest pad as the fill source those pads come a
tick or a few late instead of being lost: in this branch's L1 runs at 10 ms
each way the joiner had as many late pads in those ten seconds (220 and 225;
the review 203 and 210), but only 40 and 38 were sealed other than sent (the
review 198 and 203), and those mostly a tick late, not lost. (The grace stays:
a rise in it would follow the JOIN step's long step on every machine, cause 5
above.) `saves/online.log` now says what the two-window case needs: the
game's passes and pictures per second with the longest pass (every ten
seconds), vsync, the swap interval,
the display's refresh rate, the frame limit, and a line whenever the window
goes to the background, is covered, minimized or comes back; the session
latency at the end counts only the pads that waited for the network (it
mixed in the solo ticks' 0 ms: "17 ms" for 67 ms). The indicator's "Ping" is
the network's round trip: the relay's PING / PONG median holds the wait of a
PING for this game's next pass (half a pass on average, 8 ms at 60 pictures a
second: "34 ms" at 24 ms of network); F1 > Online shows both and wraps its
lines, with "Copy path" and "Open folder" for the log. A second game whose
`online.log` the first holds no longer rotates it (it deleted `online.3.log`
and left no `online.1.log`); a rotation moves the current file aside first.

*What a guest should feel.* Lockstep without prediction delays every input by
d ticks of 16.7 ms plus the wait for the next picture, nothing else: at 20 ms
of ping 3 ticks (50 ms), at 25-30 ms with a 60 Hz display 4 (67 ms), at 40 ms
5 (83 ms), at 80 ms 7 (117 ms), at 150 ms 11-12. The field's "massive,
constant" lag was round 62's causes 1-3 (0.1.0 ratcheted both members to 12,
200 ms of input delay, at 20 ms of ping, a throttled host to 14); 0.1.2 fixed
that but, with a 60 Hz
display, swung between 3, 4 and 5 with a second of lost or toggled inputs every
33 s. Two games on one PC are a busy PC: each stall of either game makes its
own pads late (a tick of that input), and only a lasting share above 2 % or a
mostly late second raises its own d; the other member's d never follows.

*Checks of the review fixes.* ctest 44 of 44 (new: the type registry, the
held log of a second game, undecodable informational frames, the pass wait);
`det` 4004f056 / 396cb219 in two runs with online off; the server's unit tests
206 (new: `DelayLoopTests`, the bar, the fill source, the registry) and
integration and end-to-end tests 90 of 91 (one skipped: the deploy kit's
certificate store; a private database with a fresh data directory); the
real-client scenarios S1, S2, S3, S11a, S17a, S18b, S19b and S22a pass with
every instance's net fingerprints equal (S3 once failed on the order of its
two joins when the machine stalled for three seconds, and passed again); L1,
L2, L3 and L1s pass in every cell above; the screenshots of L1s show F1 >
Online's wrapped lines with the log's full path and the two buttons, and the
indicator "Ping 27 ms  Delay 4 (67 ms)" (27 ms network, 28 ms at the relay for
a client without vsync).

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
  not loading, presenting, no fork pending. LEAVE and INTENT wait for it. A
  member's own window or scene (5.3) counts for none of it: a member joins or
  leaves while another has its menu open or talks.
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

An exit prompt of a leaving member (its own prompt, or the one asking it)
does not wait for the leader's pad: it closes as No in the step after the
LEAVE's tick, on every machine alike (`Engine::close_leaving_prompt`, a
SELECTCTRL REMOVE as the prompt's own timeout uses it). Other members do not
see that prompt (its picture is filtered), so the leader's pad answered it
only by chance: a leader who only walked kept the instance in it for good,
with joins blocked and no prompt of its own, and a stray press answered it
Yes (round S1, second review; real-client scenario S18b). Status windows keep
the leader's pad.

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

**Round J replaces the catch-up from the load (round 65 design, 6.13).** The
steps below replay the whole visit, which grows by about one second per
minute of visit (measured: 1.31 s after one minute, 31 s after thirty, two
members). From round J a member of the instance sends its *tick state* at a
confirmed tick T, the joiner rebuilds the engine at T (the load snapshot, the
visit's load ticks, the topology journal, the values) and replays only the
ticks after T: about 0.3-0.45 s from the barrier to playing at any visit
length (6.13.5). The steps below stay as the fallback when no checkpoint can
be had, and steps 4-6 (the JOIN, the pads, the music) stay for both paths.

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
(From round J: its checkpoints start a join's or a resync's catch-up at most
600 ticks before the present, so the limit applies only to the fallback,
6.13.4-6.13.5.)

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

**The catch-up's wall clock (round S1, second review).** A catch-up runs in
the host loop: each pass replays for `replay_budget` and then draws the
loading screen. With 12 ms a pass a 60 Hz window got about 70 % of the rate
the client reported (steps per second inside the replay only), so the
server's estimates were a third too short. Now a pass replays for 50 ms (the
loading screen still gets about 15 pictures a second, the share is about
75-85 % with vsync) and the client reports steps per second of the wall
clock since the catch-up began, the host's passes included and only the
waits for ticks left out: the rate the server plans with is the one the
catch-up reaches. Before its first catch-up the calibration's rate counts
for the share of a pass, `budget / (budget + 1/59.94 s)`.

**Before J (round S1, second review).** From JOIN_AT the instance is sealed
from J while the log before J still comes from the leader's LOG; the ticks
from J wait beyond the log until it does. If the leader goes before its LOG
reached J (a crash or a lost connection right after JOIN_AT, past the
reconnect window), or the waiting ticks pass `Instances:MaxAheadSeconds`
(15 s), that gap can never close: the joiners' engines never got their JOIN,
so the relay sends them back to their own load snapshot as NEW ("Joining ...
failed") and drops the waiting ticks; a leader still there is closed (its
client waits for ticks that cannot come). (Before, the joiner waited on its
loading screen for good while the instance sealed into memory.) A client
keeps the LOG pads a broken connection did not send and sends them again
after its RESUME. A JOIN_AT whose joiner went meanwhile (BYE, a lost
connection, its own barrier, the JOIN_REQ tries ran out) is answered all the
same: the relay seals J with the leader alone and LEAD_FROM J+1, so the
leader's client, which switched to delayed sampling and waits for SEALED(J),
leads alone again (the instance turns solo-led once the log reaches J).

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

A BARRIER that is decided alone may send a whole state (a join), so a member
gets at most one such decision per `Instances:LoneBarrierMs` (1 s) and none
while its last state is still being built; more are dropped (an honest
client loads a stage in between). (Round S1, second review: one member's
4,276 BARRIER built states for minutes, kept 2.5 cores busy and froze a
sealed pair in another room for over a minute.)

**A fork confirmed at a barrier (round S1, second review).** A member that
takes an exit reaches its stage change about two seconds after the fork tick
(110 ticks in the real runs); if a partner's SPLIT comes later than that (a
WLAN dropout, a TCP back-off), the leaver's BARRIER reaches the parent before
the fork is confirmed and stops its sealing. When the fork comes, the
barrier's reports go with their side: the side that reported waits for its
own members (or parks at once), the side without reports plays on at once,
its sealing shifted by the stall, and a side of one member leads alone
(LEAD_FROM). A barrier none of whose reporters is still in the instance never
ends it while it has members: they play on. (Before, the child copied the
stopped sealing and never started, and the parent's barrier ended its last
member's instance without LEAVE or LEAD_FROM: frozen for good.)

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

**Floods (round S1, second review).** A visit has one snapshot upload: its
first chunk fixes the total (at most 4 MiB), another total is a protocol
fault, and the buffer grows with the chunks that came (a new total per
17-byte frame made the relay allocate 4 MiB each, 13 GB in 20 s). Every member
has a budget of state bytes (`Relay:StateBytesPerMinute`, 64 MiB, a burst of a
minute's worth refilled over a minute; a single larger state passes when the
budget is full); a member past it is closed as abuse. At most
`Relay:MaxConcurrentStateBuilds` (2) states are built at once in the whole
relay, and a state that a newer one for the same member replaced is not
built. A connection's unsent game frames are limited to
`Relay:MaxQueuedBytes` (4 MiB, a member is sent about 5 KB/s) and its unsent
state transfers to `Relay:MaxQueuedStateBytes` (24 MiB): one address with
its 16 connections could park 512 MiB before.

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

- A majority (two or more equal) wins. Without one (two members) the member
  whose engine no resync's replay contradicted wins, then the longer member.
- The others get RESYNC: they rebuild by replay from snapshot and log (6.6
  rules, with the fork sides), keep their slot, and the server fills their
  pads meanwhile; the member sees the progress and the expected duration.
- **Limit.** A resync whose expected catch-up exceeds
  `Instances:MaxCatchUpSeconds` is not attempted: the server stamps LEAVE for
  the member's slot and its machine continues the stage alone from its own
  state (it despawns the others, a solo instance from then on) with "Lost sync
  with the group - you continue alone; you meet again at the next stage". The
  same happens after `Instances:ResyncLimit` (3) resyncs of one member within
  `Instances:ResyncWindowMinutes` (10): one more lost vote and it continues
  alone. The server keeps both fingerprint lines and the last ticks for
  diagnosis.
- **The replay judges (round S1, second review).** A resync's READY carries
  the fingerprint of its replay of the server's log at that tick: what every
  engine of the instance must have had. A member whose own report of that
  tick differs (now or when it comes) is the one that is off: it is resynced
  and loses the next two-member votes. (Before, the longer member always won:
  a desync on its machine resynced the healthy partner every ten seconds,
  each catch-up a longer freeze, until the healthy one was removed.) Only a
  replay from the load judges: from round J a READY after a rebuild from a
  checkpoint carries the flag *from checkpoint* and never judges, because its
  state came from one of the two members (6.13.4).
- **Continue alone.** The member's LEAVE is stamped like any other; a member
  taken out while it still plays (a resync too long or too many, lag, a
  missing hash) is sent the ticks up to its LEAVE, and its client, seeing its
  own LEAVE live, plays on alone from its own state: the others' players are
  marked leaving and removed at the first membership-safe step, nothing is
  logged or compared, and its next barrier arrives alone (6.7). (Before, it
  was sent nothing after its removal and waited for ticks for good.)
- A mismatch at READY restarts the catch-up once, then falls back (6.6).
- **One report per member and tick, no resync at a barrier.** A second NETFP
  of a member for the same tick is dropped (one for a tick already compared
  equal started a vote against its partner at will). While the instance waits
  at a barrier nobody is resynced, and a client takes RESYNC only while it
  plays the instance's sealed ticks and drops a state that reaches it parked
  (a resync that reached a member after its BARRIER threw it back into the
  stage it had left, restored as its load). A fork reported at or before a
  tick delays that tick's vote until it is confirmed or runs out
  (`Instances:ForkConfirmMs`), then the tick is compared (a forged SPLIT every
  two seconds hid every comparison). The instance's flags and context (STATE)
  come from one member only, the connected one that has been in the instance
  longest (then the lowest slot).
- **One vote per tick.** Only sealed ticks count, every tick is voted on once,
  a member is resynced at most once at a time and once per
  `Instances:ResyncCooldownSeconds` (10 s; further lost votes meanwhile are
  only counted), and the relay does not lag-remove a member while it catches
  up. A resync ends with the member's READY: no JOIN follows (it never left
  the instance), the client goes back to sending its pads from the tick it
  reached (`Session::finish_resync`; a replay that reaches the group's stage
  change parks with the group; a LEAD_FROM the replay reaches, after the
  partner left during the catch-up, makes it lead alone, and its own DELAY
  events of the replay count). (Round S1 review: the client waited for a JOIN
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
- The relay's room heartbeat (the members' last-seen times in the database)
  runs on its own task: a slow or stalled database no longer holds up the
  pings, holds, timeouts and rosters of every room (round S1, second review:
  a suspended database stopped them for as long as it hung). The address limit
  (`Relay:MaxConnectionsPerIp`) answers `ERR busy` before it closes.
- Giving up counts the local START held for two seconds, not the sealed
  ticks (under lag a filled tick reset the count).

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

### 6.13 Instant joins and rollback (round 65 design)

**The feedback (user, 2026-11-01, after round 62's test build).** The lag is
better but reaches 80 ms at times, and it does not feel online yet: a new
player first has to "catch up" with the players already in the same world.
That should disappear, and everything must still be in sync. Two goals
follow:

1. **Instant join.** A member who joins an instance mid-stage plays within a
   second, at any visit length, bit-identical with the others.
2. **Own input without the lockstep delay.** The own character reacts in the
   frame of the input (local prediction with rollback), while every engine
   stays deterministic and the instance stays in sync.

Both need what 6.6 and round 62 called missing: a state of a running stage
that can be taken at any tick and put back. This section designs it in two
forms. The **tick state** is portable: a serializer per manager with stable
ids and a canonical hash, for joins, checkpoints, resyncs and desync
diagnosis. The **state arena** is in-process: the online engine's heap in one
write-watched region, saved per tick as dirty pages, for rollback. Nothing of
it is built; rounds J and R (17) build it.

**Revision 2** (the critique of d0e489a; every point checked against the
code before it was taken, plus two probes of this revision, 6.13.1). The two
forms stay, and so does the order J before R. What changed:

- Memory that grows in a static or crosses between engine and host as a
  `shared_ptr` is closed by types, not by where it was built: growing
  statics get a host allocator, the immutable sound banks and IGB documents
  move to host stores, the draw list holds no `shared_ptr`, and a write-watch
  check catches any host write into the arena (6.13.6, the contract).
- The relay keeps the whole log, so the classic path (snapshot and log)
  stays for every fallback; a desync soon after a rebuild from a checkpoint
  marks the instance's checkpoint path *suspect* and switches it to the
  classic path; a READY from a checkpoint never judges a two-member vote;
  the load end K comes from the members' votes, never from a donor's header
  (6.13.3-6.13.4). The first draft's claim that the STATEHASH vote catches a
  field the serializer misses was wrong: it hashes the serializer's bytes.
- The frontier runs on the local clock (the estimated server tick plus `d`)
  with a jitter reserve, so a late SEALED no longer stops it at once; UDP
  for INPUT and SEALED (round S4's lane) comes with or before round R
  (6.13.6, 17).
- Re-simulations run with their own flag and keep their outputs (the replay
  mode switches port events off and gates simulation state); the ADX sound
  effects on the SE streams are reconciled like the sfp ones; the mirror
  follows confirmed calls only, and predicted effects play as preview voices
  that the confirmed calls take over (6.13.6).
- A JOIN no longer stops the frontier: it executes predicted. Its engine part
  measures 2.6-4.5 ms; the first draft's 46-62 ms was a latency maximum
  around the join, not the step (6.13.1).
- Round R starts with a probe that has the scopes split, a rendered picture,
  audio and wrong pads in its trials; the first draft's arena numbers hold
  for a headless engine re-executing true pads only (6.13.1).

**Decisions in short.**

- A joiner no longer replays the visit. A member of the instance (the
  *donor*) serializes its tick state at a confirmed tick T; the joiner loads
  the stage from the visit's load snapshot, replays the visit's few load
  ticks, re-runs the topology changes made since, applies the tick state and
  replays only the ticks after T (tens of ticks). Expected 0.3-0.45 s from its
  barrier to playing at any visit length on the reference machine (measured
  today: 1.31 s after one minute, 31 s after thirty, refused beyond about 29
  minutes with two members and 12-14 with four).
- Every member hashes its tick state every 600 ticks (STATEHASH). The relay
  keeps the newest agreed one as the instance's *checkpoint*, next to the
  snapshot and the whole log as today: a resync restores the checkpoint
  instead of the load, so `Instances:MaxCatchUpSeconds` no longer takes
  anyone out, and a mismatch names the object that differs. The classic path
  (snapshot and whole log, 6.6) stays for every case a checkpoint cannot
  serve, a suspect checkpoint path included.
- Rollback: a member executes its tick at once with its own pad and the
  others' predicted pads, sends its pad as today and rolls back when SEALED
  disagrees. Its frontier runs on its own clock (the estimated server tick
  plus `d`), not on the newest SEALED, and stops only at the prediction cap
  `P_i` (the steady rollback depth plus a reserve of three ticks, at most
  15). Relay, INPUT, sealing and the delay rule stay. The own input runs in
  the frame it was sampled in at any round trip whose steady depth stays
  under the cap (about 200 ms).
- Per-tick saves come from the state arena (measured headless: 0.07-0.18 ms
  per tick; a rollback of three ticks costs 1.7-1.9 ms in a four-player field,
  and every trial was bit-identical), not from the serializer (1.2-1.5 ms per
  encode, too slow for every tick).
- What leaves the simulation (port events, log lines, NETFP, STATEHASH,
  STATE, splits, barriers, story, notices, music) comes from confirmed ticks
  only; sound effects (sfp and the ADX effects on the SE streams) play from
  predicted ticks as preview voices and are reconciled.

#### 6.13.1 Measured for this design

Throwaway probes of the round 65 design, not committed (scratch builds of
main 870a416 and of `fix-r62netlag` 58c0b8c; hidden, headless, loopback only,
the reference machine busy with other rounds): `state_probe` (direct start,
fast loading, synthetic circle pads with attacks; counts the state, times
copies, a prototype compact serializer), `r65_probe` (global `operator new`
replaced by one contiguous `MEM_WRITE_WATCH` arena; per-tick dirty pages,
changed bytes, in-process rollback trials), and four join scenarios on the
local stack (`tools/online_test`, scratch scenarios J1 / J5 / J15 / J30).
Line references in this section are to main 870a416.

*The join today* (host a alone in stag0101, wandering and fighting, solo-led;
guest b joins 1 / 5 / 15 / 30 minutes later; two members; fingerprints equal
after every join, 21 comparisons each):

| | J1 | J5 | J15 | J30 |
| --- | --- | --- | --- | --- |
| visit log (ticks) | 3,576 | 17,834 | 53,800 | 107,779 |
| STATE_DATA | 49,926 B | 163,990 B | 451,718 B | 883,550 B |
| barrier to BARRIER_OK (JOIN) | 0.002-0.010 s | | | |
| the joiner's own transition and load step (its fallback) | 0.158-0.169 s | | | |
| restore of the load snapshot | 0.009-0.011 s | | | |
| catch-up (replay of the whole visit) | 1.064 s | 6.088 s | 15.401 s | 30.792 s |
| READY to member (JOIN_REQ, JOIN_AT, the JOIN step) | 0.064-0.104 s | | | |
| **barrier to member** | **1.31 s** | **6.34 s** | **15.68 s** | **31.04 s** |

The catch-up grows by about one second per minute of visit (two members,
3,000-3,560 steps per second on the wall clock, music render-and-drop
included). J30 passed only with `MaxCatchUpSeconds` raised to 300: with the
default 30 it would have opened a parallel instance. Four members (estimate
from the probe's step costs): 2.3-3 s after one minute, 9-11 s after five,
about 38 s after fifteen (refused), about 77 s after thirty.

*What a running stage holds.* stag0102 with four players: 10 files (6.2 MB,
51,564 IGB objects), 4,912 scene graph nodes, 1,079 MODELCTRL and 1,079
ACTION2 RAMs (40-96 generated), 72 generators, 3,775 messenger rows. The IGB
documents are immutable after the load; every RAM pool, the messenger rows,
the info tables, the update order and the collision world are fixed by the
load and change mid-stage only through six kinds of topology operations
(6.13.2). Per tick 22-40 nodes change (at most 31-64), most of them node
clocks; after 3,000-20,000 steps only 67-156 nodes differ from the state
right after the load. Lua ran 6-22 root executions in 3,000 steps. The live
heap is 27-50 MB, almost all of it read-only resources; `sizeof(Engine)` is
4,936 B.

*Copies and the prototype serializer* (state_probe; the compact serializer
writes only the mutable fields of nodes, MODELCTRL and ACTION2 RAMs, pointers
as ids; compression measured for this section on its dumps, zlib in one
core):

| Stage, players, steps | encode | raw | zlib 1 | zlib 6 | xz 6 | delta vs the load: raw / zlib 6 |
| --- | --- | --- | --- | --- | --- | --- |
| stag0102, 4, 3,000 | 1.48 ms | 1,334 KB | 76.5 KB | 60.2 KB (4.5 %) | 40.8 KB | 100.8 KB / 12.8 KB |
| stag0102, 4, 20,000 | 1.39 ms | 1,334 KB | 76.9 KB | 60.5 KB | 41.0 KB | 101.2 KB / 13.0 KB |
| stag0101, 2, 6,000 | 1.46 ms | 1,344 KB | 68.4 KB | 51.7 KB | 33.2 KB | 97.1 KB / 9.4 KB |
| dng101b1, 4, 3,000 | 1.31 ms | 1,185 KB | 50.8 KB | 37.8 KB | 21.2 KB | 52.9 KB / 8.8 KB |
| stagBS02, 4, 1,400 | 1.17 ms | 1,107 KB | 48.4 KB | 36.5 KB | 21.0 KB | 52.5 KB / 9.4 KB |

zlib 1 took 2.6-3.3 ms and zlib 6 7-8.5 ms for the full form. Naive deep
copies of all nodes, ModelRams and ActionRams cost 2.5-4 ms per tick; only
the mutable node fields 0.53-0.88 ms; GameSystem 0.004-0.009 ms.

*Rollback in the arena* (r65_probe; mean per presented frame with a rollback:
restore of the pages written since the saved tick, then `d` replay-mode steps
with the union traversal; worst case in brackets; range quiet to loaded):

| Stage, members | step + simulation traversal | `d` 1 | `d` 3 | `d` 4 | `d` 6 | `d` 8 |
| --- | --- | --- | --- | --- | --- | --- |
| stag0102, 4 (four cameras) | 0.36-0.52 ms | 0.88-1.07 (1.4) | 1.72-1.90 (2.9) | 2.07-2.76 (3.5) | 2.74-3.83 (4.9) | 3.6-4.6 (5.4) |
| stag0101, 2 | 0.19-0.25 ms | | 0.90-1.26 (1.6) | | | 2.1-2.5 (3.4) |
| dng101b1, 4 | 0.18-0.29 ms | | 0.87-1.32 (1.6) | | | 1.7-2.8 (4.3) |
| stagBS02, 4 | 0.15-0.27 ms | | 0.81-1.27 (1.6) | | | 1.7-2.8 (3.3) |

Pages written per step: 120-207 (0.48-0.83 MB, with 108-822 allocations per
step); bytes really changed: 7-31 KB per step; saving the written pages costs
0.07-0.18 ms per tick, restoring them 0.19-0.46 ms; a whole-image save and
restore 1.4-2.8 ms each (a join's once, never per tick). Every trial was
bit-identical (the fingerprint of every re-simulated step and the net
fingerprint): 12 whole-image and 80 dirty-page trials per mark, at 1, 5, 15
and 30 minutes of play, in all four stages. So the complete simulation state
lies in the process heap, the Engine object and the process-wide generator
(`fight::generator`). The one deviation seen was the probe's own: the CRT's
locale facets, built lazily by the `ostringstream` in `net_fingerprint`,
landed in the arena inside a trial and were freed under the CRT by the
restore (60 of 60 after one warm-up call). An arena must therefore keep the
CRT's and every other lazily built static out (6.13.6).

*What the arena probe did not show (revision 2).* Its trials executed the
same pads again after the restore, so no misprediction was ever undone (no
RAM, node or allocation that only the wrong pads made had to go). Its
replaced `operator new` put every allocation of the process into the arena,
the host's included, so the split between engine and host memory, where the
hazards of 6.13.6 rules 2-4 arise (a growing static, a host-held
`shared_ptr`), did not exist: the probe shows that the process heap is
complete, not that the engine's part of it is. It drew no picture (the
simulation traversal only), ran its trials without audio, and re-simulated in
replay mode, which drops outputs and gates state (6.13.6, re-simulation). The
pages, costs and the bit-identity above therefore hold for a headless engine
executing true pads again. A picture renders once per tick at 60 Hz and
about 2.4 times at 144 Hz; if the render's presentation caches stay
engine-owned, its writes add to every tick's save. Round R's first task
repeats the probe with the product's split (17).

*The JOIN step (revision 2; probes `joinstep` and `joinsplit`, linked against
the engine of main 870a416, headless, warm file cache).*
`online_join_player` for a member whose species the stage has not loaded
(p_dorumon, p_vmon, p_girumon into stag0101 or stag0102 with one or three
present) costs 2.6-4.5 ms, and the twelve steps after it cost what steps cost
before (0.2-1.0 ms with their traversals). One package load
(`load_common_now`, 1.0-1.25 ms) splits into the read from the disc's
archive (0.21-0.26 ms), the IGB parse (0.54-0.61 ms for 0.42-0.59 MB and
6,000-7,000 objects) and the registration of its infos, RAMs, messenger rows
and schedule entries (0.21-0.43 ms). The first draft's "46-62 ms of hitch"
was the host's `exec_max` in the five seconds around the JOIN of J1-J30
(sample to execution, `d` 2 included: 38-60 ms), while later windows without a
join reached 57-72 ms on the busy machine; it was not the step. So the
JOIN step is cheap to execute predicted and to execute again after a
rollback. What it costs on a member's screen beyond that (a cold read from
the disc, the first draw of a new species with its texture upload) is host
work, which round R's probe measures with the picture.

*The write watch and the kernel (revision 2; the critique's `ww` probe, run
for this revision).* Pages that a `ReadFile` fills inside a `MEM_WRITE_WATCH`
region are reported like user writes: 256 of 256 pages for a buffered and for
an unbuffered 1 MB read. A file read into an engine-owned buffer is saved and
restored like any other write.

*Prediction.* The only real pad recording (the user's keyboard game in a
save state, 1,491 ticks) changes in 0.5 % of ticks (new presses 0.3 %), so
repeating the last pad would mispredict 1-2 % of ticks with one to three
remote players. An analog stick (the bot driver's steering, 32,793 distinct
values in 30,743 steps) changes nearly every tick: with analog pads a
rollback of `d` ticks every tick is the normal case, and the budget below is
sized for it.

*Other games.* Photon Quantum (deterministic predict / rollback) keeps
verified frames from server-confirmed input apart from predicted ones; a late
joiner gets a snapshot (its own if younger than 10 s, a "buddy snapshot" from
another client at that client's verified tick, or one from a server plugin)
and then catches up to the present. Factorio (lockstep) sends a map save and
re-simulates every tick since the download began, and that catch-up can fall
behind. GGPO has save / load / advance callbacks, caps prediction at 8 frames
and has no late join of players. The common pattern, used here: transfer the
state at one confirmed tick, then follow the sealed stream and replay only
the ticks since that tick.

#### 6.13.2 The tick state

**Definition.** The tick state of tick T is the engine's whole simulation
state after the step of T and its simulation traversal (the traversal is
simulation, 4.2), taken only at a *state-safe* tick (below). Restored on any
machine with the same build and data, the engine continues bit-identically.
It never contains the read-only resources or the load topology: the joiner
rebuilds those by loading the stage.

**How a joiner rebuilds the engine at T.**

1. A fresh engine restores the visit's load snapshot (6.6, as today).
2. It replays the visit's ticks `[0, K)`, K the visit's *load end* (the
   first tick with the stage active and the loader idle; 16-17 steps with
   fast loading, more with the original load timing), with the logged pads
   (`set_replay`, no music). The pads do nothing during a load; the replay
   makes the topology the members built: the same files in the same order,
   `start.lua` and `first.lua`, the entry's LAYER ORDER, the randomizer's
   donor loads. K is the members' agreed value (6.13.4), never the donor's
   header alone, and the joiner checks it: its own engine must reach its load
   end exactly at K, else CHECKPOINT_BAD (topology) and the classic path.
3. It re-runs the *topology journal* after K in its order. The engine
   appends one entry, with its tick, for each of the six operations that
   change the topology mid-stage, at the level where they act (not the
   compound calls around them, whose value side effects depend on the
   moment): a package load (`load_common_now` and the package loads of
   `online_join_player`: the file, its infos, RAMs, messenger rows and
   `schedule_` entries), a weapon attachment or detachment (the created
   BoneSelect nodes under the actor's children, `model_ctrl.cpp:2817-2819`),
   LAYER ORDER and REMOVE (children of `global_root` moved or deleted,
   `managers_core.cpp:128-209`), `Engine::attach_scene_root`, and round 63's
   own copies (`ModelCtrlManager::add_copy`, `ActionManager::add_copy`,
   `SelectCtrlManager::add_copy`, `TextCtlManager::make_copies`). Each
   result depends only on the topology before it and the entry's arguments,
   so re-running the list rebuilds the same topology. A *topology hash* in
   the header (files in order, the RAM count of every manager, the messenger
   rows, the `schedule_` order, the nodes the journal made) is checked after
   this step.
4. It applies the values: every block below overwrites the mutable fields of
   the objects it names; nodes the donor created lazily are created by id;
   every cache is emptied.
5. It hashes its own canonical tick state. Equal to the donor's hash, the
   engine is at T; otherwise the join falls back (6.13.5).

**State-safe.** `Engine::state_safe()`: the stage is active and loaded, no
stage is pending, no teardown, reboot or held field (a movie), no barrier
parked, no fork pending, and the transient fields are empty between steps
(script depth 0, no trigger slot, no demo under construction, the messenger's
port-only and trigger fields clear, the MatCtrl context clear; by the code
always so, the completeness test asserts it every tick). Until round J's
window lane lands (17), also no SUBWND program. Hub and UI stages are never
state-safe; they are solo instances and never joined (3.3).

**Contents** (sizes for stag0102 with four players: measured for the first
three blocks of objects, else estimated from the state count):

| Block | Contents | Ids | Raw size |
| --- | --- | --- | --- |
| header | magic `DW4T`, version, form (raw, canonical), stage, visit tick T, frame, load end K, topology hash, section table | | < 0.1 KB |
| engine | world clock (`time_`, `frame_`, `update_frame_`, clock lateness, applied lateness, world delta), held flags, pad input, stage active, soft reset, flags and counters, the loader's timing fields (`loader_fast_`, `loader_common_`, `loader_teardown_`), `pads_` and the console reset, the online fields (`OnlineRules`, `personal_`, barrier pads, fork sides, split identity, heading, exit view, exit port) | | < 2 KB |
| generator | `fight::generator` (MT19937: 624 words and the index) | | 2.5 KB |
| game system | `serialize_system` as in the snapshot | | 10 KB |
| journal | the topology operations of the visit with their ticks and arguments | file paths, RAM and node ids | < 1 KB |
| scene graph | per node: matrix, mask, the beModelCtrlNode2 clock (scale, bias, override), fixed time, colour, blend, texture and lighting overrides, hidden, runtime matrix, LOD cache, particle timer and live flag, recorder, NodeAnim | (file index, object ref); journal-made nodes (entry, ordinal) | 4,912 nodes, about 0.6 MB |
| attributes | colours changed by LIGHTCTRL SETCOLOR and TEXTCTL COLOR (`edit_attribute`) | (file index, object ref) | bytes |
| animation databases | `AnimationDatabase::looping_` (set by MOTIONSET, shared by the RAMs of one info) | info id | bytes |
| MODELCTRL | every RAM's mutable fields (sequence by index, cursor, labels, HP, base matrix, motion channels with their sharing as an index, control mode, pdmove, checks, ride, item fields, the queues, attachments, owner, hit state, timers, combo, panel, LUACALL prefixes, spiral, capsule, AI and homing, follow, status work, named timers) and `item_labels_` | (manager, RAM index) | 1,079 RAMs, about 0.5 MB |
| ACTION2 | action and cursor, the flags, motion work, joint, fade, wait and flash state, colours, timers, node clock, `MotionState` with the previous pose, the combiner cache, the traversed bone matrices and their time; `random_wait_min_` | RAM index, clips by database index | about 0.3 MB |
| GENERATER | every RAM's fields, the randomizer's shuffle pools | RAM numbers | 30 KB |
| CAMERA | the RAMs (box, shake, state, CAMHOKAN) and the manager (viewer by index, centre, blend, boxes, saved eye and target, `frame_` (the cull camera), the per-slot boxes and frames, viewer events) | RAM, path index | 2-4 KB |
| HitLand and collision | deliveries (position, previous, land), hit groups, `CollisionWorld::groups_`, ChangePos rings | RAM index | 25 KB |
| messenger and demo | the deferred queue and its records, the pause, the message objects; demo works with records by (info, index) and the exit step's port as a tag plus its `ExitPort` (state, owner, stage, mat, kind, point, text, candidates, asked, asking, opened, finish) | | < 5 KB |
| Lua | the VM: globals, the table and closure arenas with slot layout, generations and free list, the GC counters, `stack_last`, next execution and call, stopped; closures by (program, prototype path), host functions and host objects by name | | a few KB |
| sound simulation | `sfp_[48]`, the SNDF driver (voices, tracks, sequences as bank and offset, the message deques, AX pool, counters), the streams' game parts, `CriStreams`, play serials, pause, the SE lock, the random value, BGM, volumes, the movie's game fields, fx | bank, sequence index | 15-25 KB |
| small managers | MATCTRL, PARTICLE, SELECTCTRL, NDMWPANEL, NUMBERCTRL, METERCTRL, STAGECTL, BKCOLOR, PLAYERPOINT, the LAYER mask bits, WEAPON's attachment list, TEXTCTL's texts (raw form only) | RAM index | about 50 KB |
| SUBWND | every program (the original's and one per slot, round 63): running state, program, player, shown values, stocks, junk counter, and the window tree as (class tag, fields, children) | class tag | a few KB |

Total about 1.2-1.5 MB raw and, by the measured ratios, 45-80 KB compressed
(estimate; the three measured blocks give 37-61 KB with zlib 6). A delta
against the state after the load would be smaller (9-13 KB for the measured
blocks) but needs a baseline copy and a compare on every member; at these
sizes the full form wins on simplicity, and it hashes without a baseline.

**Left out, with the reason.** Caches, emptied or rebuilt on apply
(`found_managers_`, `schedule_run_`, `lua_programs_`, the messenger's
`by_category_`, `SceneCache::geometry_`, `clips_`, the collision memos,
`still_` and space matrices, copy resolutions, `Follower::resolved`, pose and
locals at a matching pose time, the shadow light). Presentation (ActionRam
peek, particle geometry, TEXTCTL and NUMBERCTRL geometry, rebuilt from the
text and the shown value, water, `picture_*`, view slot, hidden roots, port
events, debug camera, traversal statistics). The audio side of the sound
manager (decoders, `frames_read`, the mirror, output, mixes): a joiner seeks
its streams to the position the CRI handle counters imply (decoded, moved,
reclaimed, played base), without the render-and-drop of today's catch-up.
The load topology. The solo-only managers (LOGO, TITLE2, the load and save
interfaces, SAVEUTIL): never in a state-safe instance. The loader: never
active at a state-safe tick.

**Encoding.** The `StateArchive` style (one `io()` per struct for both
directions), little endian. Never pointer order: maps as sorted, unordered
containers sorted by id at write, floats as their bits, optionals with a
presence byte. Pointers become the ids of the table; a `std::function` is
written only where its target is known (the exit step's port, as a tag); the
sharing of `MotionChannels` lists as an index into the RAM's list; a Lua
closure as (program id, prototype path such as `main/0`), and `Lua4Value`'s
owner is restamped to the new VM. Sections are (tag u32, length u32, bytes)
in a fixed order, so a mismatch can be located. The *raw* form carries the
presentation fields a joiner needs for an identical picture (TEXTCTL texts,
NUMBERCTRL's drawn value); the *canonical* form zeroes them, as the snapshot's
canonical form does (6.8). The hash is XXH64 over the canonical bytes (the
xxhash of the zstd sources the build already has; FNV-1a byte by byte would
cost about 1.4 ms on 1.4 MB). Transfers are zstd-compressed (level 1-3): the
client links only zstd's decoder today, round J adds the encoder sources of
the same release.

**Versioning.** `kTickStateVersion` follows the protocol's simulation version
(6.12: it changes with every simulation change), the header carries it and a
decoder refuses another. Each serialized struct gets a size guard next to its
`io()` (`static_assert(sizeof(ModelRam) == 1752, "tick state: update io()")`):
a new field fails the build until the serializer and the version follow.

**Cost** (estimates from the prototype, measured in round J's first task):
encode 1.5-2 ms, hash 0.1-0.2 ms, zstd 1-2 ms, apply 2-4 ms on the
reference machine.

**Product changes beyond the serializer** (online off unchanged, 15): TEXTCTL
`Line` keeps the text it was set with (the glyph geometry is built from it as
today; nothing else holds the text); `Lua4Vm` gets `io()` (its copy is
deleted and its implementation hidden today); the topology journal; the
engine calls `Engine::state_safe`, `save_tick_state(form)`,
`apply_tick_state(bytes)`, `tick_state_hash`, `load_end_tick`.

#### 6.13.3 Completeness proof

A tick state is complete when an engine rebuilt from it continues exactly as
the engine it was taken from. The test (`DW4_TICKSTATE_TEST`, ctest
`tick_state`, as `DW4_STATE_TEST` for save states):

- Process A plays a scenario and writes, at every tick from the load end to
  300 ticks after it and then at every 97th tick, the raw tick state and the
  pads of every step. Process B rebuilds from each state (6.13.2, steps 1-5)
  and runs N steps with A's pads (N = 2,000; 10,000 at twenty states per
  run). Every step's canonical hash, fingerprint and net fingerprint must
  equal A's. A save of the rebuilt engine at T must give the same raw bytes
  (idempotence), and the topology hash must match. An in-process variant for
  ctest saves, runs N steps, drops the engine (one engine per process, 6.12),
  rebuilds and runs again.
- Scenarios: stag0101, stag0102, dng101b1, stagBS02 (with its boss intro and
  fight), a water stage (`beWaterPlain`), a fight with particle effects, a
  ride if one exists; one, two and four players; at T a status window open
  on each page (the original and a member's own, round 63), an NPC talk, a
  demo with the exit step's port, an open exit prompt, a Lua collection due
  within the next steps, a generator wave in progress, a member who joined
  and one who left; and the r25c story segments in online rules (one hundred
  states per segment, 10,000 steps each).
- A failure names the first step and the first differing section and object
  (`tools/online/tickstate_diff.py` on the two states written at that step).

The places where a field is easily missed are known from the inventory and
get their own cases: the Lua slot layout and GC counters (handle order keys
tables and functions, and the collection's moment decides slot reuse),
`AnimationDatabase::looping_`, edited attributes, `MotionChannels` sharing,
lazily created nodes, the `schedule_` order after package loads, the
transient fields between steps. The size guards catch fields added to a
serialized struct later; they do not catch a field the serializer never
wrote, a new member of a struct it does not cover, or a new manager.

**What a hash cannot see (revision 2).** STATEHASH hashes the serializer's
bytes, so it covers exactly what the serializer writes: a missed field is in
no member's hash, the members agree, and a joiner rebuilt from their
checkpoint carries a wrong value while every vote passes. A missed field
shows only in what the engine does afterwards: in this test, in the
continuation's hashes and fingerprints; in play, when its effect reaches the
net fingerprint, which 6.13.4's suspicion rule turns into the classic path.
(The first draft said the STATEHASH vote catches what slipped through; it
cannot.)

**Divergence trials** close the gap the continuation leaves when a missed
field happens to hold the same value in A and in the rebuild (a timer at
zero, a slot never reused). Process A saves the tick state at T, executes
1-60 ticks with random wrong pads, applies the saved state in place (the same
engine; a trial whose window ran a topology operation or created a node
lazily is skipped and counted) and executes the same ticks with the true
pads: every hash, fingerprint and net fingerprint must equal the straight
run. A field the serializer misses keeps the wrong pads' value and shows. The
completeness test runs them over its scenario set (ten thousand trials per
stage, round J); from round R the torture test alternates them with arena
restores (6.13.6).

#### 6.13.4 Checkpoints on the relay

- **Checkpoint ticks.** `C_k` is the first state-safe tick at or after
  `600 · k` of the visit (a deterministic rule every engine evaluates alike).
  Every member of a field instance, solo-led or sealed, sends
  STATEHASH(C_k) once `C_k` is confirmed (with rollback: from its saved state
  of that tick, 6.13.6), with the visit's load end K its engine reached (or
  rebuilt to). Cost on every member: about 2-3 ms every ten seconds.
- **Vote.** The relay compares (K, STATEHASH) like NETFP (6.9: majority, the
  replay judges, one vote per tick, no vote at a barrier). A mismatch is a
  desync vote; the loser is resynced. Equal reports make `C_k` and K
  *agreed*. One member's report (solo-led) is accepted unagreed; the next
  joiner's check and READY verify it.
- **Donor.** The member that reports STATE (connected, longest in the
  instance, then the lowest slot), skipping a lagging one. The relay asks it
  with CHECKPOINT_ASK: periodic (upload every agreed `C_k` while the
  instance is joinable, about 50-80 KB every ten seconds) or now (its next
  confirmed state-safe tick, for a join or a resync). The donor uploads
  CHECKPOINT chunks under its state budget (6.8).
- **Store.** Per instance the relay keeps the newest two checkpoints with
  their hashes and the agreed K, next to the visit's snapshot and its whole
  log as today. Nothing of the log is dropped (revision 2; the first draft
  dropped the log before the older checkpoint and so lost the classic path
  its own fallbacks needed): a log costs about 0.9 MB after thirty minutes of
  a two-member field (STATE_DATA of J30, 6.13.1), and
  `Instances:MaxVisitHours` bounds it as before. K is never taken from a
  checkpoint's header: it is the agreed K of the votes (the leader's,
  unagreed, in a solo-led instance), and the joiner checks it against its own
  replay (6.13.2 step 2). A fork's child keeps the parent's newest checkpoint
  before the fork until its own first agreed one.
- **Resync from the checkpoint.** RESYNC is followed by a STATE_DATA with the
  newest agreed checkpoint and the ticks since (at most about 600 plus the
  transfer). The expected catch-up is then under half a second at any visit
  length, so the resync limit of 6.9 applies only to the classic path (no
  checkpoint, or a suspect checkpoint path).
- **Suspicion (revision 2).** A tick state that misses a field passes every
  STATEHASH vote and shows only as a NETFP mismatch later (6.13.3); a resync
  from the same checkpoint would bring the difference back each time, and
  after `Instances:ResyncLimit` the member would play alone. So the relay
  notes every rebuild from a checkpoint (a join or a resync) with its tick.
  A NETFP or STATEHASH mismatch of any member within
  `Instances:CheckpointSuspectTicks` (1,200 ticks, twenty seconds) after
  such a rebuild marks the instance's checkpoint path *suspect*: from then on
  its joins and resyncs take the classic path (snapshot and whole log, 6.6,
  with today's limits), the relay asks the rebuilt member and the donor for
  their tick states at their next common state-safe tick (CHECKPOINT_ASK,
  flag diagnosis) for `tickstate_diff.py`, whose differing fields point at
  the missed one, and it logs `checkpoint_suspect` (instance, build,
  checkpoint tick, donor). A desync with another cause inside the window
  costs the instance its checkpoint path too, which errs on the safe side.
  `Instances:CheckpointJoins` (true) turns the checkpoint path off for the
  whole relay, for a released build whose serializer is known to miss a
  field, without a client release.
- **The replay judges only from the load (revision 2).** 6.9 lets a
  resync's READY judge a two-member vote because its replay of the server's
  log is independent of both members. A rebuild from a checkpoint is not: it
  starts from a state one of the two produced, and a missing field would make
  it side with the wrong one. Such a READY carries the flag *from checkpoint*;
  its fingerprint is compared as every READY's, but it never judges. In a
  two-member instance the first mismatch therefore resyncs as 6.9 says (the
  shorter member, from the agreed checkpoint before the mismatch); a second
  one within the suspicion window makes the path suspect, and the classic
  resync's READY judges as today.
- **Diagnosis.** On a STATEHASH mismatch the relay asks both sides for their
  bytes of that tick (kept with the STATEHASH, flag 1) and keeps them with
  the fingerprint lines (6.9) for `tickstate_diff.py`.

#### 6.13.5 The instant join

1. **Barrier.** As today: BARRIER, BARRIER_OK(JOIN) with the slot. The relay
   at once takes the target's newest checkpoint if it is younger than
   `Instances:FreshCheckpointMs` (2,000), else sends the donor
   CHECKPOINT_ASK(now). Meanwhile the joiner runs its own transition and
   load step as today and keeps its load snapshot as the fallback (0.16 s).
2. **Donor.** At its next confirmed state-safe tick T it encodes the raw and
   the canonical form, hashes, compresses and uploads (3-6 ms of work, one
   frame's hitch; 50-80 KB at an uplink of 1 MB/s take 50-80 ms).
3. **Relay.** STATE_DATA, layout 2 (6.13.7): the visit's snapshot, its ticks
   `[0, K)` (K agreed, 6.13.4), the checkpoint (T, hash, agreed or fresh,
   bytes), the forks after T with the side that leads to the instance, and
   the tick records after T. About 70-110 KB.
4. **Joiner.** A fresh engine rebuilds T (6.13.2, steps 1-5), seeks its
   music from the CRI counters, replays the ticks after T (those that passed
   during the transfer: 20-60 ticks, 10-30 ms) and sends READY with its net
   fingerprint as today and the flag *from checkpoint* (6.13.4). Its screen
   shows the loading screen's "Joining..." line without a countdown; nothing
   else. The stage files are in the file cache: the joiner's own load read
   them a moment before.
5. **JOIN.** The relay stamps the JOIN as today: sealed at
   `StampTick` (beyond every member's horizon, 6.5), solo-led by
   JOIN_REQ / JOIN_AT. Every machine spawns the joiner at its entrance point
   with SQ_ENTR (6.6 step 4). Its engine part costs 2.6-4.5 ms (6.13.1). From
   round R the members learn the tick at once (EVENT_AHEAD, 6.13.6), execute
   the JOIN step predicted like any other step (no stop before it) and read
   the joiner's package files into the file cache when EVENT_AHEAD arrives (a
   host-side prefetch, no engine state), so a cold read from the disc never
   lands in the step.

| Part | Today (J1 / J30) | Round J (estimate) |
| --- | --- | --- |
| barrier to BARRIER_OK | 0.002-0.010 s | the same |
| own transition and load step | 0.16 s | the same, in parallel with the checkpoint |
| checkpoint ready and sent | (STATE_DATA 50 KB / 884 KB) | 0.05-0.12 s, inside the 0.16 s |
| rebuild: snapshot, `[0, K)`, journal, apply, hash | 0.01 s (snapshot only) | 0.08-0.15 s |
| catch-up | 1.06 s / 30.8 s | 0.01-0.03 s |
| READY to member | 0.06-0.10 s | the same |
| **barrier to member** | **1.31 s / 31.04 s** | **about 0.3-0.45 s at any length** |

On a PC three times slower the rebuild and the catch-up take three times
longer: about 0.6-0.9 s. A later step can drop the joiner's own load (0.16 s)
when the checkpoint is ready, loading the fallback only when the join fails.

**Failure and fallbacks.**

- No checkpoint within `Instances:CheckpointWaitMs` (1,000; the donor lags,
  went, or its instance was not state-safe for that long): the newest stored
  checkpoint (the catch-up grows with its age), else today's path (snapshot
  and the whole log, under `MaxCatchUpSeconds`), else a parallel instance
  with the notice of today (3.4).
- CHECKPOINT_BAD from the joiner (version, decode, K, topology hash or state
  hash mismatch): one retry with a fresh checkpoint from another member, then
  today's path, then NEW. Each case is logged with the section that differed.
- A suspect checkpoint path (6.13.4) or `Instances:CheckpointJoins` off:
  today's path at once, with its catch-up limit.
- A READY mismatch, a target that moves on during the join, a leader that
  goes before J: as today (6.6).
- An old relay or client never meets a new one: the protocol rises (6.12).

#### 6.13.6 Rollback for the own player

**The model.** Today member i executes tick t when SEALED(t) arrives (plus
its one-tick playout) and sends its pad for `t + d_i` then; its input waits
`d_i` ticks, and `d_i` covers the whole round trip. With prediction the
member's *frontier* runs on its own clock (revision 2): at each tick of its
pacing it executes `F = s + d_i`, where `s` is its estimate of the tick the
relay seals now (the newest SEALED's tick, plus the time since its arrival,
plus the downlink's half of the smoothed minimum round trip; paced at
±0.1 % as today), with its own pad sampled for F and the predicted pads of
the others, and sends that pad. The pad leaves about `d_i` ticks before
`S(F)` and needs only the uplink: its margin is `d_i · f` minus the uplink
time, against `d_i · f` minus the round trip and the playout today. The
relay's delay rule is unchanged and adapts `d_i` from the margins it
measures, so `d_i` settles lower by about the downlink and the playout tick
(estimate: 3 to 2 at 20 ms of round trip, 7 to 4-5 at 80 ms; measured in
round R), and an error in the estimate of `s` shows as a margin and is
absorbed the same way. Relay, INPUT, sealing and fills stay as round 62 left
them. The member keeps two ticks:

- the *frontier* F, the newest tick it executed, and
- the *confirmed* tick, the newest whose execution used sealed pads only.

When SEALED arrives, the client compares the sealed pads and events with what
it executed. The first tick that differs is restored and every tick from it
to the frontier is executed again, sealed pads where they are in, predicted
pads after. Ticks that agree become confirmed without any work.

**The prediction cap.** The rollback depth, from the oldest unconfirmed
tick to the frontier, is `d_i` plus the downlink and its jitter in ticks in
steady state (about the round trip: 2-4 ticks at 20 ms, 6-8 at 80 ms) and
grows while SEALED is late. It is capped at
`P_i = min(15, d_i + ceil((down_i + J_i) / f) + 3)`: the steady depth plus a
reserve of three ticks (50 ms) for jitter spikes and a lost datagram's
repair, about `d_i + 4` at 20 ms and `d_i + 6` at 80 ms. Only at `P_i` does
the frontier stand still; such a presented frame is counted as a frame with
a *standing frontier* (the network indicator, `saves/online.log` and round
R's acceptance, 17). The first draft tied the frontier to the newest SEALED
(`newest sealed + d_i`, `P` = 8), which left a reserve of `8 - d_i`: five
ticks at `d` 3, one at 80 ms, none above about 110 ms, so every late SEALED
froze the frontier, and the user's "sometimes 80 ms" are exactly such moments
(round 62: rises of `d` after stalls and TCP losses). Over TCP a lost segment
on the downlink holds SEALED back for about one round trip plus 50 ms
(6.4: 15-60 times a minute at 0.5-2 % loss), longer than the reserve at
every round trip; with round S4's UDP transport (redundant SEALED datagrams,
6.4) an isolated loss costs nothing. So the UDP transport comes with or
before round R (17). A deeper cap costs only after a stall: the one rollback
then re-executes up to `P_i` ticks (about 6-9 ms at 15 in the four-player
field, extrapolated from the table below), and the cost fallback catches a
slow PC. When the steady depth itself passes 15 (a round trip of about
200 ms or more) the client samples its pad for F as before but executes only
up to `newest sealed + 15`, so the input waits the remainder: a hybrid that
is never worse than today. Own input latency below the cap: the sample runs
in the host pass that took it and is seen at the next present (one frame).

**The state arena.** A tick save must cost a small part of a tick, and every
byte must come back:

- The online engine lives in a `StateArena`: one reserved region with
  `MEM_WRITE_WATCH`, a deterministic allocator whose metadata is inside the
  region, and the Engine object itself placed in it. The game executable
  replaces `operator new` / `delete`: with an `ArenaScope` open on the thread
  they allocate in the arena, without one from the CRT heap; delete goes by
  address. Offline no arena exists and the replacement forwards to `malloc`
  as MSVC's does.
- After each executed tick (step and simulation traversal) the arena copies
  the pages written since the previous tick into a ring entry (0.07-0.18 ms)
  and saves the 2.5 KB generator (or the generator moves into the engine,
  6.12). A shadow image of the region at the oldest kept tick takes the
  oldest entry when the ring passes `P_i + 1` ticks. Restoring tick t writes
  back every page written after t from the newest copy at or before t
  (0.19-0.46 ms). Memory: the region (41-69 MB) twice plus the ring (about
  8-13 MB for 16 entries of 0.5-0.8 MB, headless; more if the render's pages
  stay in the arena, rule 1).
- Why not the serializer per tick: a full encode costs 1.2-1.5 ms (2-3 times
  that on a slower PC) every tick and allocates, and a per-field dirty
  tracking would touch every writer of the engine. The arena is complete by
  construction and measured bit-identical; the serializer stays the portable
  form and the cross-check (its hash compares rolled-back and straight runs).

**The arena's contract** (every rule closes a hazard the probe, the
inventory or the critique of revision 1 showed; round R's first task audits
the online host against it):

1. Engine-owned memory lives in the arena, host-owned memory outside, and
   no object of one side owns, shares or reference-counts memory of the
   other. The engine opens `ArenaScope` in every entry that may allocate or
   write engine-owned memory: the step, the simulation traversals, restore
   and apply, the online mutators (join, remove, intent, barrier, fork sides,
   leaving marks), the setters the host calls (`set_view_slot`,
   `set_split_identity`, `set_fork_sides` and the like), and a const accessor
   that fills a cache (`get<T>()` and `found_managers_`). Other const
   accessors run without it, so what they return by value is host memory.
   The picture's render runs in the arena only if its presentation caches
   stay engine-owned. The preferred form keeps them in host memory, keyed by
   stable ids plus a serial (as `GeometrySerial` already lets host caches
   tell a new geometry at a reused address apart, `scene_graph.hpp:18-31`),
   so a render writes no arena page and a rollback never touches them; at
   144 Hz about 2.4 renders per tick would otherwise add their pages to every
   tick's save. Round R's first probe decides between the two (6.13.1, 17).
2. **Values cross by deep copy, never by move or by sharing.** Arguments are
   copied inside the scope (barrier results, carries, fork sides); a
   snapshot or a state crosses as bytes and is decoded inside the scope,
   never as a decoded `StageSnapshot`, whose `system` and `sound` are
   `shared_ptr`s (`engine.hpp:414-415`; the save-state slots copy one today,
   `game_main.cpp:1628`); `take_*` calls copy out and clear. **The host holds
   no `shared_ptr` into the arena beyond the call that produced it**: a copy
   or a drop outside the scope writes the reference count into an arena page,
   which a restore turns back, so the object is freed while still in use or
   never. The render's out-parameters (the draw list, the shadow frame) use a
   host allocator type, and the draw list carries the skin palettes as plain
   pointers valid until the next engine call (today
   `DrawItem::palette` is a `shared_ptr`, `scene_graph.hpp:323`).
3. **Statics and thread-locals with heap content in engine code get a host
   allocator by type** (revision 2). Building them outside the arena is not
   enough, because several grow during a tick: the collision's scratch
   vectors by `push_back` and `resize` inside `query`
   (`collision.cpp:416-419, 484, 517, 603, 696`), the randomizer's dialog
   memo on demand (`randomizer.cpp:434`), the trace statics as they log
   (`managers_core.cpp:2519`, `model_ctrl.cpp:470`). A growth inside
   `ArenaScope` puts the new buffer into the arena while the static itself
   does not roll back: after a restore the block is free, is handed out
   again, and the heap is corrupt, rarely and far from the cause. So each
   such static becomes `std::vector<T, HostAlloc<T>>` (strings and maps
   likewise), which allocates from the CRT heap whatever scope is open, or it
   becomes a member of the object that uses it (`CollisionWorld`, `Engine`)
   and rolls back with it; scratch that lives within one call keeps the host
   allocator, so a query writes no arena page. The grep test fails for every
   `static` or `thread_local` of a type with heap content in `src` and
   `include` whose allocator is not `HostAlloc` (plain pointers and bools,
   such as the `getenv` statics and `GeometrySerial`'s counter, hold no
   heap). The CRT's lazy facets are warmed up when the session starts (a
   locale, an `ostringstream` round, one net fingerprint).
4. **Immutable resources live in host stores** (revision 2). The sound
   banks (`BankData`, the decoded PCM) and the IGB documents are made under
   `ArenaOff` into a host `BankStore` and `DocumentStore`, keyed by path; the
   engine, the sound driver, the mirror and its calls hold plain pointers or
   ids (today `std::shared_ptr<const BankData>` in voices, sequences and
   calls, `sndf.hpp:122, 142, 210, 254`, and
   `std::shared_ptr<const IgbDocument>` in `LoadedFile`, `engine.hpp:317`).
   Then the mirror's copy of the driver (`mirror_.start(driver_, ...)`,
   `managers_core.cpp:1928`) and `mix_frame`'s reads of the banks
   (`managers_core.cpp:2028-2033`) touch no reference count in the arena;
   without the stores a restore can set a bank's count back and free it while
   the audio side still mixes it, or keep it for good. A store frees an entry
   only at a confirmed stage change, once the confirmed state no longer names
   it and the audio side reports no voice or pending call that uses it. A
   predicted package load parses into the store; when a rollback undoes the
   load, the document stays and the next execution only registers it again
   (0.2-0.4 ms, 6.13.1). Offline the stores free an entry where today the
   last `shared_ptr` goes (15).
5. The sound manager is split: its simulation side (the state GETSTATUS and
   the game read) stays in the arena and rolls back; its audio side
   (decoders, `frames_read`, `LinearSrc`, the mirror, the preview voices,
   output, compressor, reverb, mixes, `audio_delta_`) becomes an `AudioSide`
   object outside the arena whose methods, `record()` included, run under
   `ArenaOff`. The audio never rolls back.
6. **Host writes into the arena are detected** (revision 2). At the end of
   every `ArenaScope` the arena takes the pages written so far (for the
   tick's save) and resets the watch; at the start of the next scope it
   reads the watch again. A page written in between was written by the host
   or the audio side (a reference count, a moved buffer, a cache), which the
   contract forbids. Debug builds stop there with the page and the
   allocation that covers it; release builds count it in `net_stats`, switch
   the member to delayed mode for the rest of the session (it restores
   nothing then) and log it. This also catches every `use_count` change of
   rule 2 without instrumenting `shared_ptr`. A file read into an engine
   buffer is no false alarm: it happens inside a scope, and the kernel's
   writes are reported like user writes (6.13.1). The check costs one
   `GetWriteWatch` per scope entry; round R's probe measures it, and if it is
   too dear for every entry, release builds check at the tick's first scope
   and at the render's.
7. Debug builds poison the blocks a restore frees and keep them from reuse
   for 64 ticks, so a reference that escaped shows up in the torture test.

**Budget per tick** (the reference machine, the four-player field with four
cameras, the slowest measured case; measured headless, without the
picture's own render and its pages, 6.13.1; the depth is the rollback depth,
about the round trip in ticks):

| | 60 Hz picture (16.7 ms) | 144 Hz picture (6.9 ms, 0.42 ticks per frame) |
| --- | --- | --- |
| normal tick: step + traversal + save | 0.43-0.70 ms | the same in the frames that carry a tick |
| rollback every tick (analog pads), depth 3 | +1.7-1.9 ms (worst 2.9) | fits: 4-5 ms left for the picture |
| rollback every tick, depth 5 | +about 2.4-3.3 ms | fits on this PC; a 3x slower PC misses frames, not ticks |
| rollback, depth 8 | +3.6-4.6 ms (worst 5.4) | drops to the frames it can make |
| once after a stall, depth up to 15 | +about 6-9 ms (extrapolated) | one missed frame |

A member whose rollbacks (restore plus re-simulation) exceed 8 ms at the
95th percentile over five seconds switches to *delayed mode* (today's
lockstep: no prediction, input delay `d`) and back after thirty seconds below
4 ms; the switch is local and changes nothing for the others. Splitting the
traversal's clock work from its cull (about 60 % of a four-player step is the
union pass) would roughly halve every re-simulation; it is not part of
round R.

**Input prediction.** A remote slot's predicted pad is its last *sealed* pad
for every tick ahead: held buttons and sticks repeat, a new press is never
invented. With the engine's own edge detection (`OriginalPads`, rolled back
with the state) a remote press shows when its tick is sealed, as a
correction of up to the rollback depth. The own slot executes the pad it sent; it is
corrected only when the relay filled it (late) or carried a press to the
next tick, which the delay rule keeps rare (round 62 measured 0-2 % of own
pads filled on a busy PC without loss, up to 7 % at 1 % loss: 6.4 on
`fix-r62netlag`). An
absent or leaving slot's pad is the engine's rule (6.5), never predicted.

**Confirmed ticks and events.** Events stamped into tick records (JOIN,
LEAVE, INTENT, DELAY, LEAD_FROM) would reach a predicting member only with
SEALED, after it predicted past their tick. The relay therefore announces
each event when it stamps it (EVENT_AHEAD), and `StampTick` lies beyond
every member's frontier (the sealing tick plus the largest `d_i`) by
`EventMarginTicks` = 2 plus the newest one-way time in ticks, so the members
execute it at its tick in prediction already. An event that still arrives
late is a misprediction like a pad. LEAD_FROM: the member stops predicting at
L - 1 and leads alone from L once every tick before L is confirmed. DELAY
changes only the member's own lead.

**Rollback barriers.** Some steps must not be predicted past or undone:
`Engine::rollback_barrier()` after a step reports a pending stage (the
barrier tick B, 6.7), held fields, a fork decided in the step, and the
barrier park. The frontier then stops at that tick until it is confirmed: on
agreement the client goes on (parks at B), on disagreement it restores
before it. Each costs one stall of about the rollback depth where today's
lockstep costs the same.

Package loads and the JOIN are no barriers (revision 2). The simulation
itself loads packages inside steps (`load_common_now` from MODELCTRL at a
direct start, `model_ctrl.cpp:861`, the weapon packages of a join), so a load
is only noticed after a predicted step ran it; with the document store
(contract rule 4) a load undone by a rollback costs only its registration
when it runs again. The JOIN step executes predicted at J like any tick, from
EVENT_AHEAD, and again after a rollback across it: its engine part costs
2.6-4.5 ms (6.13.1). The first draft stopped every member's frontier at
J - 1 until J was sealed: a standing picture of the rollback depth for every
member at every join, the moment the user's feedback is about. Parsing the
joiner's packages on another thread from EVENT_AHEAD is not needed (the parse
costs 0.54-0.61 ms per package, 6.13.1); the host's prefetch of the files
keeps only a cold disc read out of the step.

**Re-simulation keeps its outputs** (revision 2). The arena probe
re-simulated in replay mode, which is wrong for rollback: `set_replay(true)`
switches the port events off (`online.cpp:695-698`, `engine.hpp:775`) and
also gates simulation state (`note_departure` sets no heading while
replaying, `online_exits.cpp:395`). A tick whose last execution was a
re-simulation would lose its achievements and story credits. A
re-simulation therefore runs as a normal execution under a host flag of its
own (`Engine::set_resimulating`, read by no simulation code): port events,
the engine's log lines (`Engine::log` writes every line to `std::cerr` today,
`engine.cpp:627-631`, which would repeat each line per execution and cost
console time) and every other output go into the tick's output record, which
the next execution of the same tick replaces and the confirmation delivers,
once. No picture is drawn for a re-simulated tick, only for the frontier.

**What stays confirmed-only.** The session's work after a step moves from
"after each executed tick" to "when a tick is confirmed":

| Output | When |
| --- | --- |
| picture, overhead labels, PRESENCE, the HUD | the frontier (predicted), with the correction blend below |
| sound effects: sfp (SNDF) and the ADX effects on the SE streams 2-5 | at once from predicted ticks as preview voices; taken over or faded when confirmed (below) |
| BGM and ambience (streams 0 and 1), global sound commands (BGMSTOP, ENVSTOP, volumes), movies | confirmed ticks: the rollback depth late (about one round trip plus a tick) |
| NETFP | computed at every execution of a multiple of 60, sent when that tick is confirmed (the value of its last execution) |
| STATEHASH, the donor's checkpoint | from the saved state of the confirmed tick: restore, encode, restore the frontier (2-4 ms every ten seconds) |
| port events (achievements, story credits), story lines, Discord, notices, the engine's log lines | kept per tick, replaced by each execution of the tick, delivered once when confirmed |
| STATE, SPLIT, HEADING, BARRIER and the park, INTENT, go-alone | confirmed |

**Sound** (revision 2). The simulation side rolls back with the arena; the
audio side does not. The mirror (the audio side's copy of the SNDF driver,
which replays the game driver's calls per sequence slot,
`sndf.hpp:245-290`) is fed from confirmed ticks only: its slot model cannot
follow a prediction, because sfp entries are taken by priority
(`choose_sfp_entry`, `managers_core.cpp:1418`), so after a different
prediction the same sound sits in another slot and a later `stop` or
`volume` would hit the wrong voice in the mirror. Predicted effects play as
*preview voices* instead:

- `record()` attaches to each call a key: (tick, kind, request, ordinal of
  that request within the tick). The mirror's `Call` has kind, sequence,
  request and time today, no key.
- Every effect start of a predicted execution plays at once as a preview
  voice on a small driver of the audio side's own, outside the mirror and
  the arena. A re-execution of the tick that makes the same key again keeps
  the preview (it is playing); a new key starts one, late.
- When the tick is confirmed, each confirmed start call looks for its
  preview by key. Found: the mirror runs the call silently, bound to that
  preview, and the call's later `stop`, `volume` and `set` on that sequence
  act on the preview voice, which keeps sounding where it is. Not found (a
  remote press that was not predicted): the mirror plays it, late by the
  rollback depth like the press itself. A preview that no confirmed call
  claims fades out within 20 ms.
- The ADX effects on the four SE stream channels (`start_effect` with
  request 0xFFFFFFFF, channels 2-5, `managers_core.cpp:1618-1627`; likely the
  cries and attack calls) are effects in the same sense: they start as
  preview streams on the audio side's decoders and are matched the same way.
  The first draft held them back with the BGM, so they would have come
  50-117 ms after the own attack while its sfp effect played at once.
- Only the BGM and the ambience (channels 0 and 1), the movie and the global
  commands (BGMSTOP, ENVSTOP, volumes) wait in a per-tick queue that is
  applied when the tick is confirmed and dropped when it is undone.

Rolling the audio itself back is never done: the device has played it.

**Presentation of corrections.** After a rollback the frontier's picture
differs from what was shown. `MotionInterpolator` keeps the matrix it last
presented per draw instance (the `instance` hash it already blends by); for
each instance whose corrected matrix moved, it adds the difference and lets
it decay over six frames (100 ms). It snaps instead when the instance
appeared or vanished, moved more than 2 m, or its sequence phase jumped (the
existing no-blend rules). The camera blends the same way. Values (HP, the
meters, text) and effects spawned in an undone tick change at once. A remote
character can enter an attack a few ticks late: its pose catches up, its
position glides.

**Determinism and the fingerprints.** Rollback changes no confirmed
execution: each tick's final execution uses the sealed pads, from a state the
arena restored byte for byte, so every machine ends each tick as a lockstep
machine would. NETFP and STATEHASH compare confirmed states only and are
equal across members whatever their rollback histories; that equality is
round R's proof. The torture test makes it hard:
`DW4_ROLLBACK_TORTURE=<seed>` runs an online engine in its arena with the
product's split (host on the CRT heap, the stores, the host allocators),
with audio and a hidden picture, and, at random ticks, executes 1-15 ticks
with random wrong pads, restores and executes them again with the true ones
as re-simulations; it alternates arena restores with in-place tick-state
applies (6.13.3's divergence trials). Every 60th tick's canonical tick state
hash, every fingerprint and net fingerprint, and the port events delivered
must equal a run without rollbacks, over the stage set of 6.13.3 and the
r25c segments in online rules, also in the poisoning debug build, and the
host-write check (contract rule 6) must stay silent.

**Settings and display.** F1 Online gets "Input: instant (rollback) /
delayed (lockstep)", default instant. The network indicator and
`saves/online.log` (round 62) add the rollback depth, the frames with a
standing frontier, corrections per second, the re-simulation cost and the
mode.

#### 6.13.7 Protocol changes

Proposed numbers; the integration pins them after rounds 60 and 62 merge
(NETSTAT keeps 0x8F, round 60's PRESENCE2 to clients moved to 0x90; client
0x14 is round 60's).

| Dir | Type | Message | Fields | When |
| --- | --- | --- | --- | --- |
| C to S | 0x15 | STATEHASH | inst, visit, tick, hash (XXH64 of the canonical tick state), load end K u32, flags (1 bytes kept) | every member, once a checkpoint tick is confirmed (round J) |
| C to S | 0x16 | CHECKPOINT | inst, visit, tick, hash, total, offset, chunk (at most 60 KiB of zstd) | the donor, on CHECKPOINT_ASK |
| C to S | 0x17 | CHECKPOINT_BAD | inst, visit, tick, reason (version, decode, K, topology, hash), section tag | a joiner or a resyncing member whose rebuild failed |
| C to S | (READY) | READY | as today, plus flag bit *from checkpoint* | a joiner or a resyncing member after a rebuild from a checkpoint; such a READY never judges (6.13.4) |
| S to C | 0x92 | CHECKPOINT_ASK | inst, visit, tick (0: the next confirmed state-safe tick), flags (1 periodic, 2 diagnosis) | to the donor (round J) |
| S to C | 0x93 | EVENT_AHEAD | inst, visit, tick, event (the tick record's event layout) | to every member when an event is stamped (round R) |

**State blob, layout 2** (STATE_DATA): purpose u8, slot u8, codec u8 (0 raw
records as today; 2 with a checkpoint), snapshot blob, load tick count u32 and
the records `[0, K)`, checkpoint tick u32, hash u64, flags u8 (1 agreed),
length u32 and the zstd bytes, fork count u8 and the forks after the
checkpoint (as today), first tick u32 (T + 1), the tick records after T.

**Relay configuration.** `Instances:FreshCheckpointMs` (2,000),
`Instances:CheckpointWaitMs` (1,000), `Instances:KeepCheckpoints` (2),
`Instances:CheckpointSuspectTicks` (1,200), `Instances:CheckpointJoins`
(true; false sends every join and resync the classic path); the checkpoint
period of 600 ticks is a protocol constant, not a setting. The log is kept
whole (6.13.4). `Sealing:EventMarginTicks` adds the one-way time in round R,
counted from the largest frontier. The protocol and the snapshot version rise
with each round.

#### 6.13.8 What stays

The sealing, the fills, the delay rule and its bounds, NETFP every 60 ticks,
the barrier and fork rules, RESUME after a reconnect, the whole log on the
relay and the classic catch-up from it (as the fallback, also for a suspect
checkpoint path), 6.9's rule that a replay from the load judges, the
solo-led instance with its own clock and delay 0 (it
needs no prediction; from JOIN_AT it keeps its pace with prediction instead
of switching to delayed sampling, and the relay places `S(J)` `d` ticks after
its estimate of when the leader executes J). Online off: the tick state, the
checkpoints, the arena and the prediction exist only in online sessions
(section 15).

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

**Mid-stage completeness (round 65 design, 6.13).** The load snapshot is
complete for a stage load because the load rebuilds everything else. Round J
needs more: the *tick state*, every mutable field of every manager at any
state-safe tick (6.13.2), and the same test one level deeper: a state taken at
any tick, rebuilt in another process, continues bit-identically for N steps
(6.13.3; the canonical tick state hash, the fingerprint and the net
fingerprint of every step), plus divergence trials, because a hash of the
serializer's bytes cannot see a field it misses. The rules of this section
carry over: every field
that survives a step and is not serialized breaks joins and resyncs; the
known traps are listed in 6.13.3 (Lua slot layout and GC counters,
`AnimationDatabase::looping_`, edited attributes, `MotionChannels` sharing,
lazily created nodes, the `schedule_` order after package loads, the exit
step's port closure, TEXTCTL's text held only as geometry, the SUBWND window
trees). The bump discipline extends to it: the tick state version follows
the protocol, and a size guard next to every serialized struct's `io()` fails
the build when a field is added without its serializer. The process-wide
generator stays the only simulation state outside the engine (6.12); round
R's arena saves it with every tick or moves it into the engine.

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
    bool scoped_scenes = false;      // (unused: round 63 scopes the own windows and scenes online always, 5.3)
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
                                        // (round 63: a member's own window or scene counts for none of it, 5.3)
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
| Persistent | every other flag a script sets for good (145 with a constant FLGset(..., 1); 49, 82, 334, 383, 406-408 through computed indexes) and the difficulty copies of the story working set; 503 (the training) from round 61's design, not applied at a barrier into stag0001 (8.8) | OR |
| Key items | flag folder 0-10 (K0-K10) | OR |
| Counters | COUNTER 1 (junk shop donations), COUNTER 7 (training attempts) | additive (8.3) |
| Never merged | the 404 flags `gameStart.lua` clears, 0, 508, 900-937 (derived at each lobby load), the non-monotone 611, 905, folder 11, COUNTER 0 and 2-6 (session values, 3.7) | - |

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
guests alike (the client sends the own story's bytes as well, 8.8), which
avoids revision conflicts between story merges and character uploads. A
guest's own story is untouched by playing in someone else's room; the
server's state achievements (clear flags 406-408, `CharacterSummary.cs:26`)
therefore never come from a borrowed story.

Plausibility before a merge: the sender is a member of the room and in an
instance; the phase rises by at most one per delta; each flag only from a stage
that sets it (the census's setter table: 100 of 145 persistent flags have
exactly one setter stage; hand entries for the six computed ones) against the
sender's instance stage; a rate limit.

### 8.5 When the story reaches an engine

- **At the online title start**: at every TITLE2 GAMESTART (before
  `gameStart.lua`; `Engine::title_game_start`, a host field outside snapshots
  and the fingerprint), the client replaces flags, flag folder and counters with
  the room story and sets the room difficulty, camera mode and light mode;
  `gameStart.lua` then clears the temporaries as in the original. (Until the
  round 56 fix round the trigger was `GameSystem::game_started`'s rising edge:
  the flag stays set after "Continue? No" took the player back to the title,
  and the title's option menu sets it without a game start, so the session's
  next Game Start got neither.) Round 61's design: no Game Start without a
  confirmed story (the title waits for it, 8.8); the same replacement
  after any character call of player 1 outside the title
  (`Engine::common_rebuilt`), and the call shop, the only such path, is
  closed online (8.8).
- **At every barrier** (6.7): the load step merges the barrier's room story
  into the engine with the same rules, touching only keys whose value changes
  (FLGisOn creates zero entries and the fingerprint hashes every key,
  `lua_host.cpp:400`, `engine.cpp:774-781`), and removes dropped values (8.6).
  It runs before the snapshot, so joiners get the same state. Every member of
  the group applies the same bytes at the same step. Round 61's design: flag
  503 is left alone when the barrier loads stag0001 (8.8).
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
and 2-6. (Round 61's design moves 503 to the persistent flags, 8.8: its
DERIVED entry made every guest of a phase-2 room play the training.)

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
it at the Game Start. Refused keys (not for the rate) wait for the next
barrier; held ones are sent again (the server keeps them); keys answered
*wait* or *not_member* go again three seconds later
(`SessionConfig::story_retry_interval`), not only at the next barrier, where
they would come from a stage that does not set them. The confirmed story is
`Session::confirmed_story()`; `OnlineRun::after_live_step` hands it to
`GameEngineLink::set_room_story` on the step of the title's GAMESTART (the
title's application, 8.5).

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

**Per visit (round 56 fix round).** The checks above work per STORY
message; the review forged a guest client that BARRIERed from stage to stage
at once and sent the phase step by step, every key item and COUNTER 7 growth
on 32 invented lines (revision 10 of the owner's story in 50 ms). Now
`RoomStoryKeeper.Take` gets the sender's visit (`StoryVisit`: member, a key
per visit, its age) from the relay: phase steps, flags and key items count
only from a visit at least `Instances:StoryMinVisitSeconds` (10) old, and a
visit gives one phase step and one key item; the rest is refused with the
reason *wait* (9), which the client sends again. A room takes only its own
difficulty's clear flag (406 + difficulty). COUNTER 1 grows at most
20,000,000 per line (wallet and bank hold 9,999,999 each). The relay takes
counter lines only where it put the member (`Place`, a fork's child; the last
16) and at most 8 instance 0 lines per room session. A member without an
instance with a stage (its barrier parked while its client went on as NEW
after the five seconds, taken out of its group, a lost connection in a lobby
or at a barrier, a reconnect without resuming) counts against the stage its
client plays in (`HubMember.LoneStage`) instead of being refused
(not_member). A STORY refused for the rate is answered without the story
blob. This slows a forging client down to real visits; the repair is the
story history below.

**Storage.** Migration 0008 adds `online_story (account_id, revision, flags
VARBINARY(1024), folder BINARY(12), counters BINARY(64) as 16 x i32 LE,
updated_at)`; only newer revisions replace a row (the relay saves each new
revision off the room lock). `GET /online-character` composes the record's
BASE_FLAG / FLAGFOLDER / COUNTER (big endian) from the account's own story,
`PUT` replaces the uploaded ones with it before validation, summary and state
achievements. `GET /rooms/{code}/story` (members) shows revision, phase,
flags, keys, counters, policy, owner presence, the dropped set and the held
count.

Round 56 fix round: migration 0010 keeps every written revision in
`online_story_history` (the last `OnlineCharacter:StoryHistoryRevisions`,
100, with the reason room / reset / admin_restore). The online character's
reset empties the story as its next revision (a new character starts from a
new game's story and Normal); staff list the revisions and put one back as
the next revision (`GET /admin/accounts/{name}/online-story/history`, `POST
.../online-story/restore/{revision}`, the admin account page's *Put back*).
Reset and restore close the account's own room first (its hub holds the old
story). The relay's saves go through a queue in `OnlineStoryService`: the
newest revision per owner, a failed write tried again after 1 .. 30 s, a
room's load sees a queued revision before it is written, and the service's
stop writes what is left (`StoryFlushService`). Deleting, locking or banning
an owner closes its rooms (close reason host_left), and no story row is ever
written for a deleted account. `POST /rooms` refuses Hard and Very Hard
without the owner's own clear on the difficulty below (difficulty_locked).

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
`S14a`; drop, hold, apply; rebuilt at the integration of round 56, when the
title began to apply the room story). The owner (k2 without flag 505, the
hint panel of stag0101's matb01; `seed_story`, so the room story is that card
at revision 1) stays at the title, where no STORY goes, and leaves the room
when its run ends. The guest (k2, whose own card has 505) waits at the title
until the owner is away and starts its game: the title gives it the room
story ("the game starts with the room story (revision 1)"), so 505 is 0 in
its lobby (a fail check). In stag0101 it steps on matb01, whose panel sets
505 again: its pending set is 505 alone, and stag0101 sets it (plausible).
Drop: `story_dropped keys=flag:505`, 505 stays for the rest of the stag0101
visit, the barrier into stag0102 removes it and the guest sees "Story progress
was not saved: the room owner is away." Hold: `story_held keys=flag:505`, the
guest is told once, no revision, 505 stays after the barrier. Apply: revision 2
at once, 505 stays. (The lane's first form used flag 501 of the lobby's
`first.lua`: with the title's application the guest no longer brings it from its
card, and k2 without 501 stalls in the first visit's talk at n_digilff #2's
`ISSEQNAME SQ_WALK02`, a state the original never reaches.) Without the seed
the guest's whole card story would be pending against the owner's empty
online story, and drop would take its key items too.

### 8.8 Guests play on the host's story (round 61 design)

The user's requirement of 31.10: a member who joins someone else's room has
the room owner's story while it plays there (every story flag, the phase,
the unlocked areas, gates and dungeons, the key items: everything story
progress depends on), so it gets everywhere the owner gets and skips what
the owner already did, the tutorial first of all. Its own online character
never stores that story, and outside the room it is back on its own. Its
personal progress is its own. This section says exactly what that means,
what round 56 already does, why the user's guest still had to play the
tutorial, and what closes the gaps. Base: main 87d9f0d. Round 59
(`fix-r59s2`: a room whose owner has no online story starts at zero, the
STORY_ACK reason not_started, `Session::await_hello` before the character
load, the character select's new-character rule) runs in parallel and
touches the same client files; the implementation round (17, round G)
builds on main after round 59 is merged. Nothing here was run: the findings
come from the scripts, the code, the story sets and the campaign
checkpoints.

Revision 2 (the critique of the first draft, 185f3b4) closes two paths by
which the story flowed the wrong way between guest and owner (the call shop,
the card file), replaces the round 59 spacing's stall for the owner's own
steps, adds the guests ahead of the owner to the tests and names the quest
boards' personal gates (question 10). All of it is read from main 87d9f0d,
`fix-r59s2` (`git show`) and the decompiled scripts.

**Already built (round 56).** Most of the requirement holds today:

- At every Game Start in a room the confirmed room story replaces FLAG,
  FLAGFOLDER and COUNTER (`GameEngineLink::after_live_step`,
  `src/game/online/engine_adapter.cpp:412-424`; `Engine::apply_room_story`,
  `src/engine/online.cpp:673-685`) before `gameStart.lua`, which then
  restores the working set and K0-K10 from the room difficulty's copies the
  server derived (8.7). A guest thus starts with the owner's phase,
  persistent flags and key items.
- Every barrier merges the room story into the engine (8.5); a guest's
  progress flows into the owner's story (8.3, 8.7).
- The server never stores a borrowed story: `PUT /online-character`
  replaces the uploaded story bytes with the account's own `online_story`
  before validation, summary and state achievements, and `GET` composes them
  (`OnlineCharacterService.cs:29-36, 48-55`).
- Leaving the room ends the engine (`offline_title`, `game_main.cpp:1809-1816`):
  a fresh engine with the local save store and its card settings.

**Why the guest had to play the tutorial.** In phase 2 the lobby gate
(`stagML11_m1__mate00.lua:9-23`) leads to the training field stag0001 while
flag 503 is 0 and to stag0101 once it is 1. `stag0001_m1__first.lua:8-13`
sets 503 on the first entry (and plays the intro only while it is 0);
`stag0001_m1__restart.lua:4` clears it right before its NEXTSTAGE stag0001,
so that *Retry* plays the intro again. 503 is read nowhere else, and
`gameStart.lua` does not clear it: in the original it lives in the save. The
story census lists it as non-monotone (`tools/online/story_census.py:80`
DERIVED), so it is in `story_sets.json`'s *never* set: no pending set sends
it, the keeper refuses it (not_story), no `online_story` holds it, and
`apply_room_story` clears it at every Game Start in a room. Phase 2 spans
the whole first area (the r25c checkpoint k2 is phase 2 with K0-K2), so a
guest in a phase-2 room is sent to the training whatever the owner did; by
the code the owner too after every fresh Game Start (not run). The tests
hid it: `run.py` `seed_story` (`tools/online_test/run.py:511-541`) writes
every flag byte of the owner's card into `online_story`, and every r25c
checkpoint (k2-k10, p4-p6) has 503 = 1 (read with a read-only script), so
the S14 / S16 guests went to stag0101. The script scan finds no other
progress value outside the room story: 0, 508, 611 and 900-937 are derived
or talk switches, 504, 554-555, 559-560, 567-569, 596-597 are read but set by
no script (0 for everyone), and the computed FLGset indexes (`MDLgetRamNo() +
171 / 181 / 248`, `L0 + 166 / 173 / 214`) hit dungeon flags of the instance
class.

Further gaps: a Game Start before the room story arrived keeps the
character's own story (and its pending set would then carry the guest's own
key items into the owner's story: key items have no stage check); the call
shop reloads the card's story into a running online engine (below); the
save room leaves the owner's story in the card file, where backups and the
save import find it (below); round 59's spacing stalls the owner's own
steps, and the guests wait behind it (*Lag*, below); the card cache keeps
records with a borrowed story (the recovery path loads one, 9);
`numemon_freed` counts the engine's Numemon flags, borrowed ones included
(11); the guest is not told whose story it plays; the character select
offers the difficulty dialog from the guest's own unlocks although the room
decides; the quest boards test the guest's own level, forms and cards
(*Quests*, below).

**Which fields are whose.** The record splits along the bytes 792..1891
(RecordCodec, SAVES.md):

| Part | Where | In a room | Outside |
| --- | --- | --- | --- |
| Phase (flags 1-6, 86) | BASE_FLAG | the room story's | own |
| The 110 persistent flags (109 of round 56 plus 503): bosses 67-70, Numemon 89-99, the world events 19-20, 22, 35, 49, 82-83, 320-327, 329-335, 383, 404-408, the intros 501, 502, 505-507, the training 503, quest clears 426-452, quest boards 469-474, the quest line's rewards 392-403, 476, 479, 509-513, 556-558, 561, 570, 576, 582, 588, 595 (question 9) | BASE_FLAG | room | own |
| The difficulty copies (700-812, 7-18, 74-81, 87-88, key copies 518-553) | BASE_FLAG | room (derived by the server for the room's difficulty; read only by the title) | own |
| Key items K0-K10 | BASE_FLAGFOLDER 0-10 | room | own |
| COUNTER 1 (junk shop donations, the shop tiers) and COUNTER 7 (training attempts) | BASE_COUNTER | room | own |
| Session values: the 404 temporaries, 0, 508, 611, 900-937, folder 11 (quest item), COUNTER 0 and 2-6 | engine | the engine line's (3.7): cleared by `gameStart.lua`, 900-937 derived again at every lobby load from the phase, COUNTER 1 and the member's own level, forms and cards (question 10 (b): for a guest the quest gates 908-910 / 922-927 also from the owner's) | - |
| UNIQUE, Digimon, name, level, HP, MP, XDATA, bits, devices, disks, cards, bank, equipment, the forms with level, EXP, skills and counts | record 0-791, 1892-3315 | own | own |
| Difficulty, camera mode, light mode, randomizer | GameSystem | room settings | own choice |
| Gate disc return, bit capsule, NINZU | session | personal session values (3.7) | - |

Nothing personal is lent, and the story needs nothing personal: no story,
field, dungeon or boss script tests a level, device, disk, form or card
count for progress (such tests exist only in the hub stages ML11-13: quest
and shop tiers 908-927, the Digi-Lab from level 16, quest trades, the
keychain), and the story stages give no items (stag0001, the lobby and
briefing intros give nothing; forms come only from the ML11 / ML12 quest
rewards). A guest who skips what the owner did loses only the EXP and drops
he would have fought for. In its solo hub instance the guest is slot 0, so
the personal reads of the lobby scripts (level, cards, forms) read its own
character. One consequence is not story but limits where the guest can
follow the owner: the quest boards (*Quests*, below).

**Flag 503 becomes story.** The census gets a class for it: 503 is
persistent (OR; setter stage stag0001), with the original's meaning "has
been to the training field" (`first.lua` sets it on the entry; the lobby
gate reads only that). Changes:

- `story_census.py` takes 503 out of DERIVED (its one clear is the retry's,
  below); `data/online/story_sets.json`, `src/engine/online_story_sets.inc`
  and the server's embedded copy follow (110 persistent flags), the setter
  table gets 503 -> stag0001, `session_census.py` drops it from NEVER_MERGED
  and `data/online/session_values.txt` from the instance class.
- The pending set sends it from stag0001, the keeper takes it (stage
  stag0001; the visit rules hold: the training's load and entry take about
  21 s, G1 in 3.4), the title gives it to every member's Game Start.
- **Retry keeps its intro.** `story::apply` gets the barrier's target stage
  (`Engine::apply_barrier` runs while `pending_stage_` holds it,
  `engine.cpp:2009-2013`) and leaves 503 alone when the target is stag0001:
  `restart.lua` cleared it on purpose, and every member of the group loads
  the same target, so the rule is the same on every engine. Both ways: the
  merge does not set it there and a dropped 503 is not cleared there
  (`restart.lua` did), and `holds_dropped` takes the same target, so the
  "not saved" notice agrees with the engine. The pending set never takes
  anything back, so the clear is not sent.
- **Stories saved before the fix.** The server's rebase (room open,
  `StoryMerge.Rebase`, and the C++ / Python twins with golden vectors) sets
  503 when the story shows the training was played: phase 3 or later
  (3-6, 86), COUNTER 7 above 0, any key item, or any persistent flag whose
  setter stages are all outside stagml02, stagml11 and stag0001 (progress
  beyond the training). All r25c checkpoints pass the rule (COUNTER 7 = 1,
  K0 from k2 on). An owner in phase 2 who never left the lobby keeps 503 = 0
  and his guests start at the training, as he does. The rule reads the
  stored story before its rebase onto the room's difficulty (the working
  set and every difficulty's copies), so a Hard room of an owner who
  cleared Normal keeps 503 = 1, as the original's save does (503 has no
  difficulty copies; G12).
- The protocol version rises (rooms are per protocol): a client with the old
  tables would not add 503 at a barrier its group's new clients add, and the
  instance would desync.

**When the room story comes and when it goes.**

- *Game Start in a room* (TITLE2 GAMESTART, `Engine::title_game_start`): full
  replacement with the confirmed room story, then the room's difficulty,
  camera and light mode, as built. New: never without a confirmed story.
  When the GAMESTART step finds none, the client parks before the title's
  next live step, as at a barrier (the title is a solo-led instance: no
  ticks meanwhile, as in a loading hitch; `gameStart.lua` has not run yet)
  and shows "Loading the room story..." until STORY_ACK brings it; a closed
  session ends the park as every session end does, and after 15 s without
  the story the client leaves the room with "The room story did not arrive.
  Join the room again." With round 59's `await_hello` the story is in before
  the title in practice; this is the safety net that keeps the guest's own
  story out of the engine and out of its pending set. The log line "the game
  starts without a room story yet" goes.
- *Every barrier*: the load step merges the barrier's room story (the
  phase rises to the room's, persistent flags, keys and the two counters
  come in, dropped values go), before the stage's load scripts and the
  snapshot; one blob for the whole group. New: the 503 rule above. A
  barrier only adds: what an engine holds beyond the room story stays
  there, so nothing may put a member's own story into a running online
  engine (the call shop, below).
- *Every other rebuild of the common story* (revision 2): `reset_common`
  and `link_common` of slot 0 replace FLAG, FLAGFOLDER, COUNTER and the
  modes at boot, at the title's load (LOADCTRL2 GENERATE_TITLE) and when
  player 1 makes or calls a character (`managers.hpp:2267-2281`). At boot,
  at the title's load and in the title's character select the title's
  GAMESTART follows, which applies the room story; only the call shop does
  it outside the title. New: when player 1 makes or calls a character while
  LOADCTRL2 is not in title mode (GENERATE, not GENERATE_TITLE:
  `title.cpp:515-540`, `load_intf.cpp:744-775`) and the online rules are
  on, the engine raises a host field `Engine::common_rebuilt` (outside
  snapshots and the fingerprint, as `title_game_start`), and
  `GameEngineLink::after_live_step` treats it as the title's Game Start:
  the confirmed room story replaces the three and the room's difficulty,
  camera and light mode are set, in the same step; without a confirmed
  story the client holds as at the Game Start. With the call shop closed
  online (below) nothing raises it; it is the net for a path nobody found.
  LOADCTRL2 runs only in UI stages, which are solo, so it never fires in a
  shared instance.
- *The owner's new progress mid-session*: every new revision reaches every
  member at once (STORY_ACK broadcast) as its confirmed story, but the
  engine takes it only at the member's next barrier, never mid-stage: the
  stage was built from the flags at its load (8.5), and a solo hub is no
  exception (the lobby's `start.lua` lays out its NPCs and gates by the
  phase). Any stage change brings it, also a walk into the save room and
  back.
- *Joins and co-arrivals*: the member takes the instance's engine state,
  which holds the room story of its visit start plus the barrier merges.
- *Gone*: leaving the room, the server closing the session, a kick, the
  room's close and quitting all end in `offline_title` (a new engine, the
  local save store); "Continue? No" goes to the title, whose next Game
  Start replaces the story again (no rest of the old one survives a full
  replacement); a crash loses the engine. Nothing has to be restored: the
  guest's own story never left the server, and the client keeps it outside
  the engine (uploads, below).

**The call shop is closed online (revision 2).** It is reachable today: the
lobby Digi-Elf `n_digilff` offers it (`stagML11_m1__bML01007.lua`, case 0
runs `callshop.lua`), and `stagML11_m1__callshop.lua` refuses only with
NINZU = 4, which a solo hub never has (1), before its NEXTSTAGE stagIS13
(line 19). Section 5's "off online" was planned for round S3 and is not
built (no `callshop` or `stagIS13` in `src/`). There player 1 calling a box
runs `reset_common` (`load_intf.cpp:744`: FLAG, FLAGFOLDER, COUNTER,
difficulty, camera and light mode to 0) and `apply_record(..., idx == 0)`
with `link_common` (774): the engine's story becomes the box's, and
LOADCTRL2 writes the modes back only in title mode
(`managers.hpp:2279-2281`), so they stay 0. Without GAMESTART the room
story is not applied, and the barriers only add (`story::apply`,
`online_story.cpp:200-235`). A guest's box 0 is its own record (the room
entry's `GET`), so its engine then holds its own story beside the room's:

- back in the lobby (no STORY goes from stagIS*) its pending set carries
  its own key items (no stage check: one per visit, with round 59 one per
  60 s), the persistent flags the hub stage it stands in sets (ML11 / ML12
  / ML13: the intros, the quest line's rewards 392-403 and the like) and,
  when its
  phase is exactly one above the room's, the phase step from a setter
  stage (stagML02 for 2-6, stagML11 for 86; a phase further ahead is a
  phase jump and refused);
- so the owner's story grows from the guest's own save without the owner
  playing, the guest sees its own areas instead of the owner's, and a far
  advanced friend can farm key items into a new account's story;
- the difficulty, camera and light mode fall to 0: a guest in a Hard room
  plays Normal, and every instance it starts is Normal;
- the call shop also calls local players 2-4 into an online engine (one
  player per PC online, question 5); the owner's own call shop reloads his
  card's older story and drops the modes the same way.

Round G closes it for every member: the port wraps `callshop.lua` (class
`callshop`, 5.1) and, under the online rules, runs only its refusal path
(the NINZU = 4 branch: its two lines, if they read as a refusal, and the
talk's close; the round checks the lines) with the hint overlay "The call
shop is not available online: everyone plays their own character." instead
of NEXTSTAGE stagIS13. The guard of `common_rebuilt` (above) stays behind
it. On the server the keeper checks key items by stage as it checks flags
(revision 2): the census lists each key's stages, K2 from
`stagML02_m1__first.lua:195`, K3 from `stagML02_m1__matf00.lua:186`, and
the others, drop items of item table 2 (`items.cpp:295`), from the item
placements in the stage data or, where they do not tell, from the stage of
each FLAGFOLDER rise in the r25c campaign; a key from another stage is
refused (stage). A key without a measured stage keeps today's rule and is
named in the census output for the review. So a guest's own key that
reached its engine some other way cannot enter the owner's story from the
lobby.

**Determinism.** The room story in an engine is the engine's one shared
story (8.1), so every engine of an instance must hold the same bytes. The
title's replacement runs in the member's own solo title instance (stagIS*,
never joined); the barrier's merge applies one blob, in the same step, with
the same target stage, on every member of the group; the title's hold is
solo. Nothing per member is ever written into the shared flags, folder or
counters: the guest's own story lives on the server, in `CharacterSync`
(outside the engine) and in its slot's `base_flags` / `base_flag_folder` /
`base_counters`, which are part of the shared GameSystem and equal on every
engine (they travel in the carry and the snapshot). No script reads them;
`link_common` copies slot 0's into the shared story, and only when player 1
makes or calls a character: in the title (the Game Start replaces it
after) and in the call shop (closed online, the guard of `common_rebuilt`
behind it). (The first draft said nothing reads them, which missed
`link_common`.) A per-member overlay inside a shared instance (each machine
its own story) is ruled out: the fingerprint and the snapshot hash every
flag.

**The card file keeps the guest's own story (revision 2).** The first draft
left the save room's card write as the engine made it and said the card
holds the borrowed story "until the room is left". That is wrong, and the
file can be imported:

- The save room writes box 0 with `make_record(slot 0)`, so with the
  engine's flags: the room story, straight into `online_card.dat`.
- `CharacterSync::card_written` (`character.cpp:426-441`) uploads with
  `write_cache = false`, and `request()` returns at "unchanged since
  revision n" (about line 487) before it writes any cache. With the own
  story bytes in every upload (below) the stage-change and leave saves
  after a save room visit are mostly unchanged too, so nothing rewrites the
  box: the file keeps the owner's story after the session.
- `OnlineCard::backup()` (lines 379, 412, 602: an unsent save at a newer
  server revision, an old cache at a 404, a 409) copies the file into
  `backups/`.
- The file is a GCI save, and F1 > Saves > Import takes every GCI
  (`save_import.cpp:209-213`, the tab at `game_main.cpp:3469`): the guest
  gets the owner's full story into `savedata.dat`, plays it offline, and
  the cloud backup sends it up with his account.

Fix: the online card store writes the own story. `CardStore` gets an
optional story override (the 1100 story bytes as the record holds them:
flags, folder, counters big endian). For a guest's session the client sets
it to the account's own story bytes of the room entry (below); every write
through that store, the save room's and a new character's file alike,
replaces bytes 792..1891 of every used box in both data copies and fixes
both checksums before the atomic write. For the owner and offline the
override is empty (the owner's engine story is his own; offline stays
bit-identical). The engine never notices: the save menu keeps the card's
records in RAM from its read at the menu's start (`SaveCommon::read`,
`save_menu.cpp:306-330`) and writes from them, and a card's presence comes
from the card setting and, for slot B, the file's existence
(`save_util.cpp:26-37`, read by `CardIo::update` at `save_menu.cpp:98-121`),
never from its bytes, so a rewritten file is no card change. The next read (the
next save room visit, the title after "Continue? No") sees the own story,
which the Game Start replaces anyway; the call shop, the only other reader,
is closed. So the file, every copy `backup()` makes, an import of it and a
cloud backup of that import hold the own story; the card file never holds
the room story, not even between the engine's write and a later
correction (which a rewrite after `card_written`, the critique's first
proposal, would have left open for a crash). The room entry also rewrites
the story bytes of every used box of a cached card with the own story
(caches written before round G).

**Uploads keep the guest's own story.** The server stays the authority
(`PUT` replaces the story bytes before anything reads them; unchanged). The
client keeps the card cache honest as well, so no record with a borrowed
story is ever loaded or sent:

- `CharacterSync` keeps the account's own story bytes (792..1891) from the
  record `GET /online-character` returned at the room entry (200), from the
  record of a 409 reload, and zeros with no character on the server (404;
  the server composes the first upload anyway).
- Every record it caches or uploads gets those bytes: the save room's
  upload (`card_written`), the stage-change and leave saves
  (`character_record` -> `auto_save`), the recovered save. The check
  "unchanged since revision n" then compares like with like. `make_record`
  keeps the original rule (slot 0 writes the engine's story); in a shared
  instance as slot 1-3 the record already carries the slot's own
  `base_*`.
- The save room's card write goes through the store's override (above).
  Every room entry builds box 0 from `GET` (the own story); the recovery
  path (a pending save at the server's revision, `character.cpp:367`) puts
  the own story of that entry's `GET` into the cached record before it
  goes on the card and up.
- The owner's own story is the room story; the server composes his newest
  `online_story` into every upload, so his cache's entry bytes are only a
  placeholder.

**Key items and story items.** The key items are flags in the shared flag
folder (K0-K10, `items.cpp:295`), not inventory: while in the room the guest
holds every key the owner holds (the status window's Folder page shows them,
`subwnd_folder.cpp:297`), the gates read them (`stag0101_m1__start.lua:166`,
`stag0102_m1__matg00.lua:3`, `stagML12_m1__hit_g_evenconsz05.lua:8`,
`stag0202_m1__start.lua:110`, `stag0302_m1__n_gekoa_00.lua:4`,
`stag0401_m1__start.lua:107`), and outside the room they are gone. No
script clears K0-K10 and the barrier merge removes a key only when it was
dropped (8.6), so a guest cannot use one up. Folder 11 (the quest item) is a
session value of the quest (3.7): used up inside the quest, never merged.
Consumables are personal (disks, recovery items, the Key Chain disk the
keychain ladder takes): spent from the guest's own stock as offline; story
progress never needs one.

**The guest's own progress in the room.** Story-wise it counts for the
owner (8.3, with the owner-away policy and round 59's not_started), never
for the guest's own story: only the guest's own room writes that. Personal
fields are its own and go up with every save. So a player who only ever
plays as a guest keeps an empty own story, and its own room starts at zero
(round 59), which is what the user asked for.

**Achievements.** A borrowed story gives none: `apply_room_story` and
`story::apply` write the maps directly (no `story_flag` port event), event
achievements come only from live steps ("was there": a boss fought, a clear
flag rising in the instance gives `clear_*`, the story challenges stay the
owner's, 11), and the server's state achievements read the composed record
(the own story). One leak closes: online, `numemon_freed` counts the
Numemon flags that rose in this account's own live steps (a mask in the
online profile, as `quest_mask`), not the engine's total
(`game_main.cpp:1499`, `achievements.cpp:612`): freeing the tenth Numemon
in a world where the owner freed nine is one, not *Numemon Liberator*.

**What the guest sees.**

- At every Game Start in a room it does not own, a toast: "Playing on
  <owner>'s story (<difficulty>)" and "Your level, items and Digimon stay
  yours. The story stays in <owner>'s save." The F1 Online tab and the room
  view show "Story: <owner>'s, phase n, k key items" (the owner sees "Story:
  yours").
- When the confirmed story gains a phase step or a key item while the
  member is inside a stage: "<owner>'s story moved on. It reaches your game
  at your next stage change." (once per stage).
- The character select's difficulty dialog is not offered online (after
  `hostLoad.lua`, `load_intf.cpp:780-786`, `difficulty_max` is 0 while the
  online rules are on): the room decides, and the toast names it. Offline
  unchanged.
- The owner-away and not_started notices stay as they are.

**Offline and local saves** are untouched: the local card store is not the
engine's while a session runs (`game_main.cpp:622`, put back by
`offline_title`), the cloud backup works outside rooms only, and no path
writes the room story into `savedata.dat`: the online card file, its
backups and therefore an import of them hold only the own story (the
card store's override, above; in the first draft the import was such a
path). Online off stays bit-identical: the story sets are read only online,
the 503 rule lives in `apply_barrier`, the dialog rule, the call shop
wrapper and `common_rebuilt` need the online rules, and the card store's
override is empty offline.

**Edge cases.**

- *The owner leaves the room*: the room runs on (owner away, 8.6); the
  guests keep the story as it was, new progress follows the policy (drop by
  default: removed at the next barrier with the notice). The owner's return
  merges held values. *The owner closes the room* or staff remove the
  owner: every session ends, the guests are back offline.
- *Ownership transfer* does not exist (`RoomService` has none). A round that
  adds it must send every member back to the title: barriers only add, so
  the old owner's story would stay in every engine and its pending sets
  would carry it into the new owner's.
- *The guest owns a room too*: an account is in one room at a time; its own
  room runs owner-away meanwhile, and with *apply* (or *hold* on its return)
  its own story can change while it is a guest elsewhere. That room's
  session does not care; the server composes the newest own story into every
  upload.
- *An owner without a story* (round 59): the room story is a new game's;
  the guests start as the owner does, and what they send is dropped
  (not_started) until the owner's game enters a stage.
- *A guest ahead of the room story*: its own in-room progress stays in its
  engine and goes to the owner as pending; a value the keeper refuses for
  plausibility (stage, phase jump) stays in that engine for the session (only
  dropped values are removed), as built.
- *A 409* (the account saved on another PC): the uploads stop, the cache
  goes to `backups/` (with the own story: the card never holds the room's),
  the server's character is reloaded into the card (its own story); the
  running engine keeps the room story.
- *A guest ahead of the owner* (its own story further than the room's):
  the Game Start replaces its story completely (the owner's phase, keys and
  flags, nothing of its own), so it plays exactly the owner's story and
  sends nothing of its own lead; outside the room it is back on its own.
  This is what the user asked for ("the same story flags"), and it is the
  case the call shop broke (it put the guest's own story back into the
  engine).

**Lag (revision 2).** The room story is the owner's saved online story, not
his running engine: a value reaches it after the keeper's rules and reaches
a guest at its next barrier. The first draft said that is seconds behind
the owner. With round 59 it is not:

- `RoomStoryKeeper.PerVisit` (`fix-r59s2`) spaces every account's phase
  steps 300 s and key items 60 s apart (`Instances:StoryPhaseStepSeconds`,
  `StoryKeySeconds`), the owner's too, and refuses the rest with *wait*;
  the client sends a *wait* again three seconds later from wherever it is.
- `Take` checks the stage before the per-visit rules (`SetsFlag(stage,
  ...)`). A step still waiting when its visit ends is then refused with
  *stage* from every later stage: the phase steps 2-6 are set only in
  stagML02 (`matf00.lua:92 / 185 / 252`, `n_ofanim_00.lua:11 / 37`,
  `first.lua:274`, `matf02.lua:69`). The pending phase is the engine's
  phase, so after its next step the member sends a phase two above the
  room's, a *phase_jump*: the room story stops at that phase for the room
  session.
- It happens in every new story: phase 1 comes with the lobby's first load
  (`stagML11_m1__start.lua:33`), phase 2 with the first briefing in
  stagML02 a few minutes later, and the training follows. A new owner's
  phase 2 (round 59 starts his room at zero) waits for the 300 s, is
  refused once he leaves the briefing room, and his room story stays at
  phase 1: the guests stand at the closed lobby gate or replay the
  briefing themselves, and the owner's own next Game Start throws him back
  to phase 1 as well. A key item within 60 s of another, or a second one in
  a visit, waits the same way; with the key setter stages above it would
  stall too.

Round G changes the keeper (C# with the C++ / Python twins where they
model it):

- **Earned values are queued.** A value that passed the plausibility checks
  (stage, phase limit, visit age) and waits only for the one-per-visit rule
  or the spacing is queued per account and merged when it is due (a later
  visit of that member at least `MinVisitSeconds` old, the spacing's time),
  by the relay under the room lock, whatever stage the member is in then;
  the owner-away policy applies at that moment. STORY_ACK answers it with
  a new reason *queued*, which the client treats like *held* without its
  notice (not sent again; the confirmed story brings it). The queue holds
  at most the next phase step and keys and ends with the room session.
- **The owner is not spaced.** The spacing guards the owner's story against
  a guest's client; the owner writes his own, and round 59's spacing never
  held for an owner across rooms anyway (a new room makes a new keeper).
  The owner keeps the per-visit rules (a visit ten seconds old, one phase
  step and one key per visit, the stage checks). Guests keep round 59's
  spacing.

So the owner's step reaches the room story about ten seconds after it
happens (a second key of one visit at his next visit) and a guest at its
next barrier; a guest's own steps that wait are not lost. Sending the
owner's running engine to the guests instead stays rejected: unchecked
bytes, values the keeper refused would spread through the guests' engines
and pending sets, and the owner may be anywhere.

**Quests (question 10, revision 2).** The guest does not get everywhere the
owner gets: the quests. The lobby derives the quest boards' gates from its
slot 0 at every load (`stagML11_m1__start.lua:125-159`): 908 / 909 / 910
from `SYSgetPlayerLevel(0)` at 16 / 46 / 76, 922-924 from the forms
(`SYSisGetShinkaDigimon`, at least 2 / 5 / 8), 925-927 from the card
folder's count (25 / 35 / 45). The boards offer their quests only with
them: `stagML11_m1__n_digilfm_01.lua:49-51` gives `bML01013` (on to
`quest01.lua`) with 931 + 908, on Hard 932 + 909, on Very Hard 933 + 910;
boards 02 and 04 read 922-924 and 925-927. A quest instance (stag0401
point 3, dngq*) is joined only with an equal context hash (409, 376-381,
COUNTER 2, ..., 3.7), that is by a member who took the same quest. In its
solo lobby the guest is slot 0, so a guest below level 16 (Hard 46, Very
Hard 76) cannot take a quest the owner took and cannot follow him into it.
In local co-op the lobby is shared and player 1's level decides for the
group. These are personal values (the lobby's derived 900-937, 3.7), not
story, so the requirement does not settle it. The choices (question 10):

- (a) as now: quests are side content, each member takes them with its own
  level, forms and cards;
- (b) for a guest the lobby's gates are the higher of its own and the
  owner's: the owner's tiers (the three level, form and card steps, from
  the record of the owner's newest online character upload, which the
  server decodes anyway) come with every BARRIER_OK into a hub, and a port
  wrapper after the derivation in
  the ML11-13 `start.lua` sets the owner's tiers on top under the online
  rules for a non-owner; a guest can then take the quest the owner took
  and share his instance (the same context). Personal, read only in solo
  hubs, so the determinism is untouched; one lane;
- (c) a member may join another member's quest instance without taking the
  quest: the join adopts the context, and DISSOLVE gives the joiner's
  personal values back. The quest's return and its rewards then run in a
  lobby that never took the quest; larger, not recommended.

Default: (b), the nearest to local co-op.

**Quest rewards (question 9).** The quest line's first rewards (weapons per
partner, bits, form unlocks through `SYSctlShinka SETSHINKA`, all for slot 0)
and the keychain ladder are gated by persistent flags set and read only in
the hub stages ML11-13 (392-403, 476, 479, 509-513, 556-558, 561, 570, 576,
582, 588, 595). As room story (today, the default): the guest cannot take
in the room a reward the owner already took; when the guest takes an open
one, it gets the item (it is slot 0 of its solo lobby) and the flag goes
into the owner's story, so the owner can never take that one (in local
co-op the owner, player 1, always got it); the guest can take it once more
in its own story. Per player (the alternative): those flags leave the room
story; every account keeps the rewards it took in a store of its own
(`online_rewards`, written by any room it plays in from its pending set,
composed into its record; per box once an account has four characters,
round 64 design 9.1.9), the title sets the member's own over the room
story, the 3.7 personal mechanism keeps them aside at joins and writes them
back at DISSOLVE (they are read only in solo hubs), and the room story blob
carries none. Nobody loses a reward to another player and nobody takes one
twice; it costs one lane (round G, lane hub extras).

## 9. Online character and cloud backup

- **Virtual card.** Entering a room, the client builds memory card A as a
  single-file card store in `saves/online/<account>/` from `GET
  /online-character` (box 0 = the record, header from the save utility); card B
  is absent. 404 gives an empty card: the original new game flow. The original
  character select runs unchanged. The local save store is put back when the
  room is left (as `end_session` does today). (Round 64 design, 9.1: all four
  boxes, composed by the server from its tables, the card in memory.)
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
- **Story bytes.** Every upload and every record the client caches carries
  the account's own story (from the room entry's `GET`), never the room's:
  the server replaces the bytes anyway, the client keeps its card cache and
  the recovery path clean, and a guest's online card store writes the own
  story into every box it writes, so the card file, its backups and an
  import of it never hold the room's (round 61 design, 8.8).
- **Cloud backup** of the local save (F1 > Saves, outside rooms only): upload
  with a preview (`summarize_save`) and `X-Sha256`, download with a backup of
  the current file first (`.bak`), restore of the five earlier revisions; the
  endpoints exist (`/cloud-save`, `/cloud-save/meta`, `/cloud-save/restore/{n}`).

**Round 56 fix round.** A character the title's character select makes new
(*Create Digimon*, its first option) while the account has an online
character is another character, even with the same UNIQUE (the frame count
of its creation, the same in every fresh session): `CharacterSync::new_game`
(the port event `new_character` of the own slot) marks the game until a
Game Start with the card's character, its saves are neither uploaded nor
cached, and the player is told at once (toast and status line); a skipped
save of another UNIQUE is told once as well. The server refuses a record whose
form levels went down (`form_level_dropped` is an error), so a collision
cannot replace the online character either. *Reset online character* is on
the account screen (password, a warning; not inside a room; the server
empties the online story with it). A refused login or an outdated version
(401 / 403 / 426) no longer drops the session's later saves: they go into the
card cache as pending, and the next room entry uploads the newest when the
server still has the revision it was based on.

**Built in round S2 (character lane).**

- `src/game/online/character.{hpp,cpp}` (library `dw4_online`, no SDL):
  `character_api::get_request / put_request(record, revision, play_seconds,
  Context) / reset_request / parse_character / parse_saved / problem_code /
  current_revision` (revision 0 = create with `If-None-Match: *`, else
  `If-Match: "<revision>"`; the body `{record: base64, playSeconds, context:
  {roomCode, players, roomPlayers, randomizer, cheats: false, rebalanced:
  false, saveStateLoaded: false, difficulty, protocol, clientVersion}}`; since
  the integration `players` is the own stage instance (1-4) and `roomPlayers`
  the room (the relay's ROSTER, else the presence; `Context::room_players`, 0
  leaves it out), the numbers the online achievements report, refreshed at
  every save, so the state achievements of an upload see the room).
- The card cache `OnlineCard` (`online_card_folder(saves/online, account)`):
  `online_card.dat` is the single-file card store of slot A (slot B a file
  that is never written, `Engine::set_card_setting(1, none)` as well),
  `online_card.state` is text (`revision=`, `play_seconds=`, `pending=0|1`,
  `box=`, `unique=`), `backups/online_card-YYYYMMDD-HHMMSS.dat` keeps a cache
  a conflict or a reset replaces. `card_with_record(base, record, box,
  keep_others, modified)` writes a record into both data copies (checksum,
  version 4; the entry and the 8 KB header of a valid base stay).
- `CharacterSync`: `enter(GET answer)` builds the card (200: the record in box
  0 and nothing else; 404: no file, the new game flow; else the room is not
  entered); a save left `pending` by a crash is uploaded first when the server
  still has the revision it was based on, else kept in `backups/` with a
  notice. `card_written(box)` uploads the box the save room wrote,
  `auto_save(record, Why::stage_change | leave)` writes the record into the
  cache's character box first. One PUT at a time, the newest waiting save
  wins, a record equal to the last confirmed one is skipped, a record of
  another UNIQUE is never uploaded (the server would answer
  `unique_changed`). 409 (and 404 on an update): the uploads stop for the
  room session, the cache goes to `backups/`, `GET` reloads the server's
  character into the card, the player gets a notice; a 409 whose
  `currentRevision` is the revision the save was based on is a race lost
  inside the server and goes again after 2 s (three times at most); 429, 5xx and network
  errors retry after 20 / 10 s; 401, 403, 426 stop with a notice; 422 drops
  that save with the server's text. The play time is the server's plus the
  live steps in a started game outside loads (`kFieldSeconds` each).
- The engine: the observe-only port event `PortEvent::Kind::card_written`
  (slot = the panel's player, value = card 0/1, extra = box 0-3, -1 for
  FILEMAKE) from `SaveCommon::write` / `create` (`src/engine/save_menu.cpp`).
- The game: `OnlineRun::load_character(online_folder, version, error)` (the
  host's `online_folder`: `saves/online` beside the program, `--online-folder`
  in test runs) before
  the engine starts (the engine and every engine of the session play from
  `character_card()`), `after_live_step(now)` (replaces
  `link().after_live_step()`: the room's difficulty, camera and light mode
  and `GameEngineLink::set_room_story` at `game_started`, the play time, the
  automatic save on the rising edge of `pending_stage()` from
  `GameEngineLink::character_record(own slot)`, which is `make_record` on a
  copy of `GameSystem` with `carry_player`'s slot, none before a game started
  or for a defeated member), `port_events(events)` (card_written of the own
  slot on card A), `save_on_leave()` (from `leave()` and when the server
  closes the session; up to 8 s for the uploads, the rest stays pending).
  Notices are toasts ("Online character"); the F1 Online tab shows
  `CharacterSync::status_text()`.
- The cloud backup: `src/game/online/cloud_save.{hpp,cpp}` (`cloud::
  meta_request / download_request / upload_request / restore_request /
  parse_meta / parse_uploaded`, `CloudBackup`, `sha256_hex`) and the F1 Saves
  section `src/game/cloud_backup_panel.{hpp,cpp}` (log in or a remembered
  login, overview with boxes and history, upload / download / restore each
  confirmed in the panel; an upload carries the revision shown, a 409 asks to
  refresh). Tests: ctest `online_character`; on the local stack
  `tools/online_test/character_test.py` (C1 a new character: created at the
  first stage change, the save room, leaving; C2 = S15: a called character,
  another PC's save, 409, reload, no overwrite; C3 the unsent save of a crash
  uploaded at the next entry; P1 the cloud panel: show, upload over another
  PC's backup, download a restored revision, with a picture of the panel).
- The server (found by those runs): the first writes of two accounts at
  once deadlocked under REPEATABLE READ (gap locks of the `FOR UPDATE` reads
  of missing rows) and InnoDB's victim answered 409 although nobody else had
  written that account's data. Online character and cloud save writes run in
  READ COMMITTED and a deadlock victim runs again
  (`MySqlErrors.RevisionedWrite`, `DeadlockAttempts`, `src/DW4Online.Data/Database.cs`);
  integration tests write twelve accounts at once (before: 7 of 12 got 409).
- Checks of the lane's final state: ctest 41 of 41 (with `online_character`);
  online off bit-identical (`det.sh` 4004f056 / audio 396cb219, two runs each
  of the release and the `DW4_ONLINE_DEV` build); r25c 10 of 10 segments,
  1,748,011 frames, the same story everywhere; the r48int save state tests 17
  of 17 identical after loading; the S1 scenarios on real clients 14 of 14
  (every client now plays the virtual card: a new account's first save is a
  stage change with `If-None-Match: *`, then `If-Match` at the next ones and
  on leaving); `character_test.py` C1, C2, C3, P1; DW4Online unit tests 130
  and integration tests 66 (4 skipped: the end-to-end client and the
  certificate store).

### 9.1 The server's character (round 64 design)

The user's requirements of 31.10: every value the game keeps about the
player's Digimon lives on the server in readable, editable database columns
(Bits, EXP, level, the stats, X Data, the forms: "alles was das Spiel halt
bietet"), the game loads it from there, so cheating gets harder; HeidiSQL no
longer shows "questionable characters" (hashes, the record blob, binary IP
addresses); and the website gets real player profiles: all four save slots,
every Digimon and every digivolution in them with its picture, level, stats,
Bits, the current weapon, armor, chips and mods with the gear's colour as the
game shows it (white, blue, purple, ...), the currently played Digimon as the
profile picture. Base: main eb01aed (round 61 merged with the user's answers:
quest rewards per player, quest gates (b)). Round 59 (`fix-r59s2`: the port
event `character_called`, `CharacterSync::character_called`, migration 0011)
and round G (8.8, 17) come first; nothing here contradicts them, and round G
is asked to key its reward store by box (9.1.9). This is a design: nothing
was built, no game, server or network ran.

**Revision 2** (the critique of 63c36d6; every point checked against the
code before it was taken): the byte mapping stays; what changed is the flow
around it. A staff edit no longer costs a playing member his session (the
upload is merged with the edit instead of refused with 409, pins are gone,
9.1.7); a new character is checked against the game's new-character record
(9.1.6); the change rules count visits by sequence and by box, never by
time, and wait for a visit the relay has not closed (9.1.6); the played box
is a persistent binding, so a restart never sends members out (9.1.1);
refusal rules refuse only values the upload changed, with a breaker for
rules that misfire (9.1.6); the quest rewards are kept per box, as round
61's answer *per player* needs with four characters (9.1.9); concurrent
HeidiSQL edits are locked, merged after a quiet time and reverted into a
draft when invalid, never locking the box (9.1.7); the rules that the game
data settles exactly refuse in round D already (rookie pairs, form rewards,
drop values, EXP and Bits per visit); hashes and addresses are stored as
text and the blobs leave the browsed tables (9.1.3); the migration needs no
RENAME and its data step runs inside the migrator's started mark (9.1.8);
the Cheats tab, open in seamless rooms today, is closed there first (9.1.10).

**Evidence.** Three read-only research passes of this round (scratchpad tools
linked against `dw4_core.lib` / `dw4_engine.lib`, the DOL disassembled, the
game data dumped; nothing committed) and the critique's read-only pass:

- The record map below, offset by offset, with the writer 80364AEC, the reader
  803657D8 and the packers 803B0D4C ff. Round trip proven byte-exact:
  `decode -> encode` per box and `read_file -> write_file` per file with the
  product codec (`src/save_data.cpp`) and with the server's C# codec
  (`RecordCodec.cs`) over 5,941 GCI files, 47,528 boxes, 0 differences (the
  user's card, 47 saves of the original game from Dolphin, 5,893 written by
  the port). Every gap byte was 0 in every sample.
- The record rules of the table in 9.1.6 checked on all 511 distinct used
  records (5 of the original, 506 of the port): no violation; no device is
  in two equipment places, every pocket list is a prefix, card bytes are 0 or
  1. In none of the 5,941 files is more than one box used, no record has a
  Mega as its current form, none has bank devices: four boxes and the bank
  are covered only by the code and synthetic tests.
- The game's rules (EXP curve 80370DBC, growth tables 804EAD24, RECALCPLAYER,
  the skill limit, EXP per kill, drops, Bits, death, the shops, form unlocks)
  from the DOL, the port and the 7,168 decompiled scripts; the gear colours
  (803B07E0) and the card colours (`subwnd_shop.cpp:2641`); the pictures the
  game has (charaicon.png, the staff roll's renders, the font's icon glyphs,
  the card art). `SYSctlShinka SETSHINKA` exists only in
  `stagML12_m1__sin_digi.lua` (the rookies 0-3) and in ML11 `treasure02-04`
  / ML12 `treasure06-08` (the forms 4-15).

#### What changes

- An online account has up to **four online characters**, the four boxes of
  the game's data select (9.1.1). Each lives in readable tables
  (`online_slots`, `online_forms`, `online_devices`, `online_disks`,
  `online_cards`, its quest rewards in `online_rewards`) named as the game's
  menus name things; the 3,316-byte blob is no longer stored as the live
  state.
- **The server composes** each box's record from the tables when a room is
  entered; the client builds its virtual card from that answer only and never
  plays a local copy online.
- **Uploads are decoded into the tables** after validation against the
  game's own rules: impossible changes are refused (the member leaves the
  room and loads the server's character again), improbable ones are kept and
  flagged; every refusal and flag is visible and can be undone by staff.
- **Staff edit** in HeidiSQL or on the website; the edit reaches the player at
  the next load; the uploads of a player who is online meanwhile are merged
  with the edit (his progress is added to the staff values), or staff send
  him back to load it now (*Reload now*); every edit has an audit row.
- **Readable database**: hashes are stored as hex, IP addresses as text,
  record and file blobs in side tables; views and virtual columns show the
  story as phase / flags / keys / counters; secrets stay hashes.
- **Player profiles** with the four boxes, every form with its picture, stats
  and gear in the game's colours; the pictures and item names come from the
  user's own game data at deploy time.

#### 9.1.1 Four boxes

A copy of the game's save file holds exactly four boxes (`kRecordsPerCopy`,
`include/dw4/save_data.hpp:26`; the loops 80363A64 `cmpwi r24,4` and
803674C0, stride 0x2000); the second copy is redundancy, card B a second
file. Each box is a whole character: its own UNIQUE, PLAYERNAME, current form
(DIGIMONNAME), all 16 forms (BASE_*), folder, bank, disks, cards and story
bytes. The user's "4 Save Slots" are these boxes. Today an account has one
online character in box 0 (`online_characters` PRIMARY KEY account_id,
`sql/0001_initial.sql:84`; `CharacterSync::enter` writes it into box 0 and
empties boxes 1-3, `character.cpp:383`; a box of another UNIQUE is not
uploaded, `character.cpp:434`).

**Numbering, pinned once.** The server and the website count boxes 1-4
(`box` columns, `/online-characters/{box}`, "Box 2" on the profile); the
client and the engine count 0-3 (`CharacterSync::box_`, the port event's
`extra`, `online_card.state` `box=`). `character_api` is the only place that
converts (server box = client box + 1). Today's `online_card.state` with
`box=0` and today's online character are server box 1.

- **Online card A holds all four boxes** from the server; card B stays absent
  online (as today). The character select shows them as the original does.
- **Create Digimon** is allowed while a box is free (round 59's rule "a new
  character while the account has one is another character" becomes "...
  while all four boxes are taken": the player is told "All four boxes hold
  online characters. Reset one on the account page to make a new one.").
  The new character belongs to the lowest free box; its first save (the
  first stage change, or the save room) creates it there with
  `If-None-Match: *`, checked against the new-character record (9.1.6).
- **The played box is a binding.** At the Game Start the client calls `POST
  /online-characters/{box}/play` (the box `character_called` of round 59
  named, or for a new character the lowest free box with `new: true`, the
  UNIQUE and the record `make_record` gives right after `create_player`).
  The server keeps the binding in `online_play` (one row per account and
  room, the room being the upload context's `roomCode`: room membership,
  box, new-character data, the visit it started at), so a service restart, a
  re-login after 401 or a reconnect keep it; it ends when the member leaves
  the room or the room closes. The same account in two rooms (two PCs) has
  two bindings; two saves of one box from both meet the revision conflict
  of today. `/play` for the bound box
  is idempotent; another box is accepted only while the relay has the
  member in its title instance (stagIS*, where "Continue? No" and the room
  entry put it), so switching characters needs the title. Uploads:
  - without a binding: 409 `play_required`; the client sends `/play` again
    for its box and then the save (no refusal, no leaving);
  - for another box than the binding's: 422 `wrong_box` (refused, 9.1.6);
  - the client sends no PUT before its `/play` is confirmed (saves wait in
    the card cache as pending, as during a retry).
  Identity by UNIQUE, name and partner would not do: UNIQUE is the frame
  count of the creation and repeats across fresh sessions, and a player may
  give every character the same name.
- **No clones.** A character lives in one box. When the save room writes it
  into another box (the original allows copies), the client does not upload
  that box and says "Your online character lives in box n; the copy in box m
  is not kept online."; a forged upload of it is `wrong_box`. The next room
  entry shows the server's boxes again. Overwriting a box that holds another
  online character is refused the same way; deleting one is the account
  page's *Reset box n* (password, as the reset of today, per box).
- **The active box** is the box bound last (`last_played_at`), else the box
  with the newest upload. Its current form is the profile picture.
- **Story and rewards.** The story bytes of every box are the account's one
  online story (8.4), as today for box 0: a room plays the owner's story
  whichever box he calls (8.5, 8.8). The quest line's reward flags (8.8's
  per-player list) are the exception: each box keeps its own taken rewards
  in `online_rewards` (round 61's answer 9, *per player*, with four
  characters per account: 9.1.9). Resetting one box empties its rewards and
  leaves the story; resetting the last box empties the story with it, as
  the reset does today (a fresh start stays possible). Question 11 asks
  whether each box should have its own story instead (the original's
  model).
- The local card store and the cloud backup are untouched; the cloud backup's
  boxes stay private (question 13).

#### 9.1.2 Tables

Naming: columns are named as the game's English menus name the value
(`txt` of stagML12: *Strength*, *Defense*, *Wisdom*, *Spirit*, *Speed*, *HP
Max*, *MP Max*, *Blunt* ... *Force*, *¶ Data*, *BITS*, *Item Bank*, *Mods*);
every column's COMMENT gives the GAMERAM variable and its record offset, so
HeidiSQL shows both. Every table a person may edit holds only values the
server takes as input; values the server computes live in `online_slot_stats`
and in the views, except the forms' stats, whose edit sets the boost (9.1.7).
Readable columns come first in every table, as HeidiSQL lists them.

| Table | One row per | Holds |
| --- | --- | --- |
| `online_slots` | account and box (1-4) | the character: UNIQUE, name, current form, HP, MP, ¶ Data, BITS, BITS Bank, generation and revisions, flags, times |
| `online_slot_stats` | account and box | computed by the server only (a table COMMENT says so; an edit is overwritten at the next write): level and the current form's stats with its gear, the forms' techniques |
| `online_forms` | account, box and form (16 rows per box) | BASE_ISUSE, BASE_LEVEL, BASE_EXP, the nine skills, the eleven ¶ Data / Plug boosts; the form's stats without gear (an edit sets the boost), next level's EXP |
| `online_devices` | device (surrogate `id`) | DEVICEFOLDER (36) and BANKDEVICE (96): kind, record, Mods, +N, its order, where it is equipped; virtual: rarity colour, packed word |
| `online_disks` | disk slot (surrogate `id`) | the disk (conItem row), its count and its order (12 per box) |
| `online_cards` | account, box and card held | CARDLIST |
| `online_rewards` | account, box and reward flag | the quest line's rewards the box took (8.8's per-player flags; round G's store, keyed by box) |
| `online_play` | account and room | the played box of the room session (9.1.1) |
| `online_slot_history` | box revision | summary, the changes, the reason, the visit window; blobs in `online_slot_history_records` |
| `online_rejected_uploads` | refused upload | the issues, the context, the staff decision; the record in `online_rejected_upload_records` |
| `online_edit_drafts` | HeidiSQL edit that was not applied | the edited rows as JSON, the issues (9.1.7) |
| `online_visits` | stage visit of a member | sequence number, room, instance and visit, stage, account and box, slot, the members with their boxes, difficulty, times (from the relay) |
| `online_rule_state` | account, box and rule code | consecutive refusals, the breaker's state (9.1.6); `account_id` 0 for a code's global state |
| `ref_forms` | form (16) | form row, model name, the name the repo already has (seeded) |
| `game_*`, `game_data_meta` | row of a game table | the game's tables imported from the deploy-time export (9.1.11) |
| `util_seq` | n = 0..1023 | a numbers table for the views |

`online_slots` (PRIMARY KEY (account_id, box), FK accounts ON DELETE
CASCADE):

| Column | Type | Game (record offset) | Notes |
| --- | --- | --- | --- |
| `box` | TINYINT UNSIGNED, 1-4 | the data select's box (copy offset 8 + (box-1) * 0x2000) | |
| `playername` | VARCHAR(8) utf8mb4 | PLAYERNAME (42) | the full-width characters (U+FF01-FF5E, U+3000) the card holds; an ASCII name typed in HeidiSQL is converted (`CardName`, as `save::name_to_card`); `playername_ascii` virtual for the website |
| `digimonname` | VARCHAR(11), FK `ref_forms.model_name` | DIGIMONNAME (8) | the current form (`p_dorumon`); HeidiSQL offers the 16 names |
| `hp`, `mp` | INT | HP (76), MP (84) | |
| `mhp`, `mmp` | INT | MHP (80), MMP (88) | as the game wrote them (it derives them again at load, `recalc_player`); derived again after a staff edit |
| `xdata` | INT | XDATA (92), *¶ Data* | |
| `bits` | INT | BIT (96), *BITS* | |
| `bank_bits` | INT | BANKBIT (728), *BITS Bank* | |
| `unique_id` | INT UNSIGNED | UNIQUE (4) | identity; not editable |
| `generation` | CHAR(16) ascii | - | a random 64-bit value in hex, new at every creation of the box; part of the ETag, so a deleted and re-made row never matches an old If-Match |
| `revision` | INT UNSIGNED | - | per box, every write (upload or staff); keeps counting across a reset |
| `visit_seq` | BIGINT UNSIGNED | - | the visit the newest accepted upload closed: the next upload's window starts after it (9.1.6) |
| `served_revision` | INT UNSIGNED | - | the newest revision a client loaded (room entry); the history keeps every revision from the oldest one a client may still name |
| `layout` | VARCHAR(16) | - | `gdje-v4.1`: GC USA record (card version 4), mapping version 1 (9.1.4) |
| `play_seconds` | BIGINT UNSIGNED | - | |
| `flagged`, `flag_reasons` | TINYINT(1), JSON | - | codes and texts of every flag (`grandfathered:<code>` and `migrated:<code>` among them); `reviewed_by`, `reviewed_at`; a flagged box is *under review* on the profile |
| `tables_sha256` | CHAR(64) ascii | - | the server's fingerprint of the box's rows after its last write (9.1.7) |
| `written_at` | DATETIME(3) | - | the server's own last write of the box (9.1.7) |
| `changed_at` | DATETIME(3) ON UPDATE CURRENT_TIMESTAMP(3) | - | as on every `online_*` table of a box: the last change by anyone |
| `client_version`, `created_at`, `last_upload_at`, `last_played_at` | | - | |

`online_slot_stats` (PRIMARY KEY (account_id, box)): `level` SMALLINT
(LEVEL (72) = BASE_LEVEL of the current form), `strength`, `defense`,
`wisdom`, `spirit`, `speed` (TSUYOSA, MAMORI, KASHIKOSA, SEISHIN, SUBAYASA)
and `res_fire`, `res_ice`, `res_thunder`, `res_dark` (HONOOTEIKO,
KOORITEIKO, KAMINARITEIKO, YAMITEIKO): the current form with its gear
(`recalc_player`, `combat.cpp:836-1020`); `techniques` JSON per form (the
pcTech rows each form knows, TECHn_LEVEL / _SKILLk, `combat.cpp:931-947`);
`rules_partial` (computed without the game data, 9.1.5).

`online_forms` (PRIMARY KEY (account_id, box, form); 16 rows per box, a
form never reached has the start values):

| Column | Game (record offset, s = form row) | Notes |
| --- | --- | --- |
| `form` TINYINT 0-15, FK `ref_forms.form` | the shinka row | 0 Agumon, 1 Veemon, 2 Guilmon, 3 Dorumon, 4 WereGarurumon, 5 H-Kabuterimon, 6 WarGreymon, 7 Cl-Angemon, 8 Beelzemon BM, 9 Alphamon, 10 BK WarGreymon, 11 IPDramonFM, 12 IPDramonPM, 13 MetalGarurumon, 14 Gallantmon CM, 15 Susanomon (`Species.cs`) |
| `reached` | BASE_ISUSE (1892 + s) | *digivolve technique* obtained; 0 set in HeidiSQL hides the form (9.1.7) |
| `level` | BASE_LEVEL (1908 + 4s) | 1-999 |
| `exp` | BASE_EXP (1972 + 4s) | |
| `skill_blunt`, `skill_slash`, `skill_stab`, `skill_bash`, `skill_shot`, `skill_crush`, `skill_blast`, `skill_heal`, `skill_force` | BASE_SKILL[s][0..8] (2036 + 36s + 4k) | the skill window's names |
| `up_hp_max`, `up_mp_max`, `up_strength`, `up_defense`, `up_wisdom`, `up_spirit`, `up_speed`, `up_res_fire`, `up_res_ice`, `up_res_thunder`, `up_res_dark` | BASE_UPCNT[s][0..10] (2612 + 44s + 4k) | what the ¶ Data Keeper and the Plugs added |
| `max_hp`, `max_mp`, `strength`, `defense`, `wisdom`, `spirit`, `speed`, `res_*` | derived | the form's own values without gear (an edit sets the boost, 9.1.7) |
| `next_level_exp` | virtual: `IF(level >= 999, NULL, (level + 140) * level * level + 200 * level)` | NEXTEXP (80370DBC) |
| `changed_at` | DATETIME(3) ON UPDATE CURRENT_TIMESTAMP(3) | 9.1.7 |

`online_devices` (PRIMARY KEY `id` BIGINT AUTO_INCREMENT; KEY (account_id,
box, folder, sort)):

| Column | Game | Notes |
| --- | --- | --- |
| `account_id`, `box` | | the box the device belongs to |
| `folder` | ENUM('folder','bank'): DEVICEFOLDER (100), BANKDEVICE (344) | *Device Folder*, *Item Bank* |
| `sort` | the order in the list | not unique: the record's index is the device's rank by (`sort`, `id`), 1-36 / 1-96, without gaps (the reader skips -1, so a gap would move every later index, 803657D8); the server writes `sort` back as 1..n after each processing, so moving a device in HeidiSQL is a one-row edit (`sort = 4.5` is not possible: INT; `sort = 4` beside another 4 orders by `id`) |
| `kind` | ENUM('weapon','armor1','armor2','abChip'): `_kindNo` (bits 12-15) | the game's table names: *Weapon Unit*, *∋ Armor Board* (Core), *∨ Armor Board* (Board), *Ability Chip* |
| `record_no` | `_recordNo` (bits 0-9) | the row of that table: weapon 0-276, armor1 0-31, armor2 0-44, abChip 0-185 (CHECK) |
| `pocket` | `_pocket` (bits 16-19) | chip pockets 0-5, the item text's *Mods: n*; weapon and armor1 only |
| `plus` | `_revision` (bits 20-30) | the *+N* after the name, 0-2047; not on chips; decides the colour |
| `equip`, `equip_pos` | POWERSOCKET (732) 1-3, POWERPOCKET (744) 1-5, MAINSLOT (764) 1, MAINPOCKET (768) 1-5, SUBSLOT (788) 1 | where the device is equipped (NULL: in the folder only); `_isEquip` (bit 10) is set exactly for these; no unique key, so two rows can swap places in two edits (two devices in one place at processing time make the edit invalid, 9.1.7) |
| `rarity` | virtual (803B07E0) | `white`, `blue`, `green`, `yellow`, `orange`, `purple` (9.1.12) |
| `word_hex` | virtual | the packed word as the record holds it (debugging) |
| `changed_at` | | 9.1.7 |

Equipment as a role of the device row (instead of index columns) keeps
HeidiSQL edits safe: deleting or moving a device row cannot leave a list
pointing at the wrong device, the server renumbers the order and the lists
follow. All 511 records keep each device in at most one list position and
every pocket list a prefix (checked); a record with an index twice is
refused (`equip_duplicate`).

`online_disks` (PRIMARY KEY `id`; KEY (account_id, box, sort)): `sort` is
the DISKFOLDER slot's order (244; the folder's order, which the player can
change; not unique, as with devices), `record_no` 0-11 the conItem row (each
exactly once per box, checked at processing), `count` 0-9 (the word is
`record | 4 << 12 | count << 16`; all twelve slots always exist).
`online_cards` (PRIMARY KEY (account_id, box, card_no)): one row per card
held, `card_no` 0-49 the CARDLIST byte (292 + n; the `card` table's row,
whose RARE column gives the card's colour, 9.1.12); the two bytes after the
50 are 0. `online_rewards` (PRIMARY KEY (account_id, box, flag)): `flag` one
of 8.8's per-player reward flags (392-403, 476, 479, 509-513, 556-558, 561,
570, 576, 582, 588, 595), `set_at`, `visit_seq` (the visit whose pending set
brought it).

Key items (impItem 0-11, the Folder's key page) are not inventory but story
flags (BASE_FLAGFOLDER, 8.8); `v_online_story` shows them. The game's data
has no DNA or battle digivolution: a form is reached (`reached`) and chosen
in the Digi-Lab (`digimonname`), whose change takes off the armor and the
chips and puts on the strongest usable weapon (8039CA4C,
`subwnd_shop.cpp:2124-2141`).

`online_slot_history` replaces `online_character_history` (PRIMARY KEY
(account_id, box, revision); reasons: the client's `create`, `upload`,
`upload_merged`; staff's `staff_edit`, `sql_edit`, `edit_reverted`,
`admin_restore`, `admin_accept`, `migrate`): `record_sha256` (hex), level,
form, bits, play time, flagged, `warnings` JSON, the visit window
(`visit_from`, `visit_to`) and **`changes` JSON**: what changed against the
previous revision in readable form (`{"bits": [12000, 13506],
"form:3.exp": [1745778, 1761002], "folder.7": [null, "weapon 19 +4 mods
2"]}`), so HeidiSQL shows a character's past without decoding. The exact
record served at that revision and, when a merge made them differ, the
record the client holds (`client_record`, 9.1.7) are in
`online_slot_history_records` (an archive for restores, merges and
debugging; never in a view). `online_rejected_uploads` keeps 30 days,
`online_visits` 30 days, `online_edit_drafts` until staff close them (the
privacy page names them).

The game tables (`game_shinka`, `game_weapon`, `game_armor1`,
`game_armor2`, `game_abchip`, `game_conitem`, `game_insitem`,
`game_impitem`, `game_pctech`, `game_card`, `game_enemy`, the shop tables
`game_shopweapon`, `game_shoparmor`, `game_shopchip`, `game_shopjunk`,
`game_shopdisk`, the identification tables `game_coweapon`, `game_coarmor1`,
`game_coarmor2`, and `game_stage_enemies`: per stage and difficulty the
enemy rows of its generators, `<stage>/separate/generator{N,H,V}`, with
their EXP and Bits bands) have the game's column names and types
(beDataBase: char, short, int, float, bool, string; names decoded with
`game_text_utf8`, so a weapon's name keeps its icon glyph, `∈Sword Break`).
They are empty after the migration and filled at the service start from the
deploy-time export (9.1.11); `game_data_meta` holds the export's data hash
and time. No row of an `online_*` table depends on them (no foreign key), so
the service runs without them (9.1.5).

#### 9.1.3 Readable database (HeidiSQL)

The user's complaint is about the tables themselves, so the fix changes how
they store values, not only what views show. One migration; a hash stored
as hex is the same hash (never a secret in clear; `password_hash` is
already text, `$pbkdf2-sha512$...`):

| Table | Today | After round D |
| --- | --- | --- |
| `accounts` | `recovery_code_hash` BINARY(32) | CHAR(64) ascii, lower-case hex |
| `sessions` | `token_hash` BINARY(32), `created_ip`, `last_used_ip` VARBINARY(16) | `token_hash` CHAR(64) hex (the unique key on it rebuilt); the addresses VARCHAR(45) ascii in the canonical text form (`IPAddress.ToString()`, IPv4 and IPv6) |
| `email_tokens` | `token_hash` BINARY(32) PRIMARY KEY | CHAR(64) hex PRIMARY KEY |
| `audit_log` | `ip` VARBINARY(16) | VARCHAR(45) text |
| `cloud_saves`, `cloud_save_history` | `data` MEDIUMBLOB (the user's save file), `sha256` BINARY(32) | `sha256` CHAR(64) hex; `data` in the side tables `cloud_save_data` / `cloud_save_history_data` (a private file, never in a view) |
| `online_characters`, `online_character_history` | `record` BLOB, `record_sha256` BINARY(32) | left untouched as legacy (table COMMENT "legacy: replaced by online_slots in 00NN, dropped in round D2"), 9.1.8 |
| `online_slot_history`, `online_rejected_uploads` | (new) | no blob: the records in their `_records` side tables |
| `online_story`, `online_story_history` | `flags` VARBINARY(1024), `folder` BINARY(12), `counters` BINARY(64) | stay (the story keeper's working bytes, round G's files); before them the virtual columns `phase` (1-6, 86 as `cleared`), `keys` (`K0,K1,K2`), `counter_1` (junk shop donations), `counter_7` (training attempts); the set flags as a list in `v_online_story` |
| `schema_version` | `checksum` BINARY(32) | stays (the migrator compares the bytes, and its all-zero started mark); a virtual `checksum_hex` before it |

The conversion happens at the repository boundary only: the domain keeps
`byte[]` hashes and `IPAddress` values, the Data layer writes and queries
the text (`Convert.ToHexString(...).ToLowerInvariant()`, `IPAddress.Parse`);
the columns use `ascii_bin`, so a lookup is an exact match. The migration
converts in place with `ALTER TABLE ... ADD`, `UPDATE ... LOWER(HEX(x))` /
`INET6_NTOA(x)`, `DROP COLUMN` and `CHANGE COLUMN` (all under ALTER, no DROP
privilege) and moves the blobs with `INSERT ... SELECT`.

Views (prefix `v_`, read only, each with readable names instead of ids; the
HeidiSQL guide in DEPLOY.md starts there): `v_accounts` (role, status,
profile, recovery code yes/no, boxes, the active box's form and level),
`v_sessions`, `v_audit_log`, `v_characters` (one row per box: name, form,
level, HP / MP, the current form's stats with gear, ¶ Data, BITS, BITS Bank,
forms reached, devices, cards, rewards, flags, under review, last upload),
`v_character_forms` (with the game's form name and the EXP still needed),
`v_character_devices` (item name, +N, Mods, colour, where equipped),
`v_character_equipment` (one row per equipment place: Weapon 1-3 with the
five chips of weapon 1, the ∋ armor with its five chips, the ∨ armor),
`v_character_disks`, `v_character_cards` (with the card's colour),
`v_character_rewards`, `v_character_history` (no blob), `v_rejected_uploads`
(no blob), `v_character_edits` (the audit rows `character_edit` and the open
drafts), `v_visits`, `v_online_story` / `v_online_story_history` (phase,
difficulty copies, the set flags as a list via `util_seq`, keys, counters),
`v_cloud_saves` (no data), `v_rule_state` (the breakers that are open).
Item names come from the `game_*` tables; without the export they show
`weapon #19`.

The migration's first task checks on XAMPP's MariaDB 10.4.32 and on 11.4
that `INET6_NTOA`, `HEX` and the story expressions are accepted where they
are used (VIRTUAL columns, the conversion's UPDATE); where one is not, that
column exists only in the view.

**Privileges (DEPLOY.md 2).** The restricted service user has no DROP today.
Round D's migration needs none (no RENAME, no DROP TABLE, views made with
`CREATE VIEW`), only `CREATE VIEW, SHOW VIEW`. Replacing a view in a later
release (`CREATE OR REPLACE VIEW` and `ALTER VIEW` need DROP) and round D2's
removal of the legacy tables do: DEPLOY.md 2 gives the service user `CREATE
VIEW, SHOW VIEW, DROP` on `dw4online.*` from round D on (still nothing
outside that database; the backup before every update stays the way back),
and its update notes say to run that GRANT as root before `migrate`. The
migrator checks the privileges the pending migrations need (`SHOW GRANTS`)
before the first statement and refuses with the GRANT to run, so no
migration stops half way for a missing right. Root (2.1, the user's server)
has them all.

#### 9.1.4 The record and the tables (byte-exact, versioned)

| Record bytes | Table value | Rule |
| --- | --- | --- |
| 0 ISUSE | the row exists | 1 |
| 1-3 | - | 0 (never written or read; an upload with other bytes is refused, `dead_bytes`) |
| 4 UNIQUE | `online_slots.unique_id` | |
| 8-39 DIGIMONNAME | `digimonname` | NUL, then zeros (the bytes after the NUL are dead) |
| 40-41 | - | 0xFFFF (the old byte-string path 802FB59C is refused, `name_marker`) |
| 42-57 PLAYERNAME | `playername` | 1-8 UTF-16 code units, 0 after the last; 58-71 are 0 |
| 72 LEVEL | `online_forms.level` of the current form | the upload's LEVEL must equal it (`level_header`); `make_record` runs CURRENT2BASE first (`save_game.cpp:51`) |
| 76-96 HP, MHP, MP, MMP, XDATA, BIT | `online_slots` | MHP / MMP stored as written (9.1.6 checks them) |
| 100-243 DEVICEFOLDER | `online_devices` folder rows by (`sort`, `id`), then -1 | `record \| equipped << 10 \| kind << 12 \| pocket << 16 \| plus << 20`, bit 11 0 |
| 244-291 DISKFOLDER | `online_disks` by (`sort`, `id`) | `record \| 4 << 12 \| count << 16` |
| 292-341 CARDLIST, 342-343 | `online_cards` | 1 per card held, the pad 0 |
| 344-727 BANKDEVICE | `online_devices` bank rows | as the folder, never equipped |
| 728 BANKBIT | `bank_bits` | |
| 732-791 POWERSOCKET ... SUBSLOT | `equip`, `equip_pos` of the folder rows | index = rank - 1 of the device in that place, else -1 |
| 792-1891 BASE_FLAG, BASE_FLAGFOLDER, BASE_COUNTER | `online_story` of the account (8.4), the box's `online_rewards` over it: each per-player reward flag is 1 exactly when the box has its row | composed, never taken from an upload (round 61: a guest's uploads keep its own story; the reward flags reach the store through the keeper, 9.1.9) |
| 1892-3315 BASE_ISUSE ... BASE_UPCNT | `online_forms` (a hidden form, `reached` 0, as the start values) | |

- **Canonical record.** Every record the port writes is canonical (dead bytes
  0, no gaps, equipped bits = the lists, LEVEL = BASE_LEVEL of the form,
  disks in canonical words); all 511 distinct records are. The server accepts
  only canonical uploads, so `encode(tables) == upload` outside 792..1891 for
  every accepted upload that needed no merge (9.1.7). Each write checks it in
  the transaction (compose the written rows, compare with the state it meant
  to write) and refuses with `mapping_mismatch` (an error the server logs as
  a bug) instead of storing anything else.
- **Versioned.** `layout` names the record layout and the mapping version
  (`gdje-v4.1`). A later change (another region's layout, a new column) gets a
  new version; rows keep theirs until a data step converts them, and the
  history keeps the exact bytes of every revision whatever the mapping then.
- **The engine's fixpoint.** By the code `make_record(apply_record(r)) == r`
  for canonical records whose MHP / MMP equal the derivation
  (`save_game.cpp:48-160`; `apply_record` compresses the folder and the
  bank, `make_record` writes them back without gaps, `recalc_player` derives
  MHP / MMP, so a buffed MHP comes back derived, and HP 0 would be refilled);
  round D runs it for every record the tests compose (9.1.14).
- **Same code on both sides.** The C# mapping (tables <-> `SaveRecord` <->
  bytes) and the C++ codec share golden vectors (synthetic records, no game
  data) in `data/online/record_golden.json`.

#### 9.1.5 Load path

1. **Entering a room** the client calls `GET /online-characters`. The server
   first processes a pending staff edit of each box (9.1.7: a load does not
   wait for the quiet time), then composes each box's record from its tables,
   the account's story and the box's rewards (`encode`, 9.1.4) and sets
   `served_revision`. The answer: per box the ETag `"<generation>-<revision>"`,
   `record` (base64), `summary`, `playSeconds`, `flagged`, `staffEdited` (a
   staff revision since the client's last load), and `activeBox`.
2. **The client builds card A in memory** with the four boxes (two copies,
   checksum, version 4, the 8 KB header of the save utility) and gives the
   engine a card store over that image; `online_card.dat` is only written as
   the cache for crash recovery (with the account's own story bytes, as
   round G keeps every cached record) and is never read for play. (Today
   `OnlineCard::store()` reads the file, which a player could edit between
   the room entry and the character select.)
3. **An unsent save** of the last session (`online_card.state` pending, per
   box, with the ETag and the visit key it was made at) goes up **before**
   the card is built, after a `/play` for its box (its binding; `why:
   recovered`): on 200 the card holds the server's answer, on a refusal or a
   conflict the server's current record and the cached save goes to
   `backups/` with a notice. Today the unsent save itself is played
   (`character.cpp:367-376`).
4. **The Game Start** binds the box played (`POST
   /online-characters/{box}/play`, 9.1.1): the box the session's uploads may
   go to, the visits the change rules count for it, and the active box of
   the profile.
5. **Determinism.** The tables reach an engine only through the record at
   the member's own title (a solo instance, 8.5). From there the character
   travels as today: in the carry of a JOIN or co-arrival and in the stage
   snapshot (`PlayerCarry`, `online_membership.hpp:26`), the same bytes on
   every engine of the instance. A staff edit never reaches a running engine
   (9.1.7), so no instance sees a member change mid-stage.

**Without the game data export** the server composes and validates the
structure as before; the rules that need a game table are off and every
history row they skipped notes `rules_partial`. That is never silent: the
admin pages show a banner ("Game data missing: N rules are off"), `/health`
reports `degraded` with the list, and the service log warns at every start.
`OnlineCharacter:RequireGameData` (default false) refuses character uploads
with 503 `game_data_missing` instead (the client keeps them pending).

#### 9.1.6 Upload path and the rules

`PUT /online-characters/{box}` with `If-Match: "<generation>-<revision>"`
(or `If-None-Match: *` for a new box), the record, the play time and the
context (today's, with `cheats` from the engine's real flags, 9.1.10, plus
`why`: `save_room | stage_change | leave | recovered`, and `visitKey`: the
instance and visit of the stage the save closes, as BARRIER_OK named them).
The server:

1. checks the version and the ranked context (today's), decodes the record
   and checks the canonical form (9.1.4);
2. replaces the story bytes with the account's story and the box's rewards
   (as today, `OnlineCharacterService.cs:48-55`, plus 9.1.9);
3. checks the binding (9.1.1): none is 409 `play_required`, another box is
   `wrong_box`;
4. locks the box and processes a pending staff edit first (9.1.7);
5. finds the **base** `B` (what the client's game started from) and the
   **stored state** `S`:
   - `If-None-Match: *`: the box must be free and the binding a new
     character with this UNIQUE; `B` = `S` = the new-character record the
     binding holds (checked at `/play` against `create_player`, `new_template`
     below). Today a new character is checked only for `play_time_too_fast`,
     so a forged first upload that is consistent in itself (level 999, every
     form, 9,999,999 BITS, purple gear) would pass; against this base every
     such value is a change the window has to explain;
   - `If-Match` with another generation: 409 `revision_conflict` (the box was
     reset and made again);
   - `If-Match` naming the current revision, or a revision after which only
     staff revisions (`staff_edit`, `sql_edit`, `edit_reverted`,
     `admin_restore`, `admin_accept`) were written: `B` = the record the
     client holds at that revision (`client_record`, 9.1.7), `S` = the
     tables. Today every staff write raises the revision and the player's
     next save meets 409, stops its uploads for the room session and reloads
     (`OnlineCharacterService.cs:63`, `character.cpp:597-611`);
   - any other revision (another PC saved meanwhile): 409
     `revision_conflict`, handled as today (the client keeps its save in
     `backups/` and reloads; it never overwrites);
6. finds the **window**: the visits of this account **and box** whose
   sequence number lies after the base's (`visit_seq` of `B`'s revision; for
   a new box the visit the binding started at) up to the visit `visitKey`
   names, never by time. The client uploads one save at a time and lets the
   newest waiting save replace older ones (`character.cpp:506`), retries
   after 2 / 10 / 20 s (`:590`, `:613`), and makes the stage change's save at
   the rising edge of `pending_stage()`, before the barrier (holds up to 30
   s): a window by acceptance time would lose visits and refuse honest
   saves. The relay writes a visit's row when the visit starts, in the
   barrier decision before BARRIER_OK goes out (and at the title instance's
   start), and closes it when the member's next barrier is decided (after
   the keeper took that visit's pending set), when it leaves the room or at
   the disconnect timeout. So:
   - a visit still open counts with its end = now for durations;
   - a `visitKey` the server cannot resolve yet, or a window whose last hub
     visit is still open while the upload has changes that need the keeper's
     state of that visit (a new form 4-15, quest Bits), is answered 409
     `visit_pending` with `Retry-After: 2`; the client retries it like a lost
     race (never a conflict); after `OnlineCharacter:VisitPendingSeconds`
     (90; barrier holds end after 30 s, the disconnect timeout after 35 s)
     the server decides with what it has;
   - a `visitKey` of another account is refused (`visit_foreign`);
   - a save without a `visitKey` (a save cached by a client before round D)
     or with one that never resolved gets the server's own log as its
     window: every visit of the box after the base up to the newest. The
     change rules run on it as on any window (refusing), and the box's
     `visit_seq` stays at the base, so the next save counts those visits
     again (a missing key can only widen the window, never shrink the next
     one, and never switch a rule to *flag*);
   - a box migrated from before round D has no visits before the update:
     its first upload's change rules flag instead of refusing (once per
     migrated box; its record rules refuse as always);
7. merges: `M` = `merge(S, B, U)` (9.1.7), which is the upload `U` itself
   whenever `S` = `B` (no staff write since the base: the normal case);
8. checks the **record rules** on `M`, the **change rules** and the
   **bounds** on the player's own change `B -> U` with the window, and the
   **rates** on that change per minute of the window's visits;
9. decides per issue: a rule set to `refuse` refuses **only when one of the
   fields the issue rests on differs between `S` and `M`** (the upload or
   the merge changed it); an issue on unchanged fields becomes the flag
   `grandfathered:<code>`. A state the player did not make (a migrated row,
   a staff edit, a rule that got stricter, a C# `recalc_player` error for a
   form that has not changed since) therefore never refuses the next save.
   Then the breaker (below); a rule set to `flag` keeps the upload and flags
   the box;
10. writes only the rows that changed, the slot row, `online_slot_stats`, a
    history row with `changes`, the window and, when `M` differs from `U`,
    `client_record` = `U`; `visit_seq` = the window's end; the fingerprint
    (9.1.7); then the state achievements of that box (as today, per box).

The answer is today's plus the new ETag, `merged` (the field groups a staff
edit changed under the upload, 9.1.7) and `flagged`. A refusal is 422
`refused` with the issues and `leaveRoom: true`: the client stops saving,
leaves the room with "The server refused your online character's save
(<issue>). It loads again from the server when you rejoin." and the next
room entry loads the server's character (`OnlineCharacter:LeaveRoomOnRefusal`,
default on; off: the uploads stop for the session as after a 409, nothing is
played from a refused state after the next entry either way). The refused
record goes into `online_rejected_uploads`.

Every rule has a stable code and a configured action
(`OnlineCharacter:Rules:Actions:<code> = refuse | flag | off`), so a false
positive is switched off without a release. Defaults:

| Code | Rule | Default |
| --- | --- | --- |
| `record_size`, `record_blank`, `record_unused`, `dead_bytes`, `name_marker`, `name_empty`, `name_character`, `name_trailing_data` | structure (9.1.4, today's checks) | refuse |
| `unknown_form`, `form_not_reached` | DIGIMONNAME is one of the 16 and that form is reached (today a warning) | refuse |
| `level_header` | LEVEL = BASE_LEVEL of the current form | refuse |
| `exp_band` | a reached form: NEXTEXP(L-1) <= EXP < NEXTEXP(L), NEXTEXP(L) = (L+140)L² + 200L (80370DBC); level 999: EXP >= NEXTEXP(998) = 1,133,652,152 | refuse |
| `form_defaults` | a form never reached: level 1, EXP 0, every skill 1, every boost 0 (8036EB0C) | refuse |
| `skill_range` | 1 <= skill <= limit(L): 30L (L <= 36), 20L + 360 (<= 107), 10L + 1430 (<= 227), else 8L + 1884 | refuse |
| `boost_range` | every UPCNT >= 0 | refuse |
| `mhp_derived` | MHP / MMP = the server's RECALCPLAYER of the current form with its gear (growth table 804EAD24, shinka INI / GROW, UPCNT, armor1 / armor2 MAXHP / MAXMP, caps 99999); above it by a power-up buff (UPDATA[6], cleared only by the next GENERATE) | below: refuse; above: flag |
| `hp_range` | 1 <= HP <= MHP, 0 <= MP <= MMP | refuse |
| `bits_range` | BITS, BITS Bank 0..9,999,999 | refuse |
| `xdata_range` | ¶ Data >= 0 (refuse); above 9999 (flag: pickups do not clamp, only a shop opening does) | refuse / flag |
| `device_*` | folder and bank without gaps; kind 0-3; record inside its table; Mods 0-5 on weapon / armor1, 0 otherwise; no +N on chips; bit 11 0 | refuse |
| `equip_*` | indices -1 or inside the folder; Weapon in POWERSOCKET, chips in the pockets, armor1 in MAINSLOT, armor2 in SUBSLOT; the equipped bit = listed; no index twice | refuse |
| `equip_rules` | weapon 1 usable by the form (SOUBI bit) with its skill (SKILLPOINT); armor LVPOINT <= level; pockets used <= the device's Mods; WU chips in the weapon, BLUEAB chips in the armor (game data) | refuse |
| `equip_stat_twice` | two chips raising the same value in one list (80381D20) | flag |
| `disks` | twelve disk words, records a permutation of 0-11, counts 0-9, no flag or +N | refuse |
| `cards` | 0 / 1, the pad 0 | refuse |
| `new_template` | at `/play` with `new`: the record is `create_player`'s (8036EB0C, `combat.cpp:752-775`) for its rookie: the current form one of the rookies the character select offers, level 1, EXP 0, every skill 1, every boost 0, only INI_WEAPON1-3 of that form in the folder and in POWERSOCKET 1-3, the rest of folder, bank, cards and the disk counts as a fresh port save has them, HP / MP derived; only UNIQUE and PLAYERNAME are free (the rules lane takes the exact template from a fresh port save, not from this text) | refuse (the binding) |
| `unique_changed`, `form_level_dropped` | today's | refuse |
| `wrong_box` | an upload for another box than the binding's (9.1.1) | refuse |
| `visit_foreign` | the `visitKey` belongs to another account | refuse |
| `skill_dropped`, `boost_dropped`, `form_lost` | skills, boosts and reached forms never go down (`form_lost` today a warning; EXP may fall within the level at a defeat, `combat.cpp:1032-1041`, so EXP has no such rule) | refuse |
| `lobby_only` | a new form, a new current form, bank or card changes, ¶ Data spent, boosts from the ¶ Data Keeper: only with a window visit to the stage that does it (ML11 / ML12 rewards and Digi-Lab, ML12 / ML13 bank, stagIS11 cards, ML12 keeper) | refuse |
| `field_only` | EXP, skills, Plug boosts (+1 / +2 each; the MaxMP Plug raises the HP boost as the port has it, `items.cpp:148-152`, not yet checked in the DOL), ¶ Data pickups (+5 / +20 / +100): only with a field, dungeon or boss visit in the window | refuse |
| `rookie_pair` | a new rookie needs an ML12 visit in the window and its Digi-Lab predecessor reached in `M`: `stagML12_m1__sin_digi.lua` gives Dorumon to Agumon, Veemon to Dorumon, Guilmon to Veemon, Agumon to Guilmon (a chain in one visit is possible, the Digi-Lab changes the form in between); the Digi-Lab opens from the own level 16 (flag 937) | refuse (the level: flag) |
| `form_reward` | a new form 4-15 needs an ML11 / ML12 visit in the window and a reward flag the box took in the window from a form-giving script (treasure02-04, 06-08); at most as many new forms 4-15 as such flags; the form one that script's branches can give (`data/online/form_rewards.json`, derived from the decompiled scripts like `story_sets.json`, flag numbers and form ids only) | the count: refuse; the form per script: flag until round D2 has checked the table |
| `drop_plus` | a device new in the window with +N above the highest level among the window's instance members + 1 (803B0AB4 rolls with slot 0's level; each member's level from its box's newest accepted revision) unless it is a shop's, an identification's or a script reward's fixed value (32, 200, 666 ...) | refuse above that + `OnlineCharacter:DropPlusMargin` (10 levels: a member's level-up in the visit whose upload is not in yet); flag between |
| `exp_bound` | the EXP the window gained <= 10 x the sum over its field, dungeon and boss visits of seconds x the stage's highest enemy EXP for the difficulty (`game_stage_enemies`; without it the game's highest, 6,000) x 130 / 100 (EXPRITSU at its maximum 30): one kill a second of the best enemy there, ten times over | refuse |
| `bits_bound` | the BITS the window gained outside shop visits <= 10 x the sum over its field visits of seconds x the stage's highest Bits band x 130 / 100 (BITRITSU 30), plus the quest rewards of its hub visits | refuse |
| `shop_sums` | in a shop visit BITS rise only by what removed devices sell for (KAITORI + 25 * value + rank bonus) and by quest rewards (the devices and `SYSctlBit ADD` of the treasure scripts), added devices come from the shop, identification, keychain or quest reward tables and cost no more than the BITS held (803B08F8); bank moves keep BITS + BITS Bank; the ¶ Data spent is at least what the boosts bought (cost ceil((v-45)/5), 1..400, 80394814) and chip upgrades (NEXTX) cost: a lower bound, the cost follows the stat with gear at the purchase (`xdata_stat`) | flag (refuse after calibration, round D2) |
| `name_changed` | today's | flag |
| `exp_rate`, `bits_rate`, `xdata_rate`, `skill_rate`, `level_rate`, `play_time_*` | per minute of the window's visits above calibrated limits (round D2; until then generous) | flag |
| `missing_upload` | a visit of the bound box that no accepted upload's window covers within a day (the client skips unchanged saves and merges waiting ones, so a visit without its own upload is normal) | flag |

**The breaker.** A rule that refuses an honest save would send that member
out at every stage change, and *Accept* alone does not help while the box
plays on. So, per box: when the same code refused the same box twice in a
row, that box's later uploads with it are kept and flagged (`breaker:<code>`,
the box *under review*) and the admin page alerts; staff reset it with the
review. Per code: refusals of three accounts within 24 hours raise an alert
on the admin page; `OnlineCharacter:Rules:BreakerGlobal` (default off) then
also switches the code to `flag` for everyone until staff switch it back
(off by default because three accounts of one cheater would switch it).
`OnlineCharacter:Rules:BreakerConsecutive` (2, 0 = off) is question 14: the
price is that a cheater who forges the same thing a third time keeps it,
flagged and visible, until staff restore the box.

**A false positive** is fixed by staff: a flag by *Reviewed: fine*
(`reviewed_by`, an audit row; the character stays unflagged until a new
flag); a refusal by *Accept this upload* on the admin page (the refused
record is merged like an upload from the revision it named, so it also works
when the box has moved on, and becomes the next revision `admin_accept`); a
rule that misfires by setting its action to `flag` or `off`. Achievements
from flagged uploads stay hidden from rankings until reviewed (as today).

#### 9.1.7 Staff edits

Two ways, the same rules:

- **The website** (`/admin/account/{name}`, a section per box; admins edit,
  moderators review): every value of 9.1.2 with the game's names, devices
  picked by item name, *Strength = 2500* as well as the boost; a preview
  shows every completion the server will make, the destructive ones too
  (skills lowered, a form's values reset, devices taken off), before the
  save; the server normalizes, validates, writes a revision (`staff_edit`,
  the actor) and an audit row.
- **HeidiSQL**: edit the `online_*` rows directly. HeidiSQL writes each row
  on its own with autocommit, so the server waits until an edit is done:
  the fingerprint `tables_sha256` is the hash of every value of the box's
  rows (not the timestamps) as the server last wrote them; every minute the
  watcher looks at the boxes whose `changed_at` is newer than the slot's
  `written_at` and processes a box once its rows have been quiet for
  `OnlineCharacter:EditQuietSeconds` (120); a load or an upload of the box
  processes it at once (the admin page only shows a waiting edit, with
  *Process now*). A different hash is an edit: the
  server completes it (only completions that destroy nothing, the table
  below), validates it, writes a revision (`sql_edit`, actor unknown, the
  changes against the last revision) and an audit row `character_edit`
  (source `database`). Deleting a row is caught the same way.

**Validation of an edit** runs every record rule of 9.1.6 set to refuse on
the completed state (the change rules, bounds and rates judge a player's
play, not staff), with the same "only what changed" test against the last
revision (a grandfathered flag the edit did not touch stays a flag); the
website does the same before it saves. An edit the server
cannot complete without destroying something, or that still breaks a rule,
is **not applied**: the box is written back to its last revision at once
(`edit_reverted`), the edited rows are kept in `online_edit_drafts` with the
issues, and the admin page shows "Your database edit of box 2 was not
applied: <issues>. Open it as a draft." The website opens the draft with
the preview and the completions the database path may not make. The box is
never locked: loads and uploads go on from the last valid revision (the
first design's 423 kept a waiting save that then met 409 after the fix).

| Edit | Completion |
| --- | --- |
| BITS, BITS Bank, ¶ Data | clamped to 0..9,999,999 / 0..9999 |
| a form's level | EXP set to the level's start (NEXTEXP(level-1)) unless the EXP was edited too and fits; skills above the new level's limit: not applied in HeidiSQL (lowering them destroys values; the website does it with the preview) |
| a form's EXP | the level follows the EXP (the game's level-up loop, `combat.cpp:1100-1106`, downwards as well); skills as above |
| a form's derived value (`strength`, `max_hp`, ...) | the boost is set: `up_strength = wanted - (grown + INI)`, at least 0, the stat at most 9999 (HP / MP 99999) |
| `reached` | 1 adds the form (its hidden values again if it has any, else the start values); 0 hides it: the record composes the start values, the row keeps its numbers until the player reaches the form in the game again (then the game's values count; the old ones stay in the history); the current form cannot be hidden |
| `digimonname` (current form) | a reached form; the Digi-Lab's change follows (8039CA4C, `subwnd_shop.cpp:2124-2141`): the armor and the chips off, the strongest weapon the form can use on |
| devices | order renumbered (`sort` 1..n by (`sort`, `id`)); `pocket` / `plus` clamped per kind; equipment completed by the game's rules, the devices staying in the folder: a weapon the form cannot use or whose SKILLPOINT its skill does not reach comes off (the strongest usable one goes into an empty first socket), an armor whose LVPOINT the level does not reach comes off, chips beyond the device's Mods or of the wrong family come off; a wrong kind in a place, two devices in one place, an unknown record: not applied |
| disks, cards | counts clamped 0-9; a disk record twice or missing: not applied |
| HP / MP | MHP / MMP derived again, HP / MP clamped |
| `playername` | an ASCII name converted to the card's full-width form (`CardName`); empty or longer than 8: not applied |
| `unique_id`, `layout`, `generation`, `revision`, `visit_seq`, hashes, `online_slot_stats` | not editable: the server puts them back (the audit row notes the attempt) |

**Locking.** Every writer of a box (an upload, a load or watcher processing
an edit, the website's editor, restore, accept, reset) runs in one READ
COMMITTED transaction that locks the slot row and then reads every child
row of the box `FOR UPDATE` over its key range (`(account_id, box)`), checks
the fingerprint of what it read (a difference is an edit, processed first),
writes, and stores as the new fingerprint the hash of the rows it meant to
write, computed from its own state, not from a read after the write.
HeidiSQL's `UPDATE` of a locked row waits for the commit and lands after
it, and an `INSERT` that slipped in meanwhile (READ COMMITTED has no gap
locks) does not match that hash: both are an edit at the next check, never
lost and never taken for the server's own data.

**When the player is online.** The edit is in the tables at once and in his
game at his next load; his running engine is never changed (determinism,
9.1.5). His uploads meanwhile name a revision before the edit; the server
accepts them (9.1.6 step 5) and **merges**: `S` = the tables (with the
edit), `B` = the record his game started from (`client_record` of the
revision he named), `U` = his upload, `M` = `merge(S, B, U)`:

- counters (BITS, BITS Bank, ¶ Data, each form's EXP, skills and boosts,
  disk counts): `S + (U - B)`, clamped to their ranges; each form's level
  then follows its EXP and its skills are clamped to that level's limit;
- single values (the current form, the name, each form's `reached`, each
  card): the staff value where staff changed it (`S` != `B`), else `U`'s; a
  form the upload reaches newly takes the upload's values;
- devices of the folder and the bank as multisets of (kind, record, Mods,
  +N) per list: the staff's change (`S - B`: devices removed and added) is
  applied to the upload's lists: a removed device is taken out where it
  still is (one the player sold or upgraded meanwhile is noted,
  `merge_missing`), an added one goes into the folder (full: the bank; both
  full: `merge_overflow`, kept for staff in the history's note); the
  equipment is staff's where staff changed it, for the devices still there,
  else the upload's, then completed as in the table;
- the result is validated like any upload.

So a staff edit never costs the player the progress he made meanwhile and
never makes him lose the session, and nothing he holds overrides the edit:
BITS staff removed stay removed while his purchases and finds still count.
The server keeps `U` as `client_record` of the new revision (the record his
game holds), so his next upload merges from there again; the next room
entry loads `S` and ends it. The answer lists `merged`, and the client shows
"Staff changed your online character (BITS, Dorumon). Your progress was
added to it; you see the change when you next enter a room." once. A merge
makes his running game differ from the tables until then (bits a cheater
still holds can be spent in that session, against the staff value): for
corrections that must take effect now the admin page has **Reload now**,
which removes the member from its room (reason `character_edited`, the
client says "Staff changed your online character. Join the room again to
load it."; its leave save is merged like any other), as a kick does.

**Audit.** Every edit writes `audit_log` `character_edit` with `{box,
revision, source: website | database, actor, changes: [{field, from, to}],
completed: [...], merged: [...]}`; a database edit not applied writes
`character_edit_reverted` with the draft's id and the issues; restores,
accepts and reviews write `character_restore`, `upload_accepted`,
`flag_reviewed`. `v_character_edits` lists them with the open drafts.

#### 9.1.8 Migration of today's rows

Migration 00NN (the number at the integration: 0011 is round 59's) creates
the tables, the references, `util_seq`, the side tables, the virtual columns
and the views, converts the hash and address columns (9.1.3), and marks
`online_characters` / `online_character_history` as legacy by their table
COMMENT. Nothing is renamed or dropped: the service user of DEPLOY.md 2 has
no DROP (and RENAME TABLE needs it), so round D's migration runs on the
documented configuration as well as on root.

A **data step** of the migrator (new: a C# step that belongs to a migration
version) converts the rows. It runs **inside** the migration's started mark
(`Migrator.cs:30-35`: the mark is replaced by the real checksum only after
the SQL and the step finished, so a half run is refused as an interrupted
migration, DEPLOY.md 9), and the step's name and code version are part of
the migration's checksum (`SHA-256(sql + "\nstep:" + name + ":" + version)`),
so a step changed after it ran is refused like a changed file and needs a
new migration. Per legacy row: decode the record, map it into box 1 (today's
box 0, where `enter` puts it), compose it again and require the same bytes
outside 792..1891, copy `revision`, play time, flag and times, give a new
`generation` and `visit_seq` 0, move the history (the blobs into
`online_slot_history_records`, `changes` computed between neighbours, the
reason kept), and write an audit row `character_migrated`. Then **every rule
runs** on the migrated box and on its newest revision against its
predecessor, as flags only (`migrated:<code>`): what the old checks let
through (they only warned of `species_not_reached`, `form_lost`,
`disk_count`, and never checked `equip_rules`, `mhp_derived` or `exp_band`)
is marked for staff and, by the "only what changed" test of 9.1.6, never
refuses a later save. A row that does not compose byte-exactly is migrated
as decoded and flagged (`migrated_inexact`); the migrate output names the
counts (`Converted 12 online characters, 12 byte-exact, 3 flagged`). Round
G's `online_rewards` rows go to box 1 (a `box` column added with 1 if round
G built the store without it). The client treats a cache whose
`online_card.state` says `box=0` as server box 1 (9.1.1).

A data step that fails stops the migration as a failing SQL migration does
(DEPLOY.md 9's table: restore the backup, report it). Round D2's migration
drops the legacy tables (with the DROP right round D grants, 9.1.3); until
then a rollback (DEPLOY.md 8) restores the backup as always. Queries on the
old tables move: `AchievementRepository.cs:115`, the account erasure list
(`AccountRepository.cs:140`), the history housekeeping
(`SessionRepository.cs:113`).

#### 9.1.9 Guests, rewards and the room story (round 61)

The character tables hold no story: the record's story bytes are the
account's own `online_story`, composed at load and replaced at upload, so
round 61's rule holds by construction (a guest's uploads keep its own story;
its personal values, everything in the tables, are its own wherever it
plays).

**Rewards per box.** Round 61's answer 9 keeps the quest line's taken
rewards *per player* in a store of their own (`online_rewards`, round G's
hub extras lane, 8.8). With four characters per account the player is the
box: a store per account would let box 1's rewards block box 2 for ever (a
second character with another partner would never get its partner's weapon,
and never the forms box 1 already took: `treasure02` gives the forms 4-7 by
the partner), which contradicts "alle Digivolutions". So `online_rewards` is
keyed (account, box): the keeper writes the reward flags of a member's
pending set to the box its binding names, every box's record carries its
own (9.1.4), and a reset of a box empties its rewards. Round G is asked to
build the store with the `box` column from the start (always 1 until round
D); if it is already built without it, round D's migration adds it (9.1.8).
`form_reward` reads these flags of the uploading box and the visits of its
window; the room story no longer holds them.

**Visits and quests.** The change rules read the visits of the uploading
member and box, whoever owns the room: a guest's lobby visits are its own
solo hubs, its field visits the shared instances (slot 0 there, whose level
bounds a drop's +N, may be the owner). Round 61's answer 10 (b) gives a
guest the owner's quest tiers at the boards: they come from the box the
owner's binding names (its newest accepted revision; without a binding,
the owner at his title, from his active box), not from "his newest online
character".

#### 9.1.10 What the rules cannot catch

The server never simulates (1.2: no arbiter engine) and the random numbers
of drops, +N, Mods, Bits amounts, skill-ups and identification live only in
the engines' generator. So, plainly:

- **The Cheats tab is open in seamless rooms today.** It disables its
  controls only for the old direct connection: `BeginDisabled(net.active())`
  (`game_main.cpp:3566`) and the player buttons with `sys &&
  !net.active()` (`:3650`), the same on `fix-r59s2`, `fix-r62netlag` and
  `feat-r63ownmenus`. In a room God mode, Infinite MP, One-hit, the sliders,
  the randomizer drops, *+1 / +10 levels*, *+100,000 bits* and *Refill* all
  work: `set_online` resets the cheats only when the engine is built
  (`online.cpp:212`). They desync the instance, the upload context reports
  `cheats: false` as a constant (`character.cpp:118`), God mode and One-hit
  leave no trace in the record, and with round D an honest click on "+10
  levels" in the lobby would be refused (`field_only`). Fixed before round D
  (17): the tab's controls disabled and the player buttons hidden whenever
  `online` is set, and `cheats` in the upload and achievement contexts from
  the engine's real flags (an upload with cheats on is refused by today's
  ranked check).
- Within the game's rules nothing is caught: luckier drops than chance,
  faster kills inside the bounds, a bot that plays all night, a client that
  changes values inside the limits (a few more EXP per kill, a better
  plausible drop), items given between a player's own accounts (giving is a
  game feature).
- A modified client plays with modified values until its next upload: at
  most one stage visit plus the answer's round trip. Members of its instance
  see it meanwhile; what they gain from it (EXP of kills it made easier, a
  plausible item it gave them) is theirs. A client that never uploads keeps
  nothing (`missing_upload` flags it).
- A refused change never survives a room entry. A **flagged** change does:
  it stays in the box until staff review or restore it, and the profile
  shows the box *under review* (no details) meanwhile, so flagged values are
  never shown as if checked. Above the bounds of 9.1.6 nothing is merely
  flagged: what the game cannot produce in the window's time is refused.
- The rates and the shop arithmetic need calibration (round D2); until then
  they only flag. The per-script form table of `form_reward` flags until D2
  has checked it (its count refuses).
- Cheating offline is not a concern: offline saves never reach the server
  (online characters start from zero, 1.1).

What it does catch: every value the game cannot produce (ranges, packing,
levels against EXP, skills against level, stats against the formulas, items
that do not exist or cannot be equipped), every change the place cannot
produce (a form, a bank or a card outside its lobby, EXP in a lobby, a
rookie without its Digi-Lab pair, a form without its quest reward), every
gain the window's time cannot produce, and a new character that does not
start as the game starts one.

#### 9.1.11 Pictures and names from the user's game data

The game's art and text may not be committed. `dw4_game.exe --data <game
data> --export-web <folder>` (beside `--export-presence-art`, reusing its
loaders: `MountedResources`, `read_igb_document`, `decode_igb_texture`,
`write_png`; about 9 s) writes, from the user's own copy:

| Output | Source |
| --- | --- |
| `art/forms/<model>.png` | the 64x64 round faces of charaicon.png (256x256, a 4x4 grid in the order agumon, vmon, girumon, dorumon, angelr, alpha, beelzeb, hekabut, weregaru, wargrey, impdrafm, impdrapm, dukecrim, susanoo, metalgaru, bwargrey, by the AGUMON..BWARGREY actions of `i_stdigiface` / `j_digifacewin`): the soft-halo variant of stagis10 / stagis12 `separate/usa_pack.igb` |
| `art/forms_full/<model>.png` | the staff roll's 256x256 full-body renders (stagis20 `separate/aus_pack.igb`, j_endroll), named by the model without `p_` (agumon.png ... susanoo.png) except Alphamon, whose render is `alpha8.png` (the exporter maps `p_alpha` to it; a model without a render gets no file, and the page falls back to the face); the game shows them dimmed (texels at most 99/255), the export brightens them by 255 / max (an editorial change, said on the profile page's credits) |
| `art/cards/card_NN.png`, `back.png` | card01..card50.dds (128x128) of stagis11 `separate/aus_pack.igb`; card NN = row NN-1 |
| `art/icons/{weapon,armor1,armor2,chip,xdata,fire,ice,thunder,dark,equipped}.png` | the font glyphs ∈ ∋ ∨ ∧ ¶ ⊂ ⊃ ∪ ∩ ⊆ of bectext.igb font00.png (beFontImage metrics as `TextCtlManager::load_fonts`), white with alpha, coloured by the page |
| `data/game_data.json` | the tables of 9.1.2 (`game_*`, `game_stage_enemies` from the stages' generator files), names through `game_text_utf8` |
| `manifest.json` | every key written, size, SHA-256, the game data hash, the exporter version |

The operator copies the folder to the server (`GameData:Directory`, DEPLOY.md
gets the step); the service imports `game_data.json` when its hash differs
from `game_data_meta`, and serves `art/` under `/art/` (PNG only, keys from
the manifest, no listing, no path outside the folder, cached a day). A key
the manifest lacks is never linked: the page shows a placeholder made for
the site (an outline and the form's name; checked in) instead of a broken
picture. Serving the game's pictures publicly is the operator's decision, as
with the Discord assets (13).

#### 9.1.12 Player profiles on the website

`/u/{name}` becomes the player profile; `/u/{name}/box/{n}` shows one box
in full. The site stays without JavaScript (its CSP is `default-src
'none'`), so boxes are links, not tabs, and colours are CSS classes
(`style-src 'self'` forbids inline styles).

- **Header**: the profile picture is the active box's current form
  (`art/forms_full`, else the face, else the placeholder), with the Tamer
  name, the form, the level and the box. The same face is the account's
  small picture in the room list and on the account page.
- **Boxes 1-4** side by side: face, Tamer name, form, level, play time,
  *under review* for a flagged box; an empty box says so.
- **A box**:
  - *Character*: HP / MP bars, BITS, ¶ Data, the current form's Strength,
    Defense, Wisdom, Spirit, Speed, HP Max, MP Max and the resistances (⊂
    fire, ⊃ ice, ∪ thunder, ∩ dark) with the gear included, as the status
    window shows them.
  - *Equipment*: Weapon 1-3 (∈, the name and +N in its colour, *Mods: n*,
    the chips in the pockets of weapon 1), the ∋ armor with its chips, the
    ∨ armor; each with its icon in the gear colour.
  - *Digivolutions*: all 16 forms; reached ones with the face, name, level,
    EXP and the bar to the next level, the nine skills, the boosts, the
    form's own stats and the techniques it knows; the others greyed with
    "not reached" (the count "n of 16" as today).
  - *Collection*: the cards held (card art, else the name; the name in the
    card's colour), *n of 50*.
  - *Owner and staff only*: the Device Folder (36, all colours), the Item
    Bank and BITS Bank, the Disk Folder, the revision history with changes,
    flags and refusals.
- **Privacy** (default, question 12): a private profile shows nothing but
  the name, as today (`profile_public`). A public one shows the header, the
  four boxes, every form with its values, the equipped gear, BITS, ¶ Data,
  the cards and the *under review* mark; the folder, the bank, BITS Bank,
  disks, UNIQUE, history, flags and refusals only to the owner and staff.
  The privacy page lists what is public.
- `DigimonNames` gets Cl-Angemon (`p_angelr` is missing in
  `WebOptions.cs:44-61` and shows its model name today); names come from
  `game_shinka` when imported.

**Gear colours** exactly as the game's 803B07E0 (`device_color`,
`items.cpp:51-63`), by +N; chips, disks and the unidentified records
(weapon 276, armor1 31, armor2 44) always white; Mods do not change the
colour. The CSS values are the float32 constants times 255, rounded:

| +N | Game RGB | Name on the site | CSS |
| --- | --- | --- | --- |
| 0 | 0.9, 0.9, 0.9 | white | `#E5E5E5` |
| 1-30 | 0.1, 0.9, 1.0 | blue | `#1AE5FF` |
| 31-100 | 0.4, 1.0, 0.6 | green | `#66FF99` |
| 101-500 | 0.9, 0.8, 0.1 | yellow | `#E5CC1A` |
| 501-999 | 1.0, 0.6, 0.1 | orange | `#FF991A` |
| 1000-2047 | 1.0, 0.0, 0.9 | purple | `#FF00E5` |

**Card colours** as the booster pack shows a card's name
(`subwnd_shop.cpp:2641`, the `card` table's RARE): 0 common white
`#FFFFFF`, 1 uncommon yellow `#FFFF00`, 2 rare green `#00FF00`.

The names show on a dark panel as in the game; the icons are white masks
(`mask-image`) filled with the class's colour. A drop's +N is at most slot
0's level + 1 (803B0AB4) and script rewards give at most 666, so purple needs
a level 999 player in slot 0 and a 1-in-6,000 roll.

#### 9.1.13 Performance

A load composes four boxes from about 200 rows each (PRIMARY KEY and key
ranges, about 2 ms on the XAMPP MariaDB, an estimate); an upload checks in
well under a millisecond (a merge, rare, a few more) and writes the changed
rows only (a field visit changes the slot row, one form row and a few
devices: under 15 rows, about 5-15 ms with the history row and the box's
row locks, which only that account's writers and a HeidiSQL edit of that box
ever wait for). The watcher reads one indexed `changed_at` query a minute.
Storage per box about 15 KB of rows plus about 66 KB of history (20
revisions); `online_visits` about 15 MB a month for 20 regular players. The
views scan only the rows they show; `v_online_story` expands 1024 flags per
account through `util_seq` and is meant for looking at single accounts.
No extra latency in the game: uploads stay asynchronous as today (a
`visit_pending` retry delays only the answer).

#### 9.1.14 Tests

- **Server unit**: the mapping both ways for synthetic records and the
  golden vectors (also with the C++ codec, `record_golden.json`); every rule
  code with a passing and a failing case; the "only what changed" test
  (a violation on unchanged fields is `grandfathered`, the same violation
  after a change refuses); the breaker (two in a row per box, three accounts
  an alert); the new-character base (`new_template`, and a forged first
  upload consistent in itself refused); change rules with visit windows by
  sequence and box (a late answer after 429 / 5xx, a save before its
  barrier, an open visit, `visit_pending` and its time limit, visits of box
  1 not counted for box 2, a missing log); the ETag (a staff revision in
  between merges, another PC's upload is 409, a deleted and re-made box is
  409); the merge (counters, single values, devices as multisets with
  `merge_missing` / `merge_overflow`, equipment, a second upload after a
  merge); the binding (`play_required`, `/play` at the title only, a
  restart keeps it); rule actions from the configuration; each completion
  of the edit table, an edit not applied (revert and draft); edit detection
  by the fingerprint with the quiet time; the derived values
  (`recalc_player` in C#) against values the C++ engine writes for the same
  records with the user's game data (skipped without `DW4_DATA`, never
  committed); the game data import; the bounds against hidden bot runs'
  visits (no honest visit near the factor); the rarity of every +N boundary
  and every card colour; art serving (PNG only, no listing, no path outside
  the folder, the placeholder for a key the manifest lacks).
- **Server integration** (the local stack's MariaDB): the migration on a
  database of today's schema with characters (round 59's `character_test.py`
  data) as root and as the restricted user of DEPLOY.md 2 with the new
  grants (and refused before the first statement without them): byte-exact
  conversion, history moved, the rules as `migrated:` flags, the hashes and
  addresses converted (logins and sessions still work), the client's `GET`
  gives the same record as before; the views readable and no binary column
  left in a browsed table outside the legacy, `schema_version` and story
  ones; concurrent uploads of four boxes; a HeidiSQL-like `UPDATE` and
  `INSERT` while an upload runs (neither lost, both handled as an edit); the
  profile pages as anonymous, owner and staff (what each sees).
- **Client ctests** (`online_character`): four boxes on the card, per-box
  state and the 0-3 / 1-4 conversion, the card store in memory (an edited
  cache file is not played), the recovery through the server, new
  characters into the lowest free box with their `/play`, no PUT before a
  confirmed `/play`, `play_required` answered by `/play` and the save,
  `visit_pending` retried, a copy into another box not uploaded, a refusal
  leaves the room, the merge notice; the Cheats tab disabled online and the
  context's `cheats` from the engine; the export (`--export-web` on the
  user's data: every key of the manifest, sizes, the grid order of the
  faces, `alpha8.png` for Alphamon).
- **Engine**: `make_record(apply_record(r)) == r` for every record the tests
  compose and for the 511 distinct records (hidden, with the data).
- **Scenarios** on hidden real clients, section 16's table: D1-D16.

Online off stays bit-identical: round D changes no engine code
(`character_called` is round 59's; the Cheats tab fix is UI); the checks of
section 15 run as always.

## 10. Client: account and room UI

All program UI in English, ImGui, ASCII (account names are ASCII lower case on
the server).

- **Where.** The title menu's third entry *Online*, below Game Start and
  Option (the user's wish of 30.10: it looks like the two original entries),
  opens the screens over the title; the same screens in the F1 Online tab,
  which replaces the direct host / join UI (the `--host` / `--join` command
  line stays for LAN play and tests). (Revision 2 planned a small panel at the
  bottom right of the title; round S2 built and replaced it.)
- **Account.** Log in (name, password, remember me), Register (name, password
  twice, optional e-mail; the recovery code is shown once, confirmed with "I
  saved it"), Recover (name, code, new password; a new code is shown), Log out,
  Log out everywhere, Change password, Reset online character (password and
  a warning, not inside a room: the online character and its online story
  end; round 56 fix round; UI script `reset-character PASSWORD`).
- **Rooms.** List of compatible rooms (`GET /rooms?protocol&dataHash`: name,
  owner and whether present, players n/max, lock, difficulty, randomizer, story
  frozen), refresh, Create (name, optional password, 2-10 players, difficulty up
  to what the owner's online story unlocked, camera mode, light mode,
  randomizer seed and modes, fast loading), Join (password prompt). In a room:
  members with their area (from presence), Leave room, Kick (owner). The
  room view and F1 Online show whose story the member plays; a guest's Game
  Start shows "Playing on <owner>'s story" (round 61 design, 8.8).
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
  *Built in round S2:* `deploy/tls-key.ps1` makes two keys once on the
  development PC (in use and spare, RSA 3072 under a passphrase, kept offline)
  and certificates on a kept key (a PFX that `new-selfsigned-cert.ps1
  -ImportPfx` installs on the server), so a renewal or a new server keeps the
  pin; `deploy/print-pins.ps1` prints the pins of keys, certificates, PFX files
  or a running server (a handshake only) and writes `local/online_pins.txt`
  (gitignored). The pins reach `server_pins.hpp` by a build step: CMake takes
  `DW4_ONLINE_PINS` or that file, checks the form (two base64 SHA-256 at most)
  and passes them to `dw4_online` only; `DW4_ONLINE_REQUIRE_PINS=ON` fails a
  release configure without them (DEPLOY.md 6.1 and 6.4). No key ever enters
  the repository (`.gitignore` and `publish.ps1` refuse key and certificate
  files). The relay ticket key is made once and kept as well (DEPLOY.md 4).

**Built in round S2 (r56, UI lane).** The model is
`src/game/online/lobby.hpp` (`OnlineLobby`, no ImGui, tested over a
scripted HTTP queue in ctest `online_lobby`); the REST calls of the screens
are `src/game/online/lobby_api.hpp`; the ImGui screens are
`src/game/online_ui.cpp`.

- *Title menu entry.* TITLE2 (`src/engine/title.cpp`, case 2) takes the
  cursor down from Option to a third entry (cursor 2) while the host sets
  `Engine::title_online_entry`; up goes back to Option, A there sets
  `Engine::title_online_request` and leaves the menu waiting as it is, B
  cancels as on the other entries. The moves and the choice play the model's
  own sounds (SETSOUND `CURSOR_MOVE2`, `CURSOR_START`). The host sets the flag
  only offline with the online screens (not in an online or direct session,
  not in a test run without `--online-ui`; `DW4_TITLE_ONLINE=1` shows it in a
  test run for pictures), and neither field is in a snapshot or the
  fingerprint.
- *The third row in the picture* (`src/game/title_entry.cpp`, presentation
  only: it edits the frame's draw list after `Engine::render`). The model
  `j_startmenu02` has only two rows, so the third is made of its own pieces:
  a copy of the Option item (its render state and matrix, so the open
  animation, colours and fades apply) whose quads are glyphs cut from the
  same texture row of `mojips` / `mojigc` (O, n, i of "Option", e of "Game
  Start"; the l is the i's stem drawn up to the top of its dot), one row (the
  distance Game Start to Option, 50 units) below Option; the band's lower
  half one row lower; the arrows (the model's own, with their pulse) a row
  lower while the cursor is on Online, sliding linearly at the model's 2.4
  units a frame. While the menu is open the logo moves up 30, the menu block
  33 and the copyright down 16 units (eased over 15 frames, the press start
  screen keeps the original layout), so the longer band fits between them.
  The pieces are found by their texture and texture rectangle (measured on
  the USA data); if they are not found, nothing changes.
- *Screens over the title.* Choosing Online opens the screens centred on the
  4:3 picture in the menu's colours (the dark band with its light lines, a
  dimmed title behind), Segoe UI (Arial, else ImGui's scalable font) at sizes
  that follow the window. The first screen connects (the version gate, then
  the remembered login). The controller's cursor always shows there and
  starts on each screen's main button (A presses it at once, as in the
  game's menus). B, Esc or Back go back a screen and from the first
  one (welcome logged out, the room list logged in, the room inside one, or a
  closed gate) back to the title menu, its cursor still on Online; the game's
  pad then waits until every button is up, so the B that closed them does not
  cancel the menu. The recovery code screen needs "I saved it". Over the menu
  the title runs on (it waits for the player anyway); over anything else (a
  session that could not start returns to the offline title's start) it
  waits, so no attract movie starts. Nothing goes over the network before the
  player chooses Online or opens the F1 Online tab.
- *On-screen keyboard.* A text field the controller activates (A) opens a
  keyboard: digits, letters, ' - _ . and a symbols page; the D-pad moves, A
  types, X deletes, Y switches the capitals, B, START or Done close it (a
  password shows as stars). The keyboard's typing works in every field as
  always, and the mouse clicks the keys.
- *F1 Online tab.* The same screens; a direct `--host` / `--join` session
  shows its own state there. The View / Back button opens and closes the F1
  menu (unless a binding gives that button to the game), and the menu has
  keyboard and controller navigation while it is open.
- *Screens.* Welcome; log in (remember me: the server's token with DPAPI
  plus the account's name in `saves/online/account.txt`); register (name,
  password twice, optional e-mail; the recovery code shown once with Copy and
  "I saved it"); recover; account (e-mail, the online character's summary
  from `GET /online-character`: name, form, level, play time, story clears,
  and the difficulty its rooms may use); change password; log out here or
  everywhere; the room list (all open rooms: lock, name, owner and whether
  present, players, difficulty, randomizer; rooms of another protocol or game
  data greyed at the end with a tooltip; Join, Create, Refresh, and Rejoin /
  Close for the own room); create (name, optional password 4-64, 2-10
  players, difficulty up to the account's online clears: Hard after a Normal
  clear, Very Hard after a Hard clear, camera mode, light mode, fast loading,
  randomizer seed and modes); join with the password prompt (a wrong password
  stays on it); the question that ends an offline game; the room view.
- *Version gate.* `GET /status` first: maintenance (with the message and Try
  again), an unreachable server (Try again), `client.minimumProtocol` above
  the client's protocol or `client.minimumVersion` above `DW4_PC_VERSION`
  ("Update required"; also a 426 from any call), a release build without the
  certificate pin ("not set up yet": no request at all). A closed gate shows
  only its banner. A newer `currentProtocol` / `currentVersion` shows a hint.
  The CPU gate and the environment check (6.12) are checked at start-up and
  block create and join with their text.
- *Session start from the title.* Create or join asks first when an offline
  game would end (a game started, or a stage that is no menu); then the
  server's answer (ticket, settings, size) goes to the command line's room
  start, now one step for both (`open_room` in `game_main.cpp`): the replay
  rate calibration, the online card (the session never touches the player's
  own saves), the session over its own HTTP worker with the account's token,
  the relay, the title. A failed start returns to the offline title and
  leaves the room on the server. *Leave room* (with a question; also after the
  B + X + START prompt) and every session end (kick, room closed) return to
  the offline title with the player's own cards; the lobby shows the room
  list with the reason.
- *Room view.* The room (name, code, owner and whether away, its settings),
  this member's slot, instance, mode and delay, the online character it plays
  (or that a new one starts at the title's Game Start), Play (closes the
  screens over the title or the F1 menu), the members from the room
  state (`GET /rooms/{code}` every three seconds while shown) with each one's
  area, level and connection from the relay's ROSTER (`Session::roster()`,
  once a second; a hidden member in the title's hub instance shows "Title
  screen", a hidden one elsewhere "Loading or in a scene"), the instances
  ("together" for a sealed one), Kick for the owner, Leave.
- *Test runs.* `--online-ui FILE` (development builds against a loopback
  server only) presses the game's pad (`to-menu`, `pad DOWN DOWN A`: the
  title menu's Online entry as a player chooses it), drives the screens
  through the lobby's own calls (`src/game/online_ui_script.hpp`: log in,
  register, create, join, the controller's and the keyboard's navigation
  keys, form fields, screenshots); `--online-folder` keeps a test run's login
  apart. `tools/online_test/run.py ui` runs the scenarios of
  `tools/online_test/ui` (navigation: the menu's three entries and the
  keyboard; accounts, rooms with a full room, areas, offline, gate,
  unreachable; newchars, the S2 done-when: two accounts register in the
  screens, one creates a room, the other joins, both play new characters with
  the campaign bot to the save room, then leave) with hidden 1920x1009
  widescreen MSAA 4 windows and keeps the screenshots as PNG in its run
  folder. A UI scenario's `"characters"` logs in through the API afterwards
  and checks each client's online character: entered without one, revision 1
  from the first stage change, the save room's upload, the leaving save last,
  `GET /online-character` at that revision and equal to the card cache
  outside the story bytes (a guest's story bytes empty, the owner's with the
  room's phase); `"checkpoints"` names the campaign checkpoints its campaign
  clients save at.

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
  Members elsewhere in the room do not get them. A borrowed story never gives
  an achievement; online, `numemon_freed` counts the Numemon this account
  freed in its own live steps (round 61 design, 8.8).
- **Story challenges.** `story_clear` and its challenges (speed_fast under
  4 h, speed_normal, rookie_clear, deathless_clear) read slot 0's character and
  its own play time today (`achievements.cpp:325-338`). Online they go only to
  the room owner's machine (the story is the owner's), computed from the
  owner's online character; a guest who happens to be slot 0 when the last
  boss falls gets the plain "was there" credit, never "under 4 hours".
- Offline unlocks stay local in the F1 Achievements tab (r48 default).

**Built in round S2 (achievements-deploy lane).**

- *One catalogue.* Every client achievement has its online form in
  `src/game/achievements.cpp` (`AchievementOnline`: kind, the server's goal in its
  units, the server counter of a stat, owner-only), and `dw4_achievements_tests
  --export` writes the catalogue as `data/online/achievements.json` (`format`
  `dw4-achievements/1`; per achievement `id`, `title`, `description`, `category`
  as the server's key, `kind` event / stat / state, `goal`, `stat`, `ownerOnly`).
  The ctest `achievements` compares the file with the table, the server's
  `AchievementRulesTests` compare it with migration
  `server/DW4Online/sql/0009_achievement_catalogue.sql` and the rules with 0009
  (0008 in the lane, renumbered at the integration: 0008 is the story lane's
  `online_story`), and an integration test compares it with the migrated
  database. 0009 keeps the
  ids of the 52 shared codes (their text, kind, goal and counter become the
  client's), adds the client's other 20 as ids 73-92 and retires the 20 server-only
  codes of 0002 (`active = 0`, never reused); tiers, points and the hidden flag
  stay the server's. Kinds: 42 events (story, bosses, challenges, Digivolution!,
  techniques, Lucky Draw, Numemon Liberator, quests, Ouch, Second Chance,
  together), 10 stats (`enemies_defeated`, `critical_hits`, `numemon_defeated`,
  `play_seconds` 3,600 / 36,000 / 360,000 / 1,800,000), 20 states (levels, forms,
  cards, bits, devices; the server's rules now follow the client: eight and all
  forms, Form Master = 16 forms at level 99, Bit Tycoon in the wallet, devices in
  the folder). Speed goals 8 h / 4 h (`speed_normal` / `speed_fast`, seconds of
  in-game time), Underdog level 12.
- *Online sync* (`src/game/online_achievements.hpp`, `online/achievement_api.hpp`):
  a second `AchievementTracker` with a profile per account
  (`saves/online/<account>/achievements.dat`; test runs `DW4_ONLINE_PROFILE`)
  counts this machine's player with the local rules. The achievement step of a
  room (the local tracker's too) marks only the own slot as local
  (`AchievementStep::own_slot`), takes the own slot's cards, and is
  `shared_story`: the story challenges need `story_owner`, a Numemon freed counts
  only when its flag rises in a live step here, a card only in slot 0 (the shops
  are solo hub stages). Event unlocks go out at once with the ranked context
  (`roomCode`, `players` = the own stage instance, `roomPlayers` = the room,
  the room's randomizer, difficulty, protocol, client version; never cheats,
  rebalancing or save states) and evidence (`igtMs` = the owner's character's
  live fields for speed goals and its clears, `level` for Underdog, `stage`), at
  most four in flight; the six counters (`kills`, `criticals`, `numemon_kills`,
  `bosses`, `defeats`, `play_fields` as seconds) as deltas every 60 s against the
  profile's `sent.<key>`; `GET /rooms/{code}` every 30 s for the room's members
  and `you.owner`. An unanswered report (no answer, 401, 408, 429, 5xx) is tried
  again (10 s, doubling, at most 5 min), a refused one dropped and logged; every
  leave (`leave_room()` in `game_main.cpp`: the room view's and F1's *Leave
  room*, the B + X + START prompt's, the end of the game) sends the last
  counters and open unlocks
  (two seconds at most), a closed session keeps them in `<profile>.pending` for
  the next room. The online tracker sees only steps the session runs as the
  member's own live ticks (solo, sealed, transition), never a catch-up
  (`Engine::replay`) or an observer's tick before its JOIN.
- *Server.* `PlayContext.roomPlayers` (1-10, capped at the room's members like
  `players`): Room Host (owner, three or more in the room) and Raid Night (two
  or more) read it, the couch goals read the instance's `players`; the story
  challenges are refused from anyone but the room owner
  (`AchievementRules.OwnerOnlyCodes`), the plain clears count for everybody who
  was there.
- *Checked:* ctest `achievements` and `online_achievements` (a fake request
  queue: bodies, context, events only, the own slot, the owner, batches,
  retries, refusals, the pending file); server unit 133, integration and
  end-to-end 70 + 1 skipped; scenario A1 on real hidden clients (section 16).

## 12. Name tags and presence

- Members of the own instance are engine players: the existing overhead labels
  draw them from GameSystem (name converted from the full-width PLAYERNAME,
  level, HP and MP bars), projected with the local camera. A defeated member's
  capsule gets a name marker (4.1).
- Room members in the same stage but another instance (hub stages, a fifth
  player in a parallel instance, a member on another quest) come from PRESENCE
  packets through `OverheadLabels::set_remote_players` (exists): interpolated
  between packets, hidden by the same rules (loads, demos, menus, a defeated
  player). They are presentation only and never touch the scene graph. In a
  hub stage the tag stands above the member's ghost figure (12.1).
- A lagging member's tag shows a connection icon.
- Notices (presentation, a few seconds at the top): "<name> joined", "<name>
  left for <stage>". A member in its own status window or talk (5.3) shows
  "<name> is in the menu" / "<name> is talking" over its name tag (round 63).

### 12.1 Ghost figures (round 60)

In a hub stage (3.3: every member has its own solo instance there) the other
room members in the same stage appear as their Digimon
(`src/game/ghost_figures.hpp`, `GhostFigures`): the form they play now, with
the weapon they hold, at their place and facing, playing the motion clip they
play (their wait, their run, a jump), with their name tag above, fading in
over 0.35 s when they arrive and out where they were when they leave (another
stage, a load, a scene, the room). Look only: no collision, no hits, no talks,
no effect on anything.

**Never in the simulation.** The PC host makes and draws the figures:

- Each form's package (`_common/p_<form>.igb`, the one the engine loads for a
  player, `Engine::online_join_player`) and each weapon package
  (`_common/<weapon>.igb`; only a weapon name, `w_` and up to 22 more of
  A-Z a-z 0-9 _, and at most 16 of them kept: the one asked for longest ago
  goes for a new one, never one a figure holds) is read and parsed on a
  worker thread from a *copy*
  of the resources (never the engine's object, which a catch-up replaces);
  their nodes, animation database and geometry live in the figures' own
  `SceneCache`, never the engine's. A form's model is the record 0 instance
  of its beAction2Info (the beModelCtrlNode2 with its igLod: Kage, High, Low);
  its wait and run clips come from the control info's SQ_WAIT / SQ_WALK (the
  first ACTION of the first weapon label, that action's last MOTIONSET:
  `waitD`, `runD`). The weapon's parts hang under the form's igActor nodes on
  their bones for the figure's traversal only (as `WeaponManager` GENERATE
  does), so one form's nodes serve all its figures.
- Per presented frame the figure's pose comes from the engine's own combiner
  code (`MotionState`, `evaluate_pose`) on its own clock (a new clip fades in
  over MOTIONSET's 8 frames from the pose before), and the engine's traversal
  turns the private nodes into draw items (skinning palettes, the igLod by the
  real eye distance). They go into a *copy* of the picture's draw list after
  the simulation's picture was taken and smooth motion blended it: opaque
  figures after the last opaque item of the 3D scene, fading ones after its
  last blended item (`ghost_insert_points`), so the 2D layers stay on top; the
  lights and fog are those of the scene's first lit 3D item (every model hangs
  below the stage's root light set). The Kage level joins a copy of the shadow
  frame's casters, so a figure casts its shadow inside the stage shadow's box
  as the players' proxies do.
- Nothing of this is in a snapshot, fingerprint, replay or the lockstep path;
  smooth motion never sees the figures (they are drawn at the present's own
  time, so they move smoothly at 60 and 144 Hz alike); `DW4_GHOSTS=0` turns
  them off (test runs). The sender reads its facts from the engine
  (`GameEngineLink::presence`: the action's current clip and loop mode, the
  POWERSOCKET weapon, whether the action switch shows the model) and writes
  nothing.
- On the own screen the figures go where the name tags go: none while the
  own game loads, plays a scene or a demo, or shows a menu stage (they fade
  out over 0.35 s, a stage load drops them at once). A status, shop or talk
  window of the own player leaves them standing behind it like the stage's
  other models (only the tags hide), so opening a menu never makes them
  vanish and come back.

**At most nine** figures (a room has ten members), the nearest to the own
player. A form whose package is still loading shows once it is ready (it fades
in then); the packages of members anywhere in the room are loaded ahead, so a
member who comes back from a field is usually ready. Measured (P1, three hidden
clients, 13 forms loaded one after another): 0.5-5 ms of reading and about
1 ms of parsing per package on the worker, the figures' own work at most
0.3 ms a pass on the main thread, no longer present when a form appeared (the
longest presents, 60-95 ms, were the screenshots' read-backs).

**Camera.** A figure turns semi-transparent (to 35 %, eased over 0.2 s) when it
stands in the own player (every member enters its own lobby at the same entry
point, so a newcomer stands inside the others' figures), between the camera
and the own player, or within 60 units of the camera. A fading figure draws its
nearest surface's depth first and then its colour where that depth is, so it
never shows its inside or its far side through the near one. In the lobby
rooms the camera stays high above the floor, so a figure blocks the own player
only when it stands right in front of it, which the own-player rule covers.

**Wire (PRESENCE2, 6.3 and the appendix).** PRESENCE stays as it was; round 60
adds PRESENCE2 (client 0x14, server 0x90; first 0x8F, which round 62's
NETSTAT holds in the field: moved in the fix round): a version byte (1), the PRESENCE
fields, then the motion clip (0-16 of A-Z a-z 0-9 _; empty: unknown), flags
(1 the clip loops, 2 busy: a status or shop window or a talk is open, the
figure stands and the tag hides), a warp counter (the sender counts its stage
loads and every step that moved its figure more than 24 units, far beyond a
run's 0.7: receivers never glide across a change), the sender's clock in
milliseconds (wrapping) and the weapon package (0-24 characters). The clock is
the time of the step whose place the packet carries, on a steady field grid
(`Session::track_figure`: one field after the step before, at most 50 ms
behind the wall clock; after 100 ms without a step the poll time), not the
time of the poll that sends it: a sender at 144 Hz polls between its 60 Hz
steps, and its poll times put the places up to 13 % off their spacing. HP and
MP above their maximum (a script's `MDLaddHp` raises them above it) go as the
maximum. In PRESENCE2
the stage is empty only while the figure is really gone (a load, a scene, a
menu stage, the action switch hiding the model); a window keeps the stage and
sets busy. A member sends it ten times per second while its figure moves,
turns or changes its clip or flags and five times per second while nothing
changes (about 70 bytes each: a full room in one lobby relays about 6 KB/s to
each member).

**Leaving.** A member that leaves its stage shows nowhere from that moment
until its next stage load: a departure (the stage announces its NEXTSTAGE,
HEADING: the lobby gate, a mat's scene and fade) or a barrier sends an empty
stage at once and keeps the stage empty until the load serial moves on (at
most 10 s), and a load or title screen that begins without either sends one
empty stage (`Session::depart`, `hide_presence`). The hub forwards that empty
stage to the members still in the stage it left, whose figure fades out
there. Before (the round 60 review), the figure faded out on the mat with the
mat's scene, the end of the scene showed it on the mat again on its own
machine until NEXTSTAGE parked it, and that last packet stood on the others'
screens for the two seconds a silent track stays (scenario P3, 16).

**Versions fail safe.** A client sends PRESENCE2 only after a WELCOME with the
presence-v2 flag (2); an older hub never sets it (and would close a connection
on an unknown frame), so a new client on an old server sends PRESENCE and shows
the others' figures from PRESENCE alone (no clip or weapon: a figure runs or
stands by the speed of its packets). An older client ignores the flag bit and
never gets a 0x90 frame: the hub sends PRESENCE2 only to members that send
PRESENCE2 themselves (`HubMember.PresenceV2`) and PRESENCE, the same subset,
to the others, so old and new clients share a room (the protocol number stays
45: nothing of the lockstep changed). A later version keeps the layout and
appends: the hub reads and checks its version 1 part and forwards only that,
written at version 1 with the flags version 1 knows (counted as
`presence_version`), so it never passes on what it cannot check and the
member's figure still shows; a client takes what it knows. Every version stays
within 256 bytes (version 1 takes at most 105); the hub drops a larger
PRESENCE2 unread (counted as `presence_oversize`). Scenario P2 (16)
plays a new and an old member in one lobby (`DW4_ONLINE_TEST_PRESENCE1`
makes a development client speak PRESENCE only, as before round 60 or
against an older hub).

**The hub's bounds** (`RoomHub.OnPresence2`, `PresenceMsg.Implausible`,
`Presence2Msg`): a malformed frame (lengths, a clip or weapon outside its
characters, a non-finite position) closes the connection like any malformed
frame; an implausible one is dropped and counted while the connection stays:
a form above 15, a coordinate beyond 100,000, a yaw beyond two turns, a level
above 999, an unknown flag. HP and MP above a known maximum go on as the
maximum (the engine allows them above it, so dropping the frame would hide a
member's tag and figure). PRESENCE of an older client is held to the same
bounds (it drives a ghost figure too). The rate is a token bucket per member
(12 a second, 3 at once; more is dropped and counted); PRESENCE keeps its
150 ms rule. The
hub also sends a member's presence to the members still in the stage it just
left (another stage, or hidden while it loads), so its tag and figure go at
once instead of after the two seconds a silent track takes.

**Interpolation (`PresenceTracks`).** PRESENCE2 packets keep the sender's clock
and are drawn 0.16 s behind the newest (one 100 ms interval and room for
jitter), between the two packets around that time, the yaw the short way
round; past the newest packet a member goes on along its last movement for at
most 60 ms, then holds. The offset to the receiver's clock aims at the
smallest arrival minus sent of the last 20 packets (the one the network held
up least, so a late packet does not shift the others; a slower route or clock
drift moves it up as the window moves on) and the drawn offset moves towards
it by at most 2 % of the time that passes: the drawn time never jumps, it runs
at most 2 % faster or slower. Round 60 first kept the smallest offset seen and
raised it by 2 ms per packet: the drawn time ran 2 % slow and jumped forward
at every new smallest offset, a saw the review measured as an uneven walk
(8.5 % standard deviation of the per-picture step, single pictures 1.85x the
even step on loopback, 3.65x with 30 +- 20 ms of jitter). Measured now with
the same harness (`presence2_smooth`): at most 3 % off the even step and a
standard deviation of at most 0.9 % for senders and pictures at 60 or 144 Hz,
on loopback and with 30 + 40 ms of jitter. A change of the warp counter, a step faster than 900
units per second or longer than 120 units, a new stage or instance and a
restarted sender clock start a new segment: the old figure fades out where it
was and a new one fades in at the new place (no glide). The remote name tags
take the same sample, so a tag never trails its figure. PRESENCE packets keep
their old rule (arrival times, 0.2 s behind).

**Tests.** `dw4_ghosts_tests` (placement, list order, camera fade; with the
game data a form loaded in the background, faded in, posed, its shadow proxy,
a jump and a form change faded over, the cap of nine; weapons: no package
that is not a weapon, the sword drawn, a flood of 200 names kept to 16
packages without dropping the held one), `online_wire_tests`
(PRESENCE2 round trip, golden bytes shared with the relay's `WireGoldenTests`,
versions, malformed frames), `online_session_tests` (the sender's clock and
jitter, warps, extrapolation, busy, yaw; PRESENCE2 only after the WELCOME flag,
ten or five a second; `presence2_smooth`: an even walk for senders and
pictures at 30, 60 and 144 Hz with jitter and a route that gets faster or
slower; `presence2_walk`: the step clock over the fake hub; `presence2_departure`:
hidden from a departure to the next stage, gone at once at a barrier, HP above
the maximum), the relay's `HubPresenceTests` (the WELCOME flag,
PRESENCE2 to PRESENCE2 members and PRESENCE to the others, the bounds, HP
above the maximum clamped, the 256-byte bound, PRESENCE by the same bounds, the
token bucket, leaving a stage, a later version's version 1 part), and the
real-client scenarios P1, P2 and P3 (16).

## 13. Discord

The existing IPC client gets party fields in a room: `party {id: "dw4-" + a hash
of the room code, size [n, max]}` and the state `Online room (n/max)`, never the
room name. Details stay `<Form> Lv <level>` and the area. The party size counts
the room (up to ten), not the instance. No invites or join secrets for now.

*Built in round S2 (r56):* `discord::party_id` (`"dw4-"` and 16 hex digits of
FNV-1a 64 over the room code) and the size from the relay's ROSTER (members
in the room) and the room's maximum. Discord itself writes "(n of max)" after
the state, so the state stays the area in a stage (and "Online room" in the
menus) instead of repeating the count. No join secret: it would need
Discord's join events, and a room with a password could not be joined from it
anyway; joining goes through the room list.

## 14. Server changes

| Area | Change |
| --- | --- |
| Relay | `DW4Online.Relay`: DW4R2 hub replacing the splice: one connection per member, frames, instance groups (solo-led and sealed), server-paced sealing at 59.94 Hz on a dedicated timer thread with immediate fill, delay suggestions from the arrival margins, event stamping, snapshot and log store with canonical hashes and fork lists (6.8), state delivery for join / co-arrival / resync / resume with its own rate limit, fingerprint vote, presence fan-out (round 60: PRESENCE2 with its bounds and token bucket, the stage a member left, 12.1), pings, the 64 KB/s bucket for game frames |
| Rooms | `RoomService`: instances as lockstep groups (id, stage, visit, context hash, slots, parent and fork frame, departing, boss lock, joinable), barrier decisions DISSOLVE / co-arrival / JOIN / NEW with holds and the boss choice (3.4), catch-up estimates from the members' replay rates, forks of both kinds, in-band JOIN and LEAVE, preferred slots, no instance end when its first member leaves (`EndInstanceLocked` goes), room-scoped relay tickets, the settings hash, the relay connection as heartbeat. `POST /rooms/{code}/stage` and `/stage/leave` are retired; the room state lists instances with stage and members. Room settings gain camera mode and light mode |
| Story | migration with `online_story`; `StoryMerge` in the domain with the golden vectors; plausibility table from the census; pending sets with STORY_ACK; the owner-away setting with the session's dropped set; `GET /online-character` composes, `PUT` ignores story bytes; `GET /rooms/{code}/story` for the website and debugging |
| Character | (round 64 design, 9.1) four boxes per account; `online_slots`, `online_slot_stats`, `online_forms`, `online_devices`, `online_disks`, `online_cards`, `online_rewards` by box, the persistent `/play` binding, the history with its changes and the client's record, refused uploads, edit drafts, the relay's visits by sequence and box, the breaker's state; `GET /online-characters` composes, `PUT /online-characters/{box}` (ETag of generation and revision; a staff revision in between is merged, never 409) validates by the rule table (refuse / flag / off per code, refusing only what the upload changed, against the new-character record for a new box), `POST .../{box}/play`, a reset per box; staff edits on the website (with a preview) and in HeidiSQL (after a quiet time, not applied edits kept as drafts) with completions, the merge, *Reload now* and audit rows; box locks for every writer; the migrator's data step converts today's blob rows inside the started mark |
| Database | (round 64 design, 9.1.3) hashes stored as hex and addresses as text in their tables, record and file blobs in side tables, virtual columns for the story's phase, keys and counters and the migrations' checksums, the `v_` views; the `game_*` tables (with `game_enemy` and `game_stage_enemies`) imported from the deploy-time export |
| Achievements | catalogue migration to the client's ids and thresholds (built in S2: migration 0009 (0008 is `online_story`), `data/online/achievements.json`, `roomPlayers`, owner-only story challenges; 11) |
| Config | `Sealing:MaxDelayTicks` (15, measured in S1, 6.4), `Sealing:LateTarget` (0.02 since the review of round 62, 0.001 in round 62: the share of late pads in the delay rule's ten-second window that counts, with three different seconds), `Relay:ReconnectSeconds` (10), `Relay:LagLeaveSeconds` (10), `Relay:StateBytesPerSecond`, `Instances:CoArrivalHoldMs` (3000), `Instances:JoinHoldSeconds` (30, measured in S1, 3.4), `Instances:MaxCatchUpSeconds` (30), the flood and recovery limits of the S1 reviews (`Relay:MaxQueuedBytes` 4 MiB, `Relay:MaxQueuedStateBytes` 24 MiB, `Relay:StateBytesPerMinute` 64 MiB, `Relay:MaxConcurrentStateBuilds` 2, `Instances:LoneBarrierMs` 1000, `Instances:MaxAheadSeconds` 15, `Instances:ResyncLimit` 3 in `Instances:ResyncWindowMinutes` 10, 6.6-6.10), `Rooms:StoryWhileOwnerAway` (drop, built in S2), `Instances:StoryBurst` (10) and `Instances:StoryMessagesPerMinute` (60, S2); round 64 design: `OnlineCharacter:Rules:Actions:<code>` (9.1.6), `OnlineCharacter:Rules:BreakerConsecutive` (2) and `BreakerGlobal` (false), `OnlineCharacter:LeaveRoomOnRefusal` (true), `OnlineCharacter:VisitPendingSeconds` (90), `OnlineCharacter:DropPlusMargin` (10), `OnlineCharacter:EditQuietSeconds` (120), `OnlineCharacter:RequireGameData` (false), `OnlineCharacter:RejectedUploadDays` and `VisitDays` (30), `GameData:Directory`; `Play:CurrentProtocol` follows the client. `Sealing:StallMs` and `Instances:MaxJoinLogMinutes` of revision 1 are gone |
| Website | the room page shows instances per stage (built in S2: `/rooms` lists each room's instances by stage with the client's stage names from `data/online/areas.json`, "2 together" / "alone" / boss fight / leaving, the hub stages last; names only for signed-in members of the room); round 64 design (9.1.12): player profiles with the four boxes, every form with its picture, stats and gear in the game's colours, the current Digimon as the profile picture, pictures under `/art/` from the export, the admin character editor |
| Deploy | fixed-key self-signed certificate and the pin script (10); port 52041 as before (built in S2: `tls-key.ps1`, `print-pins.ps1`, the pin build step, DEPLOY.md 4, 6.1, 6.4); round 64 design (9.1.11): `--export-web` on the user's PC, the folder copied to `GameData:Directory`, `CREATE VIEW, SHOW VIEW, DROP` on `dw4online.*` for the restricted database user (checked by the migrator before it starts), the HeidiSQL guide starting at the `v_` views |
| Tests | unit tests for sealing schedules and fills, events, forks, barrier decisions (holds, co-arrival, context key) and merge vectors; integration tests with scripted fake clients; end-to-end tests with the real game client (16) |

## 15. Online off stays bit-identical

- `OnlineRules` default off; offline games, save states, story runs, the
  campaign tools and the direct `--host` / `--join` sessions never turn it on.
- The camera mask 0 is today's camera and traversal code, unchanged.
- The Lua overrides and wrappers are registered only online.
- Draw filters (item label lines, exit prompts, the members' own windows and
  scenes) act only online. A member's own window or scene (5.3) exists only
  online outside the hub stages: offline no scope is ever entered, so the
  messenger, the window programs, the text lines, the demo counts and the
  sound output run the original code.
- New port events are observe-only (not in fingerprint or snapshot).
- The title menu's third entry exists only while the host sets
  `Engine::title_online_entry` (offline with the online screens); test runs,
  replays and online sessions never set it, and the test inputs never reach
  it (none presses down at Option).
- The net fingerprint is a separate function; `Engine::fingerprint` stays.
- No FMA3 switch; the libm routing of round S4 selects the MSL routines only in
  online sessions. Input quantization, the console reset switch and the
  environment check act only online.
- The MXCSR guard runs offline too and is a no-op with the default MXCSR.
- Round 65 design (6.13): the tick state, checkpoints, the state arena and
  prediction exist only in online sessions. Offline no arena is created and
  the game's replaced `operator new` forwards to `malloc` as MSVC's own does;
  the topology journal is an append-only list nothing offline reads; TEXTCTL
  keeping its text changes no geometry; `Engine::state_safe` and the tick
  state hash are separate functions like the net fingerprint. Revision 2 adds
  three changes that run offline too and change no value: the growing statics
  allocate through `HostAlloc` (the same CRT heap), the sound banks and IGB
  documents live in host stores that free an entry once neither the engine
  nor the audio side uses it (offline at the stage change that drops it, as
  the last `shared_ptr` does today; only memory, no value depends on the
  moment), and the draw list carries palettes as plain pointers;
  `Engine::set_resimulating` exists only online. Rounds J and R prove it with
  the commands below plus the save state suite.
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
| S8 | status window of one member while the other fights far away and near: the member's own window, nothing pauses (round 63: M1) | r63 |
| S9 | pit: one member falls and respawns alone, no fade on the other screen | S3 |
| S10 | disconnect and reconnect within ten seconds; and after the timeout | S4 |
| S11 | injected desync (test hook) and resync; a resync beyond the limit continues alone | S1 (S11a, S11b: `DW4_ONLINE_TEST_DESYNC` on the shorter / the longer member, resync, equal after), S4 |
| S12 | latency, jitter and loss proxy matrix | S1 (measure), S4 (tune) |
| S13 | room of ten: instances 4 + 4 + 2 plus solo members, 30 minutes | S4 |
| S14 | owner-away policies drop (removal at the barrier, notice) / hold / apply | S2 |
| S14p | owner present: a guest's new story value reaches the owner's `online_story` within ten seconds (`scenario.json` "story_saved") and the owner's own instance only at its next barrier | S2 |
| S15 | save room upload, 409 from a second machine (`character_test.py` C4; C2 the same after a stage change's upload) | S2 |
| S16 | quest and story in stag0401: a quest-1 member arrives at point 3 while a story member plays there: separate instances, both keep their state; two members on quest 1 share one | S1 |
| S17 | two members take the area gate 2 s apart (co-arrival, one entry scene) and 8 s apart (hold, then join) | S1 |
| S18 | exit mat: A answers *No* and stays, B steps off, dies or despawns on the mat: no prompt; A answers *Yes* while B stands on the mat: B is asked; B *No*: only A leaves; S18b: B leaves the room with its own prompt open: it closes as *No*, A leaves by the mat | S1 |
| S19 | dead partner: A dead, B (the only living member) takes an exit or the gate disc: A goes with B, no game over; A gives up: A alone to the lobby | S1 (exit, gate disc), S3 (story trigger) |
| S20 | dng103f2 dwbutton2 with a member who just arrived (15-frame window as in local co-op); a puzzle mat shows *Waiting for* | S3 |
| S21 | B+X+START held for 4 s on slot 1: no reboot anywhere, *Leave room?* on slot 1's machine | S1 |
| S22 | a joiner whose target leaves the stage during the catch-up (fallback NEW) or forks (follows the instance id) | S1 |
| S23 | boss-locked target: *Wait for them* (joins after the boss falls) and *Go alone* | S3 |
| S24 | map recorder read while the partner moves: the reader's own scene, the floor map on its screen only (round 63: M2 in dng101b1; dng102b3 with the offline two-member harness) | r63 |
| M1 | own menus (5.3): a alone opens its status window among the enemies and is hit; b joins while it is open, comes over and fights the goburi that go on hitting a; a's menu while b fights near it; both menus at once; a's menu while b walks far away; a talks with the field recorder while b moves; screenshots from both machines (`"shots": true`, the scripts' `shot NAME`) | r63 |
| M2 | own scenes (5.3): a and b enter dng101b1 together (both wait on matz00 for each other); b opens its menu among the e_nume while a fights beside it and is hit in it (its window's HP line follows); a reads the map recorder (talk window and floor map) while b walks about; screenshots from both machines | r63 |
| S25 | Trailmon ride (dng412b3) with one member not aboard: no fork, the rider moves alone | S3 |
| A1 | online achievements: S5's run with achievements on; each machine reports its own player (online_join in its solo lobby, first_defeat in the sealed field) with the ranked context, the counters at the end; the server holds them (`scenario.json` "achievements") | S2 |
| P1 | ghost figures in the lobby: three members (k7, k10, k2); one stands, one walks around it and stops right in front of it, the third shows another form every 6 s (`DW4_ONLINE_TEST_FORM`, the form PRESENCE2 names; the engine's stays), leaves through the lobby gate for a field (stag0101), falls there and comes back after the game over; `"shots": true` takes the scripts' `shot` pictures (hidden 1920x1009, widescreen, MSAA 4, all tags) and the `ghosts` condition waits on the figures a client shows | 60 |
| P2 | ghost figures with an older client: two members (k7, k10), b speaks PRESENCE only (`DW4_ONLINE_TEST_PRESENCE1`, as a client before round 60 or one of an older hub); each sees the other's figure, without weapon and with run or wait by the speed of the packets (the hub turns a's PRESENCE2 into PRESENCE for b); b walks around a and stops right in front of it; pictures from both | 60 |
| P3 | ghost figures, a member leaving over a mat: a (k7) stands by the lobby's mat matz00 and takes a picture every 45 frames while b (k10) walks over it into stagml12; a's trace every 10 frames: b's figure fades out on the mat and never shows again (no later segment of b, never two figures), b's trace has its departure | 60 fix |
| D1 | a new account: Create Digimon (its `/play` with the new-character record), the first stage change: box 1's rows in the tables (SQL), `v_characters` and `v_character_devices` readable, the profile shows the box with its face picture | D |
| D2 | four boxes: characters made in boxes 1 and 2 in two sessions, the character select shows both; the save room's copy of box 1 into box 3 is not uploaded (notice) and gone at the next entry; the profile picture follows the bound box; a forged upload for box 3 is `wrong_box`; `/play` for box 2 inside a stage is refused, at the title after "Continue? No" accepted | D |
| D3 | a staff edit while the player is offline (an `UPDATE` of BITS and of a form's level, as HeidiSQL sends it, row by row): processed after the quiet time, the next entry's engine has them (trace), an `sql_edit` revision with the completed EXP, an audit row `character_edit` | D |
| D4 | a staff edit while the player plays (website, then SQL, then *Restore*): every later upload of his is accepted and merged (no 409, no stop of his uploads): the staff values stay, his progress since is added (BITS removed by staff and a purchase of his both count), the notice comes once, the values are in his game after the next entry; *Reload now* takes him out of the room. Counter-proof on main: the first upload after the edit meets 409 and the uploads stop | D |
| D5 | a forged upload (a `DW4_ONLINE_DEV` switch: EXP beyond its level's band, a chip with +N, a form unlocked in a field, a +2047 drop at level 20, a million EXP in a minute): 422, the refused upload stored, the member leaves the room with the notice, the next entry loads the server's character; staff *Accept* makes it the next revision. Counter-proof on main: main stores the forged record | D |
| D6 | a flagged upload (rate limits set low in the scenario's server settings): kept and flagged, the profile shows *under review*; *Reviewed: fine* clears it | D |
| D7 | round 61's G2 (a guest on the owner's story): the guest's tables get its personal progress, its story bytes stay its own, the owner's tables and story are untouched by the guest's uploads; a quest reward the guest takes goes to its bound box's `online_rewards`, the owner can still take it | D |
| D8 | migration: a database of main's schema with characters (`character_test.py` data) migrated as the restricted user with round D's grants: every record composed byte-exact from the tables, the history moved, the rules as `migrated:` flags, logins and sessions still valid, the client enters with the same record and its next save is accepted | D |
| D9 | two members in one instance with characters loaded from the tables (S2's run): equal fingerprints, each carry equal to its composed record's values | D |
| D10 | the website with the export (art and data) and without it: every picture of the manifest shown (Alphamon from `alpha8.png`); placeholders and `weapon #n` names without, the admin banner and `/health` *degraded*; no broken image, no inline style | D |
| D11 | a forged first upload of a new box (a `DW4_ONLINE_DEV` switch: level 999, every form, 9,999,999 BITS, purple gear, consistent in itself): refused against the new-character record; a forged `/play` record is `new_template`. Counter-proof on main: accepted | D |
| D12 | the server restarts while two members play (the service stopped and started): their bindings survive, the leave and the next stage change saves are accepted, nobody is sent out with `wrong_box`; a binding deleted by hand gives `play_required`, the client binds again and saves | D |
| D13 | visit order: the server answers the first save after a lobby visit with 429 (a scenario switch), the member walks lobby -> field -> lobby meanwhile and changes the bank and its form; the delayed and the newer saves are accepted (no `lobby_only`); a save sent before its barrier's visit closed gets `visit_pending` and passes on its retry | D |
| D14 | box switching: box 1 plays the field and the Digi-Lab, the member goes to the title, binds box 2 and uploads box 2 with new EXP and a new form: refused (box 1's visits do not count for box 2) | D |
| D15 | grandfathering and the breaker: a migrated box with a violation the old checks let through (`exp_band` on a form) plays three stage changes: accepted with `grandfathered:exp_band`; a rule set to misfire (a scenario setting) refuses one box twice, then flags it and alerts | D |
| D16 | HeidiSQL while the player plays: a half edit (the current form set before its `reached`) is not processed before the quiet time; an invalid edit (a disk twice) is reverted into a draft at the next upload, the upload is accepted (no 423); an `INSERT` of a device during an upload is neither lost nor taken for the server's own row; the Cheats tab is disabled in the room | D |

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
  room opens; since the title applies the room story at game start, every
  card scenario has it, or its members would start from the owner's empty
  online story; S16's story member carries the quest members' flags 89 / 714
  for the same reason), `"server": {...}` gives the scenario its own server with those
  settings and its own database (S14h / S14a: `Rooms:StoryWhileOwnerAway`),
  `"min_shared_ticks"` lowers the shared ticks below (S14: one member plays),
  `"story_saved": {"client", "after", "flag", "seconds"}` times the room
  owner's `online_story` row in the database from that client's log line
  (S14p: the guest's "505 set" step, ten seconds at most). The condition
  `roomflag:N` reads the room story the client holds as confirmed (S14p: the
  owner's machine has the guest's value there while its engine does not).
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

**Round G (guests on the host's story, 8.8).** The room story of a card
scenario must look like one real merges leave: `seed_story` writes only the
merge sets of the owner's card (the phase one-hot, the persistent flags,
K0-K10, COUNTER 1 and 7) and derives the room difficulty's copies, as the
keeper does (today it copies all 1024 flag bytes, 503 and the temporaries
included, which hid the tutorial bug). Scenarios on hidden real clients
(the owner `o`, the guest `g`; each with a counter-proof on the build
before round G where it applies):

| Id | Scenario |
| --- | --- |
| G1 | owner k2 (phase 2 after the training, 503 = 1, K0-K2), guest a new account (Create Digimon): the guest's lobby gate leads to stag0101 (`!stage:stag0001`); on main it leads to stag0001 |
| G2 | owner k10 (phase 6, K0-K10), guest the k2 card with its own `online_story` seeded from it (phase 2, K0-K2): its Game Start gives it phase 6 and K0-K10 (`flag`, `folder` checks), the lobby gate offers stag0401 and the guest enters it, the K9 / K10 entrance of stag0401 is open (905) |
| G3 | G2's guest saves in the save room, changes stage and leaves: its own `online_story` row keeps its seeded revision, the stored record's story bytes are its own (database), every upload body carries its own story bytes (`DW4_ONLINE_TRACE`); bytes 792..1891 of every used box of `online_card.dat` equal its own story right after the save room's write (still in the room) and right after leaving, and so do those of every file in `backups/` (revision 2: on the first draft's code the card holds the owner's story after leaving) |
| G4 | after G2 the guest leaves: its local `savedata.dat` is byte-identical to before the session (sha256); an import of `online_card.dat` into a scratch save store (`DW4_MENU_IMPORT`, the menu's import path) gives a box with its own story (phase 2, K0-K2), not the owner's; then it opens its own room and its game starts on its own story (phase 2, K0-K2) |
| G5 | the owner takes a new value while the guest stands in the lobby (S14p's 505): the guest's engine lacks it until its next barrier (save room and back), then holds it; the "moved on" notice comes once |
| G6 | retry in the training: an owner in phase 2 without 503 enters stag0001 (503 set and merged), answers *Retry*: the barrier into stag0001 leaves 503 at 0, the intro talk plays again, 503 is 1 after it |
| G7 | a dev-build test switch makes the client ignore the room story for its first 10 s in the room, and the title's Game Start comes before: it waits with "Loading the room story...", then starts with the room story; no STORY from that member before |
| G8 | a crash after a stage-change save (the client killed before the answer, C3's way): the next entry uploads the cached save with the guest's own story bytes and puts it on the card with them |
| G9 | owner and guest in one stag0101 instance over three barriers: equal fingerprints (the usual check), the same story on both engines, 503 = 1 on both |
| G10 | a guest AHEAD of the owner (revision 2): owner k2 (phase 2, K0-K2, 503 = 1), guest the k10 card with its own `online_story` seeded from it: its Game Start gives exactly phase 2 and K0-K2 (`flag` / `folder` checks: K3-K10 = 0, phase flags 3-6 = 0, areas 2-4's story flags 0), the lobby gate offers only stag0101 and the guest enters it; no STORY of the guest carries K3-K10 or a phase above 2 (`DW4_ONLINE_TRACE`); the owner's `online_story` revision stays the same over three barriers (lobby, stag0101, lobby, save room, lobby); after leaving, `GET /online-character` of the guest still has its k10 story and its `savedata.dat` is unchanged |
| G11 | the call shop (revision 2): G10's guest talks to the lobby Digi-Elf and chooses the call shop: the hint comes, no stage change to stagIS13, its engine keeps phase 2 and K0-K2 and the room's difficulty; it stays in the lobby, goes to the save room and back twice more than 60 s apart: the owner's revision does not change. Counter-proof on main: the call shop calls box 0, the engine holds K3-K10 and difficulty 0, and the owner's story gains a key item |
| G12 | a Hard room (revision 2): an owner whose `online_story` unlocks Hard (the k10 card's story seeded with the clear and the unlock flags `POST /rooms` checks, 711 / 77, or the campaign's clear checkpoint) opens a Hard room; the room story is the rebase on Hard (working set and keys from the Hard copies, 503 kept); the guest's Game Start gives difficulty 1 and that story, its instance at stag0101 runs Hard, its save room upload keeps its own story, and the owner's Normal copies in `online_story` are unchanged |
| G13 | flag 503 and dropped values (revision 2): (a) `Rooms:StoryWhileOwnerAway = drop`, the owner (phase 2 without 503) leaves the room, the guest enters stag0001: `story_dropped keys=flag:503`, the barrier out of the training (into stag0101 or the lobby) removes 503 with the notice, and back in the lobby the gate leads to stag0001 again; (b) the same with a new owner whose game has not entered a stage (round 59's not_started) |
| G14 | "Continue? No" (revision 2): the guest loses every life in stag0101 and answers *No*: back at the title, the next Game Start gives exactly the room story again (no own key, no leftover of the last visit beyond the room story) and the room's difficulty |
| G15 | a 409 after the save room (revision 2): the guest saves in the save room, then the same account's character is saved elsewhere (a PUT through the API with the server's revision, C2's way), the guest's next stage-change save gets 409: the copy in `backups/` and the reloaded card hold its own story bytes, the notice comes, the running engine keeps the room story |
| G16 | the owner's own steps (revision 2): a new owner (no `online_story`, round 59's zero start) plays from the Game Start through the first briefing (phase 2) into stag0001 within five minutes; the guest stays in its lobby: the room story has phase 2 within about 20 s of the owner's step (`story_saved`), and the guest's next stage change takes it to the gate of stag0001. Counter-proof on round 59: phase 2 waits, is refused with stage once the owner left stagML02, and the room story stays at phase 1 |

Unit tests: ctest `online_story` (the census tables with 503 and the key
setter stages; golden cases: 503 merged by OR, applied at a barrier into
stag0101 and left alone at one into stag0001, `story::apply` and
`holds_dropped` with the barrier's target, a dropped 503 removed at a
barrier into the lobby and left alone at one into stag0001, the rebase
derivation from phase 3, COUNTER 7, a key item, a stag0101 flag, and not
from a phase-2 lobby-only story), the C# `StoryGoldenTests` with the same
vectors, `RoomStoryKeeperTests` (503 from stag0001 taken, from stag0101
refused with stage; K3 from stagML02 taken, from stagML11 refused with
stage; a guest's second key within 60 s queued and merged after the
spacing while the guest stands in another stage; a phase step queued for
the one-per-visit rule merged at the member's next visit; the owner's
steps not spaced, his one-per-visit rule kept; a queued value under *drop*
with the owner away at its time dropped; the queue's end with the room
session), `online_session` (a Game Start without a confirmed story holds
until STORY_ACK, no STORY meanwhile; *queued* is not sent again;
`common_rebuilt` applies the room story and the room's modes in its step),
`online_character` (every cached and uploaded record carries the own
story: entry 200, 404, a 409 reload, the recovery path; the unchanged check
with a borrowed engine story; the entry rewrites every used box of an old
cache), `card_store` (a store with a story override writes the override
into every used box of both copies with valid checksums and leaves empty
boxes alone; without one it writes the bytes unchanged), `achievements`
(nine borrowed Numemon plus one rising count one), `online` (the difficulty
dialog is not offered with the online rules, offline unchanged;
`callshop.lua` under the online rules takes the refusal path and never
reaches NEXTSTAGE, offline unchanged; `common_rebuilt` is raised by a
character call outside title mode with the online rules and never
offline). With question 10 (b): a lobby scenario where a level-10 guest's
board offers the quest a level-20 owner can take, and `online` with the
owner's tiers set over the derived 908-910 / 922-927.

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

**Second review and fix round (round 55, branch `fix-r55s1`).** A second review
(game and server, live attacks on a private stack, probes on real clients)
checked the first fix round: 3 blockers and 8 should-fix faults, all fixed now
with a test or a scenario each, together with the S1 leftovers above and the
cheap minors:

- *Relay floods (6.7, 6.8, 6.10)*: one member's BARRIER frames that each
  arrived alone built state after state (4,276 BARRIER, 2.5 cores, a sealed
  pair in another room frozen for over a minute): lone decisions at most once
  a second and none while a state is built, replaced states not built, two
  builds relay-wide, a state budget per member, separate queue limits for game
  frames and states; a snapshot upload keeps its first total and grows with
  its chunks (13 GB allocated in 20 s); the room heartbeat on its own task (a
  suspended database stopped every room's pings); `ERR busy` for the address
  limit. Server tests `A_flood_of_lone_barriers_...`,
  `A_snapshot_upload_keeps_its_total_...`, `A_stalled_heartbeat_does_not_stop_the_pings`,
  `An_address_with_all_its_connections_gets_err_busy`.
- *A fork confirmed at a barrier (6.7)*: the reports go with their side, the
  other side plays on, a side of one leads alone; Park never ends an instance
  with members (`A_fork_confirmed_while_the_parent_waits_...`, two and four
  members; before, the child never sealed and the stayer froze for good).
- *Joins (6.6)*: a gap before J that never closes sends the joiners back to
  their own stage; a JOIN_AT whose joiner went seals J with the leader alone and
  LEAD_FROM (`A_gap_before_J_...`, `A_join_at_whose_joiner_went_...` with BYE,
  a barrier and the JOIN_REQ tries); a LOG frame lost with the connection goes
  again after the RESUME.
- *Resyncs (6.9)*: RESYNC and a state that reach a parked member are ignored
  (the session test `resync_while_parked` fails without it: the member landed
  in the stage it had left, as in the review's Y4); a resync whose partner
  left during the catch-up leads alone (`resync_then_alone`); a member taken
  out continues alone (`taken_out_continues_alone`, the relay sends it the
  ticks up to its LEAVE); the replay's READY judges two-member votes; three
  resyncs in ten minutes, then alone; one NETFP per member and tick; no resync
  at a barrier; an unconfirmed SPLIT only delays a vote; STATE from one member
  (`The_replay_decides_who_is_off_...`, `A_second_fingerprint_...`,
  `An_unconfirmed_split_...`, `Only_one_member_reports_...`).
- *Exits (6.5)*: a leaving member's exit prompt closes as No. Real-client
  scenario S18b: b holds its prompt open and leaves the room, the prompt
  closes, b's LEAVE applies 42 frames later and a, who only walks, leaves by
  matz02 itself; with main's client the same run fails (present stays 3, a's
  step runs out after 1,800 frames: the review's L1).
- *Leftovers*: the catch-up reports the wall clock's rate (6.6; in the hidden
  runs, where nothing is drawn, the reported rates stay as before, 3,100-4,200
  steps per second; the capped slow joiner of S22b now reports 300 for its
  measured 290, where main reported 2,958); five arrivals hold for those that
  fit (3.4, `Five_arrivals_at_once_hold_for_those_that_fit`); the real-client
  resync scenarios S11a (the desync on the shorter member: one vote, b
  resynced in 0.5 s, READY equal to a's fingerprint) and S11b (on the longer
  member: b resynced first, its READY contradicts a, `replay_disagrees`, a
  resynced, equal after), each with 5,700 ticks of equal net fingerprints after
  the resync.
- *Minors*: a sealed visit past `Instances:MaxVisitHours` takes no joins (its
  log is still kept whole: about 5.6 MB per hour of four random pads); giving
  up counts the local START; a co-arrival's BARRIER_OK names its partners;
  "left" only for members that were there; neutral pads after a resync and its
  own DELAY from the replay.

Checked on the fixed code: online off `det.sh` four times 4004f056 / 396cb219
(two exes: the client change after the first is online only), the save state
suite 17/17, r25c 10/10 with 1,748,011 frames (the reference of round 53),
ctest 40/40; server unit tests 140, integration and end-to-end 69 (one skipped,
as before); the fake-client scenarios 9/9; the scenarios on real game clients
17/17 in one run (five at a time): the 14 of round S1 with 10,140 to 13,860
shared ticks each and S18b, S11a, S11b; 2,886 fingerprint ticks compared by two
clients, the only two that differ are the injected desyncs before their
resync's READY.

Not fixed (minor, listed): a member that never sends NETFP still blocks the
votes of its instance (the expected reporters are every connected member); a
sealed visit's log is not trimmed past what a join or resync could replay; the
status window of a leaving member still reads the leader's pad (6.5).

### Round S2 (r53): accounts, online character, room story

Goal: the real loop from the title: log in, pick or create a room, play the
online character, save, and see the room story grow from every member's
progress.

| Lane | Scope |
| --- | --- |
| ui | the title menu's third entry Online and F1 Online (account screens, room list, create / join with camera and light mode, in-room view), version gate, session start and end from the title, Discord party, member areas from presence |
| character | virtual card from the online character, `card_written` and the upload with If-Match and 409 handling, cloud backup in F1 Saves, room difficulty, camera mode and story at `game_started` |
| story | merge sets and golden vectors (C++ and C#), pending sets against the confirmed room story with STORY / STORY_ACK, the counter line key (after the census of where COUNTER 1 and 7 change), real story application and dropped-value removal at barriers, server `online_story` with composition, plausibility and the owner-away setting with the dropped set |
| achievements-deploy | catalogue alignment and the client's online sync (unlocks by the local slot, stats, "was there" from live ticks only, story challenges for the owner only), room page with instances, DEPLOY.md fixed key and pin script, pin constants |

**UI lane (round 56, branch `feat-r56ui`).** Built as section 10 says,
with the user's wish of 30.10: the title menu's third entry *Online* (it
replaces the planned title panel). For the other lanes:

- *character*: the online card comes from `online_card_root(room)` in
  `game_main.cpp` (today a new temporary card, `--online-card` in
  development builds); the virtual card from `GET /online-character` goes
  there. `open_room` is the one session start of the command line and the
  title; the session's end runs `offline_title()` and
  `OnlineLobby::session_ended`, the place for the upload when leaving. The
  lobby already reads `GET /online-character` (`api::CharacterInfo`:
  revision, summary, play seconds) for the account screen and the room
  difficulty (`api::unlocked_difficulty`).
- *story*: the room view shows the owner's presence from the room state
  (`room.ownerPresent`, "away: story progress is not saved"); a notice from
  STORY_ACK can go through `Session::take_notices` as today.
- *achievements*: nothing of the UI depends on them.
- *engine* (every lane that touches TITLE2 or the title stage): the third
  entry is `Engine::title_online_entry` (host-set, off in every new engine)
  and `Engine::title_online_request` (TITLE2 sets it, the host clears it);
  `Title2Manager::menu_open()`, `menu_waiting()` and `cursor()` (2 = Online)
  are the host's view of the menu. Neither field is in a snapshot or the
  fingerprint; a lane that adds title or menu state keeps it that way.
- *host* (`game_main.cpp`): the screens over the title are `TitleOnline
  title_online` with `open_title_online()`, drawn by `draw_title_online`
  (`online_ui.hpp`); `OnlineUiState::close_requested` is the room view's
  Play; `pad_latch` holds the game's pad after they close. A screen another
  lane adds to the lobby goes into `draw_online_screens` (both places show
  it) and, if it has a way back, into `OnlineLobby::back` / `can_back`.
- Wire and REST: unchanged except that `GET /status` is read for
  `client.minimumVersion`, `currentVersion` and `registration`, and the
  create / join answers for `room.maxPlayers` and `room.owner`
  (`api::RoomJoined::max_players`, `owner`).

Checked: ctest 41/41 (the new suite `online_lobby`, the ROSTER, Discord
party and close reason checks, `DW4_TITLE_ONLINE` on the forbidden list of
`online_env`); online off `det.sh` twice 4004f056 / 396cb219 (four runs),
r25c 10/10 with 1,748,011 frames (every segment as on main), the save state suite
17/17; the seven UI
scenarios on the local stack with hidden 1920x1009 widescreen MSAA 4
windows, every screen looked at in its screenshot: the title menu with the
cursor on each of the three entries (also after B back from the screens),
welcome, register, the on-screen keyboard, the recovery code, the account,
change password, log out, a wrong password, recover, the room list, create,
the password prompt and a wrong password, a full room, the room view over
the online title with Play, the room in F1 with areas, kick, leave, the
version gate (over the title and in F1) and an unreachable server. No test
or bot input reaches the third entry: the test runs without `--online-ui`
never turn it on, and neither the input scripts (det.sh, r25c) nor the
campaign bot's title presses (START, A) press down at Option.

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

**Achievements-deploy lane (round S2, branch `feat-r56ach`).** Built as
section 11 ("Built in round S2"), 10 (the deployment detail) and 14 (website,
deploy) describe: the one catalogue (`data/online/achievements.json`, server
migration 0008), the online tracker per account with its reports, the room
page with instances, `tls-key.ps1` / `print-pins.ps1` and the pins as a build
step. Checked:

- Online off: `det.sh` twice 4004f056 / 396cb219; the save state suite 17/17
  and states through a file (direct start, new game frame 4500) identical
  after loading; r25c 10/10 with 1,748,011 frames in all (the reference); ctest
  41/41 (the new `online_achievements`; `achievements` and `presence` compare
  the checked-in JSON files).
- Server: unit 133, integration and end-to-end 70 + 1 skipped against a
  private MariaDB, twice in full (with the story lane's pause in the Python
  relay spike test, `feat-r56story` 6164e7e: on a busy machine its barrier
  overtook the other connection's HEADING; this branch leaves that script
  alone, and without the pause it fails in full runs here and passes alone;
  the room page test plays its fake clients until its checks are done and
  takes the names in either slot order); the new tests: the catalogue against the JSON
  and the migrated database, owner-only story challenges, `roomPlayers`, the
  rules of the new and changed state achievements, the room page with two
  fake relay clients in one sealed instance (names only for members).
- Scenario A1 on real hidden game clients (S5's run, k2 cards,
  `DW4_ACHIEVEMENTS=1`): both members report `online_join` from their solo
  lobbies (players 1) and `first_defeat` from the sealed field (players 2;
  neither machine counts the other's death), the server confirms each
  ("Online achievement: Connected / Ouch"), the last counter batch at the end
  of the game gives `play_seconds` 220 and 222; `online_host`, `online_raid`
  and `mp_couch` stay locked; 10,440 shared ticks with equal fingerprints.
- The pin path with throwaway keys outside the repository: the key's pin
  (OpenSSL), its PFX's (`print-pins.ps1`) and `DW4Online.Server tls-pin` agree,
  a renewed certificate on the same key keeps it, `print-pins.ps1 -Server`
  finds the key in use on a live TLS listener and refuses another; a build with
  `local/online_pins.txt` carries both pins, a malformed pin fails the
  configure step, `DW4_ONLINE_REQUIRE_PINS=ON` fails it without pins.

For the other lanes: the barrier's story merge (story lane) must not raise
`story_flag` port events (a guest would get the clear credit for the room
story it receives); the story lane's migration is 0008 as well
(`0008_online_story.sql`, its tests expect schema 8) and the migrator takes
1..n without gaps, so the merge renames this lane's file to
`0009_achievement_catalogue.sql` (`AchievementRulesTests` find it by name, the
migrator and health tests here take the embedded list; the table count is the
story lane's 17); the online character upload
(character lane) sends `achievements::context_json(report_context())` as its
context, so its state achievements unlock in the room; the title and F1 room
UI (ui lane) creates the room's `OnlineAchievements` with
`make_online_achievements(*online, created, test_run, online_folder)` right
after its `connect` succeeds, calls `flush(now, 2.0)` before every
`online->leave()` (F1 *Leave room*, the lobby's leave request, the end of the
game) and `save()` + `reset()` where it drops a closed session.

**Integration (round 56, branch `dref-r56int`).** The four lanes merged
with `--no-ff` into the main of round 57 (its deployment kit included) in the
order story, character, UI, achievements-deploy; each merge commit names its
conflicts and their solutions. `fix-r55s1` (the S1 re-check round) was not in
main yet and is not part of this integration: its `session.cpp` /
`RoomHub.cs` changes meet the story lane's STORY handling and the UI lane's
ROSTER at its own merge. The interfaces as pinned:

- *Session start* (`src/game/game_main.cpp`): `open_room(OnlineRoom room,
  std::unique_ptr<HttpWorker> http, bool created, std::string& error) -> bool`
  is the one start of the command line (`--online-create` / `--online-join`,
  `created` = a create) and of the title's screens and F1 Online
  (`LobbyRoom::created`): the replay rate calibration, `OnlineRun`,
  `OnlineRun::load_character(online_folder, DW4_PC_VERSION, error)`, the
  online card store in the engine and in `EngineHooks::fresh_engine` (card A
  the cache `online_card.dat`, card B `CardSetting::Mode::none`),
  `--online-card` (development builds) writing one DW4 save into the cache,
  `link().start_online(0, 1)`, `connect`, then `make_online_achievements(
  *online, created, test_run, online_folder)`. A failure at any point resets
  `online` and returns false with the reason; the title path then shows it
  and returns to the offline title. The UI lane's interim
  `online_card_root(room)` is gone. `online_folder` is `saves/online` beside
  the program (`--online-folder` in test runs) and holds the remembered login,
  the online card cache, the achievements profile and the cloud backup
  panel's login.
- *Leaving* (`leave_room()` in `game_main.cpp`): `OnlineAchievements::
  flush(now, 2.0)`, then `OnlineRun::leave()` (the online character's
  automatic save, up to 8 s for its upload, BYE, the REST leave): the room
  view's and F1's *Leave room*, the lobby's leave request, the end of the
  game. A session the server closes: `OnlineRun::save_on_leave()`, the tidy
  reason (toast, `OnlineLobby::session_ended`), `OnlineAchievements::save()`
  and reset, `online.reset()`, the offline title.
- *The room story at game start* (8.5): `OnlineRun::after_live_step(now)`
  hands `Session::confirmed_story()` to `GameEngineLink::set_room_story` on
  the step that sets `GameSystem::game_started` and prints "online: the game
  starts with the room story (revision N)" (or "without a room story yet");
  `GameEngineLink::after_live_step()` applies it on that step with the room's
  difficulty, camera and light mode, before `gameStart.lua`. The barrier's
  merge (`story::apply`) and `Engine::apply_room_story` write the maps
  directly and raise no `story_flag` port events (the achievement lane's
  condition). A room whose owner has no `online_story` row has the empty
  story (revision 0): a new owner's game starts from a new game's story, and
  so does every member's.
- *Protocol*: `kOnlineProtocol` 45 (round S2; 44 was S1; 46 since round 63,
  5.3) for the REST `protocol`, the settings hash and the rooms. The server
  lists and admits only rooms of the client's own protocol, so S1 and S2
  clients never meet (an S1 client would desynchronize at its first barrier;
  a 45 client would pause its instance for a menu).
- *Uploads and reports*: the online character's PUT context and the
  achievements' reports carry the same numbers: `players` the own stage
  instance (1-4), `roomPlayers` the room (1-10).
- *Engine and port events*: `PortEvent::Kind::card_written` follows
  `new_character` (observe-only); `Engine::title_online_entry` /
  `title_online_request` are host fields outside snapshots and the
  fingerprint; `Title2Manager::menu_open() / menu_waiting() / cursor()`.
- *Server*: migrations 0008 `online_story` (story lane) and 0009 the
  achievement catalogue (the achievements lane's 0008, renamed; the migrator
  takes 1..n without gaps): schema 9 (`/health`), 17 tables. The online
  character and cloud save writes run in READ COMMITTED with the deadlock
  retry; `online_story` is written by the relay's story saver.
- *Deployment*: DEPLOY.md keeps the achievements lane's kept-key flow
  (`tls-key.ps1`, the PFX, `new-selfsigned-cert.ps1 -ImportPfx`,
  `print-pins.ps1 -Out` into `local/online_pins.txt`, the CMake pin step) with
  round 57's corrections (the loopback check endpoint 127.0.0.1:5280, the port
  check, the Remote Desktop path for the PFX, `-PinServedBy`,
  `restore-db.ps1`, `--disable-build-servers`).
- *Test stack*: `run.py start_server(stack, connection, extra, name)` gives
  every named server its own folder (certificate, settings, log, relay
  diagnostics): the UI scenarios' shared servers `server0`, `server1`, ...,
  a scenario's own server by its name. Every card scenario has
  `seed_story`; S14 / S14h / S14a use stag0101's flag 505 (8.7).

Checked on the integrated tree: the release build with tests and the
`DW4_ONLINE_DEV` build (Ninja, MSVC); ctest 44 of 44; online off `det.sh`
4004f056 / audio 396cb219 in all eight runs (twice before and twice after the
last code commit), the save state suite 17 of 17 identical after loading
(both builds), r25c 10 of 10 segments with 1,748,011 frames (every segment's
count as in the lanes' runs and main; the build of f696838, before the log
line of 41eec76, which only an online session reaches); the server with the 11.0 RC SDK and
`--disable-build-servers`: no build errors, unit tests 150, integration and
end-to-end 74 passed and 1 skipped (the certificate store test) against a
private MariaDB; on the local stack with hidden game clients: the 14 S1
scenarios (S1, S2, S3, S5, S16, S17a, S17b, S18, S19a-c, S21, S22a, S22b:
equal fingerprints in every shared instance, no desync), S14, S14h and S14a,
A1 (both members hold `online_join` and `first_defeat` with the ranked
context, and `hoarder` from the state check of a character upload), the
seven UI scenarios at 1920x1009 widescreen MSAA 4 (the room view with Play,
the online title after Play, the F1 room with areas and the room list after
leaving looked at), `character_test.py` C1, C2, C3 and P1. C1, C2 and C3
compare the server's record with the card cache outside the story bytes
since the integration: the server keeps the account's own story there (8.4).

Open after the integration: the reset of the online character in the UI
(`character_api::reset_request` exists, the notice names the Online tab);
release builds stay "not set up" until `local/online_pins.txt` holds the
server's pins (DEPLOY.md 6.1, 6.4); the fake clients ignore STORY_ACK; the
calibration of the achievement goals on real online runs; the .NET 10 SDK of
the development PC is broken (only its Roslyn folder is left), so the server
builds with the 11.0 RC SDK and `--disable-build-servers` (round 57).

**The done-when on the integrated tree (round 56 verification).** Each part
of the done-when above, driven through the client's own code paths with
hidden clients on the local stack (its own MariaDB and ports):

- *Two accounts, a room, new characters, saves*: `run.py ui newchars`. Both
  accounts register in the screens, the first creates a room, the second
  finds it and joins, both play new characters with the campaign bot (each
  "enter without a character"); for each, the first stage change makes
  revision 1, changed stage changes upload, the campaign's checkpoint p2
  saves in the save room (revision 4). The guest leaves right after its save
  room, so its leaving save finds nothing new ("unchanged since revision 4").
  The owner plays on into stag0101 and leaves there: its leaving save uploads
  revision 6. `GET /online-character` returns both records at those
  revisions, equal to the card caches outside the story bytes. The guest's
  story bytes are empty (its own story is untouched), the owner's hold phase
  2. `character_test.py` C1 shows the same for one member with every kind of
  upload.
- *A guest's story flag*: S14p. The guest's stag0101 flag 505 was in the
  owner's `online_story` 0.5 s after the guest's engine set it. The owner's
  machine held it in its confirmed room story while its lobby engine kept
  505 = 0 for another 180 frames, and its next barrier (into stagml12)
  brought it.
- *S14*: S14, S14h and S14a (drop, hold, apply) pass on real clients.
- *S15*: `character_test.py` C4. Another PC saves while the client walks to
  the save room, so the save room's upload gets the 409. The player is told,
  the cache is kept in `backups/`, the other PC's revision is reloaded and
  stays on the server. C2 shows the same for a stage change's upload.
- *Golden vectors*: C++ `online_story` (ctest), C# `StoryGoldenTests`, and
  `story_golden.py --check` (equal).
- *Achievements*: A1 (`online_join` in the solo lobby with players 1,
  `first_defeat` in the sealed field with players 2, `hoarder` from an
  upload).
- *Everything else*: all 19 game scenarios pass with the final development
  build (the 14 of S1, S14, S14h, S14a, S14p and A1), with no differing
  fingerprint. So do the 8 UI scenarios, `character_test.py` C1-C4 and P1,
  and ctest 44 of 44. The server builds, and its tests give 150 unit tests
  and 74 integration and end-to-end tests passed, 1 skipped.
- *Section 15*: `det.sh` gives 4004f056 / 396cb219 in every run: plain, with
  `DW4_CAMERAS=1 DW4_CAM_CHECK=1` (0 of 1902 frames), with
  `DW4_LEASH_OFF=1` and with the extra renders hook. The four-player runs
  (stag0102 ded601be / boxes 1f1b3b50, dng101b1 586f711d / e4da6a4e) are
  equal headless and with local cameras 0, 2 and 3, as in round 52. The
  save state suite is 17 of 17 with both builds, and the states through a
  file (direct start dng101b1 frame 2000, new game frame 4500) are
  identical. r25c is 10 of 10 with 1,748,011 frames, every segment as in the
  integration's run.

Found and fixed in the verification: the room view told a player who was
already playing "Your online character starts new: Play, then START and Game
Start at the title", even after several saves. Once the game runs it now
shows the session's save state ("Online character: saved (revision N)").
New test hooks: the script condition `roomflag:N`, `wait-members <=N` in UI
scripts, and the scenario keys "story_saved", "characters" and
"checkpoints".

Still open after the verification:
- When a member leaves on purpose (BYE), the hub sends the others
  `Disconnected` before `MemberLeft` (`RoomHub.Disconnect`), so they see
  "X lost the connection" just before "X left the room".
- The story runs nc1 were not run in this round.

**Fix round of round 56 (`dref-r56int`).** main with `fix-r55s1` (0950c69)
is merged into the branch (4406220: the seven conflicting files resolved,
both sides kept). The review's findings and their fixes:

- *Story per visit* (blocker): 8.7 "Per visit"; the client's retry of *wait*
  and *not_member* keys (8.7, pending sets).
- *A deleted owner's room* (blocker): the owner's rooms close with the
  deletion (and a lock or ban), no story is written for a deleted account
  (8.7 storage).
- *A new character beside the online one* (blocker): section 9 "Round 56 fix
  round"; the reset on the account screen (section 10).
- *The title's application after "Continue? No"* and the option menu: 8.5.
- *Stay* in the room view closes B + X + START's prompt
  (`OnlineUiState::stay_requested`).
- *not_member* after the r55 merge (members taken out of their group,
  instance 0): `LoneStage` and the client's retry (8.7).
- *Saves after a refused login* stay pending (section 9).
- *Story history, reset, restore* and the *save queue* (8.7 storage).
- *Privacy page*: the online story and its revisions, the public "where they
  play" lines, the story in the deletion list.
- Minors fixed: the join toasts for members already in the room, online
  achievements without the local setting, the owner-away text and the
  website's badge by the policy (`storyPolicy` in the room summary),
  the recovery code buffer, the UI script log's passwords, the cloud panel's
  device name (DW4_PC, not COMPUTERNAME), the server's difficulty unlock,
  Lone Tamer with the server's member count, a rate-refused STORY without the
  story blob, the protocol (45) and the story keys in the deploy settings and
  DEPLOY.md (also: how to open an elevated PowerShell for
  `install-service.ps1`).
- Minors left: `leave_room()` / `open_room()` still block the main loop while
  the server is slow; the relay's instance size does not cap `players` in
  achievement reports; the admission's owner presence is read before the
  story load (not reproduced); a new owner without a story starts from what a
  guest did in the room before (a design question for the user); the cloud
  panel's *Log out* does not revoke the shared login on the server;
  `api::unlocked_difficulty` still answers Normal without an online character
  (since the reset empties the story too, an account without a character has
  no clears left).
- Found in the verification and fixed: on a loaded machine (four scenarios at
  once) A1's two clients stalled together for ten seconds; the hub took both
  out for lag, the instance ended before either got the ticks up to its LEAVE,
  and both waited for them for good. Nobody is taken out for lag now while
  every connected member of the instance lags (`RoomHub`, HubLogicTests);
  alone A1 passed in 289 s before and after.

Checked on the fix round's tree (the release build with tests and the
`DW4_ONLINE_DEV` build of a57a68b, the server at its head): ctest 44 of 44;
online off `det.sh` 4004f056 / audio 396cb219 in every run (plain twice, the
camera check with 0 of 1902 frames, no leash, the extra renders hook); r25c
10 of 10 with 1,748,011 frames (every segment as before); the save state
suite 17 of 17 identical after loading; the server's unit tests 167,
integration and end-to-end 78 passed and 1 skipped against a private MariaDB
(a fresh data directory per run: XAMPP's MariaDB 10.4 crashes now and then
with the breakpoint exception at offset 0x643842 on this PC, eleven times in
three days, a data directory it left behind read short at its next start);
22 game scenarios on hidden real clients (the S1 set with r55's S11a, S11b and
S18b, S14, S14h, S14a, S14p, A1: A1 alone after the run above; with the
lag rule S1, S2, S3, S5, S11a, S11b, S21 and A1 again, 8 of 8), the 9
fake-client scenarios, the 8 UI scenarios at 1920x1009 widescreen MSAA 4
(*areas* now ends with the kicked guest resetting the online character its
game made: the account, reset and after-reset screens looked at) and
`character_test.py` C1, C2, C3, C4 and P1. S14p's flag reached the owner's
`online_story` 0.5 s after the guest set it (its visit was older than ten
seconds).

### Round S3 (r54): stage flow in shared instances

Goal: every case of the table in section 5 behaves as decided.

| Lane | Scope |
| --- | --- |
| classes | the script class table and its generator (with `local_scene`, `gather`, `warp`, `boss_intro`, `boss_end`), MATisAllPlayer per class, in-stage forks before story triggers, boss and story mats with *Go without them*, boss locks set and cleared in the simulation, the boss choice at arrival, Trailmon without a fork (participants only), the 20 direct-NEXTSTAGE exits, dngq6b01 as `gather`, joiners' entry without `first.lua` checked across stage types |
| scenes | (the status window, talks and map recorders: done in round 63, 5.3) the per-slot demo viewer for camera tours, tutorials and shaking mats as own scenes; save room and call shop rules |
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
| latency | delay tuning from the S1 matrix and S12; a UDP transport for INPUT / SEALED if the matrix showed TCP's loss stalls unacceptable; lag indicators (round 62, `fix-r62netlag`, after the field report of 31.10: the delay rule, the join's grace, the playout drain, the network indicator and `saves/online.log`, measured in 6.4; its review fixed the 60 Hz beat, held buttons through late pads, the 2 % rise, the message numbers, DW4Online 0.1.3; UDP still open) |
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
| presence | ghost models in hub stages if wanted (question 1), roster polish. *Ghost figures built in round 60 (r60ghosts, 12.1) at the user's request of 31.10* |
| travel | warp to a friend's stage from the lobby if wanted (question 4); a *play alone* switch if wanted (question 8) |
| saves | the upload policy chosen in question 3; second local player if wanted (question 5) |
| release | README "Online co-op" rewritten, this document updated to what was built, field test checklist for the user (deploy, pin, firewall), domain switch prepared as a build constant |

Done when: the chosen features have scenarios that pass on the local stack; the
docs describe the built system; the user has a checklist for the first test
over the deployed server (deployment stays the user's step). A buffer round
for what that field test finds is expected.

Depends on: S4 and the answers.

### Round 60 (r60ghosts): ghost figures in the hub stages

Goal: the user's decision of 31.10 after the first test with a friend over
the real server ("you only see yourself"; question 1): in a member's solo hub
instance the other room members of the same hub stage appear as their
Digimon, look only, and nothing of it reaches the simulation. Built as 12.1
describes: on the client the presentation-only renderer
(`src/game/ghost_figures.{hpp,cpp}`, the hooks in `game_main.cpp`), the
PRESENCE2 sender (`Session::send_presence`, `track_figure`, the read-only
facts of `GameEngineLink::presence`) and the receiver's interpolation
(`PresenceTracks`); on the hub `Presence2Msg`, `RoomHub.OnPresence2` and
`FanPresence` with the WELCOME flag; scenarios P1 and P2 (16). The lane of
round S5 called *presence* is this round's work.

Looked at in the pictures of P1 and P2 (hidden 1920x1009, widescreen, MSAA
4, from every client): the figures stand on the floor with their shadow and
face where they walk (a run from (60, -30) to (79.6, -54.4) faces 2.47, the
direction's angle; the trace's rebuild of the own player's facing from its
yaw is 0.0000 off), they play the sender's clips (`waitS`, `runS`) with its
weapon, the form change of the test hook crossfades, a figure walking in
front of the own player turns faint and the own player stays visible, c's
figure goes while c plays the field and fades in again at the lobby's entry
after c's game over, and c's own trace shows no figure in the field. With
the PRESENCE-only member of P2 each side sees the other's figure without
weapon, running and standing by the packets' speed. Nothing needed a change
after these pictures.

Checked on the branch head (the release build with tests and the
`DW4_ONLINE_DEV` build, both from the same tree): ctest 45 of 45
(`dw4_ghosts_tests` with the game data: 65 checks); online off `det.sh`
4004f056 / audio 396cb219 in every run (the release build twice, the
development build once); r25c 10 of 10 with 1,748,011 frames, every segment
as before; the server's unit tests 190, integration and end to end 90
passed and 1 skipped against a private MariaDB with a fresh data directory;
on hidden real clients P1 and P2 and the 22 scenarios of rounds S1 and S2
(S1, S2, S3, S5, S11a, S11b, S14, S14a, S14h, S14p, S16, S17a, S17b, S18,
S18b, S19a, S19b, S19c, S21, S22a, S22b, A1, three at a time): every member
passes through its solo lobby with the others' figures and the shared field
instances keep equal net fingerprints (3,060 comparisons, the only two that
differ are S11a's and S11b's injected desyncs before their resync); the 9
fake-client scenarios; the 8 UI scenarios.

Open after the round: a server built before this round has no PRESENCE2,
so until the user's server is updated a new client shows the figures from
PRESENCE alone
(without weapon, run or wait by the packets' speed; 12.1 "Versions fail
safe"). Figures in a field or dungeon (members of other instances there)
were not asked for; they stay name tags.

**Review fixes (same branch).** The round's review found two things a player
would see. A member that left the lobby over a mat faded out on the mat, came
back there about three seconds later (the end of the mat's scene showed its
model again on its own machine until NEXTSTAGE parked it, and that packet
went out) and stood two seconds until the packet turned stale: a departure or
a barrier now hides the member at once until its next stage load (12.1
"Leaving", scenario P3). And the walk of a figure was uneven: the drawn clock
offset rose 2 ms a packet and dropped back at every new smallest one (a saw:
single pictures up to 1.85x the even step on loopback, 3.65x with jitter),
and a 144 Hz sender stamped its poll time (+-13 %): the offset now moves at
most 2 % towards the smallest lag of the last 20 packets and the packets
carry their step's time (12.1 "Interpolation"; the review's harness on the
new code: at most 3 % off the even step for 60 and 144 Hz on both sides,
with and without jitter). Of the minors: HP or MP above the maximum no longer
drop a PRESENCE2 (clamped on the client and in the hub), PRESENCE of an older
client is held to PRESENCE2's bounds, every PRESENCE2 version stays within
256 bytes (a larger one is dropped unread), weapon names from the network
load only as weapon packages and at most 16 are kept, the ghost list and
shadow outlive the draw block (the renderer keeps the shadow's address) and
the development ID pass uses the picture's own shadow, `main` is merged in,
and round 59's branch (fix-r59s2) adds no conflict of round 60's (its two
with `main`, `game_main.cpp` and the server README, are round 62's and 59's).
`main` brought round 62 meanwhile, whose NETSTAT holds server type 0x8F in
the field since DW4Online 0.1.2: round 60 had given 0x8F to PRESENCE2 to
clients, and a round 60 client would have dropped its connection on every
NETSTAT of the user's server. PRESENCE2 to clients is 0x90 now (both
registries know it, and it is informational: one that does not decode is
skipped); the next free numbers are 0x15 and 0x91 (6.3).

Checked on the fixed head with round 62 merged in (release and
`DW4_ONLINE_DEV` builds from one tree): ctest 45 of 45 (`dw4_ghosts_tests` 70
checks, `online_session_tests` 190, `online_wire_tests` with both type
registries; `presence2_smooth`, `presence2_walk` and `presence2_departure`
fail on the code before the fixes); online off `det.sh` 4004f056 / audio
396cb219 in all six runs (the release build twice, the development build
once; the same before the merge); on hidden real clients all 29 game
scenarios (the 22 of rounds S1 and S2, A1, round 62's L1, L1s, L2 and L3, P1,
P2, P3; three at a time): 4,280 fingerprint comparisons, the only two that
differ are S11a's and S11b's injected desyncs before their resync. S19b
failed once in that batch: member a's fight step killed every goburi near it,
so its next step (walk into them until it falls) found none and stood at
full HP until the timeout, its 1,002 shared fingerprints equal; S19b alone
passed (288 s), and before the merge it passed in the batch. In P1 c's figure
fades out at the lobby gate and shows again only at the lobby's entry after
c's game over; in P3 b's figure fades out on the mat and never shows again
(the pictures every 45 frames and a's trace every 10 frames). the 8 UI scenarios;
the server's unit tests 212, integration and end to end 90 passed and 1 skipped against a private MariaDB with a fresh data directory (before the merge one run of ServiceHostTests failed once on the order of two FAIL log lines with the HTTP port busy, then passed six times; nothing of this round touches the service host).

### Round 63: own menus (built)

The user's decision of 2026-10-31 (5.3, question 6): a member's status
window, NPC talks and map recorders are its own and pause nothing, near or
far, also alone. Built on main 87d9f0d in branch `feat-r63ownmenus`:
own window programs per slot, the scene scope with its narrowed commands, the
members' own copies of window models, cursors and text lines, the picture
and sound filters, membership and exit rules for busy members, the online
script's `menu`, `browse`, `talkto`, `shot` and conditions `menu`, `menus`,
`talking`, `scenes`, `lost`, run.py's `"shots"`, the protocol 46 (a 45 client
would pause for a menu and desynchronize). Checked: the pause analysis of 5.3,
ctest 44/44 (the `online` scenario "dng101b1 own menus"), the real-client
scenarios M1 and M2 with screenshots from both machines (every instance's
net fingerprints equal at every compared tick), the S1 real-client scenarios
17/17 unchanged, the offline two-member harness in dng102b3 (`DW4_ONLINE=3
DW4_CAMERAS=3`, the map recorder from views 0 and 1), offline `det.sh` twice
(4004f056 / 396cb219), r25c 10/10 (1,748,011 frames), the save state suite
17/17.

Seen on the way (not a round 63 matter): a member who reached dng101b1 15 s
after its partner got `OtherContext` and an instance of its own (the session
context, 3.7, had changed meanwhile); M2 therefore lets both wait on the mat.

Left for later: camera tours, tutorials and shaking mats as own scenes (they
need a per-slot demo viewer); the story scenes the instance shares stay the
original by decision.

### Round G (proposed r64): guests on the host's story

Goal: the user's requirement of 31.10 (8.8). A guest's game in a room has
the owner's story (the training skipped when the owner played it, the
owner's areas, gates and key items), its uploads and saves keep its own
story, and it is told whose story it plays.

| Lane | Scope |
| --- | --- |
| story (sets and keeper) | 503 as a story value (census class and setter, `story_sets.json`, `online_story_sets.inc`, the server's sets, `session_values.txt`); the key setter stages (census from the stage data's item drops and the r25c FLAGFOLDER rises; the keeper refuses a key from another stage); `story::apply` and `holds_dropped` with the barrier's target (503 not into stag0001); the rebase derivation for saved stories (C++, C#, Python, golden vectors); the keeper's queue of earned values (reason *queued*, merged when due by the relay), the owner without round 59's spacing; keeper tests; the protocol version |
| client | the call shop closed online (`callshop.lua` wrapper, hint) and `Engine::common_rebuilt` with its application; the Game Start's hold until the confirmed story (with G7's dev-build switch); the card store's story override for a guest's session and the entry's rewrite of an old cache; the own story bytes in `CharacterSync` (uploads, recovery, 409 reload); *queued* in the session; the toast, the F1 / room view story line and the "moved on" notice; the online difficulty dialog rule; `numemon_freed` by mask |
| scenarios | the production-like `seed_story`, G1-G16 with counter-proofs on main (G16 on round 59), the regression S2, S3, S5, S14, S14h, S14a, S14p, S14z, S16, S17a and C1-C3 |
| hub extras (only if question 9 says per player or question 10 says (b)) | rewards: the `online_rewards` store (round 64 design, 9.1.9: keyed by account and box, the box always 1 until round D) and its composition, the keeper routing reward flags to the sender's own store, the room story blob without them, the title's overlay and the personal-class extension at joins and DISSOLVE; quest gates: the owner's tiers from his newest online character record (from round D the box his `/play` binding names, 9.1.9) in every BARRIER_OK into a hub, the wrapper after the ML11-13 `start.lua` derivation |

Done when: G1-G16 pass (G1, G3, G4 and G11 fail on main, G16 on round 59:
the counter-proofs); the regression scenarios pass; ctest; the server's
unit and integration tests; online off bit-identical (`det.sh` 4004f056 /
audio 396cb219 twice, r25c 10 of 10 with 1,748,011 frames, the save state
suite 17 of 17); a DW4Online release with the new sets, the key stages, the
queue and the rebase.

Depends on: round 59 merged (the same files: `character.cpp`,
`session.cpp`, `online_run.cpp`, `RoomStoryKeeper.cs`,
`OnlineStoryService.cs`); the answers to questions 9 and 10 (defaults:
rewards as room story, quest gates (b), so the hub extras lane runs with
the quest gates only). The user's step: deploy the server release and the
client together (the protocol rises); the rebase fixes saved phase-2
stories at the next room open. Until round G, round 59's spacing stalls a
new owner's phase 2 (G16's counter-proof); if that is wanted earlier, the
queue and the owner's exemption can go into round 59's fix round alone
(server only and without a protocol change: the keeper answers a queued
value with *wait* as today, the client's resends do no harm, and the
keeper merges it when due).

### Before round D: the Cheats tab in rooms (a small fix)

The Cheats tab works in seamless rooms today (9.1.10: `game_main.cpp:3566`
and `:3650` test only `net.active()`), which desyncs instances and lets
cheated levels and bits into online saves now, not only after round D.
Fix in the next client fix round on main after round 63 (the same lines in
`feat-r63ownmenus`): the tab's controls disabled and its player buttons
hidden whenever `online` is set, with a line "Cheats are off in online
rooms."; `cheats` in the character and achievement contexts
(`character.cpp:118`, `achievement_api.cpp:13`) from the engine's real flags.
Checks: a hidden room run where the buttons are absent and the context says
false; online off bit-identical (UI only).

### Round D (after round G): the server's character

Goal: section 9.1. Lanes build against the tables, the mapping, the rule
codes, the binding, the ETag and the merge pinned there; the integration
fixes the migration number.

| Lane | Scope |
| --- | --- |
| tables | migration 00NN (the `online_*` tables with surrogate ids for devices and disks, `online_slot_stats`, the side tables, `online_play`, `online_edit_drafts`, `online_rule_state`, `ref_forms`, `util_seq`, the empty `game_*` tables, the hash and address columns converted to text and the cloud save blobs moved, the virtual columns, the `v_` views, the legacy COMMENTs; first task: the expressions on MariaDB 10.4.32 and 11.4), the migrator's grant check and its data steps (inside the started mark, versioned in the checksum), the repositories' text conversion of hashes and addresses, the C# mapping tables <-> `SaveRecord` <-> bytes with `data/online/record_golden.json` (and the C++ side of the vectors), the box repository (every writer under the box lock, the fingerprint from the written state, the history with `changes` and `client_record`, refused uploads, visits), the data step for today's rows (box 1, every rule as `migrated:` flags), edit detection (fingerprint, watcher, quiet time), completions, drafts and the merge, `GET /online-characters`, `PUT /online-characters/{box}` with the ETag rules, `.../play` with the persistent binding, the reset per box, the queries of the other services moved |
| rules | the game data import (`GameData:Directory`, `game_data_meta`, `game_enemy`, `game_stage_enemies`), `recalc_player` in C# (growth table, shinka, gear, caps) checked against the engine with the user's data, the rule table with its codes and configured actions, the "only what changed" test and the breaker, `new_template` from a fresh port save, the change rules on the relay's visit log (`RoomHub` writes `online_visits` at the barrier decision and closes them; windows by sequence and box; `visit_pending`), `rookie_pair`, `form_reward` with `data/online/form_rewards.json` derived from the scripts, `drop_plus`, `exp_bound` and `bits_bound`, the rates (flag only), staff accept (as a merge) and review, the 409 / 422 answers, the game data banner and `/health` |
| client | four boxes in `CharacterSync` (per-box state in `online_card.state`, the 0-3 / 1-4 conversion in `character_api`, the card in memory, the recovery through the server after its `/play`, new characters into the lowest free box with the new-character record, `/play` at the Game Start and no PUT before it is confirmed, `play_required` and `visit_pending` handled, the ETag, copies into another box not uploaded, a refusal leaves the room, the merge notice), `visitKey` in the context, `--export-web` (faces, staff roll renders brightened with `alpha8.png` for Alphamon, cards, icon glyphs, `game_data.json` with the stages' enemies, `manifest.json`), ctest `online_character` and an export test with the user's data |
| web | the profile pages (9.1.12: header picture, four boxes, a box in full, gear and card colours, *under review*, privacy), `/art/` with the manifest and the placeholder, the admin character editor (the preview with every completion, drafts of database edits, *Reload now*, history, refused uploads with *Accept*, flags, the breakers, the game data banner), the account page's reset per box, the privacy page, DEPLOY.md (export, copy, the grants, the HeidiSQL guide: start at the `v_` views, what is editable, what the server does with an edit and when) |

Done when: D1-D16 pass on the local stack with hidden clients (D4, D5 and
D11 also run on main as counter-proofs: 409 and stopped uploads after a staff
edit, the forged record and the forged first record stored); the regression
S2, S3, S5, S14, S14p, S16, S17a, round G's G2 and G3 and `character_test.py`
C1-C4 pass; the server's unit and integration tests and every ctest pass; the
engine fixpoint holds for every composed record and the 511 distinct records;
the bounds of 9.1.6 never refuse a visit of the hidden bot runs (r25c
segments online); online off bit-identical (`det.sh` 4004f056 / audio
396cb219 twice, r25c 10 of 10 with 1,748,011 frames, the save state suite 17
of 17); a DW4Online release with the migration.

Depends on: round 59 and round G merged (the same files: `character.cpp`,
`OnlineCharacterService.cs`, the story keeper; round G's `online_rewards`
with its `box` column), round 62 merged (`RoomHub`), the Cheats tab fix
above; the answers to questions 11-14 (defaults otherwise). The user's
steps: the export on his PC, the folder to the server, a database backup,
`migrate` (as root on his server; the restricted user of DEPLOY.md 2 needs
the GRANT first), the client and the server together (the client version
rises).

### Round D2 (after some weeks of play): calibration

| Lane | Scope |
| --- | --- |
| calibrate | the rate limits and the bounds' factor from hidden bot runs (the campaign bot online through the r25c segments, `work/bot_combat`) and the stored visits and revisions of real play; `shop_sums` checked against the port's shops (with the quest rewards; ¶ Data as a lower bound) and set to refuse; the breakers and `grandfathered:` flags real play raised, reviewed |
| rewards | the per-script form table of `form_reward` checked against every branch of treasure02-08 (difficulty, progress, the rookie with the highest EXP, the reward flags) and set to refuse |
| cleanup | the migration that drops the legacy tables (with round D's DROP grant); `missing_upload` reviewed; if abuse shows, an option that holds a member's next JOIN until its barrier's upload is accepted (`Instances:HoldJoinUntilSaved`, off by default) |

Depends on: round D deployed.

### Round J (proposed r66): instant joins and checkpoints

Goal: 6.13.2-6.13.5 (the user's feedback of 01.11: new players no longer
"catch up"). A member joins a running instance in under a second at any
visit length, bit-identical with the others; resyncs restore a checkpoint
instead of the load.

First tasks (measurements, into 6.13 before the lanes build on them):

- the topology of a fast load against the live load with the original
  timing: the topology hash after each stage load of the r25c story run,
  live, and after restoring its snapshot and replaying its load ticks;
- encode, hash, zstd and apply cost and the compressed size of the whole
  tick state in stag0101, stag0102, dng101b1, stagBS02 and a water stage
  (the prototype measured 1.2-1.5 ms and 37-61 KB with zlib 6 for nodes,
  MODELCTRL and ACTION2 alone);
- the side effects of the SceneCache's lazy node creation (does creating a
  node by id on apply equal creating it on first use?).

| Lane | Scope |
| --- | --- |
| tick state core | `include/dw4/engine/tick_state.hpp` (the archive with the id codecs, sections, raw and canonical form, XXH64), engine scalars, generator, game system, online fields; the topology journal and its six entry kinds with their re-run; scene graph nodes and edited attributes, `AnimationDatabase::looping_`, MODELCTRL, ACTION2, GENERATER; `Engine::state_safe` with the transient-field assertions; the size guards; the completeness harness (`DW4_TICKSTATE_TEST`, ctest `tick_state`) with the continuation test and the divergence trials (in-place apply after random wrong pads, 6.13.3), and `tools/online/tickstate_diff.py`; a test hook that leaves one field out of the serializer (for JS) |
| tick state rest | `Lua4Vm::io` (slot layout, generations, free list, GC counters, prototype ids, the owner restamp), messenger and demo (the exit step's port), CAMERA, HitLand, collision groups and ChangePos, the sound simulation (sfp, the SNDF driver with bank and offset ids, the streams' game parts, `CriStreams`) and the joiner's music position from the CRI counters, the small managers, TEXTCTL `Line` keeping its text, the SUBWND window trees reachable in field stages (the status window's pages, folders, skill: about 25 classes; the shop windows assert they never open in a state-safe instance) |
| relay | STATEHASH with K and its vote (NETFP's rules), the donor, CHECKPOINT_ASK and the CHECKPOINT store (two per instance, fork-aware), the agreed K, STATE_DATA layout 2, the join with a fresh or stored checkpoint and its waits, CHECKPOINT_BAD with one retry, then the classic path, then NEW; `JoinOrNew`'s catch-up estimate from the checkpoint; resync from the checkpoint; the whole log kept (the classic path for every fallback); the suspicion rule (rebuilds noted, a mismatch within `CheckpointSuspectTicks` switches the instance to the classic path, both sides' states asked for, `checkpoint_suspect` logged), `Instances:CheckpointJoins`, READY's *from checkpoint* flag that never judges; unit and integration tests with fake clients; the protocol version |
| client | zstd's encoder sources in the build; the session's checkpoints (STATEHASH with K at `C_k`, the bytes of the last one kept, the donor's upload in chunks under the state budget), the join path (rebuild, the K check, self check, the short catch-up, "Joining..." without a countdown), the fallbacks, resync from a checkpoint; scenarios on the local stack: J1 / J5 / J15 / J30 (two members, stag0101, as measured for this design), J15x4 and J30x4 (three members play, a fourth joins), JW (a join while a member's own status window is open and while an NPC talk runs), JF (into a fork's child before its first own checkpoint), JR (an injected desync, resynced from the checkpoint), JB (a damaged checkpoint), JS (a serializer that misses a field through the test hook: the joiner drifts, the path turns suspect, the classic path repairs it; two members, so the READY from the checkpoint must not judge), JL (40 ms of round trip through the proxy) |

Done when: barrier to member under 1.0 s in J1, J5, J15, J30, J15x4,
J30x4, JW and JF on the reference machine (timed from the clients' and the
relay's logs as for this design; today 1.31 / 6.34 / 15.68 / 31.04 s), and
under 1.0 s plus one round trip in JL; every instance's NETFP and STATEHASH
equal for 10,000 ticks after each join; JR equal again within a second of the
resync; JB falls back and joins; JS ends with both members equal on the
classic path, the healthy member never resynced, `checkpoint_suspect`
logged and both sides' states kept, and later joins of that instance take
the classic path; the completeness test of 6.13.3 finds no difference over
its whole set, its divergence trials included, with idempotent bytes; the
tick state's costs
and sizes are measured and in 6.13 (compressed at most 100 KB); the
regression scenarios S1, S2, S3, S11a, S17a, S18b, S19b, L1, L2, M1 and M2
pass; ctest and the server's unit and integration tests pass; online off
bit-identical (`det.sh` 4004f056 / audio 396cb219 twice, r25c 10 of 10 with
1,748,011 frames, the save state suite 17 of 17).

Depends on: rounds 59, 60, 62 and 63 merged (`session.cpp`, `RoomHub.cs`,
the message numbers both 60 and 62 claim, round 63's own windows and copies,
which the journal and the SUBWND lane cover); the answer to question 15. The
user's step: the server release and the client together (the protocol
rises).

### Round R (proposed r67): rollback for the own player

Goal: 6.13.6 (the feedback's "sometimes 80 ms"): the own character reacts in
the frame of the input at 20-80 ms of round trip, every engine stays
deterministic, the instance stays in sync.

First tasks (revision 2: the first draft's probe re-executed true pads in
one process-wide arena without a picture, 6.13.1):

- **the split probe**: the online engine in its arena with the product's
  split (`ArenaScope` only around engine entries, the host on the CRT heap),
  a hidden picture at 60 and 144 Hz, audio, and trials with random wrong
  pads (1-15 ticks, restore, the true pads as re-simulations, compared with
  a straight run); it measures pages, save, restore, re-simulation and the
  host-write check, the first draw of a joining species, and decides where
  the render's presentation caches live (host memory with ids and a serial,
  or the arena); its numbers replace 6.13.1's and 6.13.6's budget;
- the arena contract audit: every Engine entry the online host calls,
  classified (scope, copy out, host allocator), every growing static and
  thread-local (rule 3), every `shared_ptr` that crosses (the banks, the
  documents, `StageSnapshot`, the draw list's palettes, the mirror); its
  result goes into 6.13.6 before the session work builds on it;
- the torture test on the product allocator and arena, alone, before the
  session uses them;
- the misprediction rate from recorded pads (question 17; until then the bot
  driver's analog steering, every tick a misprediction, as the budget's case).

| Lane | Scope |
| --- | --- |
| transport | only if round S4 has not landed it: UDP for INPUT and SEALED with redundancy, the ticket as the session key, TCP kept for the rest and as the fallback (6.4's decision); the S1 loss matrix again over UDP |
| arena | `StateArena` (a reserved region with `MEM_WRITE_WATCH`, the deterministic allocator with its metadata inside, the page ring with the shadow image, restore to any kept tick, the fold), the game's replaced `operator new` / `delete` (the CRT heap without a scope), `ArenaScope` / `ArenaOff` with the setters in scope, the online engine constructed in its arena, the generator saved per tick or moved into the engine, `HostAlloc` for the growing statics with the grep test, the CRT warm-up, `BankStore` and `DocumentStore` with plain pointers in the engine, the driver, the mirror and its calls, snapshots across as bytes, the host-write check (rule 6), the sound manager's split into its simulation side and an `AudioSide`, `Engine::set_resimulating` with the per-tick output record (port events, log lines), the poisoning debug build, the torture test (`DW4_ROLLBACK_TORTURE`, with in-place tick-state applies), the cost table of 6.13.6 re-measured |
| session | frontier on the local clock (the server tick estimate, `F = s + d_i`) and confirmed tick, prediction by the slot's last sealed pad, the own pad sent and executed at once, the cap `P_i` and the hybrid above 15, the reconcile (the first differing tick, one restore, re-simulation), `Engine::rollback_barrier` and the stops at barrier ticks (no stop at a JOIN or a package load), EVENT_AHEAD and the margin beyond the largest frontier on the relay, the JOIN executed predicted with the package prefetch, JOIN_AT without delayed sampling, the delayed-mode fallback by cost, the confirmed-only outputs (NETFP, STATEHASH and the donor's state from the saved tick, port events and log lines per tick, STATE, SPLIT, HEADING, BARRIER, INTENT, story, notices), `net_stats` with the rollback numbers and the frames with a standing frontier |
| presentation | the peek render also with the shared camera (the simulation traversal inside every tick), the render's caches where the split probe put them, the draw list with plain palette pointers, the correction blend in `MotionInterpolator` and for the camera, the audio side's preview voices (keyed calls, the mirror from confirmed calls only, take-over or a 20 ms fade; the ADX effects on SE streams 2-5 as preview streams; BGM, ambience, movie and global commands from confirmed ticks), F1 Online "Input: instant / delayed", the network indicator's rollback line |
| scenarios | L1 and L2 with rollback (20, 40, 80 and 150 ms of round trip; each also with 10 ms of jitter and with 1 % loss; the throttled host), a join into a predicting instance (JP: three members play at 40 ms, a fourth joins), torture runs over the stage set of 6.13.3 and the r25c segments in online rules, an analog-pad run (recorded or the bot driver's), a forced-misprediction run with port events and effects (an achievement and a cry in re-simulated ticks), a frame-time run at 144 Hz, the regression scenarios |

Done when: in L1 at 20, 40 and 80 ms of round trip (quiet and loaded
machine; each without impairment, with 10 ms of jitter and with 1 % loss)
and in L2 both members' round 62 latency (sample to its tick running here)
has a median of at most 17 ms and a 95th percentile of at most 34 ms (today
50, 83 and 117 ms without impairment); at 150 ms the same, or the member is
in delayed mode by its cost; frames with a standing frontier at most 0.5 %
of the presented frames without impairment and at most 2 % with jitter or
1 % loss; in JP no member's frontier stands at the JOIN and the joiner is in
as in round J; own-slot corrections at most 2 % of ticks without loss; every
member's NETFP and STATEHASH equal in every cell and scenario; the torture
test finds no difference over 20,000 ticks per stage and the r25c segments,
also in the poisoning build, the host-write check stays silent and the
statics grep test passes; in the four-player field a rollback at the
members' depth costs at most 3 ms on average and 6 ms at the 99th percentile
on the reference machine with the picture rendered, and a hidden 144 Hz
frame-time run keeps step plus rollback plus render under 6.9 ms at the 99th
percentile; the delayed mode switches on and back under a throttled client
without a desync; the forced-misprediction run delivers every confirmed port
event once and none of an undone tick, and its audio dump starts every
confirmed effect once (sfp and SE streams), fades every unclaimed preview
and never restarts the music; the regression scenarios pass; online off
bit-identical (as round J, the replaced `operator new`, `HostAlloc` and the
stores included).

Depends on: round J (the tick state hash for the torture comparison, the
checkpoints at confirmed ticks); round S4's UDP transport, or its lane
carried here (the frontier's reserve does not cover TCP's loss stalls,
6.13.6); the answers to questions 15 and 16. A visible 144 Hz check only with
the user's OK while he does not play.

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

1. Lobby: solo (the default). 31.10, after the first test over the real
   server: the other members appear as ghost figures there (12.1, round 60).
2. Story while the owner is away: drop (the default).
3. Saving the online character: **also automatically** at stage changes and
   when leaving the room, in addition to the save room (not the default;
   section 9 and round S2 / S5 follow it).
4. Warp to a friend: no, walking only (the default).
5. Two players on one PC online: no, one player per PC (the default).
6. Menus and talks far from the others: (a) only that member pauses, the
   enemies near it freeze too (the default). **Replaced on 2026-10-31**
   after the first field test ("every member plays for themselves, as in a
   real online game"): a member's status window, NPC talks and map recorders
   are its own - only on its screen, they never pause the instance, give no
   protection (its Digimon can be hit), near or far and also alone (5.3,
   round 63).
7. EXP: (a) only members near the kill share it (the default).
8. Play alone in a room: no switch (the default).

### Questions of round 61 (2026-10-31)

9. **Quest rewards of a guest** (8.8). The quest line's first rewards
   (weapons, bits, form unlocks) and the keychain ladder are part of the
   room story today. A guest then cannot take in the room a reward the
   owner already took, and when it takes one the owner has not taken, it
   keeps the item but the owner can never take that one in his own story;
   the guest can take it once more in its own story. Alternative: every
   player keeps its own taken rewards, in any room (the owner never loses
   one to a guest, nobody takes one twice; one more lane). Default: as
   today (room story).
10. **Quests of a guest** (8.8, *Quests*). The quest boards in the lobby
   test the player's own level (16 / 46 / 76 by difficulty), forms and
   cards, and a guest can follow the owner into a quest only by taking the
   same quest. A guest below level 16 therefore cannot follow an owner who
   took a quest. (a) Leave it: quests are side content, each player with
   its own level. (b) For a guest the boards use the higher of its own and
   the owner's level, forms and cards, as in local co-op, where player 1
   decides for the group; it can then take the owner's quest and play it
   with him (only the boards change, nothing is saved to the guest). (c) A
   player may join another's quest without taking it (larger; the quest's
   end and rewards then run in a lobby that never took it). Default: (b).

### Answers of round 61 (user, 2026-10-31)

9. Quest rewards: **per player** - every player keeps its own taken first
   rewards and keychain steps in any room; the owner never loses one to a
   guest and nobody takes one twice (the "hub extras" lane of round G).
10. Quests of a guest: **(b)** - at the boards a guest uses the higher of
   its own and the owner's level, forms and cards; nothing of it is saved
   to the guest.

Also decided on 2026-10-31 (rounds 59 and 63): a room owner without an
online story starts at zero; online, a member's START menu / status window,
talks and recorders open only for that member and never pause the
instance, with no protection (this replaces the answer to question 6).

### Questions of round 64 (2026-10-31)

(Questions 9 and 10 are round 61's and answered. Its answer 9, rewards per
player, means per box with four characters per account: 9.1.9.)

11. **One story per account or per box?** Online, an account gets the four
    boxes of the original (9.1.1). In the original every box also has its
    own story. Should (a) all four online characters share the account's one
    online story (a new character in box 2 plays on in the story the account
    has reached, but takes its own quest rewards and forms, 9.1.9; in
    someone else's room everyone plays the owner's story anyway), or (b) each
    box keep its own story (a new character starts the story from the
    beginning; the room's story is then the story of the box the owner
    plays, chosen when he opens the room)? Default: (a), which rounds 56-61
    already build on; (b) costs a lane in round D and touches the room story
    (8.4-8.8).
12. **What the profile shows to everyone.** Default: a public profile shows
    the current Digimon as the picture, the four boxes, every form with its
    level and values, the equipped gear with its colours, BITS, ¶ Data, the
    cards and an *under review* mark on a flagged box (no details); the
    Device Folder, the Item Bank and BITS Bank, the disks and the history only
    to the owner and staff; a private profile (account page) shows only the
    name. Anything to move between the two?
13. **Local saves on the profile?** The profile shows the online characters
    (they start from zero online, 1.1). The private cloud backup of the local
    save also holds four boxes (for example a level 88 Dorumon). Should the
    profile also show those, marked as "offline save, not checked by the
    server"? Default: no (the backup stays private, as promised on the
    privacy page).
14. **A rule that keeps refusing the same character** (9.1.6, the breaker).
    A refused save sends the member out of the room. If a rule is wrong for
    some honest character, it would do that at every stage change until
    staff act. Default: after two refusals in a row by the same rule, that
    character's saves are kept but marked *under review* and staff are
    alerted (the honest player plays on; a cheater who forges the same thing
    a third time keeps it, marked and visible, until staff restore it).
    Alternative: always refuse (no cheater ever keeps a refused value, an
    honest player hit by a wrong rule cannot save until staff switch the
    rule off or accept his saves).

### Answers of round 64 (user, 2026-10-31)

11. Story: **one story per box (b)**, as in the original - a new character in
   another box starts the story anew; a room uses the story of the box its
   owner plays (the extra lane of round D).
12. Public profile: **the default** (current Digimon picture, the 4 boxes, all
   forms with values, the equipped gear in its colours, BITS, Data, cards,
   "under review"; Device Folder, Item Bank, BITS Bank, disks and history only
   for the owner and staff).
13. Local saves of the cloud backup on the profile: **no** (the default).
14. A rule that keeps refusing: **the breaker** (the default): after two
   refusals in a row by the same rule the box's saves are accepted, marked
   "under review", and staff is warned.

### Questions of round 65 (2026-11-04)

15. **Which comes first?** Round J (a joiner plays within a second instead
    of catching up) is the smaller round and needs nothing of round R.
    Round R (the own character without input delay) is larger: it puts a
    new memory layer under the engine and is tested best with round J's
    state hash. Round R needs the UDP transport for its game frames, which
    round S4 was to bring (revision 2: over TCP every lost packet still
    freezes the predicting picture for about a round trip plus 50 ms); if
    S4 is not done by then, R carries that lane and grows by it. Default: J
    first, then R with UDP. Alternative: R first; the input lag goes sooner,
    joins keep catching up until J, and R's torture test compares only the
    fingerprints until J lands.
16. **Sound effects with rollback.** (a) Effects play at once from the
    predicted ticks, the cries and attack calls on the SE streams included:
    the own attacks and hits are heard in the frame they are seen. When a
    partner's input was predicted wrong, an effect can be cut off after a few
    milliseconds, or come up to one round trip late. (b) Every effect waits
    until its tick is confirmed: never wrong, but always about one round trip
    (50-117 ms at 20-80 ms) after the picture. The music waits in both.
    Default: (a).
17. **Your pad recordings.** How often the prediction fails decides how much
    rollback work there is; the only real recording is a short keyboard game
    (0.5 % of ticks change). A few minutes of normal play in a field, once
    with the gamepad and once with the keyboard, recorded as pad logs (round
    R adds an F1 switch that writes them; nothing but the pads is recorded),
    would replace the assumption of a misprediction in every tick. Can you
    record them when round R starts? Default: round R plans for the worst
    case.

### Answers of round 65 (user, 2026-11-04)

15. Order: **round J first (instant joins and checkpoints), then round R
   (rollback) with UDP.**
16. Sound effects under rollback: **(a) play at once** (a mispredicted
   partner's effect may cut short or come about a ping late); music waits.
17. Pad recordings: **yes** - the user records a few minutes of normal play
   in a field (gamepad and keyboard) when round R starts (F1 switch).

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
  (Round 65 design: measured 1 s per minute of a two-member visit, unjoinable
  after about 29 minutes with two members and 12-14 with four; the user asked
  on 01.11 for it to go. Rounds J and R plan the mid-stage state after all,
  6.13; the classic catch-up stays as the fallback.)
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
- **Own windows and scenes** are new behaviour by the user's decision (5.3):
  a member in its menu or a talk can be defeated meanwhile (its window then
  closes). The window models of a member are copies made in the simulation:
  a model the classification misses (an overlay every member should see,
  used inside a talk) would show only on the talker's screen; the names are
  listed in `online_scenes.cpp` (own_copy_model).
- **Forks** discard the other side's script run; state changes before the fork
  exist on both sides (deliberate, instance-class values travel with the
  line). Two instances of one stage do not merge again before the next stage.
- **Story trust**: a modified client can push story flags into the owner's
  save; plausibility rules limit it, the setter table needs hand entries.
  Key items get setter stages in round G (8.8); a key whose stage the census
  cannot measure keeps no stage check. Round G exempts the owner from round
  59's spacing (his own story; a new room reset it anyway).
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
- **Character rules** (round 64 design, 9.1.6) can refuse an honest save
  when the server's rules and the port disagree (a game rule read wrongly,
  a C# `recalc_player` error for one form). Such a rule does not refuse
  once: a wrong rule fails again at every stage change of that character,
  and each refusal sends the member out of the room. What bounds it: a rule
  refuses only values the upload changed (a state the player did not make,
  from the migration or staff, stays a flag), the breaker flags instead
  after two refusals in a row and alerts staff (question 14), every rule has
  a code and a configured action, refused uploads are kept for staff to
  accept (as a merge, also after the box moved on), and the rules whose
  tables are not yet checked only flag.
- **The bounds** (`exp_bound`, `bits_bound`, `drop_plus`) rest on the
  stages' enemy data and a factor of 10 over one kill a second; D's bot runs
  must stay far below them, and D2 calibrates them from real play.
- **Direct database edits** are powerful: a HeidiSQL edit is checked when the
  server processes it (after two quiet minutes, or at the box's next load or
  upload), not when it is typed; an edit that cannot be applied is reverted
  into a draft, so a mistake never locks the box but can surprise the
  editor (the admin page says why).
- **Merging staff edits into a running session** keeps the player's
  progress, but his game shows the old values until his next room entry;
  what must apply at once needs *Reload now* (9.1.7).
- **Tick state completeness** (round 65 design, 6.13.2-6.13.3): one mutable
  field the serializer misses makes joins and resyncs desync, maybe only
  minutes later (a timer, a Lua slot reused after a collection). No hash of
  the serializer's bytes can see such a field (6.13.3). What bounds it: the
  completeness test from every sampled tick of long runs and of the r25c
  segments with its divergence trials, the size guards that fail the build
  on a new field of a serialized struct, and in play the suspicion rule
  (6.13.4): a desync soon after a rebuild from a checkpoint switches the
  instance to the classic path, keeps both states for the diff tool and
  logs it; `Instances:CheckpointJoins` turns the path off for a build with a
  known hole. A miss whose effect shows later than the suspicion window
  costs one resync from a checkpoint before the second mismatch turns the
  path suspect.
- **The state arena** (round 65 design, 6.13.6) rolls back every byte the
  engine owns. Memory that crosses between engine and host (a moved buffer,
  a static that grows inside a tick, a host-held `shared_ptr` into the
  arena) is corrupted by a restore, and such a fault can show far from its
  cause. What bounds it: the contract of 6.13.6 (host allocators by type,
  host stores for banks and documents, values across as deep copies or
  bytes) with its audit as round R's first task, the host-write check on
  every scope entry, the statics grep test, the poisoning debug build and
  the torture test with the product's split (random wrong predictions at
  random ticks, compared with straight runs by the full tick state hash). If
  the contract cannot be closed, the fallback is the tick state in an
  in-process form with copies of the generated RAMs and a contiguous node
  state block (slower, a refactor of the two largest managers).
- **Rollback costs and corrections.** With analog sticks nearly every tick is
  mispredicted (6.13.1), so the re-simulation of the rollback depth (about
  the round trip in ticks) is the normal case: 1.7-2.8 ms per tick at depth
  3-5 in a four-player field on the reference machine headless, more with
  the picture (the split probe measures it), 5-9 ms on a PC three times
  slower, which then falls back to delayed input. After a stall one
  rollback goes up to 15 ticks deep (about 6-9 ms once). Remote players'
  corrections are visible as a short glide or a late start of an attack;
  effects of an undone tick vanish, and sound effects can be cut or late
  (question 16).
- **Standing frontier.** The frontier on the local clock covers jitter and
  short stalls up to its reserve; over TCP a lost segment (a round trip plus
  50 ms) exceeds it, so without UDP the user's "sometimes 80 ms" would only
  shrink, not go. Round R therefore needs S4's UDP transport and measures
  the frames with a standing frontier in its jitter and loss cells.
- **Message numbers**: rounds 60 and 62 both claimed server type 0x8F
  (PRESENCE2, NETSTAT); round 60 moved PRESENCE2 to clients to 0x90 (NETSTAT
  keeps 0x8F); round 65's numbers (6.13.7) leave 0x8F-0x91 to that
  integration.

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
`settings`, `full`, `busy` (the address has `Relay:MaxConnectionsPerIp` connections). Then frames: type u8, payload
length u32, payload; at most 1 MiB; WELCOME is the first frame.

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
| 0x14 | PRESENCE2 | version u8 (1), the PRESENCE fields, clip str (0-16 of A-Z a-z 0-9 _), flags u8 (1 the clip loops, 2 busy), warp u8, sender ms u32, weapon str (0-24, the same characters); only after a WELCOME with flag 2; a later version appends, within 256 bytes in all (the hub forwards its checked version 1 part at version 1, drops a larger one; round 60, 12.1) |

Server to client:

| Type | Name | Payload |
| --- | --- | --- |
| 0x80 | WELCOME | member u8, inst u32, visit u32, slot u8, mode u8 (0 hub, 1 solo-led, 2 sealed), stage str, last tick u32 (0xFFFFFFFF none), delay u8, max catch-up seconds u16, flags u8 (1 resumable: the member still holds that slot; send RESUME; 2 presence v2: the hub takes PRESENCE2) |
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
| 0x8F | NETSTAT | rtt ms u16 (median of the newest 30 PONG round trips, host-loop stalls out), spread ms u16 (p90 above it), d u8, margin ms i16 (the delay rule's 0.5 % quantile), sealed u32, filled u32, late u32 (the member's current stream), flags u8 (1 in a stream); once per second after PING (round 62; clients skip unknown server frames, so no protocol change; 0x8F is NETSTAT's for good, 6.3 "Message types") |
| 0x90 | PRESENCE2 | member u8, then the PRESENCE2 fields at version 1 (to members that send PRESENCE2; the others get the same member's PRESENCE) |

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
- A CO_ARRIVE's co-arrival list names the instance's other members by slot (empty carries): names only, its engine
  comes from the base's snapshot.
- RESYNC is taken only while sealed; a STATE_DATA that reaches a parked member is dropped. The member's own LEAVE in a
  live SEALED tick means it continues alone (unregistered until its next barrier).
- One NETFP per tick; a JOIN_AT is answered even when its joiner went (the leader leads alone from J+1).
