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

## Left (in order)

1. ctest for the engine paths (online_tests: redirect, HOME fork, replace,
   depart home).
2. Wire protocol 49 (wire.hpp/.cpp, golden vectors).
3. Session: world session, member ids, two confirmed stories, owner key,
   settings at a restore / DISSOLVE / guest's NEW, HOME event, INTENT leave,
   JOIN_REQ kinds, INSTANCE_INFO / WORLD_NOTICE / MEMBER_INFO, local way home.
4. App: home screen, Online > Settings, F1 Online tab *Instance* page, the
   status window line, toasts, ghost figures 30, Discord, `--online-play`,
   `DW4_ONLINE_AUTOCHOICE`, `areas.json` format 2 reader, the original
   difficulty dialog online.
5. det.sh twice, r25c, the save state suite on the DEV=OFF build.
