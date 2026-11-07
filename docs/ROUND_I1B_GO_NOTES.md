# Round I1b, lane *go along* (19.30.5-19.30.7): progress notes

Branch `feat-i1b-go` from int-i1a bfc3b0a5 (worktree `DW4_PC-wt/i1b-go`). Contract: `DW4_PC-wt/i1b-contract.md`
(entries "go 2026-11-07 16:15": the wire layout and the relay's options and log names). Own resources: mysqld 3653,
ports 58400-58599, build dirs `build/go/*` (gitignored).

## Done

- First task (measured, SEAMLESS.md 19.30.7 "Measured (round I1b, go lane)"): `dw4_world_probe DATA go [CASE]`
  (log `build/go/meas/go_r2.log`).
- Engine: `Engine::online_go(mask, stage, point)`, `ForkKind::go` (the frame after the step, like give-up and HOME),
  the GO's departure (the half fade, `departure_work` "trans", NEXTSTAGE with `I<1>`: the zone remap skipped,
  `managers_core.cpp`), `ExitHeading::asked`, `kGoExpiryTicks` (300). Offline nothing of it runs.
- Client wire: OFFER 0x9B, HEADING + asked, STATE flag 16, golden vectors (`tests/online_wire_tests.cpp` go_messages).
- Client session (`src/game/online/session_go.cpp` + hooks in session.cpp): the step's GOs in one `online_go` per target,
  the expiry (notice + INTENT 4, 0), JOIN_REQ kind go, OFFER bookkeeping with the notice's wait, INTENT 4 / withdraw,
  WORLD_NOTICE go refused texts, STATE no living member, the GO barrier's loading lines ("Joining Kim...", after 10 s
  "Waiting for Kim's floor..." with `Overlay::go_back` and `go_back_to_lobby()`). Session test `go_along_session`.

## Left (in this order)

1. Relay: offers, GoAlong wait and stamp, JOIN_REQ go, the GO's barrier, expiry backstop, HEADING asked, the
   no-living-member hold, WORLD_NOTICE go refused, OFFER frames; unit tests.
2. Client UI: the notice window, the Instance page's *Go along* box (B: Close, the half second), the waiting row.
3. Scenarios W31, W31b-W31i, W31d2 (harness files and test hooks).

## How to run the tests

- C++: `cmd //c "start /low /wait /b cmd /c build\go\bld.cmd rel ON nodev"` (Release, DEV=OFF, tests);
  `cmd //c "build\go\ctest.cmd rel -R online"`; single: `build/go/rel/dw4_online_session_tests.exe`,
  `build/go/rel/dw4_online_wire_tests.exe`.
- Probe: `build/go/rel/dw4_world_probe.exe "E:/Games/Emulator/Dolphin-x64/Games/Digimon World 4 - Extracted" go [0|1|2]`.
