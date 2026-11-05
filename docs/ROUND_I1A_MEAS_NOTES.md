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
- 16:40: `tools/world_probe.cpp` (redirect, homeover, settings). redirect
  and homeover measured (`build/meas/redirect1.log`, `homeover1.log`,
  `ho2_c*.log`, traces `ho_s*.log`).
