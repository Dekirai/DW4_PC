# Round 0.2.1, the relay lane: progress notes

Branch `fix-021-relay` from main d230037f (release 0.2.0: protocol 50, schema 14, DW4Online 0.2.0), worktree
`DW4_PC-wt/r021relay`. Relay-only fixes for DW4Online 0.2.1; protocol stays 50 (0.2.0 clients work with a 0.2.1 server and
the other way round), no schema change, the game's code unchanged. Own resources: mysqld 3750-3754, ports 60600-60699,
builds and logs in `build/r021/` (gitignored; "How to run" below).

The task: the four relay findings of "Open after 0.2.0" (docs/ROUND_INT_I1B_NOTES.md) that are bugs, the lag rule's gap
("The flapping host", left), and fix-d43's pre-existing finding on a removed member playing alone.

## Findings and fixes

| # | Finding | Fix | Test (fails without the fix) | Commit |
| --- | --- | --- | --- | --- |
| 1 | `StateReporter` kept a host in a grace started by silence (half-open link) as the reporter | in progress | | |
| 2 | `OnState` applied the reporter's STATE without the newer-by-tick check | in progress | | |
| 3 | `StartGrace` at a gate: the parked group counted as shared | in progress | | |
| 4 | the lag rule's gap: short drops with bursts in time never reach the limit | in progress | | |
| 5 | `OnForeignStory` / `PlayContext` do not see a removed member playing alone | in progress | | |

## Left

- (to be filled)

## How to run

- `build/r021/env.sh` (paths), `bld.cmd DIR TESTING [dev|nodev]` (the dev client: `bld.cmd dev OFF dev --target dw4_game`,
  copied to `build/r021/scen/dw4_r021_dev.exe`), `utest.sh LOG [filter]` (unit tests at idle priority).
