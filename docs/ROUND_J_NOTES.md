# Round J (r70): instant joins with relay checkpoints - lane plan and progress

Branch `feat-r70join` (worktree `DW4_PC-wt/r70join`), from main b226bc0.
Design: `docs/SEAMLESS.md` 6.13.1-6.13.5, 6.13.7, 6.13.8 and "Round J
(proposed r66)"; round 67's plan (19.25): J before I3, message numbers
after J (J holds client 0x15-0x17 and server 0x92; 0x93 EVENT_AHEAD is
round R's). Not here: 6.13.6 (rollback, round R).

This file is the progress note a later run continues from (the PC may shut
down at any time). Keep it short; the design stays in SEAMLESS.md.

## Lanes, in build order

| # | Lane | Scope | Test |
| --- | --- | --- | --- |
| 0 | context gap | relay: an arrival may join a visit whose own load changed the session context (dng101b1's COUNTER 2), not one whose play did | `HubLogicTests` (done) |
| 1 | tick state core: archive | `include/dw4/engine/tick_state.hpp`, `src/engine/tick_state.cpp`: sections (tag, length, bytes), header `DW4T`, raw / canonical form, XXH64 (zstd's xxhash), id codecs (documents, ObjectView, Node, Ram), `Engine::state_safe`, `save_tick_state`, `apply_tick_state`, `tick_state_hash`, `load_end_tick` | unit checks in `tick_state` |
| 2 | tick state core: engine | engine scalars and online fields, generator, game system (`serialize_system`), flags / counters, messenger and demo (with the exit port as a tag), model script contexts | divergence trials |
| 3 | tick state core: scene graph | node values by id ((document, reference) of the SceneCache; owned nodes by owner tag and ordinal), edited attributes, `AnimationDatabase::looping_`, lazily created nodes made by id | divergence trials |
| 4 | tick state core: MODELCTRL, ACTION2, GENERATER | the RAMs' mutable fields (pointers into per-info data as indexes, nodes as ids), the update order, `item_labels_` | divergence trials |
| 5 | completeness harness | ctest `tick_state` (game data): in-place divergence trials (save at T, wrong pads, apply, true pads, compare fingerprints and net fingerprints with the straight run; idempotent bytes), the cross-engine rebuild (load snapshot, `[0, K)`, journal, apply, hash, continue), `tools/online/tickstate_diff.py` | itself |
| 6 | topology journal | the six entry kinds (package load, weapon attach / detach, LAYER ORDER / REMOVE, `attach_scene_root`, round 63's copies) with ticks and arguments, their re-run, the topology hash | rebuild test |
| 7 | tick state rest | Lua VM `io` (slot layout, GC counters, closures by prototype path), CAMERA, HitLand / collision / ChangePos, sound simulation (sfp, SNDF driver, streams' game parts, `CriStreams`), the small managers, TEXTCTL lines keep their text, SUBWND programs reachable in fields | divergence trials over the stage set |
| 8 | relay | STATEHASH (0x15) with K and its vote, CHECKPOINT_ASK (0x92), CHECKPOINT (0x16) store (two per instance, fork-aware), CHECKPOINT_BAD (0x17), STATE_DATA layout 2, the join with a fresh or stored checkpoint and its waits, the fallbacks (retry, classic path, NEW), resync from the checkpoint, the suspicion rule, `Instances:CheckpointJoins`, READY's *from checkpoint* flag, protocol version | relay unit tests, fake clients |
| 9 | client | zstd encoder in the build; checkpoints in the session (STATEHASH at `C_k`, the donor's upload in chunks), the join path (rebuild, K check, self check, short catch-up, "Joining..." without a countdown), fallbacks, resync from a checkpoint | ctest `online`, real-client scenarios |
| 10 | scenarios | `tools/online_test`: J1 / J5 / J15 / J30, J15x4, JW, JF, JR, JB, JS, JL; regression S1, S2, S3, S11a, S17a/b, S18b, S19b, L1, L2, M1, M2 | hidden clients on the own loopback stack (mysqld 3560, ports 56150-56199) |

Offline stays bit-identical: `work/dref/checks/det.sh EXE NAME` must give
4004f056 / 396cb219 (twice) after each engine lane.

## Rules of this round

Hidden and headless only (`DW4_NO_ERROR_BOX=1`), loopback only, own PIDs
only, no hard deletes, at most three own game processes, `nice -n 10`,
Ninja `--parallel 2` (`build/bld.sh DIR TESTING [dev]`). No push, no merge.
Never hand a player story progress he did not earn (only the exact-same-
story-point credit).

## Progress

- 2026-11-04 21:30: worktree reused (the first run committed nothing).
  Lane 0 done (c57d19b): `HubInstance.ArrivalContext` / `EntryContext`,
  `HubRules.ContextMatches`; HubLogicTests 41/41.
- 22:30: lanes 1-7 built far enough for joins in fields: `tick_state.hpp/.cpp`,
  `tick_state_managers.cpp`; every manager's section, the SNDF driver and
  the CRI streams, the Lua VM (`Lua4Vm::save_state`), the exit port, the
  topology journal (packages, window copies, line copies) and the structure
  pass (weapon parts). ctest `tick_state` (4 scenarios: stag0101 x2 with a
  mid-visit join, dng101b1 x2 with an own status window, stag0102 x3 with
  both, stagBS02 x4): every trial and rebuild equal, idempotent bytes.
  det.sh 4004f056 / 396cb219 twice (build/det_j1.log).
  Still unsafe: a running SUBWND program (own status windows, JW); the
  window trees are not serialized yet. TEXTCTL texts (presentation) not yet.
  Costs on this machine: raw 2.0-2.4 MB, save 6-10 ms, hash 6-10 ms, apply
  3-5 ms (too slow for STATEHASH every 600 ticks; optimize).
- 23:00: compact form (b568863): raw 1.07-1.25 MB, save 3.4-5.2 ms.
  Relay lane (4dd3eba, c4c29bf): STATEHASH / CHECKPOINT / CHECKPOINT_BAD /
  CHECKPOINT_ASK, the store, the fresh / wait / stored / classic order,
  STATE_DATA codec 2, the suspicion rule, `Instances:CheckpointJoins`,
  WELCOME flag 4; DW4Online.Tests 247/247. Client lane (733ee3b, 91c2b42):
  STATEHASH every 600 ticks, the donor's upload (zstd level 3), the join
  from a checkpoint (load ticks, K check, apply, hash, short catch-up,
  READY flag), CHECKPOINT_BAD; ctest 54/54 after the wire test update.
  Scenarios J1 / J5 written (`tools/online_test/scenarios`), run with
  `build/m_scen.sh dev/dw4_game.exe run.py J1` (own mysqld 3560, ports
  56150-56199).
- Open: resync from a checkpoint (JR), periodic uploads, the window trees
  (JW), TEXTCTL texts and the music position for the joiner's picture and
  sound (presentation only), JS / JB / JL / JF scenarios, the regression
  scenarios, the protocol number (47 at the integration), SEAMLESS.md 6.13
  measured numbers.
