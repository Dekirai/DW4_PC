# Round 0.2.1: progress notes

Release 0.2.1 (release numbers follow the server; the game's own version stays the CMake project's 0.46.0, protocol 50
unchanged: 0.2.0 clients keep working with a 0.2.1 server and the other way round). Lanes in their own worktrees and
branches; these notes are the client lane's.

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

## Checks

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

## Left

- Merge and push are the user's (not done in this lane). The zip in `build/r021/dist` is the script's test output from
  this branch; the release zip comes from main after the merge (`scripts/package_windows.ps1 -Version 0.2.1`).
- `build/Game` of the main checkout (`Play-DW4-Game.cmd`) becomes a Windows program at its next configure after the
  merge (DEV=OFF): cmd does not wait for it and its lines go to the cmd window only while that is still open, else to
  `saves/game.log`; `-DDW4_GAME_CONSOLE=ON` there keeps the old console.
- The AttachConsole path was checked from a process with a console but no window (no visible window was opened); a
  real cmd window is the user's check.
- Files without an eol rule (LICENSE, `licenses/*-MIT.txt`, `data/lang/*`) ship with the checkout's line endings; a
  `.gitattributes` rule would make the zip independent of `core.autocrlf` (not changed here).

## How to run

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
