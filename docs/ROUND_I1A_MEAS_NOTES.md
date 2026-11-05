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
  twice (`build/det1.log`); ctest 56 of 56 (`build/ctest1.log`); the server
  solution builds (FakeClient with LoadGate.cs). Done; the lanes take M1-M5
  from SEAMLESS.md 19.25.

## Review fixes (2026-11-05, evening)

The review of the measurements (not ok to merge) found: the load gate's
yaw grew past MaxYaw and the hub dropped about half of the presence (all
fan-out numbers too low); M3 would count a *Give up* as defeated; the
TryEnter misses are most likely the harness's timers; the gate's max
criterion was rewritten by the measuring round; CPU and joins not measured
as 19.17.4 asks; M2 measured only solo; the alive mask never read; minors.

- 17:25: commit 0b3cdee7: LoadGate yaw in -pi..pi, invalid runs fail
  (exit 1), joins cell under the base load, wake diagnostics, NETFP only for
  sealed ticks; LoadGateNet (`loadgate --sockets`: a real RelayHub in a child
  process on 56810+); RelayHub.SealRoom with SealPasses / SealTryMisses (the
  pass's time read after the lock is taken), RoomHub.SealLateness; tests.
- 17:30: `build/meas2/run_gate.sh r1` (in-process, sockets, idle-hub joins;
  DOTNET_gcServer=1): `build/meas2/loadgate_r1.json`,
  `loadgate_net_r1.json`, `joins_r1.json`, summary `lg_table2.py`.
- Next: world_probe (redirect with two / three present and own slot 1,
  before the load step; homeover with the alive mask of every stage change
  and offline cases; settings DISSOLVE with fast loading; the card folder
  removed on failure), `build/meas2/run_probes.sh r1`; then SEAMLESS.md.
- 17:41: probes `build/meas2/redirect_r1.log`, `settings_r1.log`,
  `homeover_r2.log` (+ `.err` with DW4_TRACE_SCRIPT=1), single cases
  `ho_r1_c*`, `ho_r2_c*`. Load gate: in-process all valid (fullhub 398,538
  frames/s, 141 KB/s per receiver; select 23 KB/s); wake diagnostics: the
  in-process TryEnter misses are coincident timer wakes; sockets (real
  relay): misses 0.5-1.1 %, lateness p99 under 2 ms, relay CPU base
  0.72-0.85 / select 0.66 / fullhub 4.0 cores (fails); the write/read
  coalescing experiment (`build/meas2/connection_batch_experiment.patch`,
  reverted) changed nothing measurable (`loadgate_net_batch_r1.json`).
- 17:50: SEAMLESS.md 19.6.5 (Give up exception, alive masks, offline, the
  mat prompt is HOME's), 19.8.3 (two and three present, before the load
  step, the assertion), 19.8.7 (the DISSOLVE set-back), 19.13 (nine reports,
  the half rate, M5 conditional, 16 KB/s), 19.17.4 (redone), 19.25 (M1-M5).
  Next: ctest, server tests (unit + integration on mysqld 3624, ports
  56800-56849).
- 17:55: `build/meas2/verify.sh`: server build 0 warnings; unit tests 268 of
  268; integration tests 97 passed, 3 skipped (the WinHTTP client is not
  built; one of them uses the certificate store, which this lane must not
  change) on mysqld 3624 (stopped after); ctest 56 of 56 (`v_ctest.log`).
  det.sh not run again: src, include and CMakeLists.txt are unchanged since
  45f5f2a4 (det.sh 4004f056 / 396cb219 twice there, by the builder and the
  review). Open for later rounds: the gate again after M4 (a), (c), (e) on a
  quiet machine; the max criterion's reading (the user); STORY merges and
  mirrors; joins over sockets; a weaker GPU for M5; the probe cases as
  ctests once M1 and M2 are built.
