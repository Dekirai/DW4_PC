# Round I1b's integration: progress notes

Branch `int-i1b` from main 463c7a5b (DW4Online 0.1.9 server-only fixes of fix-019, the controller families and prompts,
the Switch 2 Pro reader), worktree `DW4_PC-wt/int-i1b`. It merges the three I1b lanes (moderation, prompt, go along;
each from `int-i1a` bfc3b0a5), resolves them against fix-019, and adds the integration's own tasks. Release: DW4Online
**0.2.0**, protocol **50**, schema **14**. Not pushed, not merged into main. Data Overflow untouched.

Own resources: mysqld 3681 (root password in `tools/online_test/.work/mysql-root.txt`, stopped with SQL `SHUTDOWN`),
scenario and relay ports 60000-60399, build dirs `build/int/*` (gitignored). The coordination contract with every merge
hint is `DW4_PC-wt/i1b-contract.md` (outside the repo); the user's answers of 2026-11-11 17:50 (decision 43) and
2026-11-12 20:40 (decisions 44-47) are its last entries.

An earlier run of this integration was cut by a PC restart on 2026-11-12 ~20:45 after the three merges and with the
snapshot work uncommitted; the second run (2026-11-13) checked that state, built it again and went on from there.

## Merges

| Order | Branch (head) | Merge commit | Decisions |
| --- | --- | --- | --- |
| 1 | `feat-i1b-mod` (aefa1edd) | c1cea16c, then 7c21efa6, 1ef2b04f | `RoomHub.Join`: fix-019's check first (a fresh arrival's member that lost its seat decides alone, `DecideAlone`), then the lane's `SendRemovedHome`; `WorldService.RemoveMemberAsync` keeps fix-019's `run`; run.py has fix-019's `{run}` and the lane's `expect_not`. **Migration**: the lane's `player_reports` / `v_player_reports` were written into the then unreleased 0013, which went live with 0.1.7: they became **0014_reports** (0013 keeps its checksum; `v_staff_audit` stays 0013's: replacing a view needs DROP, which DEPLOY.md's service user lacks). **Removal**: every held arrival of a removed member goes home from its barrier at once (the lane's `SendRemovedHome`, 19.6.6 as round I1b wrote it, scenario W10d), a fresh one waiting for its JOIN of that instance too; fix-019's refix had let such a fresh arrival decide again alone. The refix's invariants stay (a JOIN takes no seat, no fifth seat, one history row, one "joined"); `WorldFix019RefixTests`' two removal tests expect the way home. `ModerationTests`' guests are the host's friends (fix-019's decision 40 rule for the automatic join), the removed joiner's fallback is fix-019's `FallbackHome` (wid 0). |
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

(Filled in below as the runs finish.)

## Left

(Filled in at the end.)

## How to run

- C++: `cmd //c "start /low /wait /b cmd /c build\int\bld.cmd rel ON nodev"` (Release, DEV=OFF, tests; ctest:
  `build\int\ctest.cmd rel` or ctest.exe `--test-dir build/int/rel`), `... bld.cmd dev OFF dev --target dw4_game` (the
  DEV=ON game for the scenarios, copied to `build/int/scen/dw4_int_dev_<tag>.exe`).
- Server: `bash build/int/utest.sh LOG [filter]` (unit), `bash build/int/itest.sh LOG [filter]` (integration with an own
  mysqld on 3681, shut down by SQL and waited for).
- Scenarios: `bash build/int/scen.sh LOG TAG SCENARIO...` (run.py with --parallel 1 --keep, mysqld 3681, ports
  60000-60399, the r25c cards).
- det.sh: `bash work/dref/checks/det.sh <DEV=OFF copy> <name>` from the main repo; r25c: `cd work/campaign && EXE=<copy>
  bash segments.sh r25c`; the save state suite: a private copy of `work/dref/checks/st/` with its own run folder.
