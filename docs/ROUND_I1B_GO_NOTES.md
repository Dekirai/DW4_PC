# Round I1b, lane *go along* (19.30.5-19.30.7): progress notes

Branch `feat-i1b-go` from int-i1a bfc3b0a5 (worktree `DW4_PC-wt/i1b-go`). Contract: `DW4_PC-wt/i1b-contract.md`
(entries "go 2026-11-07 16:15": the wire layout, the relay's options and log names). Own resources: mysqld 3653,
ports 58400-58599, build dirs `build/go/*` (gitignored). Protocol stays 49 (no new number; OFFER 0x9B, GO event 7,
JOIN_REQ kind 3, INTENT kind 4, STATE flag 16, HEADING + asked, WORLD_NOTICE 13 were reserved by I1a or are new).

## Done

- **First task** (measured, SEAMLESS.md 19.30.7 "Measured (round I1b, go lane)"): `dw4_world_probe DATA go [CASE]`
  (log `build/go/meas/go_r2.log`): the GO lands on the port's barrier target (zone shuffle off and on), its fork records
  the frame the client's fork list names (replays of both sides equal, the exit's frame convention caught), the leavers'
  load equals the port's but for the departure's history; GO's departure 169 frames to the park (port "run" 110).
- **Engine** (online only; det.sh unchanged): `Engine::online_go(mask, stage, point)`, `ForkKind::go` (frame after the
  step, as give-up and HOME), the GO's departure (the half fade the prompt would start, `departure_work` "trans",
  NEXTSTAGE with `I<1>`: STAGECTL skips the zone remap, `managers_core.cpp`), `ExitHeading::asked` (the port's asked
  slots that stay), `kGoExpiryTicks` 300.
- **Client** (`src/game/online/session_go.cpp`, hooks in session.cpp, wire.cpp): OFFER, HEADING asked, STATE flag 16 (no
  living member, sent at once), every GO of a step in one `online_go` per target, the 300-tick expiry ("Couldn't go
  along: a scene here took too long." + INTENT 4, 0), JOIN_REQ kind go, the offers with the notice's wait (own scene,
  demo, defeated, barrier), INTENT 4 / withdraw, go refused texts, a defeated waiting member withdraws, the GO barrier's
  lines ("Joining Kim...", after 10 s "Waiting for Kim's floor..." + B: Back to the lobby), "Kim left <floor> before you
  arrived." for a GO that became a new stage group.
- **UI** (online_ui.cpp, game_main.cpp): the notice window (top centre, bar over 20 s, "View: answer" / "F1: answer", at
  most three, also over the F1 menu), the Instance page's *Go along* box (A: Go along, A on the own row: Cancel, B:
  Close: that offer's notices off and the menu closes), View focuses the row only after the notice's half second, the
  waiting line where the notice was.
- **Relay** (`server/.../DW4Online.Relay/World/GoAlong.cs` + hooks in RoomHub.cs / WorldHub.cs): offers made, extended,
  followed, closed (left / defeated / ended / gone), reopened silently within World:OfferReopenSeconds, at most four;
  OFFER per receiver (notice, late notice after a barrier / load / grace, the row without a notice for a member that was
  in a dungeon, none for the members the port asked); INTENT 4 waits for a joinable target with a living member and no
  boss lock and the sender's stage group sealing and membership-safe (GO stamped at one tick per group) or solo-led
  (JOIN_REQ kind go); 60 s (not in a boss fight); withdraw; closed / moved / ended; the GO's barrier joins the offer's
  stage group first, waits for it without JoinHoldSeconds (no Go alone, no rescue join), a new stage group only when
  nobody is left there; Leave from that held barrier goes home; the expiry backstop; HEADING's asked mask resolved
  against the port's stage group (the fork's parent); the world log names of the contract.
- **Online script** (dev): `goalong [STAGE]` (presses once the row is not greyed), `goalong closed`, `cancelgo`,
  `closeoffer`, `goback`, `testhook defeat|busy N`; conditions `offers`, `notices`, `go=`, `offerstage=`, `goback=`.
- **Scenario hooks** (restart 2026-11-07 17:30; contract entry "go 2026-11-07 17:50"): the tick event TEST 0xF0 (slot |
  hook | argument; 1 defeat: the slot's player falls as on poison's last tick, `Engine::online_test_hook`; 2 busy: the
  engine is neither membership-safe nor joinable for N steps, `Engine::online_test_busy`, cleared at a barrier). Only
  development clients apply it (`Session::apply_events`, `EngineLink::test_hook`); release clients skip the unknown type.
  Relay (`GoAlong.cs`, test options in `WorldHubOptions`, all off by default): `World:TestHooks` (INTENT kind 0x7F test
  stamps TEST in the sender's sealed stage group), `World:TestGoHook` (defeat / busy_sender:N at a GO's tick before it;
  busy_target:N in the sealed target stage group), `World:TestGoHookCount` (1), `World:TestGoBarrierHoldMs` (a GO's
  barrier waits once, HoldKind.TestGo). The client's test intent: `Session::test_hook_request`; the crossed request:
  `Session::go_along_raw`.
- **UI focus** (restart): View while the own request waits focuses its row (A: Cancel, "View then A"); View within a
  notice's first half second puts the cursor on the page's first item below the box (`OnlineUiState::focus_below_go`),
  after it on the noticed offer's row (`focus_go_id`).
- **Scenarios**: W31 (B1 crossed by the campaign bot's errand: the script's explore fell twice), W31b, W31c (with the
  crossed request), W31c2 (new: the hooked race, TestGoBarrierHoldMs), W31d (four members: b falls by `testhook defeat`,
  d's GO with TestGoHook defeat, c takes both along), W31d2, W31e, W31f (the host's way home by the Gate Disk instead of
  dng204b2's matz02 after a boss kill), W31g, W31h and W31h2 (new: TestGoHook busy_target:2400; B: Back to the lobby /
  the join after the scene), W31i (TestGoHook busy_sender:600; the expiry, then a request right after an arrival).
  No `requires` any more.

## Tests and results

- ctest 57/57 (`build/go/ctest2.log`); session tests 397 checks (`go_along_session`); wire tests (go_messages, the golden
  bytes the relay's `WireGoldenTests.Go_along_messages_equal_the_clients` holds too).
- Server unit tests (2026-11-11, 49301c82): 476 of 478; the two failing ones are
  `HubLogicTests.A_resync_starts_from_the_newest_agreed_checkpoint_and_its_ready_never_judges` (protocol 47 and 48), which
  fail the same way on int-i1a bfc3b0a5 itself (a `git archive` of its server: desync_votes 1, no resync of b), not this
  lane's (GoAlongTests: 6 + 2 new, all green). For the integration.
- Server integration tests (2026-11-11, 49301c82, mysqld 3653): 137 passed, 4 skipped, 0 failed (`build/go/itest3.log`).
  Their artifacts must lie two levels below `server/DW4Online` (`--artifacts-path bin/goit`): two tests find
  `Directory.Build.props` and `deploy/` from the test binary (with `build/go/srvart-it` they failed for that alone).
- det.sh on a DEV=OFF copy (`build/go/off/dw4_go_off_a.exe`, HEAD 53cb3afb's tree): run1 and run2 4004f056 / audio 396cb219
  (`build/go/off/det_a.log`).
- W31 runs: see the "Scenario runs" section.

## Left

1. Scenario runs of every W31 scenario at HEAD (batch c1 of 2026-11-11: see "Scenario runs").
3. The release (DW4Online 0.1.7 with I1a and I1b) is the integration's.

## How to run the tests

The helpers live in `build/go/` (gitignored); recreate them from these copies after a fresh checkout.

- C++: `cmd //c "start /low /wait /b cmd /c build\go\bld.cmd rel ON nodev"` (Release, DEV=OFF, tests);
  `cmd //c "build\go\ctest.cmd rel"`; single: `build/go/rel/dw4_online_session_tests.exe`,
  `build/go/rel/dw4_online_wire_tests.exe`, `build/go/rel/dw4_online_script_tests.exe` (parses every scenario script:
  `fail` takes commas, never `or`). DEV game for scenarios: `... bld.cmd dev OFF dev --target dw4_game`.
- Server unit tests without touching the bin folders a running scenario server uses: in `server/DW4Online`,
  `"/c/Program Files/dotnet/dotnet.exe" test tests/DW4Online.Tests/DW4Online.Tests.csproj --disable-build-servers
  --artifacts-path ../../build/go/srvart` (GoAlongTests are in HubLogicTests).
- Server integration tests: `bash build/go/itest.sh /abs/path/LOG [filter]` (an absolute log path: the script changes
  directory), only while no scenario runs (both use mysqld 3653; the scenario runner's database folder and root password).
- Probe: `build/go/rel/dw4_world_probe.exe "E:/Games/Emulator/Dolphin-x64/Games/Digimon World 4 - Extracted" go [0|1|2]`.
- Scenarios: copy `build/go/dev/dw4_game.exe` to `build/go/scen/dw4_go_dev_<TAG>.exe`, then
  `bash build/go/scen.sh LOG TAG W31 ...` (run.py --parallel 1 --keep, mysqld 3653, ports 58400-58599, the r25c cards;
  `--no-build` after the first run). Run folders: `tools/online_test/.work/run_*`. A run whose script failed early runs
  on to its frames or timeout: stop its game clients by PID (never by image name), the runner goes on.
- det.sh: `bash /c/Users/shinz/source/repos/DW4_PC/work/dref/checks/det.sh <DEV=OFF exe copy> <name>` (copy
  `build/go/rel/dw4_game.exe` to `build/go/off/`).

`build/go/bld.cmd`:

```
@echo off
rem i1b-go: bld.cmd DIR TESTING [dev|nodev] [extra build args]  (Release, Ninja, --parallel 4)
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
set CMAKE="C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set DEPS=C:/Users/shinz/source/repos/DW4_PC/build/Release/_deps
cd /d C:\Users\shinz\source\repos\DW4_PC-wt\i1b-go
set BDIR=build/go/%1
set BT=%2
set DEV=OFF
if "%3"=="dev" set DEV=ON
if not exist %BDIR%\CMakeCache.txt (
  %CMAKE% -S . -B %BDIR% -G Ninja -DCMAKE_BUILD_TYPE=Release -DDW4_BUILD_APP=ON -DBUILD_TESTING=%BT% -DDW4_ONLINE_DEV=%DEV% -DFETCHCONTENT_SOURCE_DIR_SDL3=%DEPS%/sdl3-src -DFETCHCONTENT_SOURCE_DIR_IMGUI_SRC=%DEPS%/imgui_src-src -DFETCHCONTENT_SOURCE_DIR_PL_MPEG_SRC=%DEPS%/pl_mpeg_src-src -DFETCHCONTENT_SOURCE_DIR_STB_SRC=%DEPS%/stb_src-src -DFETCHCONTENT_SOURCE_DIR_ZSTD_SRC=%DEPS%/zstd_src-src -DFETCHCONTENT_FULLY_DISCONNECTED=ON "-DDW4_TEST_DATA=E:/Games/Emulator/Dolphin-x64/Games/Digimon World 4 - Extracted"
  if errorlevel 1 exit /b 1
)
%CMAKE% --build %BDIR% --parallel 4 %4 %5 %6
```

`build/go/ctest.cmd`:

```
@echo off
set CTEST="C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
cd /d C:\Users\shinz\source\repos\DW4_PC-wt\i1b-go
%CTEST% --test-dir build/go/%1 --output-on-failure %2 %3 %4 %5
```

`build/go/scen.sh`:

```
#!/bin/bash
# i1b-go: scenarios with real hidden clients. Usage: scen.sh LOG EXE_TAG SCENARIO... (extra run.py args allowed)
export PATH="/usr/bin:/mingw64/bin:/c/Program Files/dotnet:/c/Windows/System32:/c/Windows:$PATH"
export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 PYTHONIOENCODING=utf-8 DW4_NO_ERROR_BOX=1
W=/c/Users/shinz/source/repos/DW4_PC-wt/i1b-go
LOG=$1; TAG=$2; shift 2
cd $W
if netstat -ano -p TCP | grep -qE ':3653 .*(LISTEN|ABH)'; then echo "port 3653 in use" > $LOG; exit 3; fi
echo "start $(date) head=$(git -C $W rev-parse --short HEAD) exe=dw4_go_dev_$TAG.exe" > $LOG
nice -n 10 /c/Users/shinz/AppData/Local/Programs/Python/Python313/python.exe tools/online_test/run.py "$@" --parallel 1 --keep \
  --game-exe "$(cygpath -w $W/build/go/scen/dw4_go_dev_$TAG.exe)" --data "E:/Games/Emulator/Dolphin-x64/Games/Digimon World 4 - Extracted" \
  --cards "C:/Users/shinz/source/repos/DW4_PC/work/campaign/checkpoints/r25c" --mysql-port 3653 --ports 58400-58599 >> $LOG 2>&1
echo "EXIT $? $(date)" >> $LOG
```

`build/go/itest.sh`:

```
#!/bin/bash
# i1b-go: the server's integration tests with an own mysqld on 3653 (only while no scenario runs). Usage: itest.sh LOG [filter]
export PATH="/usr/bin:/mingw64/bin:/c/Program Files/dotnet:/c/Windows/System32:/c/Windows:$PATH"
export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
W=/c/Users/shinz/source/repos/DW4_PC-wt/i1b-go
LOG=$1; FILTER=$2
DATA=$W/tools/online_test/.work/mysql-data
PW=$(cat $W/tools/online_test/.work/mysql-root.txt)
if netstat -ano -p TCP | grep -qE ':3653 .*(LISTEN|ABH)'; then echo "port 3653 in use" > $LOG; exit 3; fi
echo "start $(date) head=$(git -C $W rev-parse --short HEAD)" > $LOG
/c/xampp/mysql/bin/mysqld.exe "--defaults-file=$(cygpath -w $DATA/my.ini)" --bind-address=127.0.0.1 --port=3653 --character-set-server=utf8mb4 \
  --collation-server=utf8mb4_unicode_ci --default-time-zone=+00:00 --innodb-use-native-aio=0 "--log-error=$(cygpath -w $W/build/go/itest_mysqld.err)" > /dev/null 2>&1 &
for i in $(seq 1 40); do netstat -ano -p TCP | grep -qE '127.0.0.1:3653 .*(LISTEN|ABH)' && break; sleep 1; done
export DW4ONLINE_TEST_MYSQL="Server=127.0.0.1;Port=3653;User ID=root;Password=$PW"
export DW4ONLINE_TEST_PORTS=58450-58599
cd $W/server/DW4Online
args=(test tests/DW4Online.Integration.Tests/DW4Online.Integration.Tests.csproj --disable-build-servers -nologo --artifacts-path bin/goit)
[ -n "$FILTER" ] && args+=(--filter "$FILTER")
"/c/Program Files/dotnet/dotnet.exe" "${args[@]}" >> $LOG 2>&1
echo "EXIT $? $(date)" >> $LOG
/c/xampp/mysql/bin/mysql.exe --protocol=tcp -h 127.0.0.1 -P 3653 -u root "--password=$PW" -e "SHUTDOWN" >> $LOG 2>&1
echo "mysqld shut down $(date)" >> $LOG
```

## Scenario runs
