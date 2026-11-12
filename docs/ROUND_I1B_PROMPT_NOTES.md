# Round I1b, lane "prompt": notes

Branch `feat-i1b-prompt` from int-i1a bfc3b0a5 (worktree `DW4_PC-wt/i1b-prompt`). Design: docs/SEAMLESS.md 19.7 (19.7.3
order and preselection of decision 40, 19.7.4 the gate form, 19.7.5 the loading-screen form), 19.5.3, 19.18, 19.24, 19.25
"Round I1b". The wire and the relay options are agreed in `DW4_PC-wt/i1b-contract.md` ("prompt 16:30"); the other lanes of the
round are *moderation* and *go along*.

Own resources: mysqld 3651, ports 58000-58199, build dirs `build/i1bp/*` (gitignored).

## Done

- **The census (19.7.4's first task)**: `include/dw4/engine/online_departure.hpp` lists every departure from a hub stage into
  a joinable stage of the decompiled scripts: stagML11 `mate00` (the lobby gate's destination select `i_zzstchgcur`, quests
  included: a board's start only sets flag 409, then the gate offers stag0401 point 3) and stagML11 `mate02` (the return mat,
  `i_zzstchgwn`, Yes = DMcase 0). Everything else from a hub stage leads into a hub or UI stage. Both get the gate form.
- **`Engine::pending_departure_choice(own_slot)`** (src/engine/online_departure.cpp): read-only; the open cursor menu that takes
  input with the own slot as its pad reader, the demo work's GETSELECT switch on it, the cursor's DMcase and its
  SETGENERATEPOS / NEXTSTAGE; nothing for a target in the hub stages. `SelectCtrlManager::OpenSelect` gained `trigger` and
  `enabled` (read only), `DemoManager::works()` (read only).
- **Wire** (both sides, golden vectors equal): PLAN 0x18, CHOICE 0x19, OPTIONS 0x91, BARRIER_WAIT reason 5 *choice*;
  `src/game/online/prompt_wire.{hpp,cpp}`, `server/.../Protocol/PromptMessages.cs`; the registries on both sides.
- **Relay** (`World/Prompt.cs`, hooks in RoomHub/WorldHub): PLAN answered with OPTIONS (nothing / starting / rows with decision
  40's preselection), PLAN's throttle (one a second, `plan_throttled`), `World:TestDropPlans` (W2b); the plan's CHOICE kept
  until the BARRIER names its plan id; CHOICE re-checked at the decision (join: JOIN with 3.4's holds or a guest's NEW; a row
  gone full / ended / not open: the barrier held with a refreshed prompt naming it; new: the chosen visibility, remembered);
  the prompt hold (HoldKind.Prompt, BARRIER_WAIT choice, no time limit) for a fresh arrival from home with candidates and no
  answer; OPTIONS refreshed at most once a second and only on a change (`World:OptionsRefreshMs`); *Back to the lobby* = the
  DISSOLVE redirect to stagML11 at the back point (default 3); a held prompt forgotten at a disconnect; a member that chose is
  no automatic co-arrival base and joins no foreign base; full instances listed greyed. `World:ArrivalPrompt` (true; false =
  I1a's automatic join, kept for the load gate and the older hub tests, whose Harness turns it off). World log lines `plan`,
  `options`, `choice`, `prompt_hold`, `prompt_back`, `prompt_new`, `choice_refused`.
- **Client session** (`src/game/online/session_prompt.cpp`): the gate form's input hold in `before_step` (the engine sees
  neutral pads; the held press released as a rising A after CHOICE or at once for nothing / starting; B released as the
  game's cancel; PlanWaitMs 1 s), PLAN's throttle, OPTIONS in both forms with the cursor kept on its instance through a
  refresh, `prompt_input` (rows, visibility, X details, A, B), the loading-screen form (no five-second fallback while it
  prompts), the loss of the connection (gate: the press dropped, the menu closed by B, "Connection lost. Try the gate again in
  a moment."; loading: closed), the BARRIER's plan id, "Joining <host>..." and the guest's toasts of a chosen row,
  `DW4_ONLINE_AUTOCHOICE=first|new|cursor|back`, `DW4_ONLINE_AUTOCHOICE_DELAY_MS`, `DW4_ONLINE_TEST_GATE_FORM=0`, trace lines
  `online-trace: prompt ...`.
- **Game** (`src/game/online_prompt_ui.{hpp,cpp}`, two hooks in game_main.cpp): the overlay in both forms (19.5.3's columns,
  "Here (n)", greyed states with their words, badges, X details, the hint line; loading form "B: Back to the lobby"), the
  prompt's keys from the own pad and the keyboard's arrows / Enter / Backspace.
- ***Back to the lobby*'s point** (001f786b, 19.7.5 "at the point next to the departure it took"): the accessor names the
  lobby's point next to each named departure (`DepartureChoice::back_point`: stagML11 point 1 at the gate, 3 at the return
  mat; `dw4_world_probe DATA mats stagml11` prints the mats and the generate points); the client remembers the named
  departure its own press last started (also with the gate form off) and sends that point in CHOICE *back*; an unnamed
  departure or a press more than a minute before sends 0xFF (the relay's stagML11 point 3). The relay takes points 0-4 only.

## Review fixes (2026-11-11, after the reviewer's findings)

- **Blocker: a chosen row lost to a stranger's co-arrival** (RoomHub.Decide): a fresh group whose gate-form CHOICE was a row
  held for a stranger's HEADING (the base check looked only at the others' choices) and FinishCoArrival made it a NEW. Now
  such a group holds for nobody (`announced` empty), and FinishCoArrival never replaces a chosen join (counted
  `co_arrival_base_chose_row`, unreachable). Tests `A_member_that_chose_a_row_holds_for_no_stranger_and_joins_the_row_it_chose`,
  `A_base_holds_for_no_member_that_chose_and_takes_none_as_its_co_arrival` (PromptReviewTests.cs).
- **CHOICE only for offered rows** (Prompt.cs OnPromptChoice): a join of a wid the plan / the held prompt never offered is
  counted (`choice_unoffered`) and ignored (no OPTIONS, no log line); one answer per plan (`choice_repeated`); a held barrier
  takes one CHOICE a second (`choice_throttled`, the newest kept in `ArrivalGroup.PendingChoice`, taken in CheckHolds);
  the refused choice's line names only an offered host; `PromptHosts` accumulates (never cleared). Only taken choices log.
- **Notice by the row's reason**: full -> "full now", gone -> ended / no longer open, still listed but greyed for another reason
  (host reconnecting, too long, story not here) -> no line.
- **Full rows after the context check** (WorldHub.Candidates): a quest player saw full story instances.
- **Order** (Matching.Order, I1a code): a stage group in a scene / loading ranks with the joinable rows in steps 1-3, after
  the rows joinable now in step 4; only boss fights and greyed rows go last.
- **PLAN's throttle** 900 ms by arrival (the client spaces PLANs 1 s by its clock). Gate-form B / a withdrawn plan counts
  `plan_closed`.
- **Client**: the plan is withdrawn (CHOICE back with the plan) when the menu closes otherwise or PlanWaitMs runs out;
  "Joining <host>..." only for a row with players here (`chosen_here_`); state 6 reads "Other quest"; the keyboard hint has no
  "X: Details".
- **Load gate** (FakeClient LoadGate.cs): its hubs run the prompt as shipped (`LoadGate.ArrivalPrompt`, `--no-prompt` for
  I1a); joins go through the gate form (PLAN, CHOICE, BARRIER with the plan); new cell `prompt` (joins cell + 5 PLANs a second
  from the lobby's gate, overlays open 3 s, every second join through the loading-screen form). Test
  `LoadGateTests.A_small_prompt_cell_joins_through_both_forms_and_measures_plan_and_choice`. Measured: SEAMLESS.md 19.17.4.
- **Prompt variants of the I1a rule tests** (PromptVisibilityTests.cs): private / friends / staff, blocks either way and a
  playing moderator, the removal bar, the data hash, the host reconnecting, the boss lock (Wait / Go alone).
- **W11** expects no prompt, PLAN, OPTIONS or held barrier for a's move into dng101b1 (decision 41).
- SEAMLESS.md: 19.7.3 (the loading rows), 19.7.4 (PLAN 900 ms), 19.24 (W30c in stag0202), 19.32 (the fixes, the gate form's
  "full now" at the BARRIER, the return mat without a real-client scenario, the open question below).
- (Finding 9's open question was answered by the user on 2026-11-11: decision 43, built in the second review fixes below.)

## Second review fixes (2026-11-12)

- **Cursor rule through a refresh** (decision 40, client `open_prompt`): `PromptView::moved` (up / down moved the cursor, or
  Left / Right changed New instance's visibility); a refresh keeps only a moved cursor on its wid / New instance, an unmoved
  one takes the refresh's `preselected`. Session test `prompt_gate_form` (Kim's row loses its Here: the cursor goes to New
  instance; moved by the player: kept; back to a row when the preselection comes back). 14f2c86a.
- **Notice line** (`PromptView::notice_host`): kept through refreshes only while no row of its host is choosable. 14f2c86a.
- **Dead code**: `Overlay::prompt` removed; `Session::holding_press()` stays (the session tests observe it), its comment
  fixed. 14f2c86a.
- **Decision 43** (relay, 4cdb0f8a): Decide's holder filter `g.PromptChoice is not { Choice: ChoiceKind.Join } &&
  MaySeeBase(h, g)` (plan 0xFFFF no longer excluded); announced `!x.Prompt.ChoseRow` (`PromptPlan.Chose` -> `ChoseRow`, a Join
  only); FinishCoArrival writes `prompt_new ... coarrivals=N` for a New base; DecideChoice New and FreshJoinOrNew's PlanNew
  join a starting instance the arrival may see and was not offered (`StartingNotOffered`, `TakeStarting`, counter
  `new_choice_starting`, `candidates ... by=new_together`). Tests `PromptTogetherTests.cs` (5 cases), the review test
  `A_base_holds_for_no_member_that_chose_a_row_but_for_a_new_instance_chooser`; scenario **W4c** (b18decc0).
- **Harness** (77a4f6a3): `HubLogicTests.Harness` without `world:` runs World:ArrivalPrompt on and answers held prompts like
  the FakeClient (`Peer.AutoAnswer`, `Harness.AnswerPrompts` after every Send, `Harness.Housekeep()` for
  `h.Hub.Housekeep(h.Now)`): 43 I1a / J tests go through a held prompt (counted with a temporary instrumentation) and pass;
  I1a's notice test keeps `ArrivalPrompt = false` and has a prompt-on twin
  (`With_the_arrival_prompt_another_context_keeps_its_notice_and_a_long_catch_up_is_a_greyed_row`); decision 41's and the
  guest-NEW gate test check the prompt.
- **Docs**: SEAMLESS.md 19.1 (decision 43's row), 19.5.6 (the keyboard hint line, for the user's OK), 19.7.3 (the cursor),
  19.7.6 / 19.7.7 (decision 43), 19.24 (W4c), 19.32 (decision 43, the second review: Arriving now not built, finding 8 open,
  PLAN's stages kept).
- Rejected / for the user: finding 6 (*Arriving now*: not built, 19.32), finding 8 (a no-answer base before its own prompt:
  open for the user, 19.32), finding 9 (PLAN of any stage: kept, the return mat names any gate-disc stage, 19.32).

## Tests

- Server unit: `PromptTests.cs` (PLAN nothing / rows / starting, W1's preselection, decision 40's order and the cursor of
  W30c without friends, the hold without a time limit and Back, the refresh only on a change, CHOICE new with the remembered
  visibility, a chosen row that ended, the throttle and the drop hook, the disconnect, staff's visibility byte, golden bytes).
- Client ctest: `online_wire` (golden bytes = the relay's, malformed input), `online_session` (`prompt_gate_form`,
  `prompt_loading_form` against the fake hub's PLAN / OPTIONS / CHOICE and the fake engine's gate menu).

- Server integration: `WorldPromptTests.cs` (the gate form's PLAN round trip with the CHOICE carried by the BARRIER's plan
  id; World:TestDropPlans: the barrier decided as a loading-screen arrival, held past the client's five seconds, Back to the
  lobby; the held prompt forgotten at a disconnect); the I1a automatic-join test of WorldRelayTests runs with
  World:ArrivalPrompt false; LoadGate hubs too; FakeClient answers a held barrier's OPTIONS (as autochoice first).
- The census measured: `dw4_world_probe DATA census` (tools/world_probe.cpp), log `build/i1bp/meas/census_r3.log`: 12 of 12
  named lines equal to the departure the press started, the return mat's No and the gate's B start nothing (SEAMLESS.md
  19.7.4 "Measured"). The census test on the stage graph: `PromptTests.The_census_no_static_edge_leads_from_a_hub_stage_into_a_field`.
- Scenarios (tools/online_test/scenarios): W1 (the prompt: autochoice cursor takes the preselection), W2 (B), W2b (a lost
  PLAN), W11b (the loading-screen form and Back), W30c (decision 40's order and cursor, three processes, autochoice
  back,first); W30b, W30, W4bx expect the prompt's traces and toasts.
- Docs: SEAMLESS.md 19.7.4 "Measured (round I1b)", 19.32 (what the lane built and decided); the contract entries "prompt
  16:30" and "prompt 17:00".

## Left

- Nothing of the lane after the second review fixes (see "Second review fixes" and the runs below).
- For the integration: the contract entries "prompt 16:30", "prompt 17:00", "prompt 2026-11-11 14:25" and "prompt 2026-11-11
  16:20" (merge hints: Decide's `announced` with the go lane's `g.Go`, DecideChoice's TakeSeat bool / SendRemovedHome for the
  moderation lane, Matching.Order is I1a code, the load gate's hubs); W10's prompt check (moderation 15:20).
- A real-client scenario of the return mat's gate form (mate02): needs the go lane's `testhook mat` and the moderation lane's
  `setflag` (a powered Security Gate); the census measured it headless.
- For the user (SEAMLESS.md 19.32): *Arriving now* (not built), a no-answer base before its own prompt (finding 8), the
  keyboard's hint line in 19.5.6.

## Runs

- r1 (exe a = d154e706, `build/i1bp/scen_r1.log`): W1, W11b failed on their expectations' wording (fixed in d57f06c5,
  b15119ec); W2, W2b broke on the harness's mysqld (the run was cut off), no finding.
- At b15119ec (exe b = the DEV=ON game of fe468a54; b15119ec changed only expected files): ctest 57/57 (`presence`, a
  timing test of the waits, failed once under the machine's load with the scenarios running and passed alone), server unit
  478/478, integration 141 passed / 3 skipped (mysqld 3651), det.sh with the DEV=OFF copy
  `build/i1bp/off/dw4_i1bp_off_b15119ec.exe`: 4004f056 / 396cb219 twice (online off unchanged).
- r2 (exe b, `build/i1bp/scen_r2.log`): **W1, W11b, W2, W2b, W30b passed**; W30c: the first prompt as expected (c's row
  with Here (1) first, a's row second, the cursor on c's, B), then b stood on the mat after B and the gate's menu never
  opened again (the mat's script opens it only on a new step onto it): b.script walks back to its start first (8aee0bfd).
- r3 (exe b, `build/i1bp/scen_r3.log`, cut off by the PC's shutdown during W9): **W30c, W2 passed**; of the I1a set with
  autochoice first **W3 W4 W4b W4bx W5 W6 W7 W7b W7c W7d W7e W8 W11 W12 W18 W24 W25 W26 W27 W28 W32 and W1x passed**; W19,
  W22, W29 skipped (they need fake_world_clients / old_client, as in I1a's runs); W30 failed: a's explore bot (k10) lost
  its HP in stag0201's fights on the way to stag0202 and fell at frame 12211, so b found a at stag0201 (rows
  `[1:...:joinable:1:stag0201]`, the cursor on a's row: decision 40 right) and the instance ended at a's defeat; W30x
  (counter-proof) failed the same way. A bot flake, not the prompt: run again in r4.
- At 001f786b (exe c = the DEV=ON game with the back point, `build/i1bp/scen/dw4_i1bp_dev_c.exe`): ctest 57/57; server unit
  479/481 right after a reboot: `HubLogicTests.A_resync_starts_from_the_newest_agreed_checkpoint_and_its_ready_never_judges`
  (both protocols) fails while the machine's uptime is under ~1,016 s: `RoomHub.BecomeMember` stamps `InstanceSince` with
  the real `Stopwatch.GetTimestamp()` while the test harness's clock starts at 1,000 s, so the joiner looks older than the
  host and wins the two-member vote (an I1a/J harness finding, not this lane's; 481/481 at 14:27 with the longer uptime;
  contract entry "prompt 2026-11-11 14:25"). det.sh with the DEV=OFF copy `build/i1bp/off/dw4_i1bp_off_001f786b.exe`:
  4004f056 / 396cb219 twice (online off unchanged).
- r4 (exe c, `build/i1bp/scen_r4.log`): **W11b** (B's CHOICE back_point=1, the relay's `prompt_back ... point=1`, a back
  next to the gate), **W30** (404 s: b's gate form lists a's row `joinable:0:stag0202` with the cursor on New instance,
  autochoice first takes it, "<host> is in Grief Wasteland."), **W30x** (the counter-proof fails as it must, 402 s), **W9**
  passed. So the whole I1a W set with autochoice first passes on this branch except the three that need the fake world
  clients or the protocol 48 client: r5.
- r5 (exe c, `build/i1bp/scen_r5.log`, `build/i1bp/scen_caps.sh`: `--capabilities fake_world_clients,old_client` with the
  protocol 48 client copied from int-i1a's `build/int/old48`): **W19** (117 s), **W22** (3 s), **W29** (461 s) passed. W29's
  fake guests answer the loading-screen form (FakeClient as autochoice first): the first gets a's row `Joinable:0` (nobody
  at stag0101, the cursor on New instance) and enters as a guest in a new stage group, the second `Joinable:1`
  preselected; b's gate form showed a's row `Loading:1` preselected (a waiting row is not greyed).
- Server integration tests at 001f786b (own mysqld 3651, stopped by SQL SHUTDOWN): 141 passed / 3 skipped (`build/i1bp/it3.log`).
- r6 (exe c, `build/i1bp/scen_r6.log`): the lane's own W1 W2 W2b W30b W30c again on the final exe (r2 / r3 ran them on exe b,
  which lacks only the back point): **W1, W2, W2b, W30b, W30c passed**.

- **Review fixes** (2026-11-11, code at b01cc47b; the C++ of c5582b00 = exe d `build/i1bp/scen/dw4_i1bp_dev_d.exe`, DEV=OFF
  copy `build/i1bp/off/dw4_i1bp_off_c5582b00.exe`): server unit 494/494 (`build/i1bp/unit6.log`), integration 142 passed / 3
  skipped (mysqld 3651, `build/i1bp/it4.log`, at c5582b00), ctest 57/57 (`build/i1bp/ctest5.log`), det.sh 4004f056 /
  396cb219 twice (`build/i1bp/off/det_c5582b00.log`), r25c 10/10 reached, 1,748,011 frames, every save the same story
  (`build/i1bp/off/r25c_c5582b00.log`, main's work/campaign/runs/seg/seg_r25c_i1bp_c5582b00), the save state suite 17/17
  identical after loading (`build/i1bp/st/st_c5582b00.log`). Load gate with the prompt: SEAMLESS.md 19.17.4
  (`build/i1bp/gate/*.json`, `*.log`).
- r7 (exe d, server at bb6c7ad9, `build/i1bp/scen_r7.log`): **W1, W11 (with decision 41's new prompt checks), W11b, W2b, W30b
  passed**; W2 and W30c failed only on `relay: choice .*choice=back` (the gate form's B was no longer logged after the first
  fix; b01cc47b logs it again).
- r8 (exe d, server at b01cc47b, `build/i1bp/scen_r8.log`): **W2, W30c passed**. r9 (the same, `build/i1bp/scen_r9.log`):
  round I1a's **W4, W4b, W4bx, W30 passed** (co-arrival, the starting instance, the guest's NEW through the prompt).
  So the lane's W1, W2, W2b, W11, W11b, W30b, W30c pass on the review fixes; I1a's other W scenarios last passed on exe c
  (r3-r5), before the review fixes.

## How to run the tests

- C++: `cmd //c "start /low /wait /b cmd /c build\i1bp\bld.cmd rel ON nodev"` (Release, DEV=OFF, tests; FETCHCONTENT from the
  main repo's build/Release/_deps), `cmd //c "build\i1bp\ctest.cmd rel"`; the DEV=ON game for scenarios:
  `... bld.cmd dev OFF dev --target dw4_game`.
- Server: `source build/i1bp/env.sh` (DOTNET, DW4ONLINE_TEST_MYSQL on 3651, DW4ONLINE_TEST_PORTS 58000-58199), then
  `"$DOTNET" test --disable-build-servers server/DW4Online/tests/DW4Online.Tests` (unit) and `.../DW4Online.Integration.Tests`
  (needs the own mysqld: `bash server/DW4Online/.local/mysqld.sh` in the background, data in `server/DW4Online/.local`
  (gitignored, made with XAMPP's mysql_install_db, root password in `.local/mysql-root.txt`), stopped with SQL `SHUTDOWN`).
- Scenarios: stop the own mysqld first (run.py starts its own on the same port), copy `build/i1bp/dev/dw4_game.exe` to
  `build/i1bp/scen/dw4_i1bp_dev_<TAG>.exe`, then `bash build/i1bp/scen.sh LOG TAG W1 W2 ...` (run.py with --parallel 1 --keep,
  the r25c cards of the main repo, mysqld 3651, ports 58000-58199; add `--no-build` after the first run). Run folders:
  `tools/online_test/.work/run_*`.
- The census: `build/i1bp/rel/dw4_world_probe.exe DATA census` (DW4_PROBE_DEBUG=1 prints the menus and demo works).
- The load gate with the prompt: `"$DOTNET" build -c Release server/DW4Online/tests/DW4Online.FakeClient -o build/i1bp/gate/fc`,
  then in `build/i1bp/gate` with `DOTNET_gcServer=1`: `"$DOTNET" fc/DW4Online.FakeClient.dll loadgate --cells base,joins,prompt
  --seconds 30 --out X.json` (`--no-prompt` for I1a's join), `... joins --out Y.json`; summary
  `python lg_sum.py X.json` (a scratch script: hold_all, joins, plans, the plan / choice / join holds).
- The regression (DEV=OFF copy `build/i1bp/off/dw4_i1bp_off_<head>.exe`): r25c from the main repo,
  `EXE=<copy> NAME=seg_r25c_i1bp_<head> nice -n 15 bash work/campaign/segments.sh r25c 2` (runs in main's
  work/campaign/runs/seg/NAME); the save state suite from the own copy `build/i1bp/st` (`EXE=<copy> bash build/i1bp/st/st_all.sh`).
- The server's tests need no scenario running (a scenario's server locks DW4Online.Server/bin's Relay.dll).
