# Round fix-019: progress notes

Branch `fix-019` from main 9fcd0681 (DW4Online 0.1.8, live), worktree `DW4_PC-wt/fix019`. It fixes the eleven findings of
the adversarial review of 0.1.7 (main 3ad4621e; 9fcd0681 only adds the X-Forwarded-Prefix change) and becomes DW4Online
0.1.9. Never merged into main from here. Data Overflow is out of scope (its ctest suites only stay green).

Own resources: mysqld 3671, ports 59000-59199, build dirs `build/f19/*` (gitignored).

## Done

| # | Finding | What | Test (fails without the fix: `build/f19/cp/unit_<name>.log`) |
| --- | --- | --- | --- |
| 1 | I1a auto-joins the first non-greyed row | `Matching.AutoJoinRow` = `Preselected` (decision 40): only a first row that is the party's or a friend's or has players of its instance at the arrival stage; otherwise New instance | `A_guest_enters_at_a_stage_the_host_story_opened_...` (e, a stranger: own instance; cp `auto`) |
| 2 | staff bypass in automatic joins and co-arrivals | `Matching.Visible(..., staffChoice)` / `Matching.MayEnter`: staff pass the visibility only by their own choice (I1b's prompt, I3's browser); `MaySeeBase`, `MaySeeHost` without the bypass | `Private_and_friends_instances_take_friends_only_a_playing_moderator_too_...`, `A_co_arrival_base_takes_only_arrivals_that_may_see_its_instance` (cp `staff`) |
| 3 | a guest's STORY after END reached the host's keeper | `OnStory`: a guest's values once its instance ends are refused (owner); the host's own stay his | `A_guests_story_after_its_instance_ended_never_reaches_the_hosts_keeper` (staff end and host_left; cp `ended`) |
| 4 | a removed loading joiner whose instance vanished got an own instance by merge | `FallbackNew` -> `FallbackHome`: a joiner without a seat in a live instance goes home from a stage group of nobody's instance (detached, no story in BARRIER_OK, HOME at once, asked again by the housekeeping, `_strays`); `OnStory` refuses STORY from an engine on another's story (detached group, or a group of an instance without the member's seat) whatever the key; `RemoveWorld` detaches its remaining groups (their next barrier went the fresh-arrival way before) | `A_removed_joiner_whose_instance_is_gone_...` (cp `fallback`, `foreign`), `A_removed_members_engine_on_the_hosts_story_writes_no_story_...` (cp `foreign`, `removeworld`) |
| 5 | the fallback re-seated a removed arrival past the bar and the 4 seats | the same `FallbackHome`: no seat is ever taken back by a fallback | `A_removed_joiners_fallback_never_takes_a_seat_back_past_the_bar_or_the_four_seats` (cp `fallback`) |
| 6 | Admin End / Remove addressed only the run-local wid | `RoomHub.RunId` (random per hub), `WorldSnapshot.Run`; `IWorldRelay.EndInstance/RemoveMember(wid, run, ...)`, `StaffEnd/StaffRemove(..., run)`; `WorldService` refuses a missing run (422 `run_required`) and another run (409 `instance_list_outdated`); GET /admin/instances names `run`; the admin page posts it | `The_staff_actions_name_the_relays_run_and_another_runs_wid_is_refused` (cp `run`) |
| 6 (API) | the API and the admin page | `WorldApiTests.Admin_instances_end_and_remove_with_audit_rows` (422 `run_required`, 409 `instance_list_outdated` for another run, 204 with the run), `WebTests.Admin_pages_moderate_accounts_and_unlocks` (the page posts the run; an old run refused) | - |
| 7 | the lost test | `OnlineStoryTests.A_restore_of_a_revision_that_is_not_kept_leaves_the_owners_instance_running` back from feat-i1a-web dcd6f3ae | integration tests with mysqld 3671: 141 passed, 1 skipped (the opt-in certificate-store test), `build/f19/it2.log` |
| 1, 2 (scenarios) | the scenarios whose rules changed | W30, W30x, W6: a and b friends (`api_checks` at 1 s / 2 s: b's request, a's accept, status 200); W29: the fake guests befriend a (run.py `fake_guests.friend_of`); W18: a's instance Public (the playing moderator joins with the Staff tag); W5: the moderator c also takes the gate while a's Private instance runs and gets his own (three instances, no staff_join) | runs below |
| 9 (tool) | the load gate's GC trace | `tests/DW4Online.FakeClient/GcTrace.cs`: the collections (runtime events: generation, reason, kind, the EE pause) and the process's stalls over 20 ms (a highest-priority 1 ms watcher), per cell in `gc_trace` (over sockets the relay process's) | results below |
| 8 | the regression | r2 below: RGJ1, S22a, the UI set and --fake pass (three harness fixes, 9a870834); S22b fails on an engine defect (below) | `build/f19/scen_r2*.log` |
| 10 | documents | README.md's online section for the world; server README (rooms retired 410, migrations through 0013, the world relay, the website, the admin run); DEPLOY.md 9.1 / 9.2 (0.1.6 carried 0012, the server's way 0.1.4 -> 0.1.6 -> 0.1.7 -> 0.1.8 -> 0.1.9) and 10; SEAMLESS 19.7.1, 19.7.2, 19.7.6, 19.6.6, 19.8.5, 19.14, 19.19, 19.25, 19.30.3, 19.32; ROUND_I1A_INT_NOTES (below); `src/game/game_main.cpp`: the comment before `view.gain` at 12 spaces | - |
| 11 | version | `server/DW4Online/Directory.Build.props` 0.1.9. Client code: unchanged (only the comment of finding 10), so 0.1.9 needs no new client | - |

Server unit tests: 472/472 (`build/f19/unit1.log`; on the final tree again `build/f19/unit_final.log`). Integration tests on
the final tree (mysqld 3671): 141 passed, 1 skipped (the opt-in certificate-store test; `build/f19/it_final.log`). ctest (code of 0d8791d2, Release DEV=OFF): 62/62 (`build/f19/ctest1.log`).
det.sh (DEV=OFF copy `build/f19/off/dw4_f19_off_a.exe`, the C++ of every later commit: only the comment changed) twice:
4004f056 / audio 396cb219 in all four runs (`build/f19/det_a.log`, `work/dref/checks/det/det_f19_a1`, `_a2`).

### Scenarios

- r1 (exe a = DEV=ON build of 0d8791d2's C++, server of aeeacd67, `build/f19/scen_r1.log`, run folder
  `tools/online_test/.work/run_20261111_144357`): **17 of 17 passed**: J1 (229 s), M1 (259), W1 (229), W18 (153: the
  moderator joins a's Public instance, role staff, staff_join), W1x (counter-proof fails as it must, 229), W24 (242), W25
  (244), W26 (323), W27 (383), W28 (319), W29 (434: both fakes befriended a, fg1 a guest's NEW, fg2 joined it), W30 (403:
  the friend requests 200 / 200, b's guest's NEW), W30x (counter-proof, 403), W4 (181), W4b (202), W5 (226: three
  instances, c alone at stag0101 while a's Private instance ran, no staff_join), W6 (283).
- r2 (finding 8, the same exe, the server of 3c146cf5 / 9a870834): `scen_r2a.log` RGJ1 S22a S22b, `scen_r2b.log` the UI
  set, `scen_r2c.log` --fake, `scen_r2d.log` RGJ1 S22b again (the S22b copy with `DW4_TRACE_TICKSTATE_DUMP`, untracked,
  removed after), `scen_r2e.log` ui home and newchars again:
  - **--fake 9 of 9**; **S22a passed**; **RGJ1** failed once on its expectations' order only (the world's guest toast comes
    after the JOIN's decision), reordered (9a870834), then **passed**; **UI 10 of 10** after two harness fixes (home: the
    playing screen stays open after Play until B, as areas does it; newchars: the rooms' guest checks replaced by the
    world's, both players at home on their own stories, a save room save that found nothing new is fine; 9a870834).
  - **S22b fails** (twice, r2a and r2d): everything it expects holds, but the relay logs `statehash_mismatch` and
    `checkpoint_suspect` after b's classic join (inst 7 visit 3, ticks 4200 / 4800). The tick state diff
    (`tools/online/tickstate_diff.py`, dumps in `build/f19/dump_s22b`): beAction2's `ActionManager::random_wait_min_`
    (80535A60, the random WAITCHECK minimum) is 120.0 on a and c and 0.0 on b. It starts at 0, a WAITCHECK 'R' with 'S'
    sets it during stag0101 (all three engines 120 by tick 10200), a normal stage change keeps it (the original's global),
    but `StageSnapshot` (the classic join's state, a save state, a fallback) does not carry it: a restored engine has 0.
    An engine defect older than I1a, also in single player (a loaded save state); the RNG stays in step (both branches
    draw once), the waits differ. The fix is the field in the snapshot (kSnapshotVersion 44, the tick state's version with
    it), which old and new clients cannot mix: a client release with a protocol rise (I1b's). Not done in this server-only
    round; recorded in ROUND_I1A_INT_NOTES and here.

### The load gate (finding 9)

FakeClient Release (`build/f19/fc`, with `GcTrace`), `DOTNET_gcServer=1`, 3 s warm-up, 30 s measured (joins 60 s); results
in `build/f19/gate/` (`inproc_r1.json`, `net_r1.json`, `net_storm_r2..r4.json`, `inproc_storm_r2..r4.json`, `summ.py` prints
them; `machine_load.csv`). **Not a quiet machine**: nothing of this round ran beside it, but the I1b workflow's scenarios
did (2-8 `dw4_*` game clients at every sample, total CPU median 25 %, p90 35 %, max 100 %, 207 samples of 5 s). So this is
**not the run the review asked for**; the run on a quiet machine stays open and needs a window without the I1b workflow.

**Verdict** (corrected by the refix, finding 6 of the review of fix-019): over sockets the gate **passes** (relay CPU,
sealing lateness p99 under 2 ms). In-process the hold passes, but the **sealing lateness misses the 2 ms p99 criterion of
19.17.4 as written** in every cell with the sealer's TryEnter: p99 3.05-6.63 ms (`inproc_r1`: base 5.44, storm 6.63,
sealonly 4.86, joins 4.67; `inproc_storm_r2..r4`: 6.38 / 3.05 / 3.42), as in r71. The explanation (the in-process
harness's coincident timer wakes; the waiting sealer's cells stay under 0.6 ms, and over sockets the same relay passes) is
the r71 one, not a measurement that lifts the criterion: the earlier summary's "in-process passed" was wrong.

- In-process (`loadgate`, 8 cells): every cell valid. Hold p99 14.5-20.2 us (at most 0.5 ms: pass), p99.99 0.37-1.27 ms
  (at most 5 ms, decision 42: pass), max 2.4-11.8 ms. Sealing lateness with the sealer's TryEnter p99 4.67-6.63 ms in r1,
  3.05-6.38 ms in the storm runs (**missed**: 2 ms; the in-process harness's coincident timer wakes, as in r71 / I1a), with
  the waiting sealer (base-wait, storm-wait) p99 0.53-0.59 ms. Process CPU 0.02-0.08 cores. GC trace: 7-51 collections per cell run, the longest pause 2.5-4.9 ms, no
  stall of the process over 20 ms.
- Over sockets (`loadgate --sockets`, 7 cells, the relay in its own process): every cell valid. Relay CPU 0.15 (select),
  0.23 (fullhub), 0.34-0.40 (base, sealonly, storm, the waits): under half a core in every cell (pass). Sealing passes
  over 2 ms 0.07-0.24 % (p99 under 2 ms: pass), TryEnter misses 0.2-1.1 %. Relay GC trace: 3-11 collections per 30 s,
  pause total 6-43 ms, the longest 2.3-8.1 ms; one stall of the relay process over 20 ms in all runs (sealonly, 21.1 ms,
  7.0 ms of it a GC pause).
- The storm cell again with the GC trace (`storm` and `storm-wait`, three more runs each way): over sockets relay 0.38-0.45
  cores, passes over 2 ms 0.17-0.24 %, the relay's lateness max (since its start) 10.6-13.9 ms, GC pauses at most
  4.6-7.8 ms, **no stall over 20 ms**: the 1.34 s stall of round I1a's first storm run did not come back in 5 storm and 5
  storm-wait runs over sockets, so its cause stays unknown (the trace would now tell a GC pause from a stall of the
  process). In-process: hold p99.99 0.52-1.23 ms; one single `Seal` hold of 34.6 ms (storm r4) with no GC pause over
  3.4 ms and no stall of the process (the highest-priority watcher kept waking): the sealing thread preempted while it
  held the lock on the loaded machine, under the p99.99 reading.

## The refix (the review of fix-019)

The review of fix-019 (head 43603e57) found seven points (one should-fix, six minor). Each code fix has a unit test that
fails without it (`build/f19/cp/unit_refix_{seat,hoststory,here,context}.log`: the script reverses one fix in the working
tree, runs the unit tests, restores the files).

| # | Finding | Verdict | What | Test |
| --- | --- | --- | --- | --- |
| R1 | the JOIN path re-seats a removed member past the bar and the four seats (in 0.1.8 too) | fixed | `Remove()`: a fresh arrival held for its automatic JOIN (JOIN or boss hold) of that instance leaves the hold and decides alone (`DecideAlone` -> `FreshJoinOrNew`, the bar counts via `Visible()`), its HOME wish dropped (its engine never entered); `Join()` never takes a seat: a fresh member without one decides alone, a member removed at its barrier joins seatless and `RetryHome` sends it home (as from a NEW) | `A_fresh_arrival_removed_during_its_join_hold_decides_again_alone_and_never_takes_its_seat_back` (the review's P1 and P5: no fifth seat, one history row, one "joined"), `A_member_removed_at_its_barrier_joins_without_a_seat_and_its_story_and_context_are_a_guests` (cp `seat`) |
| R2 | the detached host's own STORY refused as foreign (a regression of fix-019) | fixed | `HubInstance.DetachedFrom` (set by `Detach`, `RemoveWorld`, `FallbackHome` from `JoinState.World`): `OnForeignStory` excepts the former host of a detached group (the story is his own) | `A_detached_groups_former_host_keeps_his_own_story_and_his_home_context` (the review's P6; cp `hoststory`) |
| R3 | "Here" counts players leaving the arrival stage (in 0.1.8 too) | fixed | `Candidates()`: Here = the members of the stage groups that stay at S (the `atStage` filter: no barrier, not departing, not log-full) and their joiners; never a group at a barrier to elsewhere, never a lone player | `Players_on_their_way_elsewhere_are_not_here_and_a_stranger_gets_an_instance_of_his_own` (the review's P2; cp `here`) |
| R4 | after `FallbackHome` client and relay read "home, own story" until HOME | fixed (relay); client unchanged | `ContextOf()`: an engine on another's story without a seat there (`OnForeignStory`: removed, a detached or seatless group, a JOIN whose seat went) is that instance's guest (its wid and history id, IsHost false, its settings, its group's players), so the API refuses story challenges and takes the instance's ruleset. The 0.1.8 client's own facts still say home (its BARRIER_OK has wid 0; changing that is a client matter); the claim is checked server-side. The review's "the client ignores JOIN_REQ outside solo" does not hold here: the fallback's BARRIER_OK restores the fallback and begins the visit in the same call (`Session::on_barrier_ok` -> `begin_visit`, mode solo), so the JOIN_REQ home that follows is answered (the unit test and W10s) | `A_fallback_from_nobodys_instance_plays_as_the_former_instances_guest_until_its_way_home` (cp `context`); W10s with real clients (below) |
| R5 | staff join only on the unit level | accepted, documented | no code: in 0.1.9 a moderator enters Private / Friends instances only as the host's friend (W18 runs Public); for I1b: pass `staffChoice` into the prompt's candidates, never preselect a staff-only row (SEAMLESS 19.7.2, 19.25) | - |
| R6 | the load gate's report | accepted, documented | the verdict above: sockets pass, in-process misses the lateness as written, the quiet-machine run is open (also SEAMLESS 19.17.4) | - |
| R7 | `random_wait_min_` missing from the StageSnapshot (older, live) | accepted for I1b | not in this server-only round: SEAMLESS 19.25 puts it into I1b's client release (`kSnapshotVersion` 44 with the protocol rise) with S22b as a done criterion | S22b (fails on 0.1.8 and here) |

W10s (new, `tools/online_test/scenarios/W10s`): S22a's classic catch-up (b at `DW4_ONLINE_RATE` 200) with a staff removal of
b at @334 s through `POST /admin/instances/1/remove?run={run}` (run.py's new `{run}`: the run GET /admin/instances names),
then a leaves stag0102: b's fallback is `fallback_home` (wid 0), `home_req` reason Staff, b's way home a DISSOLVE with the
replace; never `instance_join how=join` for b.

Documents: SEAMLESS 19.6.6, 19.7.2, 19.7.3, 19.8.5, 19.8.9, 19.17.4, 19.24 (W10s), 19.25 (I1b's fixed scope), 19.32; README
(the automatic choice). Server unit tests 477/477 (`build/f19/unit_refix.log`), integration tests 141 passed, 1 skipped
(`build/f19/it_refix.log`).

## Left

- Open beyond this round (not fixed: a client release with a protocol rise, I1b's): S22b's `statehash_mismatch` after a
  classic join, `ActionManager::random_wait_min_` missing from the load snapshot ("Scenarios", r2, above; SEAMLESS 19.25).
- The load gate on a quiet machine (a window without the I1b workflow).
- For the user: in 0.1.9 moderators cannot enter Private or Friends instances (except as the host's friend) until I1b's
  prompt passes the staff choice.
- For I1b: `staffChoice` in the prompt's candidates, `Preselected` never on a staff-only row; *Arriving now* for a group at
  a barrier for the arrival stage (its members are not *Here*).

## How to run

- C++: `cmd //c "start /low /wait /b cmd /c build\f19\bld.cmd rel ON nodev"` (Release, DEV=OFF, tests) and
  `... bld.cmd dev OFF dev --target dw4_game` (DEV=ON game for the scenarios); `cmd //c "build\f19\ctest.cmd rel"`.
- Environment: `source build/f19/env.sh` (DOTNET, DW4ONLINE_TEST_MYSQL on 3671, DW4ONLINE_TEST_PORTS 59000-59199, PY).
- Server: `"$DOTNET" build --disable-build-servers` in `server/DW4Online`; `"$DOTNET" test tests/DW4Online.Tests --disable-build-servers`;
  the integration tests the same with `tests/DW4Online.Integration.Tests` and a mysqld on 3671 (`bash server/DW4Online/.local/mysqld.sh`
  in the background, gitignored, made with XAMPP's `mysql_install_db.exe`; SQL `SHUTDOWN` after; never together with a scenario
  run, whose harness starts its own mysqld on the same port). The WinHTTP end-to-end client: `tests/winhttp_client/build.cmd`.
- Scenarios: `bash build/f19/scen.sh LOG TAG SCENARIO...` (run.py with --parallel 1 --keep, mysqld 3671, ports
  59000-59199, the r25c cards, the protocol 48 client `build/f19/old48/dw4_old48_dev.exe`, the game exe
  `build/f19/scen/dw4_f19_dev_TAG.exe`; `ui` or `--fake` instead of scenario names for those sets; `--no-build` after the
  first run). det: `bash work/dref/checks/det.sh EXE NAME` from the main repo.
- Counter-proofs: a script outside the repo applied each fix's reversal to the working tree, built and ran the unit tests
  (`build/f19/cp/unit_{auto,staff,ended,foreign,fallback,removeworld,run}.log`; the refix's
  `unit_refix_{seat,hoststory,here,context}.log`), then restored the files.
