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
- An unblock delivers the formerly blocked player's still pending requests
  (`delivered = 1`): he sent them, the blocker may answer them now.
- Cancelling a declined request hides it from the sender (`cancelled_at`)
  but keeps the decline until it expires; sending again shows it as sent
  again (with a new date) and still delivers nothing. Without this a cancel
  and a new request would get round the decline.
- A request to a player whose own request to you is open (pending, or
  declined by you and not cancelled by him) makes you friends at once (the
  crossing rule; a decline you regret is undone by sending a request).
- The incoming cap is checked before the block, so the answer is the same
  with and without a block. The sender's hourly and daily counts are kept in
  memory (a restart starts them afresh).

## Progress

- (started 2026-11-05)
