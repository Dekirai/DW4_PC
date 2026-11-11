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
| 6 | Admin End / Remove addressed only the run-local wid | `RoomHub.RunId` (random per hub), `WorldSnapshot.Run`; `IWorldRelay.EndInstance/RemoveMember(wid, run, ...)`, `StaffEnd/StaffRemove(..., run)`; `WorldService` refuses a missing run (400 `run_required`) and another run (409 `instance_list_outdated`); GET /admin/instances names `run`; the admin page posts it | `The_staff_actions_name_the_relays_run_and_another_runs_wid_is_refused` (cp `run`) |
| 6 (API) | the API and the admin page | `WorldApiTests.Admin_instances_end_and_remove_with_audit_rows` (422 `run_required`, 409 `instance_list_outdated` for another run, 204 with the run), `WebTests.Admin_pages_moderate_accounts_and_unlocks` (the page posts the run; an old run refused) | - |
| 7 | the lost test | `OnlineStoryTests.A_restore_of_a_revision_that_is_not_kept_leaves_the_owners_instance_running` back from feat-i1a-web dcd6f3ae | integration tests with mysqld 3671: 141 passed, 1 skipped (the opt-in certificate-store test), `build/f19/it2.log` |
| 1, 2 (scenarios) | the scenarios whose rules changed | W30, W30x, W6: a and b friends (`api_checks` at 1 s / 2 s: b's request, a's accept, status 200); W29: the fake guests befriend a (run.py `fake_guests.friend_of`); W18: a's instance Public (the playing moderator joins with the Staff tag); W5: the moderator c also takes the gate while a's Private instance runs and gets his own (three instances, no staff_join) | runs below |
| 10 (part) | indentation | `src/game/game_main.cpp`: the comment before `view.gain` at 12 spaces | - |
| 11 | version | `server/DW4Online/Directory.Build.props` 0.1.9. Client code: unchanged (only the comment of finding 10), so 0.1.9 needs no new client | - |

Server unit tests: 472/472 (`build/f19/unit1.log`). ctest (code of 0d8791d2, Release DEV=OFF): 62/62 (`build/f19/ctest1.log`).

## Left

- 8: RGJ1, S22a, S22b, ui, --fake on a DEV=ON build; 9: load gate; 10: docs (README, server README, DEPLOY 9.2,
  ROUND_I1A_INT_NOTES, SEAMLESS 19.25 / 19.7.1 / 19.6.6 / 19.14).
- Scenarios: W1, W1x, W4, W4b, W5, W18, W24-W29, W30, W30x, J1, M1 (and W6, whose guest's NEW needs a friend now).
- det.sh twice.

## How to run

- C++: `cmd //c "start /low /wait /b cmd /c build\f19\bld.cmd rel ON nodev"` (Release, DEV=OFF, tests) and
  `... bld.cmd dev OFF dev --target dw4_game` (DEV=ON game for the scenarios); `cmd //c "build\f19\ctest.cmd rel"`.
- Environment: `source build/f19/env.sh` (DOTNET, DW4ONLINE_TEST_MYSQL on 3671, DW4ONLINE_TEST_PORTS 59000-59199, PY).
- Server: `"$DOTNET" build --disable-build-servers` in `server/DW4Online`; `"$DOTNET" test tests/DW4Online.Tests --disable-build-servers`;
  the integration tests the same with `tests/DW4Online.Integration.Tests` and a mysqld on 3671 (`bash server/DW4Online/.local/mysqld.sh`
  in the background, gitignored, made with XAMPP's `mysql_install_db.exe`; SQL `SHUTDOWN` after; never together with a scenario
  run, whose harness starts its own mysqld on the same port). The WinHTTP end-to-end client: `tests/winhttp_client/build.cmd`.
- Counter-proofs: a script outside the repo applied each fix's reversal to the working tree, built and ran the unit tests
  (`build/f19/cp/unit_{auto,staff,ended,foreign,fallback,removeworld,run}.log`), then restored the files.
