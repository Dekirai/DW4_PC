# Round fix-019: progress notes

Branch `fix-019` from main 9fcd0681 (DW4Online 0.1.8, live), worktree `DW4_PC-wt/fix019`. It fixes the eleven findings of
the adversarial review of 0.1.7 (main 3ad4621e; 9fcd0681 only adds the X-Forwarded-Prefix change) and becomes DW4Online
0.1.9. Never merged into main from here. Data Overflow is out of scope (its ctest suites only stay green).

Own resources: mysqld 3671, ports 59000-59199, build dirs `build/f19/*` (gitignored).

## Done

| # | Finding | Commit |
| --- | --- | --- |
| 10 (part) | `src/game/game_main.cpp`: the comment before `view.gain` indented by 12 spaces | (this commit) |

## Left

- 1-9, 10 (docs), 11 (version 0.1.9).

## How to run

- C++: `cmd //c "start /low /wait /b cmd /c build\f19\bld.cmd rel ON nodev"` (Release, DEV=OFF, tests) and
  `... bld.cmd dev OFF dev --target dw4_game` (DEV=ON game for the scenarios); `cmd //c "build\f19\ctest.cmd rel"`.
