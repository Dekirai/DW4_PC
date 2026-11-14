# Round I1b's integration: progress notes

Branch `int-i1b` from main 463c7a5b (DW4Online 0.1.9 server-only fixes of fix-019, the controller families and prompts,
the Switch 2 Pro reader), worktree `DW4_PC-wt/int-i1b`. It merges the three I1b lanes (moderation, prompt, go along;
each from `int-i1a` bfc3b0a5), resolves them against fix-019, and adds the integration's own tasks. Release: DW4Online
**0.2.0**, protocol **50**, schema **14**. Not pushed, not merged into main. Data Overflow untouched. Main 12f6a1a8
(the display languages, texture pack format 2, tools/remaster) is merged in since 2026-11-14 (d80617ef, see "The merge
of main" below).

Own resources: mysqld 3681 (root password in `tools/online_test/.work/mysql-root.txt`, stopped with SQL `SHUTDOWN`),
scenario and relay ports 60000-60399, build dirs `build/int/*` (gitignored). The coordination contract with every merge
hint is `DW4_PC-wt/i1b-contract.md` (outside the repo); the user's answers of 2026-11-11 17:50 (decision 43) and
2026-11-12 20:40 (decisions 44-47) are its last entries.

An earlier run of this integration was cut by a PC restart on 2026-11-12 ~20:45 after the three merges and with the
snapshot work uncommitted; the second run (2026-11-13) checked that state, built it again and went on from there.

## Merges

| Order | Branch (head) | Merge commit | Decisions |
| --- | --- | --- | --- |
| 1 | `feat-i1b-mod` (aefa1edd) | c1cea16c, then 7c21efa6, 1ef2b04f | `RoomHub.Join`: the lane's `SendRemovedHome` first (a member removed from the target's instance goes home from its barrier; since 1ef2b04f, "Join checks SendRemovedHome first"), then fix-019's check for a seat lost any other way (a fresh arrival's member decides alone, `DecideAlone`); `WorldService.RemoveMemberAsync` keeps fix-019's `run`; run.py has fix-019's `{run}` and the lane's `expect_not`. **Migration**: the lane's `player_reports` / `v_player_reports` were written into the then unreleased 0013, which went live with 0.1.7: they became **0014_reports** (0013 keeps its checksum; `v_staff_audit` stays 0013's: replacing a view needs DROP, which DEPLOY.md's service user lacks). **Removal**: every held arrival of a removed member goes home from its barrier at once (the lane's `SendRemovedHome`, 19.6.6 as round I1b wrote it, scenario W10d), a fresh one waiting for its JOIN of that instance too; fix-019's refix had let such a fresh arrival decide again alone. The refix's invariants stay (a JOIN takes no seat, no fifth seat, one history row, one "joined"); `WorldFix019RefixTests`' two removal tests expect the way home. `ModerationTests`' guests are the host's friends (fix-019's decision 40 rule for the automatic join), the removed joiner's fallback is fix-019's `FallbackHome` (wid 0). |
| 2 | `feat-i1b-prompt` (a79be17d) | 031c5128, then cb31f502 | `Matching`: the lane's `Rank` with fix-019's `AutoJoinRow = Preselected`; `WorldHub.Candidates`: the lane's order (context first, full instances as greyed rows) with fix-019's *Here* (only the members of stage groups that stay at the stage, and their joiners); REPORT beside PLAN / CHOICE in the dispatch and both wire registries; the SEAMLESS section names (the prompt lane's 19.32, fix-019's 19.33). The moderation hints (contract 15:20 / 15:25, prompt 16:20): `DecideChoice` refuses a row whose instance may not seat a member or co-arrival (`SeatsRefused`: "no longer open", refreshed prompt), `StartingNotOffered` skips such an instance, `FinishCoArrival`'s base that chose a row lets its co-arrivals decide through `RedecideGroups` (behind `RedecideCoArrivals`). |
| 3 | `feat-i1b-go` (b2ae1ee8) | ce46800b, then 75738e92, f74ceec6 | `RoomHub.Decide`: the holder filter keeps decision 43 and the go lane's "a GO's barrier is never a base or co-arrival"; `announced = []` for a GO's barrier or a fresh row chooser (contract prompt 16:20 / go 15:55). CheckHolds' JOIN: `World:TestJoinHoldMs` and the GO's no-living-member wait. The housekeeping: the offers before fix-019's `HousekeepHome` and the strays. `HubModel`: `HoldKind.Prompt`, `TestGo`, `HubInstance.DetachedFrom` (fix-019) beside `NoLiving`, `EntryPoint`, `AskedGo`. The go lane's hub tests and the moderation lane's relay tests run with `World:ArrivalPrompt` off (contract go 17:05); the Python relay spike sends HEADING's asked byte. |

After each merge: the Release build, ctest 66/66, server unit tests (494, 527, 544 at the three steps), integration
tests (the failures each merge brought fixed in its follow-up commit).

## The integration's tasks

| Task | What | Test (fails without the change) | Commit |
| --- | --- | --- | --- |
| a. decisions 44-47 | SEAMLESS 19.1 rows 44-47, 19.7.2 / 19.7.3, 19.25's open points of the go lane, 19.32. Decision 46, *Arriving now*: an instance without a stage group at the arrival stage but with one at a barrier heading there, or with members parked on their way (`WorldHub.ArrivingAt`), is a row `RowState.Arriving` (OPTIONS state 5 with the host's name, the players not *Here*); it ranks with the boss fights after every row that can be entered now or in a moment; `Matching.Preselected` (and `AutoJoinRow`) never take it, so decision 40 is unchanged for the other rows; its CHOICE holds the barrier (`HoldKind.Arriving`, BARRIER_WAIT naming them) until their stage group stands at the stage, then `DecideChoice` again (a JOIN with 3.4's holds); `Instances:JoinHoldSeconds` without them: a NEW with the hold-expired notice. The client says "Joining <host>..." for it. | `PromptArrivingTests.cs` (3 tests): without `ArrivingAt` 2 fail, without the preselection rule 3 | 9e97de32 |
| b. fix-019's to-dos | `Candidates(..., staffChoice)` for the prompt's rows (PLAN, the held prompt, its refresh, the CHOICE's re-check): a moderator's Private / Friends rows only the staff bypass shows, as `StaffOnly` rows after the others; never preselected nor counted as the first row; never the automatic paths; a CHOICE of one seats the moderator as staff (audited `staff_join`) | `A_moderators_prompt_offers_a_private_instance_as_his_own_choice_never_preselected_and_never_automatic` (the previous run: fails without the staff rows and without the preselection rule) | cb31f502 |
| c. the snapshot | `StageSnapshot::random_wait_min` (ActionManager's 80535A60), `kSnapshotVersion` 44 (a 43 state still loads with 0; 42 and older are refused as before); the tick state's version follows. The same check over the whole tick state (a new `tick_state_tests` case: stag0101 with the minimum set, a change to stag0102, a fresh engine restored from the change's snapshot replays and compares its canonical tick state at three state-safe frames) found five more values a stage load left over and only a restored engine had at their defaults; each is written before it is read in the new stage, so they now start every stage at those defaults: CameraManager's saved eye / target, StageCtlManager's reset time, MatCtrlManager's pending MATsetRun, ShadowManager's shader values and per-slot lights, SubWindowManager's junk shop counter. Protocol 50 (client `kOnlineProtocol`, the relay's `WorldProtocol.Version` and floor; the I1b wire comments say 50), DW4Online 0.2.0 (`Directory.Build.props`). The client version stays the CMake project version (0.46.0): the repo raises it with the engine milestones, never with a protocol rise; the protocol is what the server gates. | `tick_state_tests`' stage change restore: without the snapshot field it fails ("differs in beAction2" at 2200 / 2300 / 2400, the restored minimum 0); `online_tests`: the field survives the bytes, a version 43 snapshot loads with 0. det.sh unchanged. | fa6dfaea |
| d. the base flakes | `RoomHub.BecomeMember` stamps the hub's `now` (it took `Stopwatch.GetTimestamp()`); `OnlineStoryRepository`'s `SaveAsync`, `ReplaceAsync` and `KeepAsync` run again when MariaDB makes them a deadlock victim (`HistoryWriteAsync`, `MySqlErrors.DeadlockAttempts`); W7e: SNAPSHOT_ASK (see "W7e" below) | `A_joiners_instance_since_is_the_hubs_clock_so_the_host_stays_the_older_member` (the hub's clock a year ahead: fails without the fix at any uptime; the old resync test failed 2 of 544 today within 17 minutes of the reboot); `Session_starts_of_many_accounts_at_once_are_all_kept` (24 accounts, four rounds: without the retry "Deadlock found" in `KeepAsync`'s DELETE; with it 4 of 4 runs); `SnapshotAskTests` (without the replacement both W7e tests fail), the wire's golden bytes on both sides, the session's `snapshot_ask` | c9551343, d6c0b990, 6aded751 |
| e. the join toast | the stage group's JOIN and LEAVE toasts ("<name> joined" / "A player joined", "<name> left") are gone; WORLD_NOTICE's "<name> joined.", "<name> left.", "<name> went to <stage>." tell it (19.5.6) | `solo_join_fork`: no join toast from the JOIN (fails with the old session.cpp), the relay's notice gives one "lee joined." | 0aaf3601 |
| f. button pictures | the prompt's pad line "{A} Choose   Left/Right visibility   {X} Details   {B} Back" ("{B} Back to the lobby"), the Go along box's "{OK} Go along", "{OK} Cancel", "{BACK} Close", the notices' "{MENU} answer" / "{MENU} cancel", the held GO barrier's "{B} Back to the lobby" and "{A} Yes   {B} No"; drawn by `prompt_text`; the prompt's keyboard line keeps naming Enter and Backspace; SEAMLESS 19.5.6 says how its table reads | the session tests' `go_back_line` checks take the placeholders (ImGui lines have no unit test) | 64f6676b |
| g. scenarios | W10/expected.txt: after the removal b's PLAN lists no row (`rows=[]`, `preselected=new`) and the relay's candidates for its barrier are none. **W11r** (new): the lobby's return mat through the gate form with real clients (b powers stag0202's Security Gate in its own instance, goes home by the gate disc: GATESTAGE stag0202 point 2; its return mat's *Yes* is held for PLAN purpose return mat, OPTIONS list a's row with a here, preselected; autochoice first joins a). The go lane's `testhook mat` and the moderation lane's `setflag` turned out not to be needed: the gate stage must be b's own (a guest's is set aside at its DISSOLVE) and the TEST hook needs a sealed stage group, while stag0202's gate needs only every living player of the stage group on matx01, which the explore bot reaches | the runs below | f815719a |
| h. migrations | 0014_reports after 0013 (the lanes added no other migration); `MigratorTests.Migration_14_adds_the_player_reports_to_a_schema_13_database` migrates a 0.1.9 database (schema 13) with rows to 14; 0013's test unchanged | integration tests | 7c21efa6 |
| i. Switch 2 Pro | replies matched to their command by command and subcommand, late replies of earlier start commands taken as theirs (the read goes on for the command's own), the late ones still on their way read after the tenth command (so none reaches the player lights' read, which skips a start command's reply too); a reply is any packet whose second byte lacks the request bit 0x80 (the owner's controller answered 01/01 and 08/02 with 0x04); the flash reads unchanged | `machine_late_replies` with the fake transport replaying the owner's log (07 01, none, 0c 01 00 02, 11 01, 0c 01 00 04, 01 01 00 0c, 01 04 00 01, 08 04 00 02, 03 01 00 0a, 03 01 00 0d): with the old matching it fails ("not its reply" for 11/01, 0A/08, 01/01, 08/02); the controller still sends the same 15 commands, keeps its calibration, plays, reads the lights' reply; 409 checks | b4a0a8ee |

### W7e

Cause (from the prompt lane's runs r11 / r13 and the code): a's client cuts its link at its park
(`DW4_ONLINE_TEST_CUT_AT_PARK`), but its BARRIER reached the relay before the cut. The barrier of a and b is decided as
a sealed NEW of both; the relay names the member in the lowest new slot (a) the load snapshot's uploader
(`RoomHub` NEW: `inst.Snapshot.Uploader = ... OrderBy(slot).First()`), and every client picks the same one from
BARRIER_OK's slot map (`Session` `uploader_slot_`). a never gets its BARRIER_OK, so nobody uploads the snapshot: b loads,
sends its SNAPHASH only, a leaves by the snapshot hash timeout and b goes on as the solo leader (LEAD_FROM), but its stage
group's `Snapshot.Complete` stays false for good. Every JOIN needs the load snapshot (`JoinTarget`, the state job's
bytes), so a's JOIN after its reconnect waits `Instances:JoinHoldSeconds` and ends HoldExpired in an instance of its
own. In round I1a's runs the BARRIER went down with the cut, so the barrier was b's alone.

The fix (6aded751, protocol 50 carries it): **SNAPSHOT_ASK** (0x9C, relay -> client, inst | visit). `HousekeepInstance`
replaces an uploader that left the visit or its connection (a fork side that shares the upload counts as the visit)
before the upload was complete, once the visit is 2 s old (`SnapshotAskGraceMs`: a short reconnect at the load keeps its
upload): the connected member in the lowest slot gets SNAPSHOT_ASK and is the uploader (`SnapshotStore.Reassign` drops a
partial upload; world log `snapshot_ask`, counter `snapshot_asked`). The client answers an ask for its current visit with
the engine's load snapshot of it (the bytes the first uploader would have sent) and is the visit's uploader. A chunk of
anybody else is dropped and counted (`snapshot_not_uploader`) instead of a protocol error, which would end a reconnected
first uploader's connection.

## Tests and checks

On the final code (833c10bd) unless named:

- **ctest** (Release, DEV=OFF, tests): 66/66 (`build/int/ctest_c4.log`). Before the last commit `scene` failed once: its
  "older version is refused" took the version minus one, which 44 now reads (833c10bd: two versions back, and bytes left
  over after a snapshot are refused).
- **Server**: unit 551/551 (`build/int/unit_c3.log`), integration 150 passed / 3 skipped (the opt-in end-to-end ones;
  mysqld 3681, `build/int/it_c3.log`). The protocol rise needed the relay handshake tests and the Python harness at 50.
- **det.sh** (DEV=OFF copies in `build/int/off/`): `dw4_int_off_c1.exe` (the snapshot field, before its commit),
  `_c3.exe` (6aded751) and `_c4.exe` (833c10bd), each run twice (c3 and c4 twice each): every run 4004f056 / audio
  396cb219 (`build/int/det_c1.log`, `det_c3.log`, `det_c4.log`; `work/dref/checks/det/det_int_i1b_*`).
- **r25c** (`_c4.exe`, 4 at a time): 10 of 10 segments reached their checkpoint, 1,748,011 frames
  (`work/campaign/runs/seg/seg_r25c_int_i1b_833c10bd`, `build/int/r25c_c4.log`).
- **Save state suite** (`_c4.exe`, a private copy of `work/dref/checks/st` in `build/int/st`): 17 of 17 identical after
  loading with a loaded state (`build/int/st_c4.log`).
- **Scenarios** (real hidden clients, `build/int/scen.sh`, run.py `--parallel 1 --keep`):
  - r1 (`build/int/scen_r1.log`; client c1 = the client code of 64f6676b, server of 51fd2d93): **S22b PASS** (366 s:
    no `statehash_mismatch` after b's classic join any more, fix-019's open criterion), **W11r PASS** (407 s, first
    run), **W4c PASS**; W1 and W10 stopped at once: the harness uploaded their cards with protocol 49 (426
    `client_outdated`), fixed in c63347a7.
  - r2 (`build/int/scen_r2.log`; client c2 = the client code of 6aded751, server of c63347a7): **W1** (230 s), **W10**
    (483 s, with the new prompt check), **W10d**, **W2b** (180 s), **W31**, **W7e**: 6 of 6 passed. In this W7e run a's
    link was cut before its BARRIER reached the relay (b's NEW alone, b uploaded itself), so it did not take the path of
    the fix; the unit tests do.
  - r3 (`build/int/scen_r3.log`, W7e again, client c2): W7e twice more on the final relay (r3 `build/int/scen_r3.log`, r4 `scen_r4.log`): both runs hit the race of r11 / r13 (the sealed NEW of a and b at stag0102, a's link cut 8-31 ms after it) and **SNAPSHOT_ASK did its part on real clients**: 2.0 s after a's drop the relay asked b (`snapshot_ask ... instead_of=1`) and b's 24,001 bytes completed the snapshot. Both runs still **failed**, for a second reason: r3 because b's script took a's never-loaded slot in its engine for a's return (`present=2` held at once) and b left; b.script now waits until @300 first (this commit). r4 then showed the rest: a's client resumed into visit 3 "playing apart" (`mode lost -> solo inst=3 visit=3`), sent its old barrier (visit 2's NEXTSTAGE, tick 7243) as a barrier of visit 3; the relay split it off (`barrier_split`), decided a's JOIN into b's visit 3 at once and sent its state (127 KB, checkpoint 1180), but a's client never answered with READY (`join_ready_timeout` 60 s later, then `fallback_new`: an instance of its own, "Joining ... failed"). That is the resume of a member whose link was cut right after its BARRIER_OK, a client / relay matter beyond the snapshot: open (Left).
  - The c2 client lacks only 833c10bd's check for bytes left over after a snapshot (it refuses malformed bytes; valid
    ones decode alike).

## The test matrix (2026-11-14)

Product code 0cca6307, harness commits b3dde6d3 (W30 expects a's row preselected: a and b are friends since fix-019),
a283f860 (W5's moderator takes the preselection), fb8713da (W7e's a stays until @330), 446595aa (UI scenario card's b
answers the prompt): 90 of 91 scenarios passed, the 11 UI scenarios, 9 of 9 `--fake`, the version gate (a 49 client gets
`client_outdated`) and the upgrade of a 0.1.9 database with real rows to schema 14; ctest 66/66, server unit 551/551,
integration 150 / 3 skipped, det.sh, r25c 10/10, the save state suite 17/17 (old 43 states too). The one real failure:
W7e's race path (below). The log folder is `build/ii1b/`.

## The fix stage (2026-11-14)

The review of the integration (relay and story safety, client and UI, privacy and deployment) and the test matrix's W7e.
Logs in `build/fx/` (scripts copied from `build/ii1b/`, the private save state suite in `build/fx/st`).

| Finding | Result | Test (fails without the change) | Commit |
| --- | --- | --- | --- |
| **W7e's race path** (real failure): the answers to a split barrier and to a straggler's barrier named instance 0, which no parked client matches; a member that resumed into the next visit after its link went with the visit's BARRIER_OK re-sent its old barrier, dropped the JOIN's BARRIER_OK and then the state, never sent READY, `join_ready_timeout`, an instance of its own | the lone groups answer under the instance and visit their BARRIERs named; a BARRIER past every tick a sealed stage group sealed (`StaleBarrierTicks` 120 beyond `NextSeal`) is another timeline's and is decided apart at once (`barrier_apart`: LEAVE, the others told, decided alone; no 5 s stall for the others, no desync's split); `DW4_ONLINE_TEST_CUT_AT_ANSWER` (development builds) makes the race, scenario W7f | `IntegrationReviewTests`: the split's answer, the straggler's answer, the resumed member's JOIN (split path), the old barrier decided apart at once (4 fail without); W7f; W7e's runs took all three paths (the split before `barrier_apart`, the straggler after a lag LEAVE, the cut before the decision) | 13b13d5e, 3b9b70ab |
| A GO's barrier into a floor without enough free slots threw `IndexOutOfRangeException` in `Join` under the hub's lock (should fix) | the GO's barrier waits for the slots (`JoinTarget`, CheckHolds' JOIN); `Join` decides again instead of joining without slots (`join_without_slots`) | `A_go_barrier_waits_while_its_floor_lacks_the_slots_and_joins_once_they_are_free` (threw); `An_arrival_without_a_go_never_joins_a_floor_without_the_slots` guards the ordinary path | 13b13d5e |
| Two JOIN_REQs to one solo leader at once (go + join, go + home); JOIN_AT names no kind (minor) | one at a time: `StampJoin` waits for `AskedGo` / `AskedHome`, `TrySoloHome` for `AskedGo` | `A_solo_leader_has_one_join_req_out_at_a_time_so_the_gos_answer_seals_the_go`, `A_removal_while_a_gos_join_req_is_out_asks_the_home_after_the_gos_answer` | 13b13d5e |
| Decision 43 and an explicit *New instance, Private* (minor) | not changed: **for the user** (Left) | (the review's probe: the Private chooser lands on a stranger's Public story when the stranger's barrier comes first) | - |
| This file's merge table named the wrong order in `RoomHub.Join` (minor) | corrected: `SendRemovedHome` first, then `DecideAlone` | - | docs commit |
| W5 no longer checks a staff join with real clients (minor, optional) | new scenario **W5s**: the moderator's own choice of a Private row, `instance_join role=staff how=join`, one `staff_join` | W5s | 3b9b70ab |
| The Go along box: the first A after View / F1 did nothing (ImGui activates only with a visible cursor; also *View: change*) (should fix) | `SetNavCursorVisible(true)` with the focus request (the half second's cursor below the box stays hidden) | UI scenario **goalong** (b: View, c: the menu, one A each, no direction first; `online: go along`) | 1729a5dd |
| The notice stayed above the own request's line (should fix) | `go_notices` skips the own request's offer while it runs (Cancel brings it back) | `online_session_tests` "the notice gave way to the own request's line" | 1729a5dd |
| The button that closed the F1 menu (A on a Go along row, B in the box, Play) reached the game or an open prompt (should fix) | `pad_latch` whenever the menu closes by its own request | none automatic: UI script keys reach ImGui only, not the game's pad | 1729a5dd |
| The prompt's hint line chose keyboard / pad by "a pad is plugged in" (minor) | by player 1's prompt family (the keyboard while it is the device in use) | goalong's gate form screenshots with forced families | 1729a5dd |
| The boss fight's go-along line lacked "{MENU} cancel" (minor) | boss counts as cancellable there | none automatic (an ImGui line) | 1729a5dd |
| Toasts: two for the host's leave; went_to without the floor; "You joined the group" / "You joined <name>" by timing; "View: Online menu" in words; "Joining Your friends failed" (minor) | one toast for the host's leave; the floor; the JOIN's toast waits for INSTANCE_INFO (2 s at most); `{MENU}`; "the group" without names | `online_session_tests` (7 checks fail with the old session code) | 1729a5dd |
| A lag LEAVE (and a split) left the figure gone without a word since the stage group's LEAVE toast went (minor) | WORLD_NOTICE 15 *apart*, "<name> plays apart for now." (relay and client) | `A_member_lagged_out_of_its_stage_group_is_told_to_the_others_as_playing_apart`, the session's apart check | 13b13d5e, 1729a5dd |
| A reported admin could read his reporter in the staff audit (`report_handled`'s `reporterId`) (should fix) | no reporter in the audit detail (the report row keeps it for the other staff) | `A_reported_admin_never_learns_his_reporter_from_the_staff_audit`, the staff test's audit rows | 0e7b1916 |
| DEPLOY.md's rollback left `Play:MinimumProtocol` / `CurrentProtocol` at 50 (0.1.9 would refuse every 49 client) (should fix) | section 8: set them back for a release that raised the protocol, withdraw a published client; 9.3: the client only after the checked server | - | 0e7b1916 |
| The privacy page did not cover reports or the prompt's member names; reports were kept forever (should fix) | a Reports row (what, why, Art. 6(1)(f), who sees the reporter, the period, the relay log's note); the prompt's names, levels and Digimon; `Reports:DoneDays` (180) deletes handled reports hourly | `Done_reports_go_after_reports_done_days_and_open_ones_stay` (with the page's text) | 0e7b1916 |
| The server README lacked `Reports` and `ReportsPerMinute` (minor) | added (and the website's reports, `report_limit`'s retryAfter) | - | 0e7b1916 |
| The I1b wire's comments said protocol 49 (minor) | 50 in Wire.cs, PromptMessages.cs, the WinHTTP test client and SEAMLESS 19.16 / 19.30 / 19.32 | - | 13b13d5e, 1729a5dd |
| `report_limit`'s `retryAfterSeconds` was always 3600 for a 24 h window (minor) | when the oldest report of the 24 hours leaves them (the limit cache ends then at the latest) | the daily limit test's three retryAfter checks | 0e7b1916 |

Counter-checks: the relay's 8 new tests with the logic reverted: 7 fail (the eighth guards the ordinary path); the client's
8 new session checks with the old session code: all 8 fail; the report tests with the old service: 4 of 5 fail; the old
barrier decided apart: fails without `BarrierApart`.

Checks on the fix stage's code (`build/fx/`): ctest 66/66 (`ctest_rel4.log`), server unit 560/560 (`unit_full2.log`),
integration 152 passed / 3 skipped (`it_full1.log`, mysqld 3681), det.sh on two DEV=OFF copies twice each: 4004f056 /
396cb219 (`det3.log`), the save state suite 17/17 (`st_all3.log`), r25c 10/10 with 1,748,011 frames (`r25c.log`; the
engine's list of online environment variables got the new test variable).

Scenarios on the fix stage's code (real hidden clients, `build/fx/marathon*.sh`, run.py `--parallel 1`; client DEV=ON
9cbcb9d9 from 3b9b70ab's code without the engine's list entry of the new test variable; the Android workflow ran beside them): **W7f** (the race on demand: `barrier_apart`, b's
SNAPSHOT_ASK, a's JOIN and READY under its own visit), **W7e** five runs (three took the cut before the decision, one the
race with a lag LEAVE and the straggler's answer, one the race before `barrier_apart` existed: a joined b but run.py
flagged the desync's `barrier_split`, which led to `barrier_apart`; every other run passed), **W5s**, W5, the UI scenario
**goalong** (b with View, c with the menu, one A each went along; the gate form's pad line with DualSense and Switch Pro
pictures, the own line without the notice, "went to Sand Labyrinth B1." with the floor), the smoke set W1, W4c, W10, W31,
S22b, RGJ1 and `ui home`, the go set W31b, W31c, W31c2, W31d, W31d2, W31e, W31f, W31g, W31h, W31h2, W31i (W31h2 failed once:
c healed from its disk on the mat as a's exit prompt opened and nobody left stag0201, a bot timing; passed on the repeat),
W7, W7b, W7c, W7d, S11a, S11b, W10b, W10c, W10d, W10s, the UI set (accounts, areas, card, cheats, gate, leave, navigation,
newchars, offline, unreachable), W2, W2b, W11, W11b, W11r and W30c (every one passed). Logs `build/fx/scen_*.log`, `marathon.log`;
the relays' world logs show no desync, mismatch, resync or exception beyond S11a / S11b's injected ones and w7e3's split.

## The merge of main (2026-11-14)

Main 12f6a1a8 (18 commits after 463c7a5b: the display languages, phase 1, with the German fan translation's engine, host
and tool side; texture pack format 2 with its streaming thread and the F1 Graphics options; tools/remaster) is merged as
d80617ef. The textual merge was clean (CMakeLists.txt, README.md, engine.hpp, managers.hpp, engine.cpp, online.cpp,
ui.cpp and game_main.cpp merged by git). A PC restart (~13:20) cut the first run after the merge commit, in its first
Release build (`build/mg/bld_rel1.log`); the second run went on from d80617ef.

The semantic overlaps the textual merge cannot show; none needed a code change:

| Overlap | Finding |
| --- | --- |
| l10n export and lint, I1b's texts | Both cover the game's own text (pack strings with the lines Lua sets them on, the database's display columns, card names). I1b adds none: its player-visible texts are the port UI's (ImGui), its one game-side change (subwnd.cpp) resets a counter. The port UI is not linted (a language pack's `[files] ui` is reserved). The merged build's export is main's corpus (4,512 texts, revision 44d775ae; against the l10n worktree's older export only the enemy info card's field differs, "-" since 25066ab7). |
| settings.ini | I1b adds no key (Online > Settings are the server's); main adds `[graphics]` and `[preferences] language`. |
| F1 menu | Main: *Language* at the top of the Settings tab, the *Graphics* section in place of *Texture packs*. I1b: the Online tab (the Instance page, the Go along rows and box, the player card). The Go along box's focus request and the pad latch concern the menu's own closings on the Online tab; main's combos never close the menu. The UI scripts open the Online tab by name. |
| Tick state, StageSnapshot | Main adds no engine state: a text line's pack index, laid-out geometry and alignment are presentation only and not in the tick state (its reading passes the line's pack index on). The texture streaming lives in `src/game` (renderer, `texture_stream`, `host_view`), which hands the engine's loaded documents to the reader thread as const shared pointers. `kSnapshotVersion` stays 44, the tick state's version too. |
| Environment lists | `online_presentation_env` holds both sides (I1b's `DW4_ONLINE_TEST_CUT_AT_ANSWER`, main's Graphics and streaming switches, `DW4_LANGUAGE`, `DW4_LANG_DIR`, `DW4_L10N_*`); `online_tests`' grep of `getenv("DW4_` passes. |
| Online UI | ImGui texts with the port's own tables (`presence_data`: area, stage and form names), the relay's names and story labels: English whatever the game text's language (the German runs below). |
| Docs | SEAMLESS 6.12 (presentation per machine) and 19.5.6 (the online UI stays English): 389bbb1d. README has main's Graphics options and texture pack sections and names no version that changes; the CMake project version is 0.46.0 on both sides. |

Checks on the merge's code (d80617ef; 389bbb1d and this notes commit change docs only), logs in `build/mg/`:

- **Builds**: Release DEV=OFF with tests (`build/mg/rel`, `bld_rel1.log` cut by the restart, `bld_rel2.log`) and Release
  DEV=ON with tests (`build/mg/dev`, `bld_dev1.log`); the warnings are the base's (getenv's C4996).
- **ctest**: 78/78 on both (66 + main's 12: i18n, l10n_lint, texpack and nine graphics_* cases): `ctest_rel1.log`, `ctest_dev1.log`.
- **Server**: unit 560/560 (`unit1.log`), integration 152 passed / 3 skipped (`it1.log`; an own mysqld on 3691, shut
  down by SQL).
- **det.sh**: two DEV=OFF copies (`build/mg/off/dw4_mg_off.exe`, `dw4_mg_off_b.exe`, md5 a1b6b420), det.sh twice each
  (8 runs): every run 4004f056 / audio 396cb219 (`det1.log`, `work/dref/checks/det/det_mg1_*`).
- **Save state suite** (a private copy in `build/mg/st`; `ST_ENV` adds environment to every case, `ST_RUNS` names the
  runs folder): 17/17 in English (`st_en1.log`) and 17/17 with the German game text (`st_de1.log`: `DW4_LANGUAGE=de`,
  every case "game.tsv 4515 texts, 0 rejected", "3 of 3 pictures ready").
- **l10n lint** of `local/lang/de/game.tsv` (copied from the main repo, git-ignored) against the merged build's export:
  4,424 OK, 88 squeezed, 0 need a squeeze, **0 errors**, 3 warnings (the travel dialog's two texts and "Lv", L10N.md's
  known ones): passed (`l10n/export.log`, `l10n/lint.log`, `l10n/lint_report.txt`).
- **r25c** (`dw4_mg_r25c.exe`, 4 at a time): 10 of 10 segments reached their checkpoint, 1,748,011 frames (`r25c1.log`,
  `work/campaign/runs/seg/seg_r25c_mg_int_i1b`).
- **Scenarios** (real hidden clients, `build/mg/marathon.sh`, run.py `--parallel 1`; client DEV=ON md5 71496d63 built
  from d80617ef; the Android workflow beside them; `marathon.log`): RGJ1, S22b, W1, W10, W4c, W5s passed, W7f failed
  once and passed on the repeat (`scen_smoke.log`, `scen_smoke_rep.log`, below), `ui home` and `ui goalong` passed
  (`scen_uihome.log`, `scen_goalong.log`). With the German game text (`MG_LANG=de`: `DW4_LANGUAGE=de`, the packs from
  `build/mg/scen/lang`): W1 (`scen_w1_de.log`), `ui home` (`scen_uihome_de.log`) and `ui goalong`
  (`scen_goalong_de.log`) passed; every client printed "language: de" with 4,515 texts and 0 rejected; the screenshots
  show the online UI in English (home, Online tab, the gate form's prompt with its pad line, the Go along box, the
  notice and the own line), nothing cut or overflowing. W7f twice more: passed both times by `barrier_apart` (`scen_w7f_a.log`, `scen_w7f_b.log`). The relays' world logs show no
  desync, mismatch, resync, split or exception; no client log has an error.

**W7f's first run** missed only its expected `barrier_apart`. a's link went as the sealed NEW's answer came (as
designed), b took the upload over (SNAPSHOT_ASK), a came back after the host's grace (20 s, `host_back`). The relay's
housekeeping (every 250 ms) ran 8 ms after a's resumed connection, before a's re-sent old barrier (12 ms): a's slot had
been filled with neutral pads for the whole grace, so its fill streak was far past `LagLeaveSeconds`, and with the grace
over the lag rule took a out (`lag_leave`, LEAVE, the *apart* notice); the old barrier then came as a straggler's
(`barrier_alone`) and was decided as a JOIN of b's stage group. The result is the one `barrier_apart` gives (a joined
visit 3 from checkpoint 1179 with READY, still the host, one instance, 0 of 168 fingerprint ticks differ); only the path
differs, the one the fix stage's W7e runs took once too. In the repeat the barrier came first (13 ms,
`barrier_apart`). Not a matter of the merge: the relay and the client's resume are I1b's, main changed neither.

*Corrected by the fix stage after the merge (below):* the `lag_leave` after `host_back` was no race. It came in every
run of a host back after more than `Relay:LagLeaveSeconds` away (W7, W7c and W7d too, about 240 ms after `host_back`),
since the slot's fill streak counted from the grace's start; only whether W7f's re-sent barrier came before it was
chance. Fixed there (the relay restarts the streak's clock at `host_back`).

## The fix stage after the merge (2026-11-14)

The merge's review (four findings, each checked first: all four hold) and W7f's failed first run. Logs in `build/mg/`
with `fx` in their names (scripts `fx_checks.sh`: det.sh and the save state suite; `marathon_fx.sh`: the scenarios).

| Finding | Result | Test (fails without the change) | Commit |
| --- | --- | --- | --- |
| **W7f's first run, and a host back from his grace taken out by the lag rule** (should fix; I1a's, since 56898b29, also in main and the live server 0.1.9): `HostBack` cleared only `NeutralFill`; the host slot's fill streak counted from the grace's start, so with the grace over the next housekeeping (250 ms at most) took a host away longer than `LagLeaveSeconds` (10 s) out of his own sealed stage group (`lag_leave`, LEAVE, the guest led on alone), before a pad of his resumed client could come: every W7, W7c and W7d run of the earlier fix stage, and W7f whenever the housekeeping came before the re-sent barrier. Against 19.6.7's "back in time: play on". The merge's "race" fell short. | `HostBack` restarts the streak's clock (`SlotStream.RestartFillStreakClock`): the returned host has `LagLeaveSeconds` from his return to catch up; the streak's length stays (he counts as lagging until a pad of his is sealed in time); `lag_leave` and `reconnect_timeout` are counted now (the grace test's "no `lag_leave`" assertion read a counter nobody set); SEAMLESS 19.6.7 | `A_host_back_after_more_than_the_lag_limit_away_plays_on_and_the_lag_rule_counts_from_his_return` (15 s away, back, late pads: out at once without the change; caught up after 5 s he plays on; still late 10 s after his return he is taken out as any member); W7, W7c, W7d and W7f below | 2b009cee |
| W7c's `!relay: (lag_out\|lagout\|member_lagout)` could never match: the relay logs that LEAVE as `lag_leave` (minor) | W7c, W7 and W7d expect `relay: host_back` and `!relay: lag_leave`, W7f `!relay: lag_leave` | the old relay logs `lag_leave` in every W7, W7c and W7d run (the earlier fix stage's world logs) | b33d2645 |
| Main's test tool: `Languages::at_frame` runs every pass, its frame counts steps; in passes without a step (online, waiting for sealed ticks) `DW4_L10N_SWITCH` / `DW4_L10N_SWITCH_EVERY` switched again at the same frame (an even number undid the change, each switch read the pack again) (minor) | the schedule in the header-only `LanguageSwitches` (`src/game/language_switches.hpp`), whose `due()` answers each frame once; L10N.md | i18n's `test_language_switches` (fails without the once-per-frame rule); S22b with a in German and `DW4_L10N_SWITCH_EVERY=331` (a temporary copy, not committed): 64 switches at 64 frames (every multiple of 331 up to 21,184), each once, alternating (the review's run of the old code: 130 switches at the same 64 frames, two or three at each, so most changed nothing) | 176d1e50 |
| Stale versions (minor, before the merge): the server README's `/world/enter` (`protocol (49)`, "below 49") and DW4R2 handshake (`49`), DEPLOY.md 5's `schema 10` | protocol 50, schema 14; also DEPLOY.md 9's migrate example and `RelayHub`'s comment | - | 5a7fd4d8 |

Counter-checks: the relay test with `HostBack` without the restart fails (`unit_fx_host1_nofix.log`), the i18n test
without the once-per-frame rule fails (`i18n_nofix.log`, "A pass without a step at the same frame switches nothing
more").

Checks (code of 5a7fd4d8; the first scenario batch ran on ebbe48bf with these changes uncommitted, the same files):

- **Builds**: Release DEV=OFF with tests (`build/mg/rel`, `bld_rel_fx1.log`, `bld_rel_fx2.log` after the counter-check)
  and DEV=ON with tests (`build/mg/dev`, `bld_dev_fx1.log`); no new warning.
- **ctest**: 78/78 on both (`ctest_rel_fx1.log`, `ctest_dev_fx1.log`; i18n 140 checks).
- **Server**: unit 561/561 (`unit_fx1.log`), integration 152 passed / 3 skipped (`it_fx1.log`, an own mysqld on 3691, shut
  down by SQL).
- **det.sh**: two DEV=OFF copies (`build/mg/off/dw4_fx_off.exe`, `dw4_fx_off_b.exe`, md5 26deec97), det.sh twice each
  (8 runs): every run 4004f056 / audio 396cb219 (`det_fx1.log`, `work/dref/checks/det/det_fx1_*`).
- **Save state suite**: 17/17 in English (`st_fx_en1.log`) and 17/17 with the German game text (`st_fx_de1.log`, every
  case "game.tsv 4515 texts, 0 rejected").
- **Scenarios** (real hidden clients, `marathon_fx.sh`, run.py `--parallel 1`, mysqld 3691, ports 59400-59599; client
  DEV=ON md5 7ea1ce65, `build/mg/scen/dw4_mg_dev.exe`, the merge's client kept as `dw4_mg_dev_71496d63.exe`; the Android
  workflow beside them; `marathon_fx.log`): W1, W7f, S22b (`scen_fx_smoke.log`), `ui home` (`scen_fx_uihome.log`), W7,
  W7c, W7d (`scen_fx_w7.log`), W7f twice more (`scen_fx_w7f_a.log`, `scen_fx_w7f_b.log`) and the temporary S22bmix
  (`scen_fx_mix.log`): all passed. W7f took `barrier_apart` all three times (22-30 ms after `host_back`, no
  `lag_leave`); W7, W7c and W7d: `host_back`, the host stayed in his stage group (his delay rose to 5-6 while his client
  caught up on 20 s and 46 s of ticks) until the scripts' end. The world logs show no desync, mismatch, resync, split,
  `lag_leave` or exception.
- Not run again: r25c (the change is host-side, in a test-only path; det.sh and the save state suite cover the engine)
  and the l10n lint (neither the export nor game.tsv changed).

## Left

- Not pushed and not merged into main (as asked).
- **For the user (decision):** decision 43 and an explicit *New instance, Private* (the review's finding 3): a Private
  chooser who leaves together with a stranger lands on the stranger's Public story when the stranger's barrier comes
  first, and in an instance of his own in the other order. Either (1) a *New instance, Private* chooser is never the
  co-arrival of another's base and a *Friends* chooser only of a friend's (`MaySeeBase` also asks `MayEnter` with the
  chooser's own visibility), or (2) record the order-dependent result as intended (19.7.6, 19.32).
- For the user (to confirm): `Reports:DoneDays` 180 (handled reports are deleted 180 days after they were handled; the
  privacy page shows the configured period).
- The release: the DW4Online 0.2.0 zip and the client release; the user's steps (DEPLOY.md 9.3: a backup, the settings
  `Play:MinimumProtocol` / `CurrentProtocol` 50, the server, then the client only once the server runs and was checked;
  the rollback sets both settings back to 49). The game's version stays the CMake project's 0.46.0.
- From fix-019: the load gate on a quiet machine (no workflow beside it).
- No automatic test: the pad latch when the F1 menu closes by its own request (UI script keys reach ImGui, not the game's
  pad) and the boss fight's go-along line; the prompt's ImGui hint lines (the goalong screenshots show the gate form's).
- The lag rule after `host_back` is fixed on this branch only (2b009cee); main and the live server 0.1.9 keep taking a
  host back after more than 10 s out of his stage group until the 0.2.0 release.
- To watch: each return restarts the host slot's lag clock, and 4 s of silence start a new grace; a host whose link
  keeps dropping for a few seconds and coming back would stay in his stage group without a pad in time (the guests play
  on, his Digimon stands). One absence is bounded by the grace, the sequence is not; no test makes it.
- Later: a client flag in BARRIER for a barrier of a visit the member never loaded would let the relay decide it apart
  also when its tick lies inside what the new visit sealed (now the split after `Instances:BarrierReportSeconds`, the
  others stalled 5 s; it needs the old visit to have run shorter than the new one has when the member comes back).

## How to run

- The fix stage: `build/fx/` holds copies of `build/ii1b/`'s scripts (`utest.sh`, `itest.sh`, `scen.sh`, `marathon*.sh`,
  the save state suite in `build/fx/st`); the builds stay in `build/ii1b/rel` and `build/ii1b/dev` (`build\ii1b\bld.cmd`).
- The merge of main: `build/mg/` (`bld.cmd DIR TESTING [dev|nodev]`: `rel ON nodev`, `dev ON dev`; `ctest.cmd DIR`;
  `utest.sh`, `itest.sh` with an own mysqld on **3691** and test ports 59600-59799; `scen.sh` with mysqld 3691 and ports
  59400-59599, `MG_LANG=de` for the German game text; `marathon.sh` batches, a batch ending in `_de` runs in German; the
  save state suite in `build/mg/st` with `ST_ENV` / `ST_RUNS`). German runs need `local/lang/de/game.tsv` (git-ignored,
  copied from the main repo) and `build/mg/scen/lang/de` (data/lang/de plus that game.tsv).
- The fix stage after the merge: the same `build/mg/` scripts and builds; `fx_checks.sh` (det.sh on
  `off/dw4_fx_off*.exe`, the save state suite on `off/dw4_fx_st.exe` in English and German), `marathon_fx.sh BATCH...`
  (batches `fx_smoke`, `fx_uihome`, `fx_w7`, `fx_w7f_a`, `fx_w7f_b`; `fx_mix` needs the temporary `scenarios/S22bmix`).

- C++: `cmd //c "start /low /wait /b cmd /c build\int\bld.cmd rel ON nodev"` (Release, DEV=OFF, tests; ctest:
  `build\int\ctest.cmd rel` or ctest.exe `--test-dir build/int/rel`), `... bld.cmd dev OFF dev --target dw4_game` (the
  DEV=ON game for the scenarios, copied to `build/int/scen/dw4_int_dev_<tag>.exe`).
- Server: `bash build/int/utest.sh LOG [filter]` (unit), `bash build/int/itest.sh LOG [filter]` (integration with an own
  mysqld on 3681, shut down by SQL and waited for).
- Scenarios: `bash build/int/scen.sh LOG TAG SCENARIO...` (run.py with --parallel 1 --keep, mysqld 3681, ports
  60000-60399, the r25c cards).
- det.sh: `bash work/dref/checks/det.sh <DEV=OFF copy> <name>` from the main repo; r25c: `cd work/campaign && EXE=<copy>
  bash segments.sh r25c`; the save state suite: a private copy of `work/dref/checks/st/` with its own run folder.
