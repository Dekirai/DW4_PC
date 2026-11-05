# Round G-core (r69): guests on the host's story - lane plan and progress

Branch `feat-r69gcore` (worktree `DW4_PC-wt/r69gcore`) from main b226bc0
(round 59b's three server fixes on 2d8fbee). Not merged, not pushed.

Scope: docs/SEAMLESS.md 17 "Round G" trimmed as 19.25 step 1 says: 503 as
story, the key setter stages, the rebase, the Game Start's hold, the call shop
closed, `common_rebuilt`, the card store's story override, the own story bytes
in `CharacterSync`, the reason *queued*, the twins; plus the guest's toast, the
story line and the "moved on" notice, and `numemon_freed` by mask (8.8).
Dropped (19.8.9, 19.8.10): the hub extras lane (no `online_rewards` store: in
the world the rewards are per box by construction; no quest gates with the
owner's tiers: answer 10 (b) has no case in the world, 19.8.9 is the newest
decision) and the online difficulty dialog rule (the host's box decides from
round I1a on, the original dialog stays).

The user's rule for every lane: never hand a player story progress he did
not earn himself. The guest's own story never receives the room story (the
server composes the own story into every upload; the client's card and cache
hold the own story; nothing of the room story reaches `savedata.dat`).

Round 59b in the room story (kept, built on): the owner-zero start (8.6,
*not_started*), the guests' spacing (phase 300 s, keys 60 s, flags 8 then one
per 30 s), the session growth caps for guests only (the owner's counters are
not capped, 11c4136), held flags take no flag tokens (75bd7df), a joiner may
report the joined visit's counter line (d54d7b0), the earned queue and the
owner's exemption (the keeper still answered *wait*; G-core answers *queued*).

## Lanes

### A. story (census, sets, keeper, twins) - server and engine library

1. `tools/online/story_census.py`: 503 leaves DERIVED and becomes persistent
   (setter stag0001; its one clear, `stag0001_m1__restart.lua`, is the
   retry's and is named as the only allowed clear); the key setter stages
   (`keySetters` in `story_sets.json`): K2 / K3 from the `SYSctlFlagFolder`
   SET lines of stagML02, the drops K0, K1, K4-K10 measured from the r25c
   campaign's pickups (segment logs, names mapped to keys by the impItem
   table's row order and the checkpoints' key sets); `trainingProof` (the
   persistent flags whose setters are all outside stagml02 / stagml11 /
   stag0001). `--check` keeps the files honest.
2. `session_census.py`: 503 out of NEVER_MERGED; `session_values.txt`
   regenerated (503 leaves the instance class).
3. `src/engine/online_story.cpp` (+ `.inc`): `apply` / `holds_dropped` take
   the barrier's target stage and leave 503 alone at a barrier into stag0001;
   `played_training` and `rebase` setting 503 (the rule reads the stored
   story before the rebase: phase >= 3 in the working set or any difficulty's
   copies, COUNTER 7 > 0, any key or key copy, a `trainingProof` flag or its
   copy); `StoryRefusal::queued = 11`.
4. C# `StoryMerge.Rebase` + `PlayedTraining`, `StorySets.KeySetters` /
   `SetsKey`, `RoomStoryKeeper`: a key from a stage outside its measured
   stages is refused *stage*; earned values answer *queued* (11) instead of
   *wait*; Python twin `story_golden.py` with the new golden cases (503 OR,
   apply into stag0101 / stag0001, dropped 503, rebase from phase 3,
   COUNTER 7, a key, a stag0101 flag, not from a lobby-only phase 2).
5. Protocol 47 (client `kOnlineProtocol`, the production example's
   `Play:MinimumProtocol` / `CurrentProtocol`): a 46 client would not add 503
   at a barrier where 47 clients do.

### B. client

1. `Engine::apply_barrier` passes `pending_stage_` to `story::apply`.
2. The call shop closed online: `callshop.lua` under the online rules runs
   with NINZU read as 4 (its own refusal branch: two lines, the talk's close,
   never NEXTSTAGE stagIS13) and a hint "The call shop is not available
   online: everyone plays their own character."
3. `Engine::common_rebuilt` (host field): player 1 makes or calls a
   character while LOADCTRL2 is not in title mode and the online rules are
   on; `GameEngineLink` treats it as the Game Start.
4. The Game Start's hold: no Game Start without a confirmed story; the
   session parks the title ("Loading the room story..."), 15 s then leaves
   the room ("The room story did not arrive. Join the room again.");
   `DW4_ONLINE_TEST_STORY_DELAY` (dev builds) ignores the story for N s (G7).
5. `CardStore` story override (bytes 792..1891 of every used box, both
   copies, checksums) for a guest's session; the room entry rewrites an old
   cache's boxes.
6. `CharacterSync`: the own story bytes from GET 200 / the 409 reload / zeros
   at 404 in every cached and uploaded record (auto saves, card_written,
   the recovered save).
7. Session: *queued* like *held* without its notice (not sent again).
8. The guest's toast at a Game Start, "Story: <owner>'s, phase n, k key
   items" / "Story: yours" in the F1 Online tab and the room view, the
   "moved on" notice once per stage.
9. `numemon_freed` online: Numemon flags that rose in this account's own
   live steps (a mask), not the engine's total.

### C. scenarios (hidden real clients, own stack: mysqld 3550, ports 56100-56149)

`seed_story` production-like (the merge sets of the owner's card and the
derived copies, no temporaries, no 503); G1-G16 as far as the time allows
(first G1, G2, G6, G9, G10, G11, G16, then G3, G4, G7, G13, G14, G5, G8,
G12, G15), counter-proofs on main b226bc0 for G1, G3, G4, G11; the regression
S2, S3, S5, S14, S14h, S14a, S14p, S14z, S16, S17a, C1-C3.

### Checks

ctest (all), server unit + integration tests, `story_census.py --check`,
`det.sh` 4004f056 / 396cb219 twice (online off bit-identical), the scenarios.

## Progress

(Newest last. A later run continues from the last commit of this branch.)

- 04.11 21:30: worktree made from main b226bc0; build scripts in build/
  (`bld.sh dev ON dev`, `g_scen.sh`); the dev build of b226bc0 is green
  (53 executables). Key stages measured (lane A 1): K0 dng101b2 (Blosso ID),
  K1 dng102b3 (Mammothmon ID), K2 / K3 stagML02 scripts (Boat Key, Ophanimon
  ID), K4 dng204b2 (SkullGrey ID), K5 dng205b3 (Scorpio ID), K6 dng307b2
  (Shogun ID), K7 dng308b3 (MRδ04 ID), K8 dng309b4 (Diaboro ID), K9
  dng411b3 (MRε05 ID), K10 dng412b4 (LordKnight ID).
- 04.11 22:00: lane A (story) committed a27fad5a + 8b491f27: census (503,
  key stages, training proof), C++ / C# / Python rebase and apply with the
  target, 9 new golden cases, keeper key stages + *queued*, protocol 47.
  Server unit tests 246 / 246 (GCoreKeeperTests new; the older keeper tests
  use the sets without key stages where they were written for keys from any
  stage; HubStoryTests' harness at protocol 47).
- 04.11 22:15: lane B (client) committed 3670df48: call shop closed,
  common_rebuilt, the Game Start's hold, card override, own story bytes,
  queued, toasts / story line / moved-on notice, numemon mask; tests
  card_store, online_character (guest_records_keep_the_own_story),
  achievements, online_session (hold, queued). ctest 53 / 53 (build/dev).
  det.sh on build/rel (no dev): 4004f056 / 396cb219 (run 1 of 2, both
  copies equal; log work/dref/checks/det/det_r69gcore_a).
- 04.11 22:20: lane C: run.py production-like seed_story, upload_card,
  own_story checks, script conditions key:N / difficulty; scenarios RG1
  (= G1), RG2 (G2-G4), RG7 (G7), RG10 (G10 / G11). RG1 PASS (145 s,
  tools/online_test/.work/run_20261104_220001). The call shop's refusal
  seen in RG10's guest.
- 04.11 22:45: all four RG scenarios PASS on build/dev: RG1 (145 s,
  .work/run_20261104_220001), RG10 (call shop refused twice with the hint,
  no stagIS13, the owner's story stays at revision 1, the guest's 2 used
  card boxes hold its own story; .work/run_20261104_220252), RG2 and RG7
  (.work/run_20261104_222xxx, build/scen_RG7_RG2b.log). G7's switch now
  counts from the first Game Start (the title's length varies).
  Counter-proofs on main b226bc0 (worktree DW4_PC-wt/r69main, detached:
  its dev client and Debug server; build/g_scen_main.sh with
  --compat-scripts) started: build/scen_main_counterproofs.log.

Next (if this run is cut off): the counter-proofs' result into this note
and SEAMLESS.md 17; the regression S14p, S14, S14z, S2, S17a, S16 and
character_test C1-C3 on build/dev (build/g_scen.sh dev/dw4_game.exe run.py
...; character_test.py the same way); det.sh a second time on a fresh
build/rel; then G6 / G13 / G14 / G16 scenarios if time allows (not built:
G5, G6, G8, G9, G12-G16 as real-client runs; G8 and G15 are covered by
online_character's guest_records_keep_the_own_story).
- 04.11 23:15: counter-proofs on main b226bc0 (its client and server,
  --compat-scripts; build/scen_main_counterproofs.log) all FAIL as they
  should: RG1 (the guest's room story lacks 503, its lobby gate leads into
  stag0001, the training), RG10 (the call shop goes to stagIS13; the online
  card's box 0 holds the owner's story), RG2 (the save room put the owner's
  story into the online card's box 0). Regression runs next.
- 04.11 23:55: regression on build/dev PASS: S14 (255 s), S14p (204 s),
  S14z, S17a (277 s), S2 (303 s) (build/scen_regress1.log); det.sh a
  second time on build/rel of 51342d28: 4004f056 / 396cb219 (both runs,
  work/dref/checks/det/det_r69gcore_b). Next: character_test C1-C3, S16,
  S5, S3, S14h, S14a.
- 05.11 07:45 (third run): character_test C1-C3 on build/dev of 017bd012:
  C1 and C2 FAIL, C3 passes (build/char_C1C3b.log). Cause: `CharacterSync`
  put the own story from the room entry into the room OWNER's records too,
  so his stage change and save room saves that only moved his story on were
  "unchanged" (C1: no save room upload; C2: no upload after the other PC's
  save, so no 409). Fix d0dcdb6b: `CharacterSync::Config::guest`
  (online_run: `!room_owner`); only a guest's records get the own story, the
  owner's keep the engine's story (his own, the room story); the entry's
  rewrite of the cache's boxes stays for both. New test
  `owner_records_keep_the_engine_story` (fails without the fix: no upload).
  C1-C3 PASS on build/dev of d0dcdb6b (build/char_C1C3c.log: C1 363 s,
  C2 210 s, C3 11 s).
- 05.11 08:25: regression on build/dev of d0dcdb6b PASS: S14a (256 s),
  S14h (258 s), S16 (392 s), S3 (305 s), S5 (288 s)
  (build/scen_regress2.log). With 23:55's S14, S14p, S14z, S17a, S2 the
  plan's regression list is complete. Next: RG1 / RG2 / RG7 / RG10 again on
  d0dcdb6b (build/scen_RG_final.log), character_test C4 K3 K3b K5, ctest,
  server tests, det.sh twice on build/rel of d0dcdb6b.
- 05.11 08:55: final checks on d0dcdb6b (the code; later commits are docs):
  RG1 (145 s), RG10 (293 s), RG2 (243 s), RG7 (110 s) PASS again
  (build/scen_RG_final.log); character_test C4 (230 s), K3 (326 s), K3b
  (327 s), K5 (267 s) PASS (build/char_C4K3.log, build/char_K5.log);
  ctest 53 / 53 (build/ctest_final.log); server unit tests 246 / 246
  (build/server_unit_final.log), integration tests 93 passed, 4 skipped
  (the end-to-end ones without the WinHTTP client / Python; private
  MariaDB on 3550 through scripts/local-db.ps1 -Port 3550, stopped after;
  build/server_integration_final.log); `story_census.py --check`: both files
  equal (build/census_check_final.log); det.sh twice on build/rel of
  8ecc5e85: 4004f056 / 396cb219 in all four runs
  (work/dref/checks/det/det_r69gcore_c and det_r69gcore_d).

## State (05.11 09:00)

Lanes A (story) and B (client) built and tested; lane C: RG1 / RG2 / RG7 /
RG10 with counter-proofs, the whole regression list and C1-C4 / K3 / K3b /
K5 pass. Protocol 47 (shared with round J, which ships with it).

Left for a later run (not needed for the merge, the plan's "as far as the
time allows"): real-client scenarios G5 (the moved-on notice and the next
barrier), G6 (Retry in the training), G9 (one stag0101 instance over three
barriers), G12 (a Hard room), G13 (503 dropped while the owner is away and
at not_started), G14 (Continue? No), G16 (the owner's own steps into
stag0001; its counter-proof on 57568ec); r25c 10 of 10 and the save state
suite were not rerun in this round (det.sh covers online off). The user's
steps: review, merge into main together with J, the DW4Online release with
protocol 47 (server and client together).

## Review fixes (05.11, on `int-r69r70`)

The room owner through the title's online screens (`api::owns_room`), the
training proof only from stages behind the training's gate (25 flags), a
409's own story in the card store, no call shop hint from a replay, the
MergeEarned comment. Not changed: the 404 rewrite before the backup (an old
cache may hold a borrowed room story; the backups can be imported) and the
key setter stages (one Normal run; a Hard pickup run is open).
