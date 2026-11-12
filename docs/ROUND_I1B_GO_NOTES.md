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
  Hook 3 mat (2026-11-11, contract entry "go 2026-11-11 15:00"): the slot's player stands on the mat the argument names
  (letter << 8 | number; `testhook mat matz02`): W31's way from dng204b1 to matz02, which neither the script's explore
  nor the campaign bot's errand survives at k10.
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

## Review of 2026-11-12 (fix round)

The reviewer's eleven findings (blocker 1, should_fix 3, minor 7), each checked; commits 6deab5ad (relay), 15161cbe (client),
ab170b5f (a closed offer's 120 s from its close), 908b2df0 (docs, scenarios). Every new test fails on the old code (server:
a scratch copy of HEAD~ with the new API stubbed, 6 of 6 new tests failed; client: each fix taken back alone, 6 builds, each
failed its check), and passes now.

1. (blocker) B on a held GO barrier dissolved the whole arrival group (the host too, so the instance ended): fixed, only the
   member that pressed it goes home; the host's ends the instance for everyone at the barrier (GoBackToLobby). Tests:
   `Back_to_the_lobby_from_a_held_go_barrier_takes_only_the_member_that_pressed_it_home` (host + guest, the host's own B, guest +
   guest), `Back_to_the_lobby_of_a_guest_leaves_a_defeated_host_the_side_rule_took_along_waiting`.
2. (should_fix) a Cancel crossing the stamp cleared the request: fixed differently from the review's text, because 0 also was the
   client's report of a GO that did nothing at its step (defeated: before its expiry) and the relay cannot tell them apart: 0 is
   now only the Cancel, -1 the client's report (contract). A Cancel withdraws while nothing is sealed (JOIN_REQ go out: the
   JOIN_AT seals the lead alone; a GO still in PendingEvents leaves them), else the request stays (WithdrawLate, flag 16, the
   client shows "Going along..."). Tests: `A_cancel_before_the_gos_tick_is_sealed_withdraws_it_one_after_it_keeps_the_go_and_its_barrier_rules`,
   `A_cancel_while_the_join_req_go_is_out_withdraws_it_and_the_join_at_seals_the_lead_alone`; session: the crossing cancel.
3. (should_fix) the GO path ignored the session context: fixed with the GO's own rule (GoContextFits): the review's plain
   `ContextMatches(target, sender)` would refuse every GO to B2 (dng204b1's start.lua sets COUNTER 2 = 204 at its load, so B2
   arrived with a context the field never had). At the GO barrier a changed context is decided as any arrival (NEW with
   OtherContext), provisional, for the user. Test: `A_go_never_joins_a_floor_whose_play_changed_the_context_but_a_floors_own_load_is_no_change`.
4. (should_fix) the host's single B ended the instance without a question, with a text outside 19.5.6: fixed (go_back_press;
   "B: Back to the lobby", then "End the instance for everyone?   A: Yes   B: No", the hint added to 19.5.6 for the user's OK).
5. (minor) events behind a GO in one step were not checked after it: fixed (apply_events; the fake engine is not
   membership-safe with a fork pending or a GO departing). Session check: the INTENT never applies in the leavers' copy.
6. (minor) "went on to" missing, a waited notice said "went into": fixed (go_offer_stage_, notice_stage).
7. (minor) Named never aged: fixed (GoOffer.LeftAt; a member never named is news; a closed offer counts from its close as
   19.30.5 says). Test: `A_re_entry_is_silent_only_within_two_minutes_of_the_members_own_leave_and_never_with_a_member_never_named`.
8. (minor) 19.25 called 3.4 s "as estimated": corrected; the measured time against 19.30.6 / 19.24's two seconds is open for
   the user (19.25), not changed.
9. (minor) test gaps: unit tests for two members in one GO barrier and the context added (1, 3); min_shared_ticks set to about
   half the passing runs' (W31 5000, W31b 3000, W31c / W31c2 1500, W31d 15000, W31d2 20000, W31e 15000, W31f 1200, W31g
   12000, W31h / W31h2 25000, W31i 4000); W31d expects its two sealed stamps (how=sealed). Not done (rejected as too costly for
   a minor point; noted in 19.25): a UI scenario of the box (two clients in the world plus the UI script), and replacing
   W31h's / W31i's busy hook by a real scene / exit prompt (the hooks are the stand-ins 19.24 allows).
10. (minor) merge risk with the prompt lane's harness: cannot be changed on this branch (no `ArrivalPrompt` here); the exact
    merge step is in the contract (go 2026-11-12 17:05).
11. (minor) A: Cancel on a stamped row: fixed (RoomLive::GoRow::cancellable); go_join logs the wid; the contract now names the
    world log as built (go_barrier without a decision, go_stamped tick 0 for join_req).

## Tests and results

- Code changed on 2026-11-12 (review fixes, 6deab5ad / 15161cbe / ab170b5f); the results below the review's lines are older.
- 2026-11-12 after the fixes: server unit 484/484 (`build/go/utest7.log` at 6deab5ad; the reopen case added at ab170b5f, HubLogicTests
  148/148), session tests 455 checks (`build/go/sess_r1.log`), ctest 57/57 (`build/go/ctest7.log`, Release DEV=OFF at 15161cbe),
  det.sh on `build/go/off/dw4_go_off_d.exe`: run1 and run2 4004f056 / audio 396cb219 (`build/go/off/det_d.log`).
- ctest 57/57 (2026-11-12 at 6be4b788, `build/go/ctest6.log`; before: 2026-11-11 at 2deadf82, `ctest5.log`); online script
  tests 351 checks (2026-11-12 at 093a8f0c: every scenario script parses); session tests 397 checks (`go_along_session`);
  wire tests (go_messages, the golden bytes the relay's `WireGoldenTests.Go_along_messages_equal_the_clients` holds too).
- Server unit tests (2026-11-11 20:13, with TEST mat, `build/go/utest6.log`): 478/478 (GoAlongTests 6 + 2 new). The I1a test
  `HubLogicTests.A_resync_starts_from_the_newest_agreed_checkpoint_and_its_ready_never_judges` (protocol 47 and 48) is
  intermittent: it failed four runs in a row at about 14:20 (desync_votes 1, no resync of b), on a `git archive` of int-i1a
  bfc3b0a5's server as well, and passed every run from 14:50 on, there too. Not this lane's; for the integration.
- Server integration tests (2026-11-11 22:00 at 6be4b788, mysqld 3653): 137 passed, 4 skipped, 0 failed (`build/go/itest4.log`;
  before at 49301c82, `itest3.log`, the same).
  Their artifacts must lie two levels below `server/DW4Online` (`--artifacts-path bin/goit`): two tests find
  `Directory.Build.props` and `deploy/` from the test binary (with `build/go/srvart-it` they failed for that alone).
- det.sh on a DEV=OFF copy: 53cb3afb (`dw4_go_off_a.exe`), 6493bb74 (`_b`) and 2026-11-11 at 947df0e3 (`_c`, with the mat
  rule and TEST mat): run1 and run2 4004f056 / audio 396cb219 every time (`build/go/off/det_c.log`).
- W31 runs: every scenario of the lane passed at the final code (exe f = 947df0e3's code, the same bytes as a DEV build at
  093a8f0c): W31b, W31c, W31d2, W31e, W31f, W31h, W31h2 in e1 (scripts of aa5a0c16, unchanged since), W31, W31c2, W31d,
  W31g in f1 and W31i in f2 (scripts of 093a8f0c). See the "Scenario runs" section.

## Left

The W31 set on the fixed client (run g1, see "Scenario runs") and the integration tests after it. Else nothing of the lane. The release (DW4Online 0.1.7 with I1a and I1b) and the round's other Done-when items (the I1a
scenarios with `DW4_ONLINE_AUTOCHOICE=first`, the regression) are the integration's.

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

- **b1** (2026-11-07, a5027b87, `build/go/scen_b1.log`): W31g passed; W31 (a's explore fell in B1), W31b (the straight
  walk of "mat" stuck at a wall), W31d2 (c's game over 60 s after b's request: busy a second before the close), W31e (c fell
  to stag0201's mummies) failed for their scripts.
- **c1** (2026-11-11, 49301c82, exe c, `build/go/scen_c1.log`, run folder `run_20261111_142113`): W31c, W31d2 passed.
  W31 failed again in B1 (the campaign bot's errand: 36,000 frames without finding matz02, then defeated) -> TEST hook 3
  mat (ff1e5c09). W31b: b, asked on the mat, got the notice: the engine announced a's departure twice (the port's HEADING
  with b asked, then the solo copy's NEXTSTAGE with none) and the relay took the second -> a HEADING of the same departure
  keeps the asked members (2b19f880). W31c2: the test hold sent BARRIER_WAIT once, the client's five-second fallback went
  on as an unregistered NEW -> BARRIER_WAIT every second (dccd2322). W31d: everything up to c's GO passed (b and d taken
  along alive), but d's spawn at dng204b1's entrance lies on matz01: its exit prompt, left open, kept c's JOIN from
  applying (lag_leave) -> a player that appears on a mat has not stepped on (2deadf82); `!relay: instance_end` also hit
  the host's own end at the run's end (f02444ed). W31f passed. W31e: c fell in stag0202 on its way to matz01 (two
  heals) -> c goes into dng205b1 by `depart` right after a's offer shows (6ac553a7). W31g (passed in b1): b fell to
  stag0201's mummies -> every walk to stag0201's matz00 heals at 350 HP (8a4598be, 12351e3f). W31h: the relay held b's
  GO barrier as designed (hold_join 37 s, then the join), but the script never saw `goback=1`: the online script ran only
  inside steps, and frames do not count on a loading screen -> the host runs the script while parked (conditions, goback)
  and the condition `waited>=S` (947df0e3).
- **d1** (2026-11-11, 947df0e3 code = exe f, `build/go/scen_d1.log`, run folder `run_20261111_175519`): W31 (with TEST mat:
  b's press to stamp 10.9 s, stamp to join 3.3 s; c's 21.3 s / 3.4 s), W31c2, W31d (four members: the mat rule), W31g and W31i
  passed. W31b: b fell to stag0201's mummies on its way to matz01 -> every walk to matz01 heals at 350 HP (9a12f9eb). W31e
  (everything else passed): the offers close for b (reason 4) at its GO barrier, before the join -> expected order
  (5652487b). W31h (goback worked: B on the held loading screen took b home) and W31h2 (the held barrier joined after the
  40 s scene): the world log notes test_go_hook before go_stamped, and go_join (the barrier's decision) before hold_join ->
  expected order (9b5559a5, aa5a0c16).
- **e1** (2026-11-11 20:12, aa5a0c16, exe f, `build/go/scen_e1.log`, run folder `run_20261111_201252`): W31b, W31c, W31d2,
  W31e, W31f, W31h, W31h2 passed (7 of 7).
- **f1** (2026-11-12 14:28, 6be4b788, exe f, `build/go/scen_f1.log`, run folder `run_20261112_142844`; the scenarios whose
  scripts changed after d1): W31, W31c2, W31d, W31g passed. W31i: b's GO expired as designed (300 ticks, the notice), but
  c fell to stag0201's mummies on its third walk to matz00 (two heals), and b, the owner of matz00's prompt, waited for two
  on the mat for good -> b and c reach matz00 by TEST mat (`World:TestHooks` in W31i's server; b waits 600 frames after the
  expiry first, so the hook's busy stage group is over) (093a8f0c).
- **f2** (2026-11-12 15:25, 093a8f0c, exe f, `build/go/scen_f2.log`, run folder `run_20261112_152521`): W31i passed (b's
  second request, sent right after its arrival in stag0202, waited 6.8 s for its new stage group, the barrier 2.9 s after
  the stamp, the join).
