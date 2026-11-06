# Round I1a, lane client - progress note

Branch `feat-i1a-client` (worktree `DW4_PC-wt/i1a-client`) from main d0e005b0.
Scope: SEAMLESS.md 19.25 "Round I1a", lane *client*, plus round 72's client
items (19.30.4's first task, the replace at a guest's NEW, `areas.json`
format 2's reader). The wire contract is SEAMLESS 19.18 / 19.19 and the
shared file `DW4_PC-wt/i1a-contract.md` (outside the repo).

Own stack: mysqld 3633 (not needed by the client lane so far), test ports
57400-57599, loopback only, headless only, own PIDs only.

## Builds and tests

- `build/bld.cmd DIR TESTING [dev|nodev] [--target T]` (Release, Ninja,
  `--parallel 4`, FETCHCONTENT dirs from the main repo's build/Release/_deps).
  `build/rel` = Release without DW4_ONLINE_DEV, with the tests.
- `build/ctest.cmd rel` runs ctest.
- First task: `build/rel/dw4_world_probe.exe DATA guestnew` (log
  `build/meas/guestnew_r4.log`); result in SEAMLESS.md 19.30.4 (M6) and
  19.25.

## Progress

- 12:40 contract: protocol 49 wire layout and the client's REST shapes
  proposed in `i1a-contract.md`.
- 13:50 engine paths (all behind the online rules): `story::replace`,
  `BarrierResult::story_mode` (merge / replace) applied at the top of
  `apply_barrier`, `Engine::redirect_pending_stage` (M2: the stage and the
  generate point, only for a parked DISSOLVE), `ForkKind::home` (exactly the
  mask, no side rule, the give-up's frame), `Engine::online_home(mask)` (the
  home departure: gatedisc.lua's records without SYSsetGateStage),
  `Engine::online_depart_home()` (19.6.10, a solo engine). First task
  measured (M6): the replace at a guest's NEW gives the host's story and
  settings completely; only the departing lobby's history differs.

- 15:40 protocol 49 on the wire (`kOnlineProtocol` 49; the relay
  confirmed the 12:40 layout and its golden vectors are in
  `online_wire_tests` too: settings, STORY, STORY_ACK, BARRIER, WELCOME,
  BARRIER_OK, INSTANCE_INFO, WORLD_NOTICE, MEMBER_INFO, INSTANCE_OP,
  JOIN_REQ, HOME, INTENT leave, PRESENCE out, RESUME; refusal *owner* = 12,
  CLOSE 9 *account*). Session: member ids u32, two confirmed stories (own /
  the visit's owner) and the owner key in STORY, the world part of
  BARRIER_OK (wid, host, arrival toasts once INSTANCE_INFO came), HOME
  events (forks follow their slots), JOIN_REQ kinds, INTENT leave,
  INSTANCE_OP visibility, INSTANCE_INFO / WORLD_NOTICE / MEMBER_INFO with
  the 19.5.6 texts, the local way home after a WELCOME without the
  instance, RESUME with the stage. GameEngineLink: the settings at a
  restore (M1: the snapshot's cheats stay), the host's at a guest's NEW,
  the own at a DISSOLVE (before the module reload), the redirect at
  BARRIER_OK, the original difficulty dialog online (its choice is the own
  difficulty). Tests: online (world_paths_without_data), online_session
  (world_session over the fake hub), online_wire (world_messages); ctest
  56/56.

## Left (in order)

1. (done) ctest for the engine paths.
2. (done) Wire protocol 49.
3. (done) Session.
4. App: home screen, Online > Settings, F1 Online tab *Instance* page, the
   status window line, toasts, ghost figures 30, Discord, `--online-play`,
   `DW4_ONLINE_AUTOCHOICE`, `areas.json` format 2 reader, the original
   difficulty dialog online.
5. det.sh twice, r25c, the save state suite on the DEV=OFF build.
