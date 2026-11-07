# Round I1a, integration: progress notes

Branch `int-i1a` from main d0e005b0 (worktree `DW4_PC-wt/int-i1a`). It merges the three lanes of round I1a "the world
without rooms" (SEAMLESS.md 19.25 "Round I1a", its Done-when; 19.30 round 72) and makes the Done-when hold on the
merged tree. Lane notes: `docs/ROUND_I1A_WEB_NOTES.md`, `docs/ROUND_I1A_RELAY_NOTES.md`, `docs/ROUND_I1A_CLIENT_NOTES.md`;
the lanes' contract (outside the repo): `DW4_PC-wt/i1a-contract.md`. Never merged into main from here.

Own resources: mysqld 3641 (`server/DW4Online/.local` for the integration tests, gitignored; the scenario harness starts
its own on the same port from `tools/online_test/.work`, so never both at once), ports 57600-57799, build dirs
`build/int/*` (gitignored).

## Done

| # | Step | Commit |
| --- | --- | --- |
| 1 | `git merge --no-ff feat-i1a-web` (dcd6f3ae): no conflict | 796514c1 |
| 2 | `git merge --no-ff feat-i1a-client` (ff715a62): conflict docs/SEAMLESS.md 19.25 only (both Built paragraphs kept, web's first) | e2dca7c8 |
| 3 | `git merge --no-ff feat-i1a-relay` (1f3df94d): the contract's resolutions (web 14:20) with web's `resolve_trial.py` (copy in `build/int/`): the 4 Domain stubs -> web's, ServerApp.cs web's block + the relay's WorldHubOptions, OnlineStoryTests (the relay's deletions), WorldRelayTests (`fakeWorld: false`, tickets through `WorldService.EnterAsync`); RelayGateway.cs merged cleanly without RoomSweeper | 4b3d8a82 |
| 4 | Production example `Play:MinimumProtocol` / `CurrentProtocol` 49 / 49 (web's 93261454); DEPLOY.md's two older protocol notes name 49 | c91bf976 |
| 5 | Built: server (0 warnings), C++ Release DEV=OFF with tests (`build/int/rel`), DEV=ON game (`build/int/dev`) | - |
| 6 | RELAY W7b (contract web 16:45): a HOME stamped beyond a stage group's LEAD_FROM goes to the solo leader as JOIN_REQ home (`TrySoloHome`); unit test `A_lost_hosts_end_reaches_the_guest_that_leads_alone_after_his_leave` fails without it (`build/int/cp_w7b.log`) | 8d439bbe |
| 7 | CLIENT W24 (contract web 16:05): CLOSE 8 *stopping* reconnects like a lost link (the new process's WELCOME without the wid starts the local way home); session test `close_stopping_reconnects` (counter-proof `build/int/sess_cp_w24.log`) | 2a43fa15 |
| 8 | CLIENT W7d (contract web 16:40): while the pacer runs a backlog at once, pads for ticks already sealed go 32 to an INPUT (2,761 single frames after a 45 s cut broke the relay's frame budget 400/s, burst 2,000: CLOSE abuse); session test `long_cut_resume_pads` (counter-proof `build/int/sess_cp_w7d.log`: 2761 frames for 2761 pads) | 2a43fa15 |
| 9 | HARNESS W27 (contract web 17:45): the lone defeated host held START (`giveup`), which pauses the original game over (offline too); measured with `dw4_world_probe homeover` cases 14-19 (`build/int/meas/`): stag0101 solo defeat -> stagML11 at +546 (online and offline, also after a HOME took the living partner), with START held no stage change in 6,000 frames. a.script idles instead; expectations unchanged | 91cecbbd |
| 10 | HARNESS / RELAY log W29 (contract web 17:40): the relay's `instance_start` names the instance's session context; FakeClient `--context HEX` reports it instead of the stand-in's; the harness passes the host's to the fake guests (the relay matches arrivals only by context, 19.7.2) | 8e789072 |
| 11 | HARNESS W1 / W24: the own-story card check accepts the keeper's rebased own story (flag 503 derived from COUNTER 7 etc., StoryMerge.Rebase): the card carries the session's own confirmed story since the client review fix 79aaf163 | 791fbe6b |
| 12 | CLIENT W6 / W7b: a sealed stretch whose events carry LEAD_FROM within it (JOIN_REQ home, orphan JOIN_AT: the relay seals J alone) runs without the pacer's full playout buffer; W6's guest froze at J for the rest of the run. Session test `solo_leader_home` + the fake hub's JOIN_REQ home (counter-proof `build/int/sess_cp_w6.log`) | 7d6ad52a |
| 13 | CLIENT / HARNESS W7e: `DW4_ONLINE_TEST_CUT_AT_PARK=SECONDS` (DEV builds; on the online env list) cuts the link at the member's first park with another member and holds the reconnect; W7e uses it instead of the timed proxy cut (which missed the park: the route stalled, the cut hit a sealed field), a idles at the entrance until b joined | 8b3c332a |
| 14 | HARNESS W9: the online script's `depart STAGE [POINT]` (DEV builds only, once, only alone in the stage group) posts an exit's departure records; W9's b departs into stagML02 point 1 from stag0101 after a went into dng101b1 (the Fortress walk to staggf03's math00 killed every test character) | 72f7b90e |
| 15 | W27 corrected (1ce57d09 over 91cecbbd): the game over's message waits for A (funcOneMessage for player 0), START is irrelevant; probe cases 18-21 (`build/int/meas/ho_c18..21.err`): START alone, nothing: no stage change in 6,000 frames; START with A every 40 frames: stagML11 at 1766. W27's host uses `talk` (A now and then without a player) | 1ce57d09 |
| 16 | RELAY W32: `AreaTable.InScope` (stag0205's plateau is in Death Valley's scope, 19.30.2, as AreaTableTests pins); the relay's area_escape check uses it. Test `The_Fortress_and_stag0205s_plateau_are_in_Death_Valleys_scope` (counter-proof `build/int/cp_w32.log`) | 4b6bfc2e |
| 17 | HARNESS: the disk action walks the L panel by its cursor (`PanelManager::disk_under_cursor`): a k10's 8-row panel never reached the Gate Disk with the counted walk (contract web 16:30) | ba4a2f8e |
| 18 | HARNESS W30b / W32: the ways through Dry Land and the Fortress by `depart` with the exits' own targets; W30b no longer `requires: dry_land_walk`; W32 walks the design's whole way to stag0205's plateau and back | 7354806d |

## Test results

| What | Result | Log |
| --- | --- | --- |
| ctest (Release DEV=OFF, before 2a43fa15) | 57/57 | `build/int/ctest2.log` |
| online_session tests (7d6ad52a) | 346 checks | `build/int/sess4.log` |
| online_script tests (7354806d) | 292 checks (every scenario script loads) | - |
| server unit (4b6bfc2e) | 467/467 | `build/int/unit4.log` |
| server integration (4b6bfc2e, mysqld 3641) | 139 passed, 2 skipped; the skipped Python spike passes with DW4ONLINE_PYTHON (EndToEnd 3 passed, 1 skipped: the certificate-store test, opt-in) | `build/int/it2.log`, `build/int/it2_e2e.log` |
| load gate in-process (FakeClient Release `loadgate --seconds 30`, busy machine) | every cell valid; hold_all p99 13.5-19.2 us, p99.99 0.50-1.19 ms, max 3.6-24.2 ms | `build/int/gate/loadgate49_r1.{json,log}` |
| det.sh (DEV=OFF copy of 8b3c332a, `build/int/off/dw4_int_off_a.exe`) | run1 and run2 4004f056 / audio 396cb219 | `build/int/off/det_a.log`, `work/dref/checks/det/det_int_i1a_a` |
| r25c (same exe, `segments.sh r25c 4`) | 10/10 reached, 1,748,011 frames, every save the same story | `build/int/off/r25c_a.log`, run `seg_r25c_int_i1a_a` |
| save state suite (same exe, own copy `build/int/st`) | 17/17 identical after loading | `build/int/st/st_a.log` |

### Scenario rounds

- r1 (exe b = 2a43fa15, `build/int/scen_r1.log`, run folder `tools/online_test/.work/run_20261107_085349`): W1 and W24
  failed only on the own-story card check (both clients home, the rest held) -> fix 11. W27 (idle host) stayed defeated in
  stag0101 for 1,000 ticks -> fix 15. The round was stopped there (own PIDs, mysqld SHUTDOWN "Shutdown complete") to rerun
  with the later fixes.
- r2a (exe e = 7354806d, `build/int/scen_r2a.log`, run folder `run_20261107_092351`): 6 of 11 passed: **W1** (229 s),
  **W24** (243 s), **W27** (394 s), **W29** (437 s: online_host with two fake guests in a's instance, b's online_join),
  **W7d** (262 s: the resume after 45 s, still host), **W9** (704 s: the redirect into stagML11 point 3, own story).
  Failed: W30b (b stood on stagML11's mate02 without a prompt: GATESTAGE needs dng205b2's Security Gate; reworked in
  1f2c9f16), W32 (depart into staggf04 point 2, the graph's wrong point: SQ_DEMOWAIT for good; 10a0eb8b), W6 (b, k9,
  died on stag0101's rock slope; reworked in 047fcdeb), W7b (END host_lost, HOME through JOIN_REQ home at J 10560,
  LEAD_FROM 10561 all came, but b, k2, had died on the rock slope during the cut; ce573b30), W7e (the cut hook never saw
  the park: loopback's BARRIER_OK came before the next pass; dcbececb; the rest of W7e held without a cut). Stuck clients
  of W30b, W32 and W7b were ended by PID to move on.
- r2b (exe f = dcbececb): W6 W7b W7e W30b W32 and W1x W3 W4 W4b W5 W7 W7c W8 W11 W12 W18 W19 W22 W25 W26 W28 W30 W30x,
  `build/int/scen_r2b.log` (running).

### Findings for later (not changed here)

- `data/online/stage_graph.json` (`tools/export_stage_graph.py`) records funcStageChange's fourth argument as the point;
  the point is the second (`SETGENERATEPOS I<a1>`), and NEXTSTAGE's last argument is a frame delay. The edges (what
  AreaTableTests and the scopes use) are right, the points are not.
- A host whose relay member was forgotten (back after the grace) gets a new member id, and the client's toast says "The
  server restarted. Back on your own story." (W7b's a) instead of "The instance has ended." (19.6.10's texts follow the
  member id).

## Left

- Round r2a's results; then r2b: W1x W3 W4 W4b W5 W7 W7c W8 W11 W12 W18 W19 W22 W25 W26 W28 W30 W30x; then the ported
  regression (the other game scenarios, `--fake`, `ui`).
- At the end on the final tree: ctest, det.sh, r25c, the save state suite again (C++ changed after 8b3c332a: the online
  script's depart and disk actions, the probe).

## How to run

- Environment: `source build/int/env.sh` (DOTNET, DW4ONLINE_TEST_MYSQL on 3641, DW4ONLINE_TEST_PORTS 57600-57799, PY).
- C++: `cmd //c "start /low /wait /b cmd /c build\int\bld.cmd rel ON nodev"` (Release, DEV=OFF, tests) and
  `... bld.cmd dev OFF dev --target dw4_game` (DEV=ON game for the scenarios); `cmd //c "build\int\ctest.cmd rel"`.
- Server: `"$DOTNET" build --disable-build-servers` in `server/DW4Online`; `"$DOTNET" test tests/DW4Online.Tests
  --disable-build-servers`; the same for `tests/DW4Online.Integration.Tests` (needs mysqld 3641:
  `bash server/DW4Online/.local/mysqld.sh` in the background, SQL `SHUTDOWN` after).
- Scenarios: copy `build/int/dev/dw4_game.exe` to `build/int/scen/dw4_int_dev_<TAG>.exe`, then
  `bash build/int/scen.sh LOG TAG W1 W24 ...` (run.py with --parallel 1 --keep, the protocol 48 client
  `build/int/old48/dw4_old48_dev.exe` (a copy of web's int71 DEV build), the r25c cards, mysqld 3641, ports 57600-57799;
  add `--no-build` after the first run). Run folders: `tools/online_test/.work/run_*`.
- Probe: `build/int/rel/dw4_world_probe.exe DATA homeover N`.
