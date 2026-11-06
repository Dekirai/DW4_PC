# Round I1a, lane "server and web" + "harness": progress notes

Branch `feat-i1a-web` from main d0e005b0 (worktree `DW4_PC-wt/i1a-web`). Design: docs/SEAMLESS.md 19.17-19.25, 19.30.
The shared lane contract (outside the repo): `DW4_PC-wt/i1a-contract.md` (the web lane's entries are marked `web`).

## Scope (19.25 "server and web", "harness", round 72's additions)

| # | Item | State |
| --- | --- | --- |
| 1 | migration `0013_world` (prefs, history, members, last_seen_at, instance_id columns, reason mirror, views) + the migrator's `-- Requires:` check | done |
| 2 | `WorldService` (admission, counts, instances, presence, play context, prefs, End / Remove, AccountChanged, StoryReplaced) | done |
| 3 | `IWorldRelay` (defined here, implemented by the relay lane), `NullWorldRelay`, `IInstanceHistoryRepository`, `IWorldRepository` | done |
| 4 | REST `/world/*`, `/instances`, `/me/online-prefs`, `/admin/instances` End / Remove, 410 `/rooms*`, status with the floor | done |
| 5 | achievements on the world's play context (`Play:RequireWorld`), `instance_id` on unlocks and times | done |
| 6 | `ScrubAsync` for the new tables | open |
| 7 | website: `/instances`, `/rooms` redirect, admin instances, admin account history, home counts, navigation | done |
| 8 | website: friends page prefs + "Recently played with" + presence, profile whereabouts / last online, privacy page | open |
| 9 | `areas.json` format 2 (C++ table columns, exporter), `AreaTable` (C#) | done (C++ test not yet built) |
| 10 | `data/online/stage_graph.json` + exporter, `AreaTableTests` | open |
| 11 | DEPLOY.md (HeidiSQL steps, CREATE VIEW grant, protocol 49), README | open |
| 12 | harness: fresh server per scenario, world log per instance, `--online-play`, `DW4_ONLINE_AUTOCHOICE`, regression port, W scenarios | open |
| 13 | tests: unit + integration for all of the above | partly |

## How to run the tests

Own mysqld (XAMPP 10.4.32) on port 3632, data in `server/DW4Online/.local` (gitignored). Start it from Bash in the
background (never from PowerShell), stop it with SQL `SHUTDOWN`:

```bash
cd server/DW4Online/.local
/c/xampp/mysql/bin/mysqld.exe "--defaults-file=<abs>/mysql-data/my.ini" --bind-address=127.0.0.1 --port=3632 \
  "--log-error=<abs>/mysqld.err" --character-set-server=utf8mb4 --collation-server=utf8mb4_unicode_ci \
  --default-time-zone=+00:00 --innodb-use-native-aio=0      # (run in the background)
source build/i1aweb/env.sh   # DW4ONLINE_TEST_MYSQL (root on 3632), DW4ONLINE_TEST_PORTS=57200-57399, DOTNET, PY
"$DOTNET" test server/DW4Online/tests/DW4Online.Tests --disable-build-servers
"$DOTNET" test server/DW4Online/tests/DW4Online.Integration.Tests --disable-build-servers
```

## Progress

- 2026-11-06: contract entries (IWorldRelay, the floor constant, World options, history repository, areas.json format 2,
  REST layouts, the world membership log); migration 0013 with MigratorTests (11 of 11); domain, data, API and website
  moved from rooms to the world; unit tests 409 of 409. The integration tests that used rooms are being ported (next
  commit).
