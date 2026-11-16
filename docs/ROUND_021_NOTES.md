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
| 3 | `StartGrace` at a gate: the gathering stage group counted as shared with the guests, the absence debt shortened the grace although they went on at once | `SharedGroup`: a stage group at its barrier is shared with nobody (a fresh grace); and a grace the debt shortened is fresh again from its start once the host shares no stage group with a guest (`host_grace_fresh`: a link that closed a moment before his BARRIER came, his guests going on without him) | `A_host_who_drops_exactly_at_a_gate_...`, `A_host_whose_link_closes_a_moment_before_his_gate_...`; W7j |
| 4 | the lag rule's gap: drops each below the limit with short bursts in time between them never reached it (guests; a host below `World:HostSilenceSeconds`) | `SlotStream.LagDebt`: filled ticks that held a connected member in time up, minus ticks in time, never below 0 (not while every connected member was filled, a host in his grace, a member resyncing); a lagging member is out at `Relay:LagLeaveSeconds` of it (`lag_leave why=lag_debt`); a host's return restarts it; the fill streak's rule stays | `A_guest_whose_link_hangs_open_again_and_again_...`, `A_host_whose_link_hangs_open_below_the_silence_...` (control: `Brief_hiccups_paid_back_...`); W7i |
| 5 | `OnForeignStory` / `PlayContext` did not see a removed member playing alone (`HubMember.LoneWorld`): its own key reached its own keeper with the host's progress, the context said home | `OnForeignStory` reads `LoneWorld` for a member without a stage group (or in the empty hub one of a reconnect): foreign unless it holds its seat there or the story is its own | `A_guest_removed_while_it_plays_on_alone_is_on_...`, `A_guest_back_without_its_slot_and_removed_...` (control: `A_host_playing_apart_keeps_...`) |

Counter-checks (`build/r021/counter.sh NAME FIX LOG FILTER`: a copy of HEAD's server tree in `build/r021/counter/NAME`
with one fix taken out by `counter_patch.py`, or main's four relay sources; the 13 tests of `new_filter.txt`): on main's
relay 11 of 13 fail, the two controls pass (`logs/counter_main_13.log`); without fix 1 (the whole away rule) the four
reporter tests (`counter_f1_13.log`), without its NETFP part alone (f1b) the catching-up test, without 2 its one, without
3's barrier rule (f3) the gate test, without the renewed grace (f3b) its one, without 4 its two, without 5 its two
(`counter_f1b.log` ... `counter_f5.log`).

**The test proxy** (tests/DW4Online.FakeClient `ImpairmentProxy`): it closed both sides a fixed 600 ms after one side's
end, so W7j's first run (`run_20261116_161608`, on 85f2bdf1) had a's close reach the relay 612 ms after a's BARRIER,
after b's (150 ms later) completed the barrier: the sealed NEW of both, a in it with b, an ordinary drop, 30 s of grace
(debt 40.3 s) and `host_lost`, the path the fix leaves as it was (W7e's). Now it waits for that direction's latest delivery
time (a half-open cut's end included), then 50 ms; `ProxyCutTests` pass.

## Checks

- **Unit tests** 716/716 (`logs/unit_4.log`, f6276ed4; 703 of 0.2.0 unchanged plus the 13 of `Fix021RelayTests`).
- **Integration tests** 155 passed, 3 skipped (the opt-in end-to-end tests; `logs/it_1.log`, 29e95f20, own mysqld on 3750,
  shut down by SQL).
- **The dev client**: `build/r021/dev` (Release, DW4_ONLINE_DEV=ON, main's client code), copied to
  `build/r021/scen/dw4_r021_dev.exe`, md5 b3fa754f.
- **Scenarios, first runs** (`logs/scen_new1.log`, `run_20261116_161608`, relay of 85f2bdf1): W7h passed (c held for b's
  scene at 196.3 s of the relay's clock, a's silence grace at 202.6 s, c's JOIN decided at 211.5 s when the scene ended, a
  back at 248.5 s; no `HoldExpired`), W7i passed (`lag_leave why=lag_debt lag_debt_s=10` at 211.8 s, 11-12 s after the
  first cut; a led on alone, b played apart), W7j failed on the proxy's late close (above).
- **Scenarios, the final code** (`marathon.sh`, `logs/marathon.log`, relay and proxy of f6276ed4, mysqld 3751, ports
  60650-60699, run.py `--parallel 1`): batch `m_new` (`scen_m_new.log`, `run_20261116_164553`) W7h, W7i, W7j passed.
  W7h: c held for b's scene at 197.4 s, a's silence grace at 203.4 s, c's JOIN decided at 212.5 s, `host_back` at 249.3 s.
  W7i: `lag_leave why=lag_debt lag_debt_s=10.1` at 211.6 s. W7j: the barrier-set path (a's BARRIER at 298.09 s, his close
  56 ms later: `host_away why=closed grace_s=60 debt_s=37.7`, b's BARRIER at 298.24 s, b on into stag0102; a back at
  338.1 s, `join_decided` of b's stage group, one instance, `host_left` at the scripts' end).

## Left

- The server's version (`Directory.Build.props`) is still 0.2.0: the release's integration raises it to 0.2.1.
- No real-client scenario for findings 2 and 5. 2: back from a drop, a host is away for the reporter while his engine
  catches up (its NETFP behind), so his catch-up STATEs are ignored, not stale; the newer-by-tick rule is the unit test's.
  5: an unmodified client keeps sending the host's owner key until it is home (W10c), so its STORY was refused before too;
  the own key needs a modified client, the context an achievement report in that window; unit tests only.
- A JOIN's checkpoint donor (`AskCheckpoint`) is still the connected member longest, also one whose link hangs open or
  who catches up: the join waits `Instances:CheckpointWaitMs` (1 s) and takes the classic state (W7h's c). The reporter's
  away rule would fit there too.
- Seen in the code, not tested: a host whose link hangs open exactly at a stage load (after his BARRIER, before his
  SNAPHASH) holds the new sealed stage group's start for up to `Instances:SnapHashSeconds` (60 s): before sealing starts
  his slot does not fill, so neither the silence grace nor the lag rule sees him.
- W7j takes the barrier-set path while the proxy passes a's close on before b's BARRIER (b's uplink 150 ms slower); in the
  other order the renewed grace applies (its expectation takes either).

## How to run

- `build/r021/env.sh` (paths); `bld.cmd DIR TESTING [dev|nodev]` (the dev client: `bld.cmd dev OFF dev --target dw4_game`,
  copied to `build/r021/scen/dw4_r021_dev.exe`; the client code is main's); `utest.sh LOG [filter]` (unit tests at idle
  priority; `new_filter.txt` holds the fix-021 tests); `itest.sh LOG [filter]` (integration tests, own mysqld on 3750,
  data `build/r021/it-mysql`, test ports 60600-60649, SQL SHUTDOWN after); `scen.sh LOG SCENARIO... [--no-build]`
  (run.py through `wrap/run_wrap.py`, mysqld 3751, ports 60650-60699); `marathon.sh [BATCH...]` (batches `m_new`, `m_w4`,
  `m_w7`, `m_w7f_2`, `m_w7f_3`, `m_w7g_2`, `m_w7g_3`; a failed batch's scenarios once more); `counter.sh` (above).
