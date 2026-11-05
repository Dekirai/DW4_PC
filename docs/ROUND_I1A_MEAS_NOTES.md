# Round I1a's first tasks (r71): the measurements - progress note

Branch `dref-i1a-meas` (worktree `DW4_PC-wt/i1ameas`), from main a475b98a.
Task: SEAMLESS.md 19.25, "Round I1a ... First tasks (measurements, into this
section before the lanes build on them)". Results go into SEAMLESS.md 19 at
the places that ask for them, marked "measured r71". This file is only the
progress note a later run continues from (the PC may shut down).

Own stack: mysqld 3624, ports 56800-56849 (loopback only). Hidden and
headless only, own PIDs only, at most three own game processes.

| # | Measurement | Where it goes | Tool |
| --- | --- | --- | --- |
| a | settings at a restore | 19.8.7 | `dw4_world_probe DATA settings` |
| b | redirect against NEXTSTAGE, generate point | 19.8.3 | `dw4_world_probe DATA redirect` |
| c | only defeated players after HOME | 19.6.5 | `dw4_world_probe DATA homeover [CASE]` |
| d | load gate, all cells | 19.17.4 | `DW4Online.FakeClient loadgate` (in-process hub) |
| e | 30 ghost figures, half rate | 19.13 | game probe `DW4_PROBE_GHOSTS` (build option `DW4_GHOST_PROBE`) |

Builds: `build/bld.cmd DIR TESTING [dev|nodev] [--target T]` (Release, Ninja,
`--parallel 2`); `build/rel` is the Release build without DW4_ONLINE_DEV.
Logs: `build/meas/*.log`.

## Progress

- 2026-11-05 16:00: worktree created, `build/rel` built.
- 16:20: `tools/world_probe.cpp` (redirect, homeover, settings) measured:
  `build/meas/redirect1.log`, `homeover2.log` (+ `.err` script trace),
  `ho4_c2.*`, exit prompt traces `ho_c*.err` / `ho_s*.log`, `settings3.log`
  (classic and checkpoint joins).
- 16:30: ghost probe (`DW4_GHOST_PROBE`, `build/ghost`) at 1080p and 1440p
  MSAA 4: `g_r1_*`, `g_r2_*` (`g_table.py` sums them up).
- 16:45: load gate cells and joins: `loadgate2.json`, `joins2.json`
  (`lg_table.py`); the experiment without the PING margin
  `loadgate3_nomargin.json` (RoomHub.cs restored afterwards, not committed).
- 16:50: SEAMLESS.md 19.6.5, 19.8.3, 19.8.7, 19.13, 19.17.4 and 19.25
  (M1-M5) written.
- 17:05: `build/rel` with the tests (bld_test.cmd): det.sh 4004f056 / 396cb219
  twice (`build/det1.log`); ctest running (`build/ctest1.log`).
