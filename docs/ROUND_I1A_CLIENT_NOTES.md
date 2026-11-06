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

- 12:35 contract: protocol 49 wire layout and the client's REST shapes
  proposed in `i1a-contract.md`.
- 12:50 engine paths (all behind the online rules): `story::replace`,
  `BarrierResult::story_mode` (merge / replace) applied at the top of
  `apply_barrier`, `Engine::redirect_pending_stage` (M2: the stage and the
  generate point, only for a parked DISSOLVE), `ForkKind::home` (exactly the
  mask, no side rule, the give-up's frame), `Engine::online_home(mask)` (the
  home departure: gatedisc.lua's records without SYSsetGateStage),
  `Engine::online_depart_home()` (19.6.10, a solo engine). First task
  measured (M6): the replace at a guest's NEW gives the host's story and
  settings completely; only the departing lobby's history differs.

- 13:10 protocol 49 on the wire (`kOnlineProtocol` 49; the relay
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

- 13:15 offline proof on the DEV=OFF build of 74e1772a (`build/off/dw4_i1aclient_a.exe`, copied to
  `work/campaign/bin/dw4_i1aclient_a.exe`): det.sh 4004f056 / 396cb219 both runs (`work/dref/checks/det/det_i1aclient_a`);
  r25c 10/10 reached, 1,748,011 frames, every save the same story (`build/off/r25c_a.log`, run seg_r25c_i1aclient_a);
  the save state suite 17/17 identical (`build/st/st_a.log`, own copy of work/dref/checks/st with B= this build dir).
- 13:25 the app on the world: `--online-play [BOX]` (replaces --online-create / --online-join; --online-camera / -light / -fast
  override the prefs for a test run), `online_enter_world` (status, login, POST /world/enter), reconnect tickets from
  POST /world/ticket, leave POST /world/leave; the lobby's home screen (Play: <form> Lv <n> (Box 1), Settings, Account, Log out,
  Online now), Online > Settings (PUT /me/online-prefs), the playing screen = the F1 Online tab's Instance page (host,
  visibility with the host's A, members, Leave instance with its question), the F1 menu opens on Online while the world runs,
  the line under the own status window ("Instance: Public   View: change" / "Instance: Kim's (Public)"), Discord's party
  = the instance's x/4, ghost figures 30 / 32 weapons, `DW4_ONLINE_AUTOCHOICE` (dev: new = BARRIER plan 0xFFFF), the toasts'
  stage names from presence_data. UI script commands: play, settings, wait-instance, visibility, leave-instance.
  ctest 56/56.

- 13:45 ctest online: HOME on real engines (stag0101, a stage group of two: stayer, leaver, an observer with the
  recorded side; the group move; the local way home); the host's exit line; name tags by relation; the UI script's
  wait-instance / wait-members read the last frame's session facts. SEAMLESS.md 19.25 "Built, lane client".
  Both builds compile (build/rel DEV=OFF with tests, build/dev DEV=ON without tests).

- 13:40 `--online-difficulty N` (dev builds) for W12; det.sh on build b (735926bc): 4004f056 / 396cb219 twice
  (`work/dref/checks/det/det_i1aclient_b`); r25c and the save state suite on build b running (`build/off/r25c_b.log`,
  `build/st/st_b.log`).

- 13:45 `DW4_ONLINE_TEST_NO_REPLACE=1` (dev builds): the counter-proof of W1 / W30 the harness asked for (contract 13:45):
  a replace-mode BARRIER_OK brings no story at all.

- 13:55 final offline proof: build b (735926bc): r25c 10/10 reached, 1,748,011 frames (`build/off/r25c_b.log`, run
  seg_r25c_i1aclient_b), the save state suite 17/17 identical (`build/st/st_b.log`); build c (2ff5b91b, the last code
  commit; after b only a DEV-only hook and the env name list changed): det.sh 4004f056 / 396cb219 both runs
  (`work/dref/checks/det/det_i1aclient_c`). ctest 56/56 (`build/ctest_rel6.log`). The DEV=ON build compiles (`build/dev`).

- 14:00 (second agent) Play straight into the own lobby (19.5.1): `src/game/online/title_driver.{hpp,cpp}` drives the
  title's Game Start and the box's Call Digimon from the screens' own waits (TITLE2, LOADCTRL2, the own LOADINTF2
  panel with its slot / box / difficulty helpers; read-only getters in the engine), behind a black "Entering the
  server..." with the title muted; hand-overs: the difficulty dialog (the player's; `--online-difficulty` answers it),
  no online character (Create Digimon), a card message, 90 s without the lobby. Never with `--online-script` or
  `--input` (the campaign bot drives the title there). `DW4_TEST_TITLE_DRIVE=BOX` (dev, offline) drives a run's own
  title through the app's input path (`build/smoke/drive1.log`: k2's card into stagML11 in 1,564 steps; screenshot
  `build/smoke/drive_600.bmp` shows the loading screen). ctest `online_title` (57 checks with the data).
- 14:15 the host's exit line also at a solo stage group's original prompt (exit mat scripts naming only hub stages,
  `chunk_leads_home`; TITLE_TESTS_SCAN found 29 such mats in areas.json's stages) and with the gate disk under the
  disk panel's cursor (`PanelManager::disk_under_cursor`).
- 14:20 "Welcome back, Kim. 14 players online, 2 friends." (GET /world and GET /friends at the lobby; web's I1a
  friend list carries the presence). ctest 57/57 (`build/ctest_rel7.log`).
- 14:30 offline proof on build d (15b80d40, `build/off/dw4_i1aclient_d.exe`): det.sh 4004f056 / 396cb219 both runs
  (`work/dref/checks/det/det_i1aclient_d`); r25c 10/10 reached, 1,748,011 frames (`build/off/r25c_d.log`, run
  seg_r25c_i1aclient_d).
- 14:35 web's trial-merge finding (contract 14:45 / 14:55) built: the online achievements take the world's facts from the
  session (no GET /rooms/world poll, which the world answers 410; owner = own story at home or the host; the instance's
  players; online_join only as a guest in someone else's instance). Contract entry "client 14:40".
- 14:50 final offline proof on build e (ce095e03, `build/off/dw4_i1aclient_e.exe`, the last code change): det.sh
  4004f056 / 396cb219 both runs (`work/dref/checks/det/det_i1aclient_e`); r25c 10/10 reached, 1,748,011 frames, every
  save the same story (`build/off/r25c_e.log`, run seg_r25c_i1aclient_e); the save state suite 17/17 identical after
  loading (`build/st/st_e.log`). Build f (8cd90c31, a comment only; the exe differs from e in the 4 timestamp bytes):
  det.sh 4004f056 / 396cb219 both runs (`det_i1aclient_f`). ctest 57/57 (`build/ctest_rel8.log`).

## Left (in order)

1. (done) ctest for the engine paths.
2. (done) Wire protocol 49.
3. (done) Session.
4. (done) App.
5. (done) det.sh again at the end.
6. (done) SEAMLESS.md 19.25 built notes; the contract decisions (contract file 13:30 entry, 14:40 entry of the
   second agent).
7. (done) Play driving the title's Game Start / Call Digimon behind one loading screen (19.5.1).
8. (done) The host's exit line for a solo stage group's own prompt and the gate disk; "Welcome back" with friends.
9. Not this lane's: the W scenarios with real clients (harness lane after the merge); the half-rate ghosts (M5
   fallback, built only if a weaker GPU measures over the budget); *Choose character* (round D's boxes).

## How to run the tests

- `build/bld.cmd rel ON nodev` then `build/ctest.cmd rel` (57 suites; `online_title` alone:
  `build/rel/dw4_online_title_tests.exe "E:/Games/Emulator/Dolphin-x64/Games/Digimon World 4 - Extracted"`).
- The app's drive without a server (dev build): `DW4_NO_ERROR_BOX=1 DW4_TEST_TITLE_DRIVE=0 build/dev/dw4_game.exe
  --data DATA --smoke-test 2600 --stage stagis10 --card-dir build/smoke/k2card` (k2's card: work/campaign/checkpoints/
  r25c/k2).
- Offline proof: copy `build/rel/dw4_game.exe` to `build/off/dw4_i1aclient_X.exe` and work/campaign/bin; `bash
  work/dref/checks/det.sh <absolute exe path> i1aclient_X`; `cd work/campaign && EXE=dw4_i1aclient_X.exe
  NAME=seg_r25c_i1aclient_X bash segments.sh r25c`; `EXE=<exe> bash build/st/st_all.sh` (own copy of the suite).
