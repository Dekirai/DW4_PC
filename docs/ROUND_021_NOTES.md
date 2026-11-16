# Round 0.2.1, the relay lane: progress notes

Branch `fix-021-relay` from main d230037f (release 0.2.0: protocol 50, schema 14, DW4Online 0.2.0), worktree
`DW4_PC-wt/r021relay`. Relay-only fixes for DW4Online 0.2.1; protocol stays 50 (0.2.0 clients work with a 0.2.1 server and
the other way round), no schema change, the game's code unchanged. Own resources: mysqld 3750-3754, ports 60600-60699,
builds and logs in `build/r021/` (gitignored; "How to run" below). Not merged, not pushed; the server's version
(`Directory.Build.props`, 0.2.0) is left to the release's integration.

The task: the four relay findings of "Open after 0.2.0" (docs/ROUND_INT_I1B_NOTES.md) that are bugs, the lag rule's gap
("The flapping host", left), and fix-d43's pre-existing finding on a removed member playing alone. SEAMLESS 19.32
("fix-021") has the rules; 6.4, 6.9, 19.6.6, 19.6.7 and 19.24 say them where they apply.

## Findings and fixes

| # | Finding | Fix | Test (fails without the fix) |
| --- | --- | --- | --- |
| 1 | `StateReporter` took the connected member longest: a host in a grace started by silence (half-open) stayed the reporter, the guests' STATEs were ignored, a held arrival was let in on his old flags or got a NEW (`HoldExpired`) | the reporter is the connected member longest that is not away: not a host in his grace, not resyncing, its slot not lagging, its engine not more than 180 ticks behind (its newest NETFP, `HubMember.ExecutedTick`); `StartGrace` and `HostBack` sync it | `A_host_silent_in_his_grace_hands_the_flags_...`, `A_guests_boss_fight_while_the_hosts_link_hangs_...`, `A_returned_host_who_still_lags_...`, `A_returned_host_whose_engine_still_catches_up_...`; W7h |
| 2 | `OnState` applied the reporter's STATE without the newer-by-tick check: a host back from a drop overrode a guest's newer STATE with the ones of the ticks he caught up | `Supersedes`, one rule for `OnState` and `SyncStateReporter`: none in effect, its member left, or newer by tick; at the same tick only the member in effect's own; an older one is kept (`state_stale`) | `A_host_back_from_a_drop_never_overrides_...` |
| 3 | `StartGrace` at a gate: the gathering stage group counted as shared with the guests, the absence debt shortened the grace although they went on at once | `SharedGroup`: a stage group at its barrier is shared with nobody (a fresh grace) | `A_host_who_drops_exactly_at_a_gate_...`; W7j |
| 4 | the lag rule's gap: drops each below the limit with short bursts in time between them never reached it (guests; a host below `World:HostSilenceSeconds`) | `SlotStream.LagDebt`: filled ticks that held a connected member in time up, minus ticks in time, never below 0 (not while every connected member was filled, a host in his grace, a member resyncing); a lagging member is out at `Relay:LagLeaveSeconds` of it (`lag_leave why=lag_debt`); a host's return restarts it; the fill streak's rule stays | `A_guest_whose_link_hangs_open_again_and_again_...`, `A_host_whose_link_hangs_open_below_the_silence_...` (control: `Brief_hiccups_paid_back_...`); W7i |
| 5 | `OnForeignStory` / `PlayContext` did not see a removed member playing alone (`HubMember.LoneWorld`): its own key reached its own keeper with the host's progress, the context said home | `OnForeignStory` reads `LoneWorld` for a member without a stage group (or in the empty hub one of a reconnect): foreign unless it holds its seat there or the story is its own | `A_guest_removed_while_it_plays_on_alone_is_on_...`, `A_guest_back_without_its_slot_and_removed_...` (control: `A_host_playing_apart_keeps_...`) |

Counter-checks (`build/r021/counter.sh NAME FIX LOG FILTER`: a copy of HEAD's server tree in `build/r021/counter/NAME`
with one fix taken out by `counter_patch.py`, or main's four relay sources): main's relay 9 of the 11 then-new tests fail
(the two controls pass); without fix 1 the three reporter tests, without 2 its one, without 3 its one, without 4 its two,
without 5 its two (`logs/counter_*.log`).

## Checks

(in progress)

## Left

- (to be filled)

## How to run

- `build/r021/env.sh` (paths); `bld.cmd DIR TESTING [dev|nodev]` (the dev client: `bld.cmd dev OFF dev --target dw4_game`,
  copied to `build/r021/scen/dw4_r021_dev.exe`; the client code is main's); `utest.sh LOG [filter]` (unit tests at idle
  priority; `new_filter.txt` holds the fix-021 tests); `itest.sh LOG [filter]` (integration tests, own mysqld on 3750,
  data `build/r021/it-mysql`, test ports 60600-60649, SQL SHUTDOWN after); `scen.sh LOG SCENARIO... [--no-build]`
  (run.py through `wrap/run_wrap.py`, mysqld 3751, ports 60650-60699); `marathon.sh [BATCH...]` (batches `m_new`, `m_w4`,
  `m_w7`, `m_w7f_2`, `m_w7f_3`, `m_w7g_2`, `m_w7g_3`; a failed batch's scenarios once more); `counter.sh` (above).
