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
