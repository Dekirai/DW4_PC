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

## Tests

- Server unit: `PromptTests.cs` (PLAN nothing / rows / starting, W1's preselection, decision 40's order and the cursor of
  W30c without friends, the hold without a time limit and Back, the refresh only on a change, CHOICE new with the remembered
  visibility, a chosen row that ended, the throttle and the drop hook, the disconnect, staff's visibility byte, golden bytes).
- Client ctest: `online_wire` (golden bytes = the relay's, malformed input), `online_session` (`prompt_gate_form`,
  `prompt_loading_form` against the fake hub's PLAN / OPTIONS / CHOICE and the fake engine's gate menu).

## Left

(see the end of this file; kept current while the lane runs)

## How to run the tests

- C++: `cmd //c "start /low /wait /b cmd /c build\i1bp\bld.cmd rel ON nodev"` (Release, DEV=OFF, tests; FETCHCONTENT from the
  main repo's build/Release/_deps), `cmd //c "build\i1bp\ctest.cmd rel"`; the DEV=ON game for scenarios:
  `... bld.cmd dev OFF dev --target dw4_game`.
- Server: `"$DOTNET" test --disable-build-servers server/DW4Online/tests/DW4Online.Tests` (unit), the integration tests need the
  own mysqld on 3651 (below).
