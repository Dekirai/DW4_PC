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
| 10 | scenarios | `tools/online_test`: J1 / J5 / J15 / J30, J15x4, JW, JF, JR, JB, JS, JS2 (review), JL; regression S1, S2, S3, S11a, S17a/b, S18b, S19b, L1, L2, M1, M2 | hidden clients on the own loopback stack (mysqld 3560, ports 56150-56199) |

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
- 23:00: real hidden clients: J1 (a minute) and J5 (five minutes) join
  from a checkpoint: BARRIER_OK to JOIN 307 / 298 ms, rebuild 136 / 124 ms,
  3 / 7 ticks replayed (classic J1: 3,622 ticks, 852 ms). Fixed on the way:
  SNDF banks and bank slots by name, the audio output's fields, a restored
  engine's common banks first (STATEHASH mismatches of J1 runs 1-3).
  Resync from the newest agreed checkpoint, periodic uploads (30 s),
  protocol 47 gate, JB / JS hooks and scenarios, SEAMLESS.md 3.7 and
  "Round J (built)". det.sh 4004f056 / 396cb219 twice (build/det_j2.log).
- Open (was): resync from a checkpoint (JR), periodic uploads, the window trees
  (JW), TEXTCTL texts and the music position for the joiner's picture and
  sound (presentation only), JS / JB / JL / JF scenarios, the regression
  scenarios, the protocol number (47 at the integration), SEAMLESS.md 6.13
  measured numbers.
- 2026-11-05 07:45 (third run, after the overnight shutdown): JS's hook
  also leaves the system generator out (bbf8028; the models' HP of the
  first minute equal the load end's, so run 4 never parted). JB failed on
  the classic joiner's STATEHASH, JR (co-arrival) at its load end: a member
  that loaded the visit live kept the last stage's game SNDF (ringing
  common sounds, freed sfp entries' flags, the allocation counters), a
  restored engine starts it free. Fix (c102749): online, a live load's
  snapshot gives the game SNDF and the sfp pool a restored engine's state
  (`SoundManager::fresh_game_driver`; the audio's mirror keeps what rings).
  After a resync from a checkpoint the Lua collector's live bytes drifted
  (+46: strings counted per std::string object, a restored VM cannot keep
  which values share one); strings now count once per content, as Lua 4's
  interned TStrings (c102749). ctest `tick_state`: the straight run loads
  the common banks first, as a restored engine (bd2b80f; it failed in all
  four scenarios since bb527a7). Runs (build/jset_run6.log): J1, JB pass.
- 10:40: regression on the final build: S1, S2, S3, S11a, S17a, S17b, S18b,
  S19b, L1, L2, M1, M2, J1, JB pass (build/reg_run1.log; S3 failed there and
  passed after a7392b9, build/jset_run9.log), JL = J1 behind 20 ms each way
  passes (build/jl_run1.log, JOIN 373 ms after BARRIER_OK). Two more fixes:
  the session's present mask after a checkpoint rebuild (a7392b9: S3's
  second joiner missed the first joiner's slot) and the SceneCache's stale
  edited-attribute keys of released documents (6945f60: S2's visit after
  the fork, STATEHASH never agreed). ctest 54/54 (build/ctest4.log),
  DW4Online.Tests 248/248 (build/server_unit1.log), det.sh 4004f056 /
  396cb219 twice (build/det_j3.log, build/det_j4.log). SEAMLESS.md "Round J
  (built)" has the numbers.
- Open (next run): SUBWND window trees in the tick state (JW: M1's join
  while a's status window is open still replays the visit, 2.3 s; a quick
  win before that: the donor answers an ask at once when its ticks are
  unsafe, the relay then skips CheckpointWaitMs), the joiner's music
  position, STATEHASH cost (7-10 ms every 600 ticks), scenarios J15 / J30 /
  JF (J15x4 / J30x4 need four game processes: more than this machine's
  three), the integration with round G-core (both protocol 47).
- 11:05: on the final build (6945f60) JB, JR and JS pass again
  (build/jset_run10.log), JL too (build/jl_run1.log).
- 05.11 afternoon (integration branch `int-r69r70`): the review's findings
  fixed there (blockers: the entry context at the load end, a tick state
  writer's error no longer closes the game; the should_fix list and the cheap
  minors); SEAMLESS.md 17 "Integration of G-core and J", "Review fixes".
  Open: the joiner's music position within an unchanged file, STATEHASH /
  upload cost on a worker, AskCheckpoint's donor choice, the common loops
  over a live load (`fresh_game_driver`).
- 05.11 evening (`int-r69r70`): with the review's READY check JS's missing
  field shows at READY (`ready_mismatch`, `checkpoint_suspect` why=ready, the
  classic state, no desync; run.py's per-scenario `expect_relay`); the new
  JS2 (`DW4_ONLINE_TEST_DESYNC=join+1500`, development builds only) covers
  the drift after a passed READY (`desync_rebuilt`, the classic resync).
  SEAMLESS.md 17 "JS and JS2 after the review". Independent check: JS, JS2,
  J1, J5, RGJ1, S3, JR and ui newchars pass, ctest 54/54, det.sh 4004f056 /
  396cb219 twice on a build without DW4_ONLINE_DEV (build/k_*.log).
- 05.11 evening, lane `jready` (branch `fix-j-ready` from main a475b98a,
  worktree `DW4_PC-wt/jready`, mysqld 3620, ports 56600-56649): the join
  latency the last verification found (READY waited for the next multiple of
  60: J1 859 ms, up to about 1.2 s). Design: READY from a checkpoint names its
  *check tick* T + 14, whose NETFP the donor sends after every upload
  (protocol 48, `wire::kReadyCheckTicks`, `Wire.ReadyCheckTicks`); the relay
  compares it there. Relay c98a553f, client e1aacb45; HubLogicTests 62/62,
  session tests 233 checks. Baseline J1 on this machine (main):
  522 ms (`build/scen_base_j1.log`). Next: scenarios, docs.
