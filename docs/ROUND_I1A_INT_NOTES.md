# Round I1a, integration: progress notes

Branch `int-i1a` from main d0e005b0 (worktree `DW4_PC-wt/int-i1a`). It merges the three lanes of round I1a "the world
without rooms" (SEAMLESS.md 19.25 "Round I1a", its Done-when; 19.30 round 72) and makes the Done-when hold on the
merged tree. Lane notes: `docs/ROUND_I1A_WEB_NOTES.md`, `docs/ROUND_I1A_RELAY_NOTES.md`, `docs/ROUND_I1A_CLIENT_NOTES.md`;
the lanes' contract (outside the repo): `DW4_PC-wt/i1a-contract.md`. Never merged into main from here.

Own resources: mysqld 3641 (`server/DW4Online/.local`, gitignored; `bash server/DW4Online/.local/mysqld.sh` in the
background, stop with SQL `SHUTDOWN`), ports 57600-57799, build dirs `build/int/*` (gitignored).

## Done

| # | Step | Commit |
| --- | --- | --- |
| 1 | `git merge --no-ff feat-i1a-web` (dcd6f3ae): no conflict | 796514c1 |
| 2 | `git merge --no-ff feat-i1a-client` (ff715a62): conflict docs/SEAMLESS.md 19.25 only (both Built paragraphs kept, web's first) | e2dca7c8 |
| 3 | `git merge --no-ff feat-i1a-relay` (1f3df94d): the contract's resolutions (web 14:20) with web's `resolve_trial.py` (copy in `build/int/`): the 4 Domain stubs -> web's, ServerApp.cs web's block + the relay's WorldHubOptions, OnlineStoryTests (the relay's deletions), WorldRelayTests (`fakeWorld: false`, tickets through `WorldService.EnterAsync`); RelayGateway.cs merged cleanly without RoomSweeper | 4b3d8a82 |
| 4 | Production example `Play:MinimumProtocol` / `CurrentProtocol` 49 / 49 (web's 93261454); DEPLOY.md's two older protocol notes name 49 | c91bf976 |

## Left

- Build everything (server, C++ DEV=OFF with tests, C++ DEV=ON game), then the tests and the W scenarios of the Done-when.

## How to run

- Environment: `source build/int/env.sh` (DOTNET, DW4ONLINE_TEST_MYSQL on 3641, DW4ONLINE_TEST_PORTS 57600-57799, PY).
- C++: `cmd //c "start /low /wait /b cmd /c build\int\bld.cmd rel ON nodev"` (Release, DEV=OFF, tests) and
  `... bld.cmd dev OFF dev --target dw4_game` (DEV=ON game for the scenarios); `cmd //c "build\int\ctest.cmd rel"`.
- Server: `"$DOTNET" build --disable-build-servers` in `server/DW4Online`; `"$DOTNET" test tests/DW4Online.Tests
  --disable-build-servers`; the same for `tests/DW4Online.Integration.Tests` (needs mysqld 3641).
