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

Server unit tests: 472/472 (`build/f19/unit1.log`). ctest (code of 0d8791d2, Release DEV=OFF): 62/62 (`build/f19/ctest1.log`).
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

## Left

- 9: the load gate (running).
- At the end: unit and integration tests on the final tree.
- Open beyond this round (not fixed: a client release with a protocol rise): S22b's `statehash_mismatch` after a classic
  join, `ActionManager::random_wait_min_` missing from the load snapshot (below).

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
  (`build/f19/cp/unit_{auto,staff,ended,foreign,fallback,removeworld,run}.log`), then restored the files.
