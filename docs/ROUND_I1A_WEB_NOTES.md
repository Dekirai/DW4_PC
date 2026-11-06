# Round I1a, lanes "server and web" + "harness": progress notes

Branch `feat-i1a-web` from main d0e005b0 (worktree `DW4_PC-wt/i1a-web`). Design: docs/SEAMLESS.md 19.17-19.25, 19.30;
what was built and decided is summarised in SEAMLESS.md 19.25 ("Built: the lanes server and web and harness"). The shared
lane contract (outside the repo): `DW4_PC-wt/i1a-contract.md` (the web lane's entries are marked `web`).

## Scope and state

| # | Item | State |
| --- | --- | --- |
| 1 | migration `0013_world` (prefs, history, members, last_seen_at, instance_id columns, reason mirror, five views) + the migrator's `-- Requires:` check (CREATE VIEW) | done |
| 2 | `WorldService` (admission with NewSession and the live re-check, counts, instances, whereabouts, play context, prefs, End / Remove, AccountChanged, StoryReplaced, history close / prune, last seen, the staff audit list) | done |
| 3 | `IWorldRelay` + `NullWorldRelay` + `IWorldVisibilitySink` (implemented by the relay lane), `IInstanceHistoryRepository`, `IWorldRepository` | done |
| 4 | REST `/world/enter`, `/world/ticket`, `/world/leave`, `GET /world`, `GET /instances`, `/me/online-prefs`, `/admin/instances` End / Remove, `/admin/audit`, 410 `/rooms*`, `/status` with the floor | done |
| 5 | achievements on the world's play context (`Play:RequireWorld`), `instance_id` on unlocks and times | done |
| 6 | `ScrubAsync` for the new tables and `last_seen_at` | done |
| 7 | website: `/instances`, `/rooms` redirect, `/admin/instances`, `/admin/audit`, admin account history, home and admin counts, navigation | done |
| 8 | website: friends page (whereabouts, prefs, Recently played with), profile whereabouts, privacy page | done |
| 9 | `areas.json` format 2 (C++ table columns, exporter, presence test), `AreaTable` (C#) | done |
| 10 | `data/online/stage_graph.json` + `tools/export_stage_graph.py`, `AreaTableTests` | done |
| 11 | DEPLOY.md 9.2 (HeidiSQL steps, CREATE VIEW grant, protocol 49), production example, server README | done |
| 12 | harness: fresh server per scenario, world log per wid, `--online-play`, `DW4_ONLINE_AUTOCHOICE`, cuts, restarts (ordered via Testing:StopFile), crashes, prefs, roles, API steps / checks, db checks, counter-proofs, `requires` | done |
| 13 | W scenarios of I1a's Done-when as files with expected results; S14/S14a/S14h/S14zb retired, S14z rewritten; ui `rooms` -> `home` | done (not runnable before the merge) |
| 14 | tests: unit, integration, ctest, det.sh | done (numbers below) |
| 15 | session 2: a trial merge of the three lanes in `build/i1aweb/trial` (no ref, no other worktree touched): server builds and passes, the merged DEV client builds, W scenarios run there | in progress (results below) |
| 16 | integration fixes on web's side (VisibilityNames, worldWeb, AreaTable constants and constructor, TestHost's `overrideServices`) + `WorldDomainTests` Wire.WorldProtocol == WorldProtocol.Version (reflection) | done (a98a63e9) |
| 17 | harness: fake world clients (`fake_presence` W19, `fake_guests` W29), `play_args` / `refused` (W22 with the protocol 48 client), `frame_times`, the `--fake` scenarios in the world, logins and uploads at 49 | done |
| 18 | scenario fixes from the trial runs: card k4 (missing) -> k5, `upload_card` for own-story clients, W1's host stays 15 s, W12 `--online-difficulty`, field exits by `explore`, W19's count, A1 in the world | done |
| 19 | end-to-end tools in the world: `tests/winhttp_client` (world enter, handshake 49, first_defeat, leave, 410) and `tests/e2e/relay_spike_test.py` (co-arrival from home, sealed stream, host's leave ends the instance) | done, to verify in the trial |
| 20 | release 0.1.7 (Directory.Build.props, DEPLOY.md 9.2); migration 0013 still the next number | done |

## Session 2 (2026-11-06 from 14:00): the trial merge

How it is made (all inside this worktree's `build/i1aweb/trial` = web a98a63e9 + client 03607b65 + relay 899bc3a2, and
`build/i1aweb/trial2` = web a69cbeb1 (+ the PruneAsync move) + client 8cd90c31 + relay 1470a267; gitignored; commit objects
without refs, no other worktree touched):

```bash
C1=$(git commit-tree $(git merge-tree --write-tree feat-i1a-web feat-i1a-client | head -1) -p feat-i1a-web -p feat-i1a-client -m trial)
T=$(git merge-tree --write-tree $C1 feat-i1a-relay | head -1)  # conflicts: 4 Domain stubs, ServerApp.cs, OnlineStoryTests.cs
git archive $T | tar -x -C build/i1aweb/trial2
for f in <the 4 Domain stubs>; do git show feat-i1a-web:$f > build/i1aweb/trial2/$f; done
python build/i1aweb/tools/resolve_trial.py build/i1aweb/trial2   # ServerApp (web's block), OnlineStoryTests, the relay tests' two edits
cd build/i1aweb/trial2/server/DW4Online && dotnet build --disable-build-servers
cmd //c "start /low /wait /b cmd /c build\i1aweb\trial2_cpp.cmd"   # the merged DEV game: build/i1aweb/trial2/build/dev/dw4_game.exe
```

Scenarios there: `TRIAL=trial2 bash build/i1aweb/trial_scen.sh LOG PARALLEL W1 ...` (copies run.py, character_test.py,
scenarios/ and ui/ into the trial and runs run.py there with `--capabilities fake_world_clients,old_client --old-game-exe
build/i1aweb/old48/dw4_old48_dev.exe`, the merged DEV game, the game data and the r25c cards, mysqld 3632, ports 57200-57399).
run.py starts and SHUTDOWNs its own mysqld on 3632: the integration tests' mysqld must be stopped first. The old client is a
copy of int71's protocol 48 DEV build (`DW4_PC-wt/int71/build/fix/dw4_fix_dev.exe`, 0872b8f0).

Findings for the other lanes (in the contract): the client's online achievements polled `/rooms/world` (410: never
`online_host`, the host got no story challenges) and reported `online_join` at home (refused, never again): fixed by the
client lane in f00afc22; the online card keeps the own story of the entry (minor, contract web 15:40); the relay's newer
FakeHistory needed IInstanceHistoryRepository without PruneAsync (web moved it, 7c12ffca).

## Test results, session 2 (2026-11-06 afternoon)

- This branch: DW4Online.Tests 422 of 422; DW4Online.Integration.Tests 127 passed, 8 skipped, 0 failed
  (`build/i1aweb/it_all4.log`; skipped as below: the EndToEnd tools need the world relay, which only the merge has).
- Trial merge 1 (`build/i1aweb/trial`): unit 429/429 (`trial_unit.log`), integration 129 passed / 4 skipped
  (`trial_it2.log`, WinHTTP / Python not set up there yet).
- Trial merge 2 (`build/i1aweb/trial2`, client 8cd90c31, relay 1470a267): unit 448/448; integration 136 passed, 1 skipped
  (the certificate-store test), 0 failed, with the WinHTTP client and DW4ONLINE_PYTHON: both ported end-to-end tests pass
  against the real world relay (`trial2_it.log`).
- Game scenarios with real clients: see "Scenario results" below.
- No game code changed in this session (det.sh / ctest of session 1 stand).

## Scenario results (trial merge 2, real hidden clients)

Run c1 (`build/i1aweb/scen_c1.log`, run folder `build/i1aweb/trial2/tools/online_test/.work/run_20261106_144305`):
PASS S14z, W1, A1, W11 (decision 41), W19 (35 fake presence senders, 30 drawn), W1x (the counter-proof fails as it
must), W22 (protocol 48 client: "Update required"), W25 (killed server: both home), W28 (reset and restore), W3.
FAIL W24: the client stays `closed` in the field after CLOSE 8 (no reconnect, no way home; contract web 16:05, client
lane). FAIL W12, W18, W26, W27, W29 on the harness side, fixed since (b died on Hard; a host step waited for sealed ticks
alone; the crashed client's open log blocked the rename; the host fell beside a living guest, no game over; the
achievements' format). W30, W30b, W30x, W32 stood at stagML11's gate mat (the gate step needs `explore mate00 until
onme:mate00` first, fixed); the run was stopped there and the rest went into run c2.

## Test results, session 1 (2026-11-06)

- Server unit tests: 420 of 420 (`build/i1aweb/unit3.log`; new: `AreaTableTests` 4, `WorldDomainTests` 6).
- Server integration tests (own mysqld 3632, ports 57200-57399): 127 passed, 8 skipped, 0 failed
  (`build/i1aweb/it_all2.log`). Skipped: 4 EndToEnd (no WinHTTP client, Python, certificate store) and 4 relay story
  tests of `OnlineStoryTests` (they drive the room relay through `/rooms`, which answers 410; the relay lane ports them to
  the world). New: `WorldApiTests` 9, `MigratorTests` 11 (0013, the grants), `ProxyCutTests` 2; ported:
  `AchievementTests` 8, `OnlineCharacterTests` 11, `WebTests` 6, `AccountScrubTests` 3, `OnlineStoryTests` (4 non-relay).
- ctest (own build `build/i1aweb/cpp`, Release, DEV=OFF): 56 of 56 (`build/i1aweb/ctest.log`); `dw4_presence_tests`
  3348 checks; the C++ exporter's output equals `data/online/areas.json` byte for byte.
- Online off: `det.sh` with a copy of the DEV=OFF `dw4_game.exe`: run1 and run2 fingerprints 4004f056, audio 396cb219
  (`build/i1aweb/det.log`).
- Harness smoke (no game clients): its own mysqld on 3632, a scenario server, `/status` minimum 49, `/rooms` 410, prefs,
  `/world/enter` 503 without the relay, the db check, an ordered restart (stop file, 2.2 s) and a killed one (2.0 s) on the
  same ports; mysqld ended with "Shutdown complete".

## How to run the tests

Own mysqld (XAMPP 10.4.32) on port 3632, data in `server/DW4Online/.local` (gitignored, root password in
`.local/mysql-root.txt`). Start it from Bash in the background (never from PowerShell), stop it with SQL `SHUTDOWN`:

```bash
cd server/DW4Online/.local
/c/xampp/mysql/bin/mysqld.exe "--defaults-file=<abs>/mysql-data/my.ini" --bind-address=127.0.0.1 --port=3632 \
  "--log-error=<abs>/mysqld.err" --character-set-server=utf8mb4 --collation-server=utf8mb4_unicode_ci \
  --default-time-zone=+00:00 --innodb-use-native-aio=0      # (run_in_background)
source build/i1aweb/env.sh   # DW4ONLINE_TEST_MYSQL (root on 3632), DW4ONLINE_TEST_PORTS=57200-57399, DOTNET, PY
"$DOTNET" test server/DW4Online/tests/DW4Online.Tests --disable-build-servers
"$DOTNET" test server/DW4Online/tests/DW4Online.Integration.Tests --disable-build-servers
cmd //c "build\i1aweb\build_cpp.cmd"  &&  cmd //c "build\i1aweb\ctest.cmd"     # C++ (FETCHCONTENT from the main repo)
bash work/dref/checks/det.sh <copy of build/i1aweb/cpp/dw4_game.exe> i1aweb       # from the main repo, online off
python tools/export_stage_graph.py --dec <main repo>/work/port-research/dec        # re-export the stage graph
```

The W scenarios after the merge (a DW4_ONLINE_DEV build of the merged client): `python tools/online_test/run.py W1 W1x ...
--game-exe EXE --data DATA --cards DIR --mysql-port P --ports A-B` (`--capabilities fake_world_clients,old_client` and
`--old-game-exe` for W19, W22, W29).

## What the W scenarios need from the other lanes (also in the contract)

- Client: `--online-play [BOX]`, `DW4_ONLINE_AUTOCHOICE`, the counter-proof hook `DW4_ONLINE_TEST_NO_REPLACE=1` (W1x, W30x;
  the client lane names its hook in the contract if it differs), `DW4_DIFFICULTY` for the Game Start (W12).
- Relay: the world log `world_<run>.jsonl` with `instance_start`, `instance_join` (how, role, stage), `instance_leave`,
  `instance_end` (reason), `redirect`, `area_escape`; the history rows through `IInstanceHistoryRepository`.
- Assumptions to check at the first run (cards and routes, not verifiable without the merged game): the r25c cards k2, k4,
  k7, k10 (k-number = key items; k10 opens Dry Land and the Fortress), the gate menu's second entry is Dry Land (`gate mate00
  1`), W30b's card has its last Security Gate on dng205b2, W7e's cut timing meets both members on the mat, W9's route
  through the Fortress into stagML02.

## Left (not in this branch)

- W19 and W29 need world-capable fake clients (presence senders, fake guests: the relay lane's FakeClient); W22 needs a
  protocol 48 client build (`--old-game-exe`). They are written with `requires` and report SKIP until then.
- The `--fake` scenarios of run.py (FakeClient's room protocol) and the four relay story tests: the relay lane's.
- `player_reports`, `/reports`, `/admin/reports` and the admin account page's reports: round I1b.
- Integration: take web's versions of the Domain files the relay branch holds as stubs (contract 13:05), register the
  relay's `WorldGateway` before the `TryAdd` lines, add a test that `Wire.WorldProtocol == WorldProtocol.Version`, raise the
  server version (0.1.7?) and check the migration number (0013 is the next after 0012_friends).

## Progress

- 2026-11-06: contract entries; migration 0013 with the migrator's rights check; the domain, data, API and website moved
  from rooms to the world; the relay's two requests (NewSession, the repeated admission) and the client's REST names;
  friends' whereabouts, prefs, Recently played with; areas.json format 2, the stage graph and AreaTableTests; DEPLOY.md
  9.2; the impairment proxy's cuts; the harness in the world with the W scenarios; the staff audit list; the ordered stop
  of the restart scenarios. Commits 1d50f623 ... (see `git log d0e005b0..feat-i1a-web`).
