# Round I1a, integration: progress notes

Branch `int-i1a` from main d0e005b0 (worktree `DW4_PC-wt/int-i1a`). It merges the three lanes of round I1a "the world
without rooms" (SEAMLESS.md 19.25 "Round I1a", its Done-when; 19.30 round 72) and makes the Done-when hold on the
merged tree. Lane notes: `docs/ROUND_I1A_WEB_NOTES.md`, `docs/ROUND_I1A_RELAY_NOTES.md`, `docs/ROUND_I1A_CLIENT_NOTES.md`;
the lanes' contract (outside the repo): `DW4_PC-wt/i1a-contract.md`. Never merged into main from here: the release branch
`rel-0.1.7` took this branch's state and main's Data Overflow O1b (3ad4621e, merged into main as **DW4Online 0.1.7**: ctest
62/62, det.sh 4004f056 / audio 396cb219); 0.1.8 (9fcd0681) added only the X-Forwarded-Prefix change and is live. The
review of 0.1.7 (round fix-019, `docs/ROUND_FIX019_NOTES.md`, DW4Online 0.1.9) re-ran what this round left open (r3's three
failures, `ui`, `--fake`; below).

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
| ctest (code of dc8da0da) | 57/57 | `build/int/ctest3.log` |
| det.sh (DEV=OFF copy of dc8da0da, `build/int/off/dw4_int_off_b.exe`) | run1 and run2 4004f056 / audio 396cb219 | `build/int/off/det_b.log`, `work/dref/checks/det/det_int_i1a_b` |
| save state suite (build b) | 17/17 identical after loading | `build/int/st/st_b.log` |
| r25c (build b) | 10/10 reached, 1,748,011 frames, every save the same story | `build/int/off/r25c_b.log`, run `seg_r25c_int_i1a_b` |
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
- r2b (exe f = dcbececb, `build/int/scen_r2b.log`, run folder `run_20261107_105925`): 17 of 23 passed: **W12** (133 s),
  **W18** (153 s), **W1x** (the counter-proof fails as it must, 229 s), **W22** (3 s), **W25** (242 s), **W26** (322 s),
  **W28** (319 s), **W3** (135 s), **W30x** (counter-proof, 402 s), **W32** (303 s: the whole Fortress walk to stag0205's
  plateau and back, no area_escape), **W4** (179 s), **W4b** (204 s), **W5** (185 s), **W6** (284 s), **W7** (237 s),
  **W7c** (237 s), **W8** (345 s). Failed: W11 (both k5 hosts died on the rock slope; 95dd0979), W19 (`ghosts` reached 35
  with figures fading out; 4814b42b), W30 (only the toast's regex: the place, not the region; a4514875), W30b (b's first
  arrival a NEW of its own, its way home held; a died in dng205b1 after four minutes; 58ee8395), W7b (b home on its own
  story through JOIN_REQ home, 1 s after the script's epoch bound @275: a timing failure, repeated in r2c), W7e (the cut at
  the park, a's return and join held; a's script end ended the instance before b's 900 sealed ticks; dc8da0da).
- r2c (exe g = dc8da0da, `build/int/scen_r2c.log`, run folder `run_20261107_125656`): 4 of 6 passed: **W11** (302 s),
  **W19** (115 s: 30 staying figures, never 31), **W30** (402 s), **W7e** (266 s: the cut at the park, b on at once, a back
  after 20 s joins b in stag0102, still host). Failed: W30b (b's NEW and its way home held again; a, fighting in dng205b1,
  died within 60 s; 2b277403), W7b (the timing failure again: b's control in the lobby at about @276, the bound @275;
  a43028e4 with the measured timeline).
- r2d (exe g, scenarios at a43028e4, `build/int/scen_r2d.log`): **W7b** passed (285 s); W30b: the visited rule held
  (candidates `1:Greyed:0:story_not_here`, a NEW of its own), a reached dng205b2, but b's second arrival was refused as
  another context (COUNTER 2: dng205b1's start.lua sets 205, b's lobby had 0).
- r2e / r2f (W30b alone): the cards' COUNTER 2 (4eb49382) does not reach the lobby (the context stayed 5917e7d34c9798f9),
  reverted in 976801d7, both players visit dng205b1 first; a's second lobby gate stood still (4d510afa: depart).
- r2g (`build/int/scen_r2g.log`): **W30b** passed (333 s).
- **The W set of I1a's Done-when passes**: W1, W3, W4, W4b, W5, W6, W7, W7b, W7c, W7d, W7e, W8, W9, W11, W12, W18, W19,
  W22, W24, W25, W26, W27, W28, W29, W30, W30b (I1a part), W32, and the counter-proofs W1x and W30x fail as they must
  (rounds r2a-r2g above; the scenarios whose files changed after their pass: none).
- r3 (exe g, `build/int/scen_r3.log`, run folder `run_20261107_141607`): the ported regression, every other game scenario:
  **34 of 37 passed** (A1, J1, J5, JB, JR, JS, JS2, L1, L1s, L2, L3, M1, M2, P1, P2, P3, RG7, S1, S11a, S11b, S14p, S14z,
  S16, S17a, S17b, S18, S18b, S19a, S19b, S19c, S2, S21, S3, S5). Failed: RGJ1 (only the rooms' toast line left in its
  expectations: bfc3b0a5), S22a and S22b (round J's checkpoint joins ended their catch-up before the target could leave or
  fork: both now run their server without checkpoint joins, 83647150). Not run again here; the UI scenarios and `--fake`
  were not run in this round.
- **fix-019's r2** (the review of 0.1.7, finding 8; DEV=ON build of fix-019's C++ (main's plus a comment), its server
  0.1.9, `--parallel 1`, worktree `DW4_PC-wt/fix019`: `build/f19/scen_r2a.log` .. `scen_r2e.log`):
  - RGJ1: failed once on the order of its expectations only (the world's toast "You're a guest in <host>'s story." comes
    with the instance's INSTANCE_INFO after the JOIN's decision, not at the Game Start; the room story's hold of 8 s, the
    checkpoint join, READY from it and 4,020 sealed ticks with equal fingerprints held): expected.txt reordered (9a870834),
    then **passed** (r2d).
  - S22a: **passed** (the classic join's target left during the catch-up: the fallback to NEW in a's instance).
  - S22b: **fails, twice** (r2a and r2d, the second with `DW4_TRACE_TICKSTATE_DUMP`): everything it checks holds (b follows
    the instance id through c's fork, its JOIN applies on a's side, a and b equal for 10,680 ticks of fingerprints), but
    the relay logs `statehash_mismatch` (and `checkpoint_suspect`) at the first state hashes after b's classic join (inst 7
    visit 3, ticks 4200 and 4800). `tools/online/tickstate_diff.py` on the dumps: the only difference is beAction2's
    `ActionManager::random_wait_min_` (80535A60, the minimum of the random WAITCHECKs: a 120.0, b 0.0). Every engine has
    0 at stag0101's start and 120 later in it (a WAITCHECK 'R' with 'S' sets it); a normal stage change keeps it (the
    original's global; a and c have 120 in stag0102 from their first hash), but the load snapshot (`StageSnapshot`, the
    state of a classic join, a save state, the fallback) does not carry it, so an engine restored from one has 0. A real
    engine defect, older than round I1a: a classic joiner's random waits after it differ (the RNG stays in step: either
    branch draws once), and the same holds for any restored save state. Not fixed in fix-019 (a server release): the fix
    is the field in `StageSnapshot` (kSnapshotVersion 44, so the tick state's version too), which old and new clients cannot
    mix, so it needs a client release with a protocol rise (I1b's, 19.25). Checkpoint joins (round J's default) carry the
    field in the tick state and are not affected.
  - The UI scenarios (`run.py ui`): 8 of 10 first (accounts, areas, cheats, gate, leave, navigation, offline, unreachable);
    home waited for the screens to close by themselves after Play (the playing screen stays until B or its Play, as areas
    does it) and newchars kept the rooms' guest checks (in the world both are at home on their own stories: b's record holds
    its own phase 2, and a save room save may find nothing new after the stage change into the save room uploaded the
    character): both fixed in the harness (9a870834); both then **passed** (r2e: 10 of 10 over r2b and r2e).
  - `--fake`: **9 of 9 passed** (S1, S2, S3, S5, S16, S17a, S17b, S22a, S22b).

### Findings for later (not changed here)

- The candidates of an arrival with no stage group at its stage compare the instance's own context (`WorldInstance.Context`,
  its first NEW's) while its groups' contexts move with the session values (COUNTER 2 is the dungeon, set by each first
  floor's start.lua): a return-mat arrival into a dungeon floor of an instance whose host entered from a field before any
  dungeon is filtered as another context instead of reaching the visited rule (W30b gives both players COUNTER 2 = 205
  first). Whether the no-group comparison should take the instance's newest context is a design question (19.7.2, 3.7).
- `data/online/stage_graph.json` (`tools/export_stage_graph.py`) records funcStageChange's fourth argument as the point;
  the point is the second (`SETGENERATEPOS I<a1>`), and NEXTSTAGE's last argument is a frame delay. The edges (what
  AreaTableTests and the scopes use) are right, the points are not.
- A host whose relay member was forgotten (back after the grace) gets a new member id, and the client's toast says "The
  server restarted. Back on your own story." (W7b's a) instead of "The instance has ended." (19.6.10's texts follow the
  member id).

## Left

- Nothing of this round: r2a-r2g and r3 ran (above), the release took the final tree (ctest 62/62, det.sh). What the
  review of 0.1.7 found and fixed is round fix-019's (`docs/ROUND_FIX019_NOTES.md`); open from it: S22b's
  `random_wait_min_` (the load snapshot's missing field, above) for the next client release.

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
