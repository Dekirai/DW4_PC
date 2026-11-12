# Round I1b, lane moderation: progress notes

Branch `feat-i1b-mod` from `int-i1a` bfc3b0a5 (worktree `DW4_PC-wt/i1b-mod`). The lane of SEAMLESS.md 19.25 "Round I1b"
called *moderation*: INSTANCE_OP *remove* with the bar, the owner switch and the detachment (19.6.6), the host's block as a
removal (19.11), *Report player* (REPORT on the wire, `player_reports`, `POST /reports`, `/admin/reports`, the audit rows,
19.16, 19.19, 19.20) and the player card's *Report* and *Remove* in the client (the F1 Online tab's *Instance* page, 19.5).
Scenarios W10, W10b, W10c. The decisions shared with the lanes *prompt* and *go* are in the contract file
`DW4_PC-wt/i1b-contract.md` ("moderation 16:25"); the I1a decisions it builds on in `DW4_PC-wt/i1a-contract.md`.

Own resources: mysqld 3652 (`server/DW4Online/.local`, gitignored, root password in `mysql-root.txt`; the scenario
harness starts its own on the same port from `tools/online_test/.work`, so never both at once), ports 58200-58399, build
dirs `build/mod/*` (gitignored).

## What I1a already had (and this lane kept)

The I1a relay lane built most of 19.6.6's server side: `OnInstanceOp` remove (host or staff), `Remove` (the bar, WORLD_NOTICE
5, `HomeMember` with the owed HOME, `FreeSeat`: the seat and the story owner at once, so a STORY with the host's key is
refused *owner*), the detachment of a solo-led stage group after `Instances:JoinRequestTries` unanswered JOIN_REQ home,
`StaffRemove` for the admin REST, `Matching.Visible` with the bars. The client had `NoticeKind::removed` and
`InstanceOpKind::remove` in its wire. Tests for these existed in `WorldLifecycleTests.cs`.

## Done

| # | What | Commit |
| --- | --- | --- |
| 1 | `player_reports` (+ `source`) and the view `v_player_reports` in the unreleased `0013_world`, CHECKs, FKs (`instance_id` ON DELETE SET NULL), a COMMENT on every column; `report_reopened` in `v_staff_audit` | 523c153e |
| 2 | Domain `ReportService` (`Reports:PerDay` 10, `RepeatHours` 24, `SharedInstanceHours` 24, `PageSize` 50), `IReportRepository` / `ReportRepository`, `ScrubAsync` deletes an account's own reports; REST `POST /reports`, `GET /admin/reports`, `POST /admin/reports/{id}/done`, `.../reopen` (`RateLimits:ReportsPerMinute` 10) | 523c153e |
| 3 | Relay: REPORT 0x1E (`ReportMsg`), WORLD_NOTICE 14 (`ReportResult`), `OnReport` (member id -> account, also a member that left in the last 30 min; the shared instance's history id; 3 at once then 1/s), `ReportAnswered`; `WorldGateway` hands reports to `ReportService` off the lock; the host's NEW block removes the guest (`Readmitted`); `RemoveBy` with the audit details of web 18:15 (ids only) and the world log line `remove`; `WorldInstanceRow.Barred` in the snapshot, `WorldVisibility` hides a barred row from non-staff | 33baa58e |
| 4 | Website: `/admin/reports` (Open / Done / All, Done, Reopen, the reported player's open count and live instance), the admin index's open count, the account page's "Reports about this player"; tests (API, web, relay over sockets, migration, scrub) | 4cdff6cf |
| 5 | Client: wire REPORT / NoticeKind::report / ReportResult; `Session::remove_member`, `may_remove`, `report`, `member_by_name`; the toasts; the player card on the Instance page (Report with the four reasons, Remove with its question for the host and staff playing); the online script's `remove NAME`, `report NAME REASON`, `setflag N` (DEV); the UI script's `card`, `remove`, `report`; the W10c hook `DW4_ONLINE_TEST_IGNORE_HOME=1` (DEV, on the engine's allowed list); session and wire tests | 4f540be7 |
| 6 | Scenarios W10, W10b, W10c; run.py's `api_checks` take `expect_not` and expand `{user:NAME}` / `{id:NAME}` in the regexes | 9bc4f94f |
| 7 | Integration test: the host's block through `POST /blocks` removes his guest (WORLD_NOTICE removed, the bar, audit `instance_remove` with block) | eaeee787 |
| 8 | The staff list names the instance a report came from (19.20): `GET /admin/reports` rows gain `instanceHost`, `instanceStage`, `instanceStarted`; the website's *Admin > Reports* and the account page show "#id, host's, area, started"; the relay test reports from inside the instance | 81000b02 |
| 9 | UI scenario `card` works the card with the controller (A on Report, the reasons, A on Griefing; right to Remove, the question, A); the UI script's `card` shows the nav cursor as A on the row does (ImGui activates only with a visible cursor); run.py's `run_ui` seeds a client's online story from its card (`"seed_story": true`, else the pad script's gate does not work on an empty story) | 978efd19 |
| 10 | Review 2026-11-11, finding 1: a removed member never takes a seat again (`TakeSeat` refuses under the bar or an owed HOME; `SendRemovedHome` at the removal of a waiting arrival, in `Decide`, at every `CheckHolds` pass and in `Join`: DISSOLVE home with the redirect, the rest decides on; the join's fallback without a seat asks HOME at once); finding 2: REPORT resolves only members the reporter was told about (`HubMember.Introduced`), any other id is "not available" without a name; finding 3: REPORT's bucket 3 + 10/min | 81d1e247 |
| 11 | Findings 3 and 6: the report service answers a reached daily limit without a query for 10 minutes; 403 `own_report` for staff handling a report about themselves (API and website) | 0f4bf17c |
| 12 | Findings 4, 5, 7: README (422 `own_account`, the staff list's instance fields, 403 `own_report`); the card's question without a number, the card's texts in 19.5.6; scenario W10d (removal while the JOIN waits) with the relay's test option `World:TestJoinHoldMs` and the online script's `members=N`; 19.24 names W10d | 5891f581 |
| 13 | Review 2026-11-12, findings 1 and 2: `SendRemovedHome` also sends removed co-arrivals of a holding base home (DISSOLVE of their own parked visit, never CO_ARRIVE); a base left without members re-decides its co-arrivals (`RedecideCoArrivals`, also in I1a's `DetachLocked`: each parks under its own name again); `New()` checks before the places of a NEW inside an instance; five unit tests | 92f6485f |
| 14 | Review 2026-11-12, finding 3: a report about the viewing staff member comes without its reporter and the shared instance (API null, website "a player"); API and website tests; README | 6ad0cbee |
| 15 | Review 2026-11-12, finding 4: SEAMLESS.md 19.25's moderation block on the state after both reviews; 19.6.6 (a removal while waiting at a barrier) and 19.16 (staff abuse) | 6f7f3cd3 |

## Test results

| What | Result | Log |
| --- | --- | --- |
| **Review fixes (2026-11-12, 92f6485f / 6ad0cbee / 6f7f3cd3)** | | |
| server unit (DW4Online.Tests) | 484/484 (the five new tests failed before the fix: `build/mod/unit4_pre.log`, the New() guard's test checked with the guard commented out) | `build/mod/unit4.log` |
| server integration (mysqld 3652) | 143 passed, 3 skipped (the two changed ReportApiTests failed without the fix: `build/mod/it4_reports_pre.log`) | `build/mod/it4.log` |
| ctest, det.sh | not rerun: no engine or client change since d10ad5e0 (57/57, 4004f056 / 396cb219 twice there) | - |
| scenarios W10, W10b, W10c, W10d (exe e, the client is unchanged since 5891f581) | r8 running (started 15:20) | `build/mod/scen_r8.log` |
| **Review fixes (2026-11-11, 588c83a7 / 020025e5)** | | |
| server unit (DW4Online.Tests) | 479/479 | `build/mod/unit3.log` |
| server integration (mysqld 3652) | 143 passed, 3 skipped | `build/mod/it3.log` |
| ctest (Release DEV=OFF, `build/mod/rel`) | 57/57 | `build/mod/ctest3.log` |
| online off: `det.sh` on a DEV=OFF copy (`build/mod/det/dw4_mod_det3.exe`) | 4004f056 / 396cb219 twice | `build/mod/det3.log`, `DW4_PC/work/dref/checks/det/det_i1bmod3` |
| scenarios W10, W10b, W10c (exe e = 5891f581's client) | 3 of 3 passed (484 s, 180 s, 383 s) | `build/mod/scen_r6.log`, run `tools/online_test/.work/run_20261111_151937` |
| scenario W10d | r6: the relay and a held (removal 1.8 s after b's seat, `removed_home hold=join`, DISSOLVE redirected, no `join_decided`), b's first script waited for `mode=parked`, which a script never sees (no updates on the loading screen): fixed in 020025e5; r7 **passed** (422 s) | `build/mod/scen_r7.log`, run `run_20261111_155422` |
| UI scenario `card` (the new question, "Report sent. Thank you." for a known member) | passed | `build/mod/scen_ui4.log`, run `run_20261111_160130` |
| **Before the review** | | |
| server unit (DW4Online.Tests) | 474/474 (81000b02) | - |
| server integration (DW4Online.Integration.Tests, mysqld 3652) | 143 passed, 3 skipped (eaeee787); the touched classes again at 81000b02: 23/23 | `build/mod/it2.log` |
| ctest (Release DEV=OFF, `build/mod/rel`) | 57/57 (978efd19) | `build/mod/ctest2.log` |
| online_session / online_wire / online_script tests | pass (362 / 346398 / 304 checks) | - |
| online off: `det.sh` on a DEV=OFF copy of 978efd19 | 4004f056 / 396cb219 twice | `DW4_PC/work/dref/checks/det/det_i1bmod2` |
| UI scenario `card` | passed (978efd19, exe d) | `build/mod/scen_ui3.log` |
| scenarios W10, W10b, W10c | 3 of 3 passed (978efd19, exe d, round r5) | `build/mod/scen_r5.log` |

### Scenario rounds

- r1 (exe a = 4f540be7, `build/mod/scen_r1.log`): stopped (own PIDs, the harness's mysqld by SQL SHUTDOWN): a's `remove b`
  named the account's short name, which no member has (`member_by_name` found none) and `remove_member` sent 0xFFFFFFFF.
  Fixed in e4dc1739: `remove_member` only for a member INSTANCE_INFO lists; the scripts name the member `{user:b}`,
  which run.py expands to the account name.
- r2 (exe b = e4dc1739, `build/mod/scen_r2.log`, run folder `tools/online_test/.work/run_20261107_164346`): **W10 passed**
  (482 s: the removal at a's 1200 sealed ticks with b, "You were removed from ...'s instance.", HOME reason Removed, b home
  on its own story, b's next arrival `candidates rows=[]` and an instance of its own, the API check as b without a's
  instance while the admin's list had it, audit instance_remove, leave_reason removed). W10b: everything held (b removed while
  defeated went home, a played on 1800 frames alone) but the expected file forbade any `instance_end`, and a's script end
  ends the instance (host_left): the check now expects that end at the end. W10c: everything held (JOIN_REQ home asked 3
  times, unanswered; `story_owner` for the STORY b made 6 s after its removal; `detached` 16 s after the removal; b's
  gate disc a DISSOLVE with its own story) but its gate disc names stagML11 point 3 itself, so no `redirect` line: b now
  leaves by an exit into the field dng101b1 (the depart action), which the relay must redirect home (1b0ab745).
- r3 (exe c = 1b0ab745, `build/mod/scen_r3.log`): **W10b passed** (190 s). W10c failed only because this lane's own
  integration test run shut the harness's mysqld down (SQL SHUTDOWN at 17:07 on port 3652 mid-scenario): never run the
  own mysqld while a scenario runs (both use 3652).
- r4 (exe c, 81000b02, `build/mod/scen_r4.log`): **W10c passed** (383 s).
- UI `card`: ui1 failed at once (an invalid JSON escape `\.` in scenario.json); then both clients stayed in stagML11 (an
  empty online story: the pad script's `explore mate00` never reached a working gate) -> run_ui's `seed_story`; ui2 passed
  with the script shortcuts, but A on Report did nothing (ImGui activates only with the nav cursor visible; the script's
  `card` had not shown it); ui3 (exe d = 978efd19) **passed** with the controller path, screenshots card / reasons /
  reported / question / removed and b's home_after_removal (run folder `tools/online_test/.work/run_20261107_174119`).
- r5 (exe d = 978efd19, `build/mod/scen_r5.log`, run folder `tools/online_test/.work/run_20261107_174801`): **W10 (483 s),
  W10b (197 s) and W10c (383 s) passed** on the final client, 3 of 3.

## Review 2026-11-11 (the I1b review's findings) and what was done

1. should_fix, RoomHub: a removal was undone when the relay seated the removed member again (the JOIN hold, a barrier that
   gathers and ends in a JOIN, the join's fallback). Fixed (81d1e247): `Unseatable` / `TakeSeat` guard, `SendRemovedHome`
   in `Remove`, `Decide`, `CheckHolds`, `Join`, `FallbackNew` without a seat plus `RetryHome`. Tests (fail without the fix
   except the running-join one, which guards the HOME): `A_fresh_arrival_removed_while_its_join_waits_...`,
   `A_member_removed_while_its_stage_group_gathers_at_a_barrier_...`, `A_member_removed_while_its_join_runs_...` (two), and
   W10d with real clients.
2. should_fix, REPORT as a name / presence oracle. Fixed (81d1e247): only members the reporter's connection was told about
   (MEMBER_INFO) resolve, from the reporter's own record (`HubMember.Introduced`), so the answer does not depend on where the
   member is; every other id is "not available" without a name. Test
   `A_report_resolves_only_members_the_reporter_was_told_about_...`; the relay integration test meets the player first.
3. minor, REPORT's bucket 6x REST's. Fixed: 3 at once then 10 a minute (81d1e247), and the service answers a reached limit
   without a query for 10 minutes (0f4bf17c).
4. minor, README 400 vs 422 and the staff list's fields. Fixed (5891f581, contract 15:20); the API test checks the status.
5. minor, the card's texts not in 19.5.6, "10 minutes" fixed in the client. Fixed (5891f581): "They can't rejoin it for a
   while." and a 19.5.6 row for the card.
6. minor, staff handle reports about themselves. Fixed (0f4bf17c): 403 `own_report`, the website hides Done / Reopen.
7. minor, W10's prompt check and a race scenario. W10d added (5891f581); the prompt's `options` check belongs to the I1b
   integration (the prompt lane is not on this branch): noted in the contract (15:20).
8. minor, the flaky `A_resync_starts_from_the_newest_agreed_checkpoint_and_its_ready_never_judges` (base bfc3b0a5). Not this
   lane's code: the prompt lane found its cause (`RoomHub.BecomeMember` stamps `Stopwatch.GetTimestamp()` instead of the
   hub's `now`, contract "prompt 14:25"); the integration fixes it once; a full run that fails only there is repeated once.

## Review 2026-11-12 and what was done

1. should_fix, WorldHub `SendRemovedHome`: removing the only member of a co-arrival base left its co-arrivals undecided
   (no BARRIER_OK, no BARRIER_WAIT; the client's 5 s fallback). Fixed (92f6485f): the emptied base re-decides its
   co-arrivals (`RedecideCoArrivals`). The same helper replaces I1a's loop in `DetachLocked`, which re-decided them but left
   their `HubMember.Arrival` on the gone base: a removal then waited for the next CheckHolds pass, and the co-arrival's own
   leave left its hold with nobody in it (a fault in CheckHolds: NEW of nobody). Tests:
   `Removing_the_only_member_of_a_holding_base_lets_its_co_arrival_decide_on_its_own_and_play_on_in_the_instance`,
   `The_co_arrival_of_a_base_whose_only_member_left_the_world_is_its_own_arrival_for_a_removal`,
   `The_co_arrival_of_a_base_whose_only_member_left_the_world_may_leave_too_without_leaving_an_empty_hold`.
2. should_fix, a co-arrival removed while its base holds co-arrived into the instance without a seat. Fixed (92f6485f):
   `SendRemovedHome` checks the co-arrival groups too (DISSOLVE of the co-arrival's own parked visit, redirected home, own
   story); `New()` checks before the places of a NEW inside an instance. Tests:
   `A_co_arrival_removed_while_its_base_holds_goes_home_from_its_own_barrier_and_never_co_arrives_into_the_instance`,
   `A_NEW_never_places_a_co_arrival_barred_from_the_instance_without_a_seat_whatever_path_led_there` (fails with the guard
   commented out).
3. minor, a moderator saw who reported him. Fixed (6ad0cbee): rows about the viewer have no reporter and no shared instance
   (its host may be the reporter) on the API (null), *Admin > Reports* and the own account page ("a player"); other staff see
   all. Tests in `ReportApiTests` (API: the six fields null for Moddy, set for Mona; website: "a player", no link to the
   reporter on the own account page).
4. minor, SEAMLESS.md 19.25's moderation block described the state before the first review. Fixed (6f7f3cd3): Introduced,
   10 a minute, the card's question, the limit memory, 422, own_report and the hidden reporter, TakeSeat / SendRemovedHome
   with the co-arrivals, W10d; 19.6.6 and 19.16 got a sentence each.

## Findings for the integration (not this lane's code)

- The host gets two toasts for one guest: the instance's "<name> joined." (WORLD_NOTICE joined, 19.5.6) and the stage
  group's JOIN event "<name> joined" (session.cpp, `slot_name(slot) + " joined"`, a rooms leftover; 19.5.6 lists only the
  first). When INSTANCE_INFO does not (yet, or any more) list the joining member with `same_group`, the second reads "A
  player joined" (W10 r2's a.log; the UI card run: a removal 60 ms after b's JOIN was stamped, the JOIN still applies at
  its tick, then the HOME 6 ticks later: "b left." then "A player joined"). Suggestion: no JOIN toast in world sessions
  (the instance notices speak), or none for a member the instance no longer lists. Untouched here (int-i1a, prompt and go
  have the same line).

## Left

Nothing of the lane: every item of the task and every review finding is built and green (server unit and integration,
ctest, online off, W10, W10b, W10c, W10d, the UI scenario `card`). For the I1b integration:
- Merge with the lanes prompt and go: the conflicts the contract names (online_ui.cpp's `playing()`: the go-along box above
  the page, the card below the member table; the wire type registries; WorldHub/RoomHub hook places). With the prompt
  merged, W10's b gets the prompt at its second arrival: a's instance is no row (Visible() with the bar), so b's
  `DW4_ONLINE_AUTOCHOICE=first` takes *New instance* and the scenario's checks hold as they are (no change expected).
- W10, W10b, W10c, W10d and the UI scenario `card` again on the integrated build; W10/expected.txt gains the prompt's check
  (b's `options` rows never name a's wid after the removal, review finding 7, contract 15:20).
- The prompt / go lanes' new holds and seat paths: `CheckHolds` checks every hold kind for removed members first; a new path
  that seats without `TakeSeat` or joins without `Join` should call `SendRemovedHome` (contract 15:20).
- `DetachLocked` (I1a code) now calls `RedecideCoArrivals`; the prompt lane's FinishCoArrival guard and decision 43 should
  let co-arrivals decide through it (contract moderation 2026-11-12 15:25).
- The base flake `A_resync_starts_from_the_newest_agreed_checkpoint_and_its_ready_never_judges` (review finding 8, cause in
  the contract's prompt 14:25 entry: `BecomeMember`'s real-time stamp).
- The finding above (the double join toast), if the integration wants it fixed in I1b.

## How to run

- Environment: `source build/mod/env.sh` (DOTNET, DW4ONLINE_TEST_MYSQL on 3652, DW4ONLINE_TEST_PORTS 58200-58399, PY).
- mysqld for the integration tests: `bash server/DW4Online/.local/mysqld.sh` in the background; stop it with SQL
  `SHUTDOWN` (`/c/xampp/mysql/bin/mysql.exe -h 127.0.0.1 -P 3652 -u root -p$(cat server/DW4Online/.local/mysql-root.txt) -e SHUTDOWN`).
  A fresh data dir: `mysql_install_db.exe --datadir=... --port=3652 --password=...` (as done once in `.local`).
- C++: `cmd //c "start /low /wait /b cmd /c build\mod\bld.cmd rel ON nodev"` (Release, DEV=OFF, tests) and
  `... bld.cmd dev OFF dev --target dw4_game` (DEV=ON game for the scenarios); `cmd //c "build\mod\ctest.cmd rel"`.
- Server: `"$DOTNET" build --disable-build-servers` in `server/DW4Online`; `"$DOTNET" test tests/DW4Online.Tests
  --disable-build-servers`; the same for `tests/DW4Online.Integration.Tests` (needs mysqld 3652).
- Scenarios: stop the own mysqld first; copy `build/mod/dev/dw4_game.exe` to `build/mod/scen/dw4_mod_dev_<TAG>.exe`, then
  `bash build/mod/scen.sh LOG TAG W10 W10b W10c W10d` (run.py with --parallel 1 --keep, the r25c cards, mysqld 3652, ports
  58200-58399; add `--no-build` after the first run; W10d starts its own server with `World:TestJoinHoldMs` 60000). The UI
  scenario: `bash build/mod/scen.sh LOG TAG ui card --no-build`. Run folders: `tools/online_test/.work/run_*`.
