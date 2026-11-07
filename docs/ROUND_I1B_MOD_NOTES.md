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

## Test results

| What | Result | Log |
| --- | --- | --- |
| server unit (DW4Online.Tests) | 474/474 | - |
| server integration (DW4Online.Integration.Tests, mysqld 3652) | 142 passed, 3 skipped | `build/mod/it1.log` |
| ctest (Release DEV=OFF, `build/mod/rel`) | 57/57 | `build/mod/ctest1.log` |
| online_session / online_wire / online_script tests | pass (362 / 346398 / 304 checks) | - |
| scenarios W10, W10b, W10c | see "Scenario rounds" | `build/mod/scen_r*.log` |

### Scenario rounds

- r1 (exe a = 4f540be7 + scenarios 9bc4f94f, `build/mod/scen_r1.log`): running.

## Left

- The scenario rounds above until W10, W10b and W10c pass (they need only this lane: I1a's automatic join stands in for
  the prompt; with the prompt lane merged, b's second arrival in W10 gets no row for a's instance, the same check).
- Online off: `det.sh` on a DEV=OFF copy of the final tree (the engine is untouched by this lane except the allowed-env
  list entry; the session, script and UI changes run only online or with test scripts).

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
  `bash build/mod/scen.sh LOG TAG W10 W10b W10c` (run.py with --parallel 1 --keep, the r25c cards, mysqld 3652, ports
  58200-58399; add `--no-build` after the first run). Run folders: `tools/online_test/.work/run_*`.
