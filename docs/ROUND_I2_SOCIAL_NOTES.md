# Round I2-social: friends and blocks on the server and the website - lane plan and progress

Branch `feat-i2social` (worktree `DW4_PC-wt/i2social`), from main a475b98a
(DW4Online 0.1.5, protocol 47, migrations up to 0011). Design:
`docs/SEAMLESS.md` 19.11 (model), 19.17.1 / 19.17.2 (tables, scrub,
`IWorldNotifier`), 19.19 (REST), 19.20 (website), 19.22 (migration rules),
19.25 step 2 and "Round I2-social". Server and web only: no relay, no client
session files, no protocol change, no version bump.

This file is the progress note a later run continues from (the PC may shut
down at any time). Keep it short; the design stays in SEAMLESS.md.

## Scope

| # | Part | Where |
| --- | --- | --- |
| 1 | migration `0012_friends.sql`: `friendships`, `friend_requests`, `account_blocks` (plain columns, COMMENTs for HeidiSQL, no views: views need `CREATE VIEW`, which the restricted service user lacks until round D's grants, so the views stay with round I1's `world` migration) | `sql/` |
| 2 | `ISocialRepository` + `SocialRepository` (Dapper), the scrub statements in `AccountRepository.ScrubAsync`, the hourly purge of expired requests | Data |
| 3 | `SocialService` (19.11's rules and limits, `Friends:*` options), `IWorldNotifier` + `NullWorldNotifier` | Domain |
| 4 | REST: `/friends`, `/friends/requests`, `/blocks`, `/players/{username}` with the rate limits `RateLimits:SocialPerMinute` (60) and `FriendListSeconds` (2) | Api |
| 5 | website: `/account/friends`, the profile buttons, the privacy page, the navigation | Web |
| 6 | tests: `SocialServiceTests` (unit), `SocialApiTests`, `AccountScrubTests`, `FriendPageTests`, `MigratorTests` (integration, own mysqld on 3623) | tests |
| 7 | docs: SEAMLESS.md 19 status, README (tables, API, config), DEPLOY.md (the migration with a HeidiSQL check) | docs |

## Decisions taken here (not in the design text)

- A block keeps the blocked player's open request to the blocker (it turns
  `delivered = 0`): it stays "sent" in his list, so the block is not
  revealed by a vanishing row. The blocker's own requests to him go.
- An unblock makes the formerly blocked player's still pending request a
  waiting one: delivered at once when the blocker has a place, else in its
  turn (after the review; it used to be delivered over the cap).
- Cancelling a declined request hides it from the sender (`cancelled_at`)
  but keeps the decline until it expires; sending again shows it as sent
  again (with a new date) and still delivers nothing. Without this a cancel
  and a new request would get round the decline.
- A request to a player whose own request to you is open (pending, or
  declined by you and not cancelled by him) makes you friends at once (the
  crossing rule; a decline you regret is undone by sending a request).
- (Replaced after the review.) "The incoming cap is checked before the
  block" held only for a sender's first request: cancel + resend into a full
  inbox, or a second account right after the sender, showed blocks and
  declines. Now the incoming cap never answers: a request beyond
  `MaxPendingIn` answers "Request sent." and waits (`delivered = 0`, sender
  not blocked) until a place frees, oldest first (SEAMLESS.md 19.11, 19.25;
  the user may still prefer counting silent and declined rows instead).
  The sender's hourly and daily counts are kept in memory (a restart starts
  them afresh).

## Progress

- 2026-11-05: parts 1-7 built and committed (4771e74b server and web,
  b2e889ba unit tests, 88353910 integration tests, then the docs commit).
- Tests: unit 285 of 285 (19 new `SocialServiceTests`); integration on the
  own mysqld (XAMPP 10.4.32, port 3623, PID file in
  `server/DW4Online/.local`): 108 passed, 4 skipped (EndToEnd: WinHTTP
  client, Python and certificate store not set up), 0 failed. The full
  suite needs more than the 30 ports of 56760-56789 within a minute
  (`TestPorts` hands a port out once a minute): the relay classes
  (`RelayConnectionTests`, `OnlineStoryTests`, `RoomTests`) ran again on
  their own a minute later, 18 of 18. Logs: `build/i2social/unit.log`,
  `integration.log`, `integration_relay.log`.
- The fresh XAMPP data directory had crashed Aria system tables
  (`mysql.db`, `mysql.global_priv`): repaired with mysqlcheck and a
  restart (README note); only the restricted-grants test writes there.
- Review fixes (2026-11-05, f9c1b535 code and tests, 5776b626 docs):
  waiting requests instead of `requests_full`; unblock only up to the cap;
  `memberSince` null on hidden player cards; striped per-account locks with
  a 10 s wait (503 `social_busy`); `social_limit` once per account, limit
  and hour; own id for the block form's input; privacy retention lines;
  how round I1 registers its `IWorldNotifier`. Each new test was checked to
  fail with the old behaviour put back (mutations in a scratch copy).
  Results: unit 290 of 290 (24 `SocialServiceTests`); the social
  integration classes 24 of 24; the full integration run 103 passed,
  4 skipped (EndToEnd), 6 failed only with "DW4ONLINE_TEST_PORTS has no
  free port left" (the relay classes; 30 ports, one hand-out a minute);
  those three classes alone a minute later 18 of 18. Logs:
  `build/i2fix/unit.log`, `integration.log`, `integration_relay.log`,
  `integration_social.log`. No game, relay or client code changed (the
  branch's diff outside `server/` and `docs/` is empty): no ctest, det.sh
  or game scenarios.
- The own mysqld must outlive the shell that starts it: a killed mysqld
  left `mysql.global_priv` broken (README: `--skip-grant-tables` +
  `REPAIR TABLE ... USE_FRM`).
- Open: merge into main and a server release (the user deploys it after a
  backup; DEPLOY.md 9.1), raising the version (0.1.5's zip exists without
  0012) and checking the migration number; the views, prefs, whereabouts
  and the relay's notifier come with round I1.
