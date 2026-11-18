# Round 0.2.1: progress notes

Release 0.2.1 (release numbers follow the server; the game's own version stays the CMake project's 0.46.0, protocol 50
unchanged: 0.2.0 clients keep working with a 0.2.1 server and the other way round). Lanes in their own worktrees and
branches: the relay lane (`fix-021-relay`) and the client lane (`fix-021-client`), both from main d230037f, each with
its own section below; merged on branch `int-021` for the check pass ("The integration" at the end).

## The relay lane (branch `fix-021-relay`, from main d230037f)

Branch `fix-021-relay` from main d230037f (release 0.2.0: protocol 50, schema 14, DW4Online 0.2.0), worktree
`DW4_PC-wt/r021relay`. Relay-only fixes for DW4Online 0.2.1; protocol stays 50 (0.2.0 clients work with a 0.2.1 server and
the other way round), no schema change, the game's code unchanged. Own resources: mysqld 3750-3754, ports 60600-60699,
builds and logs in `build/r021/` (gitignored; "How to run" below). Not merged, not pushed; the server's version
(`Directory.Build.props`, 0.2.0) is left to the release's integration.

The task: the four relay findings of "Open after 0.2.0" (docs/ROUND_INT_I1B_NOTES.md) that are bugs, the lag rule's gap
("The flapping host", left), and fix-d43's pre-existing finding on a removed member playing alone. SEAMLESS 19.32
("fix-021") has the rules; 6.4, 6.9, 19.6.6, 19.6.7 and 19.24 say them where they apply.

### Findings and fixes

| # | Finding | Fix | Test (fails without the fix) |
| --- | --- | --- | --- |
| 1 | `StateReporter` took the connected member longest: a host in a grace started by silence (half-open) stayed the reporter, the guests' STATEs were ignored, a held arrival was let in on his old flags or got a NEW (`HoldExpired`) | the reporter is the connected member longest that is not away: not a host in his grace, not resyncing, its slot not lagging, its engine not more than 180 ticks behind (its newest NETFP, `HubMember.ExecutedTick`); `StartGrace` and `HostBack` sync it | `A_host_silent_in_his_grace_hands_the_flags_...`, `A_guests_boss_fight_while_the_hosts_link_hangs_...`, `A_returned_host_who_still_lags_...`, `A_returned_host_whose_engine_still_catches_up_...`; W7h |
| 2 | `OnState` applied the reporter's STATE without the newer-by-tick check: a host back from a drop overrode a guest's newer STATE with the ones of the ticks he caught up | `Supersedes`, one rule for `OnState` and `SyncStateReporter`: none in effect, its member left, or newer by tick; at the same tick only the member in effect's own; an older one is kept (`state_stale`) | `A_host_back_from_a_drop_never_overrides_...` |
| 3 | `StartGrace` at a gate: the gathering stage group counted as shared with the guests, the absence debt shortened the grace although they went on at once | `SharedGroup`: a stage group at its barrier is shared with nobody (a fresh grace); and a grace the debt shortened is fresh again from its start once the host shares no stage group with a guest (`host_grace_fresh`: a link that closed a moment before his BARRIER came, his guests going on without him) | `A_host_who_drops_exactly_at_a_gate_...`, `A_host_whose_link_closes_a_moment_before_his_gate_...`; W7j |
| 4 | the lag rule's gap: drops each below the limit with short bursts in time between them never reached it (guests; a host below `World:HostSilenceSeconds`) | `SlotStream.LagDebt`: filled ticks that held a connected member in time up, minus ticks in time, never below 0 (not while every connected member was filled, a host in his grace, a member resyncing); a lagging member is out at `Relay:LagLeaveSeconds` of it (`lag_leave why=lag_debt`); a host's return restarts it; the fill streak's rule stays | `A_guest_whose_link_hangs_open_again_and_again_...`, `A_host_whose_link_hangs_open_below_the_silence_...` (control: `Brief_hiccups_paid_back_...`); W7i |
| 5 | `OnForeignStory` / `PlayContext` did not see a removed member playing alone (`HubMember.LoneWorld`): its own key reached its own keeper with the host's progress, the context said home | `OnForeignStory` reads `LoneWorld` for a member without a stage group (or in the empty hub one of a reconnect): foreign unless it holds its seat there or the story is its own | `A_guest_removed_while_it_plays_on_alone_is_on_...`, `A_guest_back_without_its_slot_and_removed_...` (control: `A_host_playing_apart_keeps_...`) |

Counter-checks (`build/r021/counter.sh NAME FIX LOG FILTER`: a copy of HEAD's server tree in `build/r021/counter/NAME`
with one fix taken out by `counter_patch.py`, or main's four relay sources; the 13 tests of `new_filter.txt`): on main's
relay 11 of 13 fail, the two controls pass (`logs/counter_main_13.log`); without fix 1 (the whole away rule) the four
reporter tests (`counter_f1_13.log`), without its NETFP part alone (f1b) the catching-up test, without 2 its one, without
3's barrier rule (f3) the gate test, without the renewed grace (f3b) its one, without 4 its two, without 5 its two
(`counter_f1b.log` ... `counter_f5.log`).

**The test proxy** (tests/DW4Online.FakeClient `ImpairmentProxy`): it closed both sides a fixed 600 ms after one side's
end, so W7j's first run (`run_20261116_161608`, on 85f2bdf1) had a's close reach the relay 612 ms after a's BARRIER,
after b's (150 ms later) completed the barrier: the sealed NEW of both, a in it with b, an ordinary drop, 30 s of grace
(debt 40.3 s) and `host_lost`, the path the fix leaves as it was (W7e's). Now it waits for that direction's latest delivery
time (a half-open cut's end included), then 50 ms; `ProxyCutTests` pass.

### Checks

- **Unit tests** 716/716 (`logs/unit_4.log`, f6276ed4; 703 of 0.2.0 unchanged plus the 13 of `Fix021RelayTests`).
- **Integration tests** 155 passed, 3 skipped (the opt-in end-to-end tests; `logs/it_1.log`, 29e95f20, own mysqld on 3750,
  shut down by SQL).
- **The dev client**: `build/r021/dev` (Release, DW4_ONLINE_DEV=ON, main's client code), copied to
  `build/r021/scen/dw4_r021_dev.exe`, md5 b3fa754f.
- **Scenarios, first runs** (`logs/scen_new1.log`, `run_20261116_161608`, relay of 85f2bdf1): W7h passed (c held for b's
  scene at 196.3 s of the relay's clock, a's silence grace at 202.6 s, c's JOIN decided at 211.5 s when the scene ended, a
  back at 248.5 s; no `HoldExpired`), W7i passed (`lag_leave why=lag_debt lag_debt_s=10` at 211.8 s, 11-12 s after the
  first cut; a led on alone, b played apart), W7j failed on the proxy's late close (above).
- **Scenarios, the final code** (`marathon.sh`, `logs/marathon.log`, relay and proxy of f6276ed4, mysqld 3751, ports
  60650-60699, run.py `--parallel 1`): batch `m_new` (`scen_m_new.log`, `run_20261116_164553`) W7h, W7i, W7j passed.
  W7h: c held for b's scene at 197.4 s, a's silence grace at 203.4 s, c's JOIN decided at 212.5 s, `host_back` at 249.3 s.
  W7i: `lag_leave why=lag_debt lag_debt_s=10.1` at 211.6 s. W7j: the barrier-set path (a's BARRIER at 298.09 s, his close
  56 ms later: `host_away why=closed grace_s=60 debt_s=37.7`, b's BARRIER at 298.24 s, b on into stag0102; a back at
  338.1 s, `join_decided` of b's stage group, one instance, `host_left` at the scripts' end).
- **Scenarios, the rest of the marathon** (same relay build of f6276ed4, the later commits notes only; exe b3fa754f): every
  batch passed on its first run, no repeat needed. `m_w4` (`run_20261116_170430`): W4, W4b, W4c, W4i passed. `m_w7`
  (`run_20261116_171740`): W7, W7b, W7c, W7d, W7e, W7f, W7g passed. W7f twice more (`m_w7f_2` `run_20261116_175108`,
  `m_w7f_3` `run_20261116_175647`) and W7g twice more (`m_w7g_2` `run_20261116_180226`, `m_w7g_3`
  `run_20261116_180736`): passed, so W7f and W7g three times each. No `lag_leave` in any of these runs (the lag debt
  takes nobody out on the ordinary cuts and hiccups); W7g ends as in 0.2.0 (nine `host_away`, eight `host_back`,
  `host_lost` inside the closing cut). The PC restarted on 17.11 after the marathon's end; no process of the lane
  survived, every mysqld of the runs logged its shutdown.

### Left

- The server's version (`Directory.Build.props`) is still 0.2.0: the release's integration raises it to 0.2.1.
- No real-client scenario for findings 2 and 5. 2: back from a drop, a host is away for the reporter while his engine
  catches up (its NETFP behind), so his catch-up STATEs are ignored, not stale; the newer-by-tick rule is the unit test's.
  5: an unmodified client keeps sending the host's owner key until it is home (W10c), so its STORY was refused before too;
  the own key needs a modified client, the context an achievement report in that window; unit tests only.
- A JOIN's checkpoint donor (`AskCheckpoint`) is still the connected member longest, also one whose link hangs open or
  who catches up: the join waits `Instances:CheckpointWaitMs` (1 s) and takes the classic state (W7h's c). The reporter's
  away rule would fit there too.
- Seen in the code, not tested: a host whose link hangs open exactly at a stage load (after his BARRIER, before his
  SNAPHASH) holds the new sealed stage group's start for up to `Instances:SnapHashSeconds` (60 s): before sealing starts
  his slot does not fill, so neither the silence grace nor the lag rule sees him.
- W7j takes the barrier-set path while the proxy passes a's close on before b's BARRIER (b's uplink 150 ms slower); in the
  other order the renewed grace applies (its expectation takes either).

### How to run

- `build/r021/env.sh` (paths); `bld.cmd DIR TESTING [dev|nodev]` (the dev client: `bld.cmd dev OFF dev --target dw4_game`,
  copied to `build/r021/scen/dw4_r021_dev.exe`; the client code is main's); `utest.sh LOG [filter]` (unit tests at idle
  priority; `new_filter.txt` holds the fix-021 tests); `itest.sh LOG [filter]` (integration tests, own mysqld on 3750,
  data `build/r021/it-mysql`, test ports 60600-60649, SQL SHUTDOWN after); `scen.sh LOG SCENARIO... [--no-build]`
  (run.py through `wrap/run_wrap.py`, mysqld 3751, ports 60650-60699); `marathon.sh [BATCH...]` (batches `m_new`, `m_w4`,
  `m_w7`, `m_w7f_2`, `m_w7f_3`, `m_w7g_2`, `m_w7g_3`; a failed batch's scenarios once more); `counter.sh` (above).

## Client hardening and release packaging (branch `fix-021-client`, from main d230037f)

Worktree `DW4_PC-wt/r021client`; scripts, builds and logs in `build/r021/` (gitignored, "How to run" below).

| # | What | Where |
| --- | --- | --- |
| 1 | Barrier silence: the five seconds a solo group waits at a barrier without the relay's word (3.4) count only `barrier_pass_limit` (0.5 s) of each long pass of the own machine. W4b's failed run of 0.2.0 was a machine-wide stall (6.3 s): the relay on the same machine stalled too, and the client's first poll after it found no hold yet. A stall of the client alone never mattered (the relay's holds wait in the socket and are read before the check) | `session.cpp` `Session::poll`; unit test `barrier_stall` (with its counter-proof: limit 1000 = before 0.2.1); hooks `DW4_ONLINE_TEST_BARRIER_STALL` / `DW4_ONLINE_TEST_BARRIER_LIMIT` (development builds) and scenarios W4bs / W4bsx |
| 2 | F1 > Settings > Language: a pack without its `game.tsv` says "(pictures only)" | `game_main.cpp`, L10N.md, PLAYER_GUIDE |
| 3 | `--help` names the running program (`DW4_PC` in the release, `dw4_game` in a build folder) and lists every option the parser takes; the error lines use the same name | `game_main.cpp` `print_help` |
| 4 | The release game (DEV=OFF, `DW4_GAME_CONSOLE` OFF) is a Windows program (PE subsystem 2): no console window on a double click. Its lines go to the starter's pipe or file (test runs), else to the console it was started from (cmd, PowerShell: `AttachConsole`), else to `saves/game.log` (the start before: `saves/game.previous.log`). Development builds keep the console | `program_output.cpp`, CMakeLists.txt, README, PLAYER_GUIDE |
| 5 | `cmake --install` produces exactly the release zip's layout; `scripts/package_windows.ps1` builds, installs, checks and zips | CMakeLists.txt, DEVELOPMENT.md "Release packages", L10N.md |
| 6 | README and PLAYER_GUIDE link every doc the zip does not carry absolutely (`https://github.com/Dekirai/DW4_PC/blob/main/...`) | README.md, PLAYER_GUIDE.md |
| 7 | The e-mail texts (the game's Register screen, the website's register and account pages) are true with and without e-mail on the server (the server sends none) | `online_ui.cpp`, `Register.cshtml`, `Account/Index.cshtml` |
| 8 | `tools/online_test/run.py` puts its own folder on `sys.path` (under `python -I` `upload_card` imports `character_test`; no wrapper needed) | `run.py` |

### Checks

Logs in `build/r021/logs/`.

- **Builds** `rel` (Release, DEV=OFF, the options of the main checkout's `build/Game`; `dw4_game.exe` PE subsystem 2,
  `dw4_catalog.exe` 3) and `dev` (DEV=ON, tests; `dw4_game.exe` subsystem 3); 139 compiler warnings in `rel` as in
  0.2.0, none in the changed files.
- **ctest** dev: the first run 76/78 (`online`, `online_env`: the new hook names were on neither list of
  `src/engine/online.cpp`; the first name, `DW4_ONLINE_TEST_STALL`, was round 62's host-loop throttle of L2 and is
  `DW4_ONLINE_TEST_BARRIER_STALL` now); after the fix the online tests 13/13. `dw4_online_session_tests` 590 checks
  (`barrier_stall` with its counter-proof).
- **The Windows program** (`gui_test.py` on a copy as `DW4_PC.exe`): `--help` into a pipe; started by a launcher
  without a console and without handles (as Explorer), `--help` and a hidden `--smoke-test 300` into `saves/game.log`
  (the start before in `game.previous.log`); from a process with a console (one without a window), `--help` into that
  console; an unknown option's stderr line in both. The first try found a real bug: the log was opened twice with
  `freopen_s` (exclusive sharing), stderr was left without a handle and `Engine::log`'s first `std::cerr` line ended
  the game (0xC0000409); stderr now shares stdout's handle (`_dup2`).
- **det.sh** twice on `off/dw4_r021_off.exe` (md5 24baae29): all four runs 4004f056 / audio 396cb219 (`det.log`).
- **Save state suite** 17/17 English (`st_en.log`), 17/17 German (`st_de.log`; "game.tsv 4515 texts, 0 rejected").
- **Server**: unit 703/703 (`unit.log`); the web page tests (`WebTests`, `FriendPageTests`) 10/10 with an own mysqld on
  3755, shut down by SQL (`it_web.log`).
- **The package** (`pkg.cmd 0.2.1`, `pkg.log`, commit abf86e9e, the main checkout's two pins): a fresh Release build in
  `build/r021/pkg`, every content rule held, `DW4_PC-0.2.1-windows-x64.zip` (4.8 MB, 15 files, `sha256sum -c` OK).
  Its file list and order equal the hand-made 0.2.0 zip's (`unzip -Z1` of both). Sizes differ only in the changed
  texts and in line endings: files without an eol rule in `.gitattributes` (LICENSE, `licenses/*-MIT.txt`, the
  language pack) come from the checkout as it is (CRLF here, `core.autocrlf`), the 0.2.0 zip had them from `git show`
  (LF); both read alike. No profile path in any file (also `grep -a` on each), `DW4_PC.exe` subsystem 2 and only
  system DLLs. det.sh on a copy of the packaged `DW4_PC.exe` (md5 c6bf8cb2): 4004f056 / 396cb219 (`det_pkg.log`); the
  unpacked zip's `DW4_PC.exe --smoke-test 1800` exit 0 (`smoke_pkg.log`), `--help` names `DW4_PC`.
- **Scenarios** with the dev client `scen/dw4_r021_dev.exe` (md5 074337af, commit abf86e9e's code; `marathon.log`,
  run.py under `python -I` without a wrapper): W4bs pass (b's 6.3 s stall, 5 holds dropped, b a's guest, 3960 shared
  ticks), W4bsx pass as a counter-proof (b's FAIL at 59 s, "The server did not answer", no arrival toast; the relay's
  `join_ready_timeout` and `fallback_new` as in W4b's failed run of 0.2.0), W1, W4, W10 (`upload_card` imports
  `character_test`) pass, W4b three times pass. W4b's second run had a real machine-wide stall (both clients' longest
  pass 6.5 s, in the lobby before the gate; the user played a game meanwhile) and passed.
- **Final state** at 7b0ded0c (after the scenarios only `session.hpp`'s comment column and these notes changed): `rel`
  and `dev` built again (`dw4_game.exe` md5 72544c60 / 0d532820), ctest dev 78/78 (`ctest_dev_final.log`); the
  package again from the clean checkout: the existing zip refused without `-Force` (`pkg_refuse.log`), then with it
  the same file list, SHA-256 f96745e9... (`pkg_final.log`, `pkg_zip_list.txt`); its `DW4_PC.exe` (md5 437ffb46) det.sh
  4004f056 / 396cb219 (`det_pkg_final.log`) and `gui_test.py` all pass (`gui_test_pkg.log`). A negative run with an
  untracked pack `data/lang/xx` (language.ini and a game.tsv; moved to `build/r021/alt/` afterwards) stopped with
  "lang/xx/language.ini: not part of the release layout" and wrote no zip (`pkg_negative.log`); its game.tsv never
  reached the stage.

### Left

- Merge and push are the user's (not done in this lane). The zip in `build/r021/dist` is the script's test output from
  this branch; the release zip comes from main after the merge (`scripts/package_windows.ps1 -Version 0.2.1`).
- `build/Game` of the main checkout (`Play-DW4-Game.cmd`) becomes a Windows program at its next configure after the
  merge (DEV=OFF). (Corrected in the fix stage: inside a batch file cmd waits for a Windows program too, `start /?`; and
  since the fix stage a run that plays leaves the console for `saves/game.log`, one line in the window naming it.)
  `-DDW4_GAME_CONSOLE=ON` there keeps the old console.
- The AttachConsole path was checked from a process with a console but no window (no visible window was opened); a
  real cmd window is the user's check.
- Files without an eol rule (LICENSE, `licenses/*-MIT.txt`, `data/lang/*`) ship with the checkout's line endings; a
  `.gitattributes` rule would make the zip independent of `core.autocrlf` (not changed here).

### How to run

- Builds: `build\r021\bld.cmd DIR TESTING [dev|nodev]` (Release, Ninja, the main checkout's FetchContent sources): `rel OFF
  nodev` = the release game, `dev ON dev` = the development game with the tests; `ctest.cmd dev`.
- Server: `utest.sh LOG [filter]`; `itest.sh LOG [filter]` (own mysqld on 3755, data `build/r021/it-mysql`, test ports
  60700-60749, SQL SHUTDOWN afterwards).
- `checks.sh det st_en st_de`: det.sh (the main checkout's) twice on `off/dw4_r021_off.exe` (a copy of `rel`'s game), the
  save state suite (`build/r021/st`, a copy of r020's) on `off/dw4_r021_st.exe` in English and German (`build/r021/lang`:
  `data/lang/de` plus the main checkout's git-ignored `local/lang/de/game.tsv`).
- Scenarios: `scen.sh LOG ARGS...` (run.py under `python -I` directly, mysqld 3756, ports 60750-60799; the client
  `scen/dw4_r021_dev.exe`, a copy of `dev`'s game); `marathon.sh [BATCH...]` (`m_stall` W4bs W4bsx, `m_base` W1 W4 W10,
  `m_w4b_1..3` W4b; a batch whose log ends with EXIT is skipped).
- The Windows program: `gui_test.py FOLDER EXE DATA` (with `no_console_child.py`, `attach_child.py`; no window, no
  focus), `pe_subsystem.py EXE...`.
- The package: `scripts/package_windows.ps1` with `-BuildDirectory build\r021\pkg -OutputDirectory build\r021\dist`, the
  main checkout's pins file and `-CMakeArgs` with the FetchContent source folders (`pkg.cmd`).

## The integration (branch `int-021`, 2026-11-17)

Worktree `DW4_PC-wt/int021`, branch `int-021` from main d230037f. Main moved after the two lane merges (the Android UX
line `feat-android-cux`, merged into main as a304d6c9 after the user's Thor check), so main was merged in before the
check pass (step c). Scripts, builds and logs in `build/r021/` (gitignored; "How to run" below). No code changed in
this stage beyond the merges' conflict resolution (notes and harness logs only); not pushed, no tag.

| Step | What | Commit | Conflicts and their resolution |
| --- | --- | --- | --- |
| a | merge `fix-021-relay` (85040ddf) `--no-ff` | 558689ae | none |
| b | merge `fix-021-client` (9ba0fff6) `--no-ff` | f0297de4 | `docs/ROUND_021_NOTES.md` (both lanes added it): both kept, each lane's text as written in its own section (the relay lane's headings one level down), one common intro. SEAMLESS (3.4 and 19.24's W4bs row of the client lane, 6.4, 6.9, 19.6.6, 19.6.7, 19.24, 19.32 of the relay lane) and PLAYER_GUIDE merged by git with both sides' text (checked against each branch) |
| c | merge main a304d6c9 `--no-ff` (the Android port and its console F1, the focus pause on the PC too with default off, the LAN hello's platform tag, Android's online / LAN / cloud refusals, docs) | 7400b578 | `CMakeLists.txt`: the client lane's release layout (install rules) with main's "no DW4_PC on Android" (`if(DW4_BUILD_APP AND NOT ANDROID)`). `game_main.cpp` `main()`: Android's lines to logcat (main), the PC's to the pipe / console / `saves/game.log` (the lane's `route_program_output`, now the `#else` branch: a no-op off Windows anyway), `program_name` for both. `game_main.cpp` `--help`: the lane's `print_help()` with main's new options added (`--mute`, `--env`, `--menu-script`, `--import-disc`), so it still lists every option the parser takes. Merged by git with both sides: `online.cpp`'s test env list (the barrier hooks and main's entries), PLAYER_GUIDE (the lane's absolute links, main's focus pause / menu size / second screen / Thor text), DEVELOPMENT (the lane's "Release packages", main's `cmake` / `android` rows), `online_ui.cpp`. README and SEAMLESS: main did not touch them |

The check pass on 7400b578: below (complete on 18.11; the result at the end of the marathon). An earlier start of it on f0297de4 (before main moved) was stopped;
its logs are in `build/r021/alt/logs_f0297de4_stopped`.

### Checks of the integration

Logs in `build/r021/logs/`. Code of 7400b578 (the later commits notes only).

- **Builds** (Release, Ninja, the FetchContent sources of the main checkout): `rel` = the release game (DEV=OFF,
  BUILD_TESTING OFF; `bld_rel.log`, 139 compiler warnings as in 0.2.0, `dw4_game.exe` md5 5f0cc9b8, PE subsystem 2);
  `dev` (DEV=ON, tests; `bld_dev.log`, md5 a44e81f9, subsystem 3); `plain` (DEV=OFF with tests; `bld_plain.log`).
- **ctest** 81/81 in `dev` (`ctest_dev.log`) and 81/81 in `plain` (`ctest_plain.log`).
- **det.sh** (the main checkout's) twice on `off/dw4_int021_off.exe` (a copy of `rel`'s game): all four runs 4004f056 /
  audio 396cb219 (`det.log`).
- **Save state suite** (`build/r021/st`) on `off/dw4_int021_st.exe`: 17/17 English (`st_en.log`), 17/17 German
  (`st_de.log`; every case "game.tsv 4515 texts, 0 rejected").
- **The PC's F1 against main a304d6c9** (`f1cmp.sh`; main's release game built from a `git archive` of a304d6c9 in
  `build/r021/mainref`, md5 cab4799e; CONSOLE_MENU.md's P0 set: the 8 tabs plus Settings at Graphics and at Second
  screen, 100 % 1280x960 with Xbox prompts and 231 % 1920x1080 with the Thor's, `DW4_MENU_STILL`, frame 240, every exe
  as `dw4_game.exe` in the same folder beside the committed `lang/de`): 20 of 20 pictures bytewise identical. With the
  German pack chosen (`f1lang.sh`, `DW4_LANGUAGE=de`, no game.tsv) the Settings tab differs only in the Language box's
  label: main "Deutsch (Fan-Übersetzung) (pictures only, no game.tsv)", int-021 "... (pictures only)" (100 %: 113
  pixels in x 353-412, y 115-126; 231 %: 1451 pixels in x 916-1130, y 138-166).
- **The console menu's scripts** (`menu.sh`: `tools/menu/run.sh d` on a copy of `rel`'s game beside the committed
  `lang/de`): d1 to d8 without a failed expect (`menu_d.log`).
- **Server**: the solution builds with 0 warnings (`sln_build.log`), unit 716/716 (`unit.log`), integration 155
  passed, 3 skipped (the opt-in end-to-end tests; `it.log`; own mysqld on 3750, shut down by SQL).
- **Publish dry run** (`pub.sh`: `publish.ps1` from a clean `git archive` of HEAD in `build/r021/pub/src`): unit
  716/716 in Release, `DW4Online-0.2.0.zip` (50.6 MB, 374 files, the same file names as 0.2.0's), every assembly
  0.2.0+ecf97f25 (the version bump is the ship stage's), `WorldProtocol.Version` 50, the 14 embedded migrations up to
  0014_reports, no appsettings.Production / Development / Local.json, no key or certificate file
  (`publish.log`, `publish_inspect.log`, `publish_zip_list.txt`).
- **Package dry run** (`pkg.sh pkg 0.2.1`: `scripts/package_windows.ps1` through `pkg.cmd`, a fresh build in
  `build/r021/pkg`, the main checkout's pins file): every content rule held, `DW4_PC-0.2.1-windows-x64.zip` (5.0 MB,
  15 files), its file list and order equal the 0.2.0 zip's; no game.tsv, no PDB, no Android file (no `.so`, APK,
  Gradle, console menu or tools/menu file), no profile path; `DW4_PC.exe` subsystem 2, system DLLs only;
  `sha256sum -c` OK (`pkg.log`, `pkg_zip_list.txt`). The unpacked `DW4_PC.exe` (md5 fa11d8a1): det.sh 4004f056 /
  396cb219 (`det_pkg.log`), `--help` names `DW4_PC` and lists main's new options (`help_pkg.log`), a hidden
  `--smoke-test 1800 --mute` exit 0 (`smoke_pkg.log`).
- **Scenarios**: the marathon (below).

**The marathon** (`marathon.sh`, `logs/marathon.log`, run.py under `python -I` without a wrapper, `--parallel 1`,
BelowNormal; mysqld 3751, ports 60700-60799; the client `scen/dw4_int021_dev.exe`, a copy of `dev`'s game, md5
a44e81f9; W22's protocol 48 client `old48/dw4_old48_dev.exe`, md5 e527c2cf; resumable: a batch whose log ends with
EXIT is skipped, a batch's failures run once more in `scen_BATCH_rep.log`). The runs (17.11 12:25 to 18.11 12:02):

- Passed on their first run: `m_smoke` (W1 W4c W10 W31 S22b RGJ1), `m_new021` (W4bs W4bsx W7h W7i W7j), `ui home`,
  `ui goalong`, `m_w4` (W4 W4b W4d-W4i), `m_w7` (W7 W7b-W7g), `m_prompt` (W2 W2b W11 W11b W11r W30c), `m_drops`
  (S11a S11b), `m_ui` (the ten ui runs), `m_go` (W31b-W31i); in `m_mod` W10b W10c W10d W10s W5.
- **W5s failed once** (`scen_m_mod.log`, `run_20261117_143735`), every expectation held, the relay logged one
  `statehash_mismatch` and `checkpoint_suspect` (why=statehash_rebuilt): c (the moderator) rebuilt from a's checkpoint
  of tick 4179 (16 ticks replayed, READY at 4193 with its check fingerprint equal), joined at 4198, and its STATEHASH of
  4200, two ticks after the join, differed from a's (5817b4ee... / 89453ef7...); every NETFP equal (21 from 4200 to
  5400), the STATEHASHes of 4800 and 5400 equal again. The repeat passed (`scen_m_mod_rep.log`, `run_20261117_150742`:
  c joined at 4203, its first STATEHASH at 4800). No machine stall in the logs. This is the rebuild gap SEAMLESS 19.32
  notes for fix-d43's W7f run `135002` (a STATEHASH after a checkpoint rebuild differs for a while, every NETFP equal):
  a client matter, not from 0.2.1 (neither lane touched the checkpoint or the tick state). Places:
  `src/game/online/session.cpp:2616` (the client's `engine_.tick_state_hash()` at the first period tick after the
  join) and `server/DW4Online/src/DW4Online.Relay/Hub/RoomHub.cs:2381-2383` (the relay's compare and `MarkSuspect`).
- **S14p failed twice** (`scen_m_reg_1.log`, `run_20261117_201312`; `scen_m_reg_1_rep.log`, `run_20261117_204548`),
  both times only on run.py's `story_saved` precondition "the owner's online_story had flag 505 before g set it ((2,
  1))"; every NETFP equal (34 / 38 compared), g's STATEHASH-free run otherwise as expected. Analysis: g logs "505 set"
  (its script step, tick 3397 / 3502), then sends its STORY in the same pass (`online-trace: story ... rev=1`, then
  `story_ack rev=2`); the relay merged it into o's keeper and the server saved revision 2 at once (the relay's `story`
  event account 1 revision 2, at the tick of the set, not earlier: no foreign story reached o's row). run.py's watcher
  (`tools/online_test/run.py:870-879`) reads g's log only every 0.1 s and reads the database's "before" after it saw
  the line; when the save is quicker than that, the precondition (`run.py:1324-1326`) fails although the flag reached
  the owner's row in well under the ten seconds. The same harness race made S14p pass only on its second run in round
  I1b (SEAMLESS 19.32, "a story save faster than the harness's log poll"). On 17.11 the machine was busy (from about
  20:00 the user played a game at normal priority, about ten cores beside the BelowNormal marathon; g's longest pass
  245 ms / 859 ms, g's slot `late`, input delay up to 5), but that was not the cause: on 18.11 S14p failed the same way
  on a quiet machine and passed on a busy one (below). No product fault; a harness race (the check would need the
  database's save time instead of the log poll's).
- `m_i1a_1..3` (W1x W3 W4bx W5f W6 W8 W9, W12 W18 W19 W22 W24 W25 W26, W27 W28 W29 W30 W30b W30x W32) passed;
  `m_reg_1` S1 S2 S3 S5 S14z S16 passed; `m_reg_2` S17a S17b S18b S19a S19b S19c passed.
- **S18 failed once on the busy machine** (`scen_m_reg_2.log`, `run_20261117_204921`): every NETFP equal (188), b's
  script step "mat matz02 no until answered>=1,asking<=0 ... frames>=400" (`S18/b.script` line 20) ran out of its
  400 frames before a's exit prompt asked b (asking=1 came 663 frames after the step began); the clients' longest
  passes 2.9-3.8 s, executions up to 4.0 s, input delay up to 15. Its repeat waits for a quiet machine (below).
- **The load gate**: with the user's game running, repeats would have failed on the same stalls, so the marathon was
  stopped after `m_reg_2` (21:25; its repeat deferred) and goes on as `marathon2.sh`: the same batches (the finished
  ones skipped), before every batch and every repeat `gate.ps1` waits until the processes at normal priority or above
  use less than 3 cores for 45 s (each wait in `marathon.log`), and at the end `m_s14p_q`: S14p once more on a quiet
  machine. Another workflow's normal-priority runs kept the machine above 3 cores, so the gate's threshold became 7 of
  the 16 cores (`gatekick.sh`, 22:22).
- `m_reg_3` (S21 S22a J1 M1 A1) passed. Two runs never started a scenario: S18's repeat (22:22) and `m_extra_1`'s first
  run (22:53) ended with "mysqld did not open port 3751" (run.py's 40 s start window, `run.py:285-286`; both
  `mysqld.err` stop after "Completed initialization of buffer pool" while Dolphin, MMXCM_PC and a game used 6-8 cores at
  normal priority); no scenario result. `m_extra_1`'s repeat (the whole batch) passed J5 JB JR JS JS2 L1; in L1s the
  session ended (17.11 23:21: the clients' exit 1073807364 = 0x40010004, the session end's termination; run.py was cut
  before its summary).
- **Resume (18.11, after the reboot of 09:15)**: HEAD 6274a65f, main still a304d6c9. No process of this stage was left;
  the scenario mysqld's data (`tools/online_test/.work/mysql-data`, 3751) had a stale pid file (moved to
  `build/r021/alt/stale_20261118`) and no clean shutdown: started once on its own for InnoDB's crash recovery and shut
  down by SQL (`logs/mysqld_recover_20261118.*`: "Shutdown complete"); `it-mysql` (3750) had shut down cleanly. The
  rest runs as `marathon3.sh` (`scen3.sh`: test ports 60600-60699, since the user's IDA MCP servers now listen on
  60734-60736; a run that ends before any scenario because mysqld did not open its port is kept as
  `scen_BATCH_infraN.log` and runs again after the gate, at most twice): `m_extra_1b` (L1s), `m_reg_2_s18` (S18's
  repeat), `m_extra_2`, `m_fake`, W7f and W7g three more times each, `m_s14p_q`. Every mysqld start of it opened its
  port in time (no infra retry).
- Passed (18.11, the gate open at 0.3-3.6 cores): L1s (`scen_m_extra_1b.log`), **S18's repeat**
  (`scen_m_reg_2_s18.log`, `run_20261118_092704`: so S18's failure of 17.11 was the busy machine), `m_extra_2` L2 L3
  P1 P2 P3 RG7, `m_fake` (the 9 fake-client scenarios), W7f three times (`m_w7f_1..3`), W7g once (`m_w7g_1`).
- **M2 failed once** (`scen_m_extra_2.log`, `run_20261118_093330`): every fingerprint equal (294 compared, 17,520
  shared ticks), only b's step "idle until lost>=1 fail ... frames>=3000" (`M2/b.script` line 14: b in its menu among
  GENE-01's e_nume, waiting to be hit) ran out: a's fight beside it drew the e_nume (a's HP 49 -> 18), b's HP fell
  (198 -> 192) at frame 3095 of the step, 95 frames after its limit; the clients' passes normal (exec p95 150 ms, no
  stall). The repeat passed (`scen_m_extra_2_rep.log`, `run_20261118_095934`: b hit after 2452 frames). The enemies'
  choice of target, which the script's own comment names (the e_nume "sometimes went for a instead"); no online fault,
  the 3000 frames leave little room. Neither lane touched it.
- W7g twice more (`m_w7g_2`, after the gate waited 32 minutes for the user's game, and `m_w7g_3` after 14): passed.
  So W7f and W7g passed four times each in this marathon (`m_w7` and three more).
- **S14p on a quiet machine** (`scen_m_s14p_q.log`, `run_20261118_113824`, the gate open at 2.0 cores): failed the same
  way, the third time (g's executions at most 363 ms). The relay's `story` event (account 1, revision 2) came at its ms
  125586.9, between its sealing of tick 3301 (125065.6) and 3421 (127068.3), so near tick 3332, and g's "505 set" step
  ran at its engine tick 3314 with input delay 5-6 and a backlog of up to 8 ticks: the save followed g's line by roughly
  0.1-0.3 s, less than the watcher's log poll (0.1 s) plus the `mysql.exe` start of its "before" read
  (`run.py:878-880`). The repeat (`scen_m_s14p_q_rep.log`, `run_20261118_115934`; the user's game ran again, the gate
  waited 18 minutes and opened at 4.8 cores) passed: "flag 505 in the owner's online_story (revision 2) 0.5 s after g
  set it". Four runs: three failed (two on a busy machine, one on a quiet one), one passed (busy): the quicker the path,
  the surer the precondition fails. The harness's race, not 0.2.1's (no change of this round touches the story merge or
  its save: the relay lane's `OnForeignStory` change, `WorldHub.cs:1704-1715`, is for a member alone outside an
  instance; g plays in o's instance).

**The marathon's result**: every one of the 105 scenario directories, the 12 ui runs and the 9 fake-client scenarios
passed (W7f and W7g four times each); every fingerprint (NETFP) compared was equal, except the one tick each of S11a,
S11b, JR and JS2 that their test hooks make differ before the resync READY (allowed). First runs that failed:
W5s (the checkpoint rebuild's STATEHASH gap, a client matter from before 0.2.1; repeat passed), S18 (a 3-4 s machine
stall; repeat on a quiet machine passed), M2 (the enemies' target, 95 frames over its step limit; repeat passed), S14p
(run.py's `story_saved` race: 3 of 4 runs failed, 1 passed). No failure from 0.2.1's changes. Two runs on 17.11 never
started a scenario (mysqld did not open its port in run.py's 40 s on the loaded machine) and one batch was cut by the
session end; each was run again. Every mysqld of the runs ended with "Shutdown complete"; no process of this stage is
left.

### Left (int-021)

- run.py's `story_saved` (S14p): read the owner's row "before" at g's earlier line "505 not in the host's story"
  (`S14p/g.script` line 13: nothing sets 505 between it and the mat), not after "505 set"; or compare the saved
  revision. Harness only.
- M2's `b.script` line 14: 3000 frames for the e_nume to hit b in its menu (this round 3095 and 2452): more room or
  a's fight farther from b.
- W5s: the checkpoint rebuild's STATEHASH gap (SEAMLESS 19.32, the client's first STATEHASH two ticks after a join
  from a checkpoint).
- run.py's mysqld start window (40 s, `run.py:285`) is short on a loaded machine.

### How to run (int-021)

Scripts in `build/r021/` (gitignored), each a copy adapted from the r020 ones: `bld.cmd`, `ctest.cmd`, `utest.sh`,
`itest.sh` (mysqld 3750), `checks.sh` (det, st), `f1cmp.sh` / `f1lang.sh` (the PC's F1 against main), `menu.sh`
(`tools/menu/run.sh d`), `pub.sh` (publish dry run), `pkg.sh` / `pkg.cmd` (package dry run); the marathon:
`marathon.sh` (17.11, without the gate), `marathon2.sh` (with `gate.ps1`), `marathon3.sh` (18.11: `scen3.sh`, ports
60600-60699, the infra retry). A batch whose log ends with EXIT is skipped; `touch build/r021/marathon_stop` stops
before the next batch. `mysqld_recover.py DATADIR PORT PWFILE LOG` starts a test mysqld once for its crash recovery and
shuts it down by SQL.

## The fix stage (int-021, 2026-11-18)

The check pass's failures and the integration's review, fixed on `int-021` (worktree `DW4_PC-wt/int021`, from
2cdda3bc); scripts, builds and logs in `build/r021fix/` (gitignored). Not pushed, no tag.

| # | Finding (review / check) | Fix | Test (fails without the fix) |
| --- | --- | --- | --- |
| R1 | `Supersedes` took another member's STATE at the same tick as a repeat: the two STATEs of tick 0 every client sends (its load report, then its first step's with the load end) counted as one, so a guest's load end that came while the host's link was lost was `state_stale`; and `SyncStateReporter` returned early when the reporter stayed, so its kept STATE never took effect once the member whose newer one was in effect left | `RoomHub.EndsLoad` (at the same tick another member's STATE with the load end supersedes one without it); `SyncStateReporter` applies the reporter's newest whenever it supersedes (`state_kept_took_effect`) | `A_guests_load_end_that_comes_while_the_hosts_link_is_lost_...`, `A_reporters_kept_state_takes_effect_once_the_member_...` (each fails without its part: `logs/unit_counter_endsload.log`, `unit_counter_kept.log`); unit 718/718 (`unit_full_2.log`) |
| R2 | `package_windows.ps1` passed the pin variables only when given: both are CMake cache variables, so a reused `-BuildDirectory` kept an earlier run's test pin (a non-empty `DW4_ONLINE_PINS` wins over any file) and every content rule stayed green; `-NoPins` only lowered `REQUIRE_PINS`, `local/online_pins.txt` still reached the test package | both variables on every run (`-CMakeArgs` names `DW4_ONLINE_PINS`, else empty; the file `-PinsFile` / `local/online_pins.txt`, for `-NoPins` an empty one in the build folder); a content rule: `DW4_PC.exe` holds exactly the pins of their source (its pin-shaped strings, compared case-sensitively); the summary names the pins and their source; `-NoPins` with pins refused | `pins_test.sh` (`logs/pins_test.log`): the script of 2cdda3bc and the fixed one in one reused build folder |
| R3 | The release game, started from cmd or PowerShell, stayed attached to that console for its whole run (the prompt back at once): the console's Ctrl+C reached SDL's quit, Ctrl+Break and closing the window ended it at once (a LAN host's guests lost the session) | `leave_console` (`program_output.cpp`): a run that plays (not `--help` or a tool) prints one line naming `saves\game.log` there, takes stdout / stderr to the log and calls `FreeConsole`; `--help` and the tools still print into the console. Docs (PLAYER_GUIDE "Command line", DEVELOPMENT, README); the client lane's note on `Play-DW4-Game.cmd` corrected (a batch file waits for a Windows program) | `console_test.py` / `console_child.py` (own console without a window, an own desktop never shown, BelowNormal; `logs/console_test.log`, `console_test_play.log`): the exe of 7400b578 is in the console's process list 3 s after the start and Ctrl+Break ends it (0xC000013A); the fixed one is not, runs on 5 s after the break, its lines in `saves\game.log`, the console holds the one line |
| R4 | The console F1 (main's merge) still said "(pictures only, no game.tsv)", and both menus' note said a pictures-only pack changed the game text | `Languages::Entry::menu_label` for both menus; the note says a pictures-only pack leaves the game text English; ANDROID.md's records annotated | `tools/menu/scripts/d8_touch_fixes.txt` expects the label |
| R5 | The e-mail texts promised "never shown to anyone" while the staff's account page shows it | "never shown to other players (the server's staff can see it)" on the website's register and account pages, the game's Register screen and PLAYER_GUIDE | `WebTests` check both pages |
| R6 | `--env DW4_NO_ERROR_BOX=1` did not count (read before the parser: a mistyped option still opened a modal box, Android's only way); `--help` pointed to `docs/DEVELOPMENT.md`, which the zip does not carry | the pre-scan reads `--env DW4_NO_ERROR_BOX=...` wherever it stands; the absolute link | `console_test.py` mode typo: the old exe blocks (its box on the test's own desktop, ended after 20 s), the fixed one exits 1 at once |
| R7 | `build.ps1 -Package` installed over `dist\DW4_PC` without removing what an earlier layout had (0.2.0's `dw4_inspect.exe`, developer docs) | `scripts/install_dist.ps1`: what the build folder's install manifest from before lists in that prefix and the layout no longer has is removed (and folders left empty); saves and settings stay | `install_dist_test.ps1` (`logs/install_dist_test.log`): the plain `cmake --install` leaves them, the script removes them and keeps `saves\`, `settings.ini` |
| F1 | S14p (3 of 4 runs): run.py read the owner's row "before" after g's "505 set", the server saved it 0.1-0.3 s after that line | `story_saved` takes `"before"` (S14p: "505 not in the host's story", about 15 s before the mat); `story_saved_verdict` | `tools/online_test/test_story_saved.py` (fails with the old run.py and scenario.json) |
| F2 | M2 once: b hit 95 frames after its 3000-frame wait | 3600 frames (a's map reading, which b must still see, began 3500 / 4240 frames after b's menu) | the runs below |
| - | W5s's STATEHASH gap after a checkpoint rebuild (a client matter from before 0.2.1, SEAMLESS 19.32) and S18's machine stall | not changed | |

### Checks of the fix stage

Logs in `build/r021fix/logs/`. Final code a3f683b3 (the later commits notes only).

- **Builds** (the check pass's folders `build/r021/{rel,dev,plain}`, incremental, BelowNormal): `rel` `dw4_game.exe` md5
  7039ab62 (PE subsystem 2), `dev` 2c34346b (subsystem 3), `plain` 94c7349b; no warning in the recompiled files
  (`bld_*_2.log`). A first build at 533141c5 (`bld_*_1.log`, rel 6fe24c73) showed the new F1 note filling the window's
  width at 100 % (`f1lang.sh`: `f1/fix/lang_100.bmp`); a3f683b3 shortened it.
- **ctest** 81/81 in `dev` and in `plain`, both builds (`ctest_*_1.log`, `ctest_*_2.log`).
- **Server**: unit 718/718 at a3f683b3 (`unit_final.log`); integration 155 passed, 3 skipped (`it.log`; own mysqld on 3750
  with the check pass's data, shut down by SQL: "Shutdown complete"); publish dry run from a `git archive` of a3f683b3
  (`pub.sh`: unit 718/718 in Release, `DW4Online-0.2.0.zip` 374 files, every assembly 0.2.0+a3f683b3, protocol 50, 14
  migrations, no settings leak, no key or certificate; `publish*.log`).
- **det.sh** twice on a copy of `rel` (7039ab62): all four runs 4004f056 / audio 396cb219 (`det.log`); also at 6fe24c73
  (`det_6fe24c73.log`).
- **Save state suite** 17/17 English, 17/17 German at 7039ab62 (`st_en.log`, `st_de.log`) and at 6fe24c73.
- **Console menu scripts** d1-d8 at 7039ab62: 0 failed expects (`menu_d.log`); d8's new label expect fails on the check
  pass's exe 5f0cc9b8 ("... (pictures only, no game.tsv)", `menu_d8_old.log`). At 6fe24c73 d3 failed 13 expects while
  three builds, ctest and a package build ran beside it (its script raced ahead of dng101b1's load into another Cheats
  page); the repeat on the final exe passed.
- **Package dry run** (`pkg.sh pkg 0.2.1 -Force`, `pkg.log`): from the clean a3f683b3 in `build/r021/pkg` with the main
  checkout's pins file: every content rule held (the new one too: `DW4_PC.exe` holds exactly the file's 2 pins), the
  summary names them and their source, `DW4_PC-0.2.1-windows-x64.zip` 15 files, the same list as the check pass's,
  `sha256sum -c` OK, no profile path; the unpacked `DW4_PC.exe` (769dd96e): det.sh 4004f056 / 396cb219, `--help` names
  `DW4_PC` and the absolute "Test runs" link, a hidden `--smoke-test 1800 --mute` exit 0 (`det_pkg.log`).
- **R2's counter-proof** (`pins_test.sh`, `pins_test.log`): the script of 2cdda3bc after a run with a test pin in
  `-CMakeArgs` built the next `-PinsFile` run and a `-NoPins` run with that test pin (all rules green); the fixed one built
  the file's 2 pins, none with `-NoPins` (while `local/online_pins.txt` held a test pin), that local pin without arguments,
  and refused `-NoPins -PinsFile`. The old `-NoPins` configure in a fresh folder took `local/online_pins.txt`
  (`nopins_probe.log`). The test's local pins file and the old script copy are in `build/r021fix/alt/pins_test`.
- **Scenarios** (`marathon.sh`, `logs/marathon.log`: run.py under `python -I`, `--parallel 1`, BelowNormal, mysqld 3751,
  ports 60600-60699, the relay of a3f683b3, the client `scen/dw4_fix_dev.exe` = `dev` 2c34346b; the load gate under 7
  cores at normal priority before every batch). S14p passed (`f_s14p_1`, `run_20261118_135758`: "flag 505 ... revision 2,
  0.6 s after g set it"). `f_new`: W4bs, W4bsx, W7h, W7j passed; **W7i failed twice** (`run_20261118_140157`,
  `run_20261118_144728`), each time `lag_leave why=streak` at 211.2 / 211.5 s (lag debt 10 / 10.2 s) where its passing runs
  had `why=lag_debt` at 211.5-211.8 s: the user's game at normal priority (8.5-12 cores; the gate opened at 0.8 and 5.0)
  stretched b's passes (315-450 passes/s, longest 146-233 ms, against 700 / 7 ms in the check pass's run
  `run_20261117_130127`, input delay 4-8 against 2-3), so b's pads in the 0.2 s between the first two cuts were not in time
  and its fill streak reached the 10 s on its own. The lag rule took b out either way; the scenario's point (the debt
  where the streak ends every time) needs b's burst in time: a run on a quiet machine below. No change of the fix stage
  touches the lag rule (`RoomHub.cs:3660-3673`). `f_w7`: W7, W7b-W7g passed.
- The rest of the marathon (the user's games at normal priority most of the afternoon; the gate waited up to 36 minutes):
  S14p twice more (`f_s14p_2` 1.2 s, `f_s14p_3` 0.5 s after g's line; so three of three with the fix), `f_w4` (W4, W4b,
  W4bx, W4c-W4i) 10/10, `f_w10` (W10, W10b, W10c, W10d, W10s) 5/5, M2 twice (`f_m2_1`, `f_m2_2_rep`; b hit 351 and 259
  frames into its 3600-frame wait), `f_join` (W1, W2, W11, W31, S11a, S11b, JR, JS, W5, W5s) 10/10 (W5s without the
  rebuild gap this time), `ui accounts` (the Register screen with the new e-mail text) passed. Six runs never started a
  scenario: mysqld stood after "Completed initialization of buffer pool" for more than run.py's 40 s while the games ran
  (`scen_*_infra*.log`); each ran again. So run.py's window is 180 s now and ends at once when mysqld exits (3b2fd68c,
  `test_wait_port.py`).
- **W7i on a quiet machine: not run.** `quiet.sh` waited behind the gate at 2 cores from 19:11 (a game of the user at
  normal priority the whole evening) and was stopped by the main session at 21:04 because the user's PC lagged under
  the test load. W7i stays open: it failed twice under the user's load in this stage (above), passed in the check pass
  and in the relay lane on the same lag code, and the fix stage changed nothing on its path.

**The fix stage's result**: the review's seven findings and the check pass's S14p and M2 fixed (R1-R7, F1, F2) plus
run.py's mysqld window, each with a test that fails without its fix (or a counter-run of the old code); builds, ctest
81/81 twice, server unit 718/718, integration 155 + 3 skipped, publish dry run, det.sh, the save state suite en/de, the
console menu scripts d1-d8, the package dry run and every scenario of the marathon passed (S14p three of three, M2 twice,
W4 / W7 / W10 families, the join set, ui accounts), except W7i (twice `why=streak` under the user's load; its rerun on a
quiet machine is open). Every mysqld that started a scenario ended with "Shutdown complete"; no process of this stage is
left.

### Left (fix stage)

- W7i once on a quiet machine (`build/r021fix/quiet.sh f_w7i_q W7i`, `MAXCORES=2`).
- W5s's checkpoint rebuild gap (SEAMLESS 19.32; passed this time) stays a client matter from before 0.2.1.
- The console detach (R3) was checked in a console without a window; a real cmd window (start `DW4_PC.exe`, see the one
  line, close the window, the game plays on) is the user's check.
- The server's version (`Directory.Build.props` 0.2.0) is the ship stage's.

### How to run (fix stage)

Scripts in `build/r021fix/` (gitignored), copies adapted from `build/r021/`: `env.sh`, `utest.sh` (unit), `itest.sh`
(integration, mysqld 3750 with `build/r021/it-mysql`), `build.sh` / `builds.sh TAG` / `ctest.sh` (the check pass's build
folders `build/r021/{rel,dev,plain}`, logs here), `checks.sh det|pkg|st_en|st_de|menu`, `f1lang.sh`, `pub.sh` (publish
dry run), `pkg.sh` / `pkg.cmd` (package dry run in `build/r021/pkg`, output `distP`), `pins_test.sh` + `pinscheck.py`
(R2), `console_test.py` + `console_child.py` (R3, R6: own console without a window, own desktop never shown),
`install_dist_test.ps1` (R7), `nopins_probe.cmd`; the scenarios: `scen.sh`, `marathon.sh [BATCH...]` (the gate, the
infra retry, a failed batch's scenarios once more), `quiet.sh NAME SCENARIO...` (`MAXCORES`, `MAXWAIT`).

## The ship stage (release 0.2.1, 2026-11-18)

From int-021 77b191d1 (the fix stage's result). Scripts and logs in `build/r021ship/` (gitignored), copies adapted from
`build/r021fix/`.

- **The server's version**: `server/DW4Online/Directory.Build.props` 0.2.1 (8ccc70e0); protocol 50, schema 14, no
  migration. Unit 718/718 at 8ccc70e0 (`logs/unit_int021.log`).
- **Main**: main a304d6c9 is an ancestor of int-021, so main is fast-forwarded to it (no merge of main needed) and pushed
  (main only, no force, no tag).
- **W7i: open for a quiet machine.** The main session decided to ship without W7i's rerun on a quiet machine: it passed
  in the relay lane and in the check pass on the same lag code, it failed twice in the fix stage only under the user's
  load (`why=streak` where the passing runs had `why=lag_debt`; the lag rule took b out either way), and the fix stage
  did not touch the lag path. To do on a quiet machine: `build/r021fix/quiet.sh f_w7i_q W7i` (`MAXCORES=2`).
- **Pushed**: main a304d6c9..33d9f20f (`git merge --ff-only int-021` in the main checkout, `git push origin HEAD`; no
  force, no tag).
- **Server zip** `DW4Online-0.2.1.zip`: `publish.ps1` from the main checkout at 33d9f20f (`logs/publish_main.log`): unit
  718/718 in Release, 374 files with the same names as 0.2.0's zip, every assembly 0.2.1+33d9f20f, `WorldProtocol.Version`
  50, the 14 embedded migrations up to 0014_reports, no appsettings.Production / Development / Local.json, no key or
  certificate (`publish_main_inspect.log`, `publish_zip_list.txt`). 50,205,953 bytes, SHA-256
  `7ed03bb6ca22e030d80831e513740781983ed3d94900e3dbc4928398382875fd`.
- **Game zip** `DW4_PC-0.2.1-windows-x64.zip`: `scripts/package_windows.ps1 -Version 0.2.1 -BuildDirectory build\Game
  -Jobs 4` from the main checkout at 33d9f20f (BelowNormal; the build folder's cached FetchContent sources; the pins from
  `local/online_pins.txt`, the summary's values kept out of the log shown, `logs/pkg_main.log`): every content rule held
  (`DW4_PC.exe` holds exactly the file's 2 pins), 15 files in the fix stage's dry run's list and order, no game.tsv / PDB /
  Android file / profile path, `DW4_PC.exe` PE subsystem 2 and system DLLs only, md5 997f6825 (equal to
  `build/Game/dw4_game.exe`); 5,028,210 bytes, SHA-256 `66bdf7bed2001d0922d051302bdd2e1be9d1cc31167ff2baf2f83119cf603cbd`,
  `sha256sum -c` OK (`pkg_main_check.log`, `pkg_main_zip_list.txt`). `build/Game` is now configured as the package script
  sets it (DW4_GAME_CONSOLE OFF: a Windows program; DW4_ONLINE_REQUIRE_PINS ON).
- **The packaged exe**: det.sh on a copy (md5 997f6825): both runs 4004f056 / audio 396cb219 (`det_pkg.log`); `--help`
  names `DW4_PC`, lists `--host` and `--join` and links "Test runs" on GitHub (`smoke_pkg.log`, `help_pkg.txt`); a hidden
  `--smoke-test 1800 --mute` of `DW4_PC.exe` in a scratch copy of the unpacked zip (no settings.ini, no saves): exit 0 at
  frame 1800 (its lines in the pipe, no file written there).
- **Delivered** to the user's delivery folder (outside the repository): both zips, the changelog, the release notes and the
  server update guide's 0.2.1 box; the 0.2.0 files moved aside (nothing deleted), the user's settings and saves unchanged
  (md5 before and after).
- **How to run** (`build/r021ship/`): `utest.sh LOG [REPO]` (unit tests), `pub_main.sh` (the server zip from the main
  checkout and its inspection), `pkg_main.sh VERSION` / `pkg_main.cmd` (the game zip from the main checkout's
  `build/Game`), `checks_pkg.sh` (det.sh, `--help`, the hidden smoke start), `desktop.sh` and `update_md.py` (the delivery).

## The delivery review's repair (2026-11-18)

A review of the 0.2.1 delivery (main 0ae01a3e, both zips, the delivery folder) had eight findings; each was checked
here. The delivery texts are corrected, no code changed, neither zip rebuilt (their content stands; SHA-256 unchanged:
server `7ed03bb6...875fd`, game `66bdf7be...03cbd`). Scripts and logs in `build/r021repair/` (gitignored); the delivery
texts as they were before in `build/r021repair/before/`.

| # | Finding | Checked | Done |
| --- | --- | --- | --- |
| 1 | The PC's step clock with VSync is new in 0.2.1 and no release text said so: `RefreshClock` (interpolation.hpp) came with the Android line merged as a304d6c9 (1d1f073f, ba2bb7a0, 0cb06d9a; not in d230037f). Offline with VSync on (the default) the game counts its steps in the display's refreshes. Measured on the Thor only (ANDROID.md); on the PC only the class's simulated unit tests ran (`interpolation_tests`: 60 Hz, a 59.94 Hz display that reports 60, 120 Hz, uneven presents, work every other pass, a 144 Hz mode at the game's own pace), never `refresh_paced` itself (`game_main.cpp:3471` leaves out `--smoke-test`, online play and runs without a window) | true; 0.2.0's clock is the `else` branch (the measured time), which `--env DW4_DEV_MEASURED_CLOCK=1` restores in the release game too (a plain `getenv`; not on the online lock list) | a "Frame pacing" line in the release notes and the changelog (with that switch), a sentence in the server update guide's client paragraph; the user's visible check below |
| 2 | R3's test started the game by `subprocess.Popen` without handles and without inheritance, while cmd and PowerShell start a program with inheritance: a Windows program might get the console's handles there, take `route_program_output`'s "usable stdout" branch, never attach, write no `saves\game.log` | not so on this Windows 11 (build 26300). `console_test2.py` / `console_child2.py` (copies adapted from the fix stage's; the packaged `DW4_PC.exe` 997f6825; an own console without a window on an own desktop, BelowNormal) had a real `cmd.exe /d /s /c` start the game (cmd started with inheritance, its standard handles the console's and inheritable), and Windows PowerShell 5.1 (`& 'EXE' ...; Start-Sleep 8`: the shell stays, as at its prompt). Each time the console held the one line, the game was not on the console's process list after 3 s and its lines were in `saves\game.log` (`logs/console_test2.log`, `console_test2_pswait.log`; the Popen control as in the fix stage). A shell that ends at once (`powershell -Command` without the wait) gets no line, since AttachConsole finds no parent console; the log is written all the same | release texts unchanged; a real cmd window stays the user's check |
| 3 | W7i: `why` names the fill streak before the lag debt when both limits hold (`RoomHub.cs:3667-3668`) | true: the fix stage's two failed runs logged `why=streak` with `lag_debt_s` 10 and 10.2, so in the second the debt had reached the limit as well. In both, b's pads between the cuts were not in time and its streak never ended (the scenario's premise); with the streak ended at each burst only the debt reaches the limit and `why=lag_debt` follows (the passing runs). A quiet rerun with `why=streak` and `lag_debt_s` of 10 or more means the premise failed again, not the rule | notes only (the expectation and the order are test and relay code: not changed) |
| 4 | Four files of the game zip have CRLF where 0.2.0's had LF: `lang/de/language.ini`, `lang/de/textures.tsv`, `licenses/enbrip-MIT.txt`, `licenses/pl_mpeg-MIT.txt`. `cmake --install` copies the working tree (`core.autocrlf` true from the Git installation's system config); those files are `text=auto` without an eol rule | true (`git ls-files --eol`: `w/crlf`, pl_mpeg `w/mixed`; the zip's copies have 17, 8, 21 and 17 CRs, 0.2.0's none, and without the CRs they are md5-equal to 0.2.0's). The parsers drop `\r` (`language_pack.cpp`, `ini.cpp`); a package from a clone with autocrlf false gets another SHA-256 | not rebuilt (both read alike; the client lane's notes named it); an `eol=lf` rule in `.gitattributes` would make the package independent of autocrlf (left) |
| 5 | The release texts said the Second screen shows "every player's form, level, HP, MP, EXP and Bits": other online players in the stage (their presence, `screen2.cpp:57-67`) get form, level, HP and MP only | true | release notes and changelog corrected; PLAYER_GUIDE ("every joined player's") and the F1 hint were right |
| 6 | `build/r021ship/logs/desktop.log`'s last compare says "differ" | true, only the path prefix (the stage-start file names `PC/settings.ini`, the end one `settings.ini`); the three md5 values are equal, checked again here, nothing under `saves` newer than 16.11 | a line appended to `desktop.log` |
| 7 | Five `tail -f` watchers of earlier stages still run (two on `build/r021/logs/marathon.log`, three on `build/r021fix/logs/marathon.log`, started 18.11 09:21-14:54, their parents gone), although the check pass's and the fix stage's notes say no process was left | true (PIDs 31088, 66096, 119320, 134132, 135408) | not ended here (not this stage's processes): `taskkill /PID 31088 /PID 66096 /PID 119320 /PID 134132 /PID 135408` in cmd, or a restart |
| 8 | The server DLLs name their PDB path (the build machine's profile folder) | true, as in 0.2.0 and every server release before; the server zip goes to the own server, not to the public release | none |

### Left (repair)

- **The PC's step clock, the user's visible check** (finding 1): offline, VSync on, started from cmd as `DW4_PC.exe --env
  DW4_FRAME_STATS=10`, a minute at the title and one in a dungeon. `saves\game.log` should have "frames: the step clock
  counts in the display's refreshes (X ms)" (X = 1000 / the monitor's rate), about 59.9 steps/s in every ten-second line
  and "a 0 next to a 2+" at or near 0 (at 60 Hz a lone one about every 16.7 s). For a comparison the same with `--env
  DW4_DEV_MEASURED_CLOCK=1` (the clock of 0.2.0). Worth it at 144 Hz and with variable refresh.
- ~~W7i once on a quiet machine~~: done, passed ("W7i's rerun" below).
- A real cmd window for R3 (the one line, close the window, the game plays on).
- ~~The five watchers of finding 7~~: ended by the main session (their own PIDs).
- An `eol=lf` rule for the installed text files (finding 4), before the next package.

### How to run (repair)

`build/r021repair/console_test2.py NAME EXE [play cmd cmdinh ps pswait cmdhelp]` (results in `contest/NAME/`; the game
started by the shell is found by its parent PID and ended by that PID after 5 s).

### W7i's rerun (main session, 18.11.2026)

On the user's request, after the delivery: `W7i` alone on the shipped code (int-021 77b191d1's relay; the client
`build/r021fix/scen/dw4_fix_dev.exe`), run.py `--no-build --parallel 1`, mysqld 3751, ports 60600-60699, without the
gate and at normal priority (the earlier runs at BelowNormal lost b's 0.2 s pad bursts under the user's games). Log
`build/r021fix/logs/scen_f_w7i_now.log`, run folder `run_20261118_220447`: **PASS**, `lag_leave why=lag_debt
lag_debt_s=10.1` at 211.3 s, as in its passing runs of the lane and the check pass (211.5-211.8 s). The fourth online
fix of 0.2.1 is so covered by a passing real-client run on the final code; nothing changes in the release.

