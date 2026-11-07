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

## Test results

| What | Result | Log |
| --- | --- | --- |
| ctest (Release DEV=OFF, before 2a43fa15) | 57/57 | `build/int/ctest2.log` |
| online_session tests after 2a43fa15 | 340 checks | `build/int/sess2.log` |
| server unit (after 8e789072) | 466/466 | `build/int/unit3.log` |
| server integration (before 8d439bbe) | 140 passed, 1 skipped (the certificate-store test) | `build/int/it1.log` |

## Left

- Scenario round r1 (W1, W24, W7b, W7d, W27, W29) running: `build/int/scen_r1.log`.
- The rest of the W set: W3, W4, W4b, W5, W6, W7, W7c, W7e, W8, W9, W11, W12, W18, W19, W22, W25, W26, W28, W30, W30x,
  W30b, W32, W1x; the ported regression (`run.py` without names runs every game scenario; `--fake` the fake ones).
- Open analyses: W6 (b too slow to matz00 before a's gate disc), W7e (the host "taken out of the group" at the barrier),
  W9 / W32 / W30b (the explore bot falls in the Fortress and Dry Land: a form without it).
- ctest again, integration and load tests again, det.sh twice, r25c 10/10, save state suite 17/17 on the final tree.

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
