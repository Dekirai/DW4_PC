# DW4Online

The online backend of the Digimon World 4 PC port: accounts, the online character, the private cloud backup of the
local save, achievements and the TCP relay, in **one** ASP.NET Core process (API, website and relay share memory)
backed by MariaDB. It runs as a Windows service on the user's Windows Server; players never forward ports.

Status of this first round (feature package "online"): the core service is complete and tested. The room API
(create / join / heartbeat, issuing relay tickets), the website pages and the game client integration are separate
work; their hooks are in place (see "Open ends").

## Layout

| Path | What |
| --- | --- |
| `src/DW4Online.Domain` | Pure rules, no ASP.NET Core and no database: the 3316-byte character record codec (C# port of `src/save_data.cpp`), validation, summaries, password hashing, tokens, recovery codes, achievement rules, the services (accounts, online character, cloud save, achievements) and the repository interfaces. |
| `src/DW4Online.Data` | MariaDB through MySqlConnector + Dapper: repositories and the startup migrator. The SQL migrations are embedded. |
| `src/DW4Online.Api` | The JSON API `/api/v1`: endpoints, bearer-token authentication, rate limits, problem details. |
| `src/DW4Online.Relay` | The TCP relay (port 52041): HMAC tickets and byte-for-byte splicing. |
| `src/DW4Online.Web` | The Razor Pages website (skeleton: one status page). |
| `src/DW4Online.Server` | The executable: composition root, configuration, Windows-service mode, CLI tools, TLS certificate selector, file log. |
| `sql/` | Numbered migrations `NNNN_description.sql` (0001 schema, 0002 achievement definitions). |
| `tests/DW4Online.Tests` | Unit tests (no database). |
| `tests/DW4Online.Integration.Tests` | HTTP + MariaDB tests against a private local instance. |
| `scripts/local-db.ps1` | Starts / stops the private MariaDB for development and tests. |
| `deploy/` | Deployment kit for the server ([DEPLOY.md](deploy/DEPLOY.md)): publish, service, firewall, backups, TLS. |

## Build and test

Target framework: **.NET 10 LTS** (`net10.0`, supported to November 2028). Any .NET SDK 10.0.100 or newer builds it;
this round was built with the installed SDK 11.0.100-rc.3 (no .NET 10 SDK is installed on the development PC; the .NET
10.0.14 runtimes and targeting packs are). Release builds for the server should use a GA SDK (install the .NET 10 SDK
or wait for the .NET 11 SDK GA). Packages: MySqlConnector 2.6.1, Dapper 2.1.79 (runtime), xUnit 2.9.3 and
Microsoft.AspNetCore.TestHost 10.0.0 (tests); everything else comes from the shared framework.

```powershell
cd server\DW4Online
dotnet build DW4Online.slnx
dotnet test tests\DW4Online.Tests                       # unit tests, no database needed

powershell -ExecutionPolicy Bypass -File scripts\local-db.ps1 start   # private MariaDB on 127.0.0.1:3427
powershell -ExecutionPolicy Bypass -File scripts\local-db.ps1 env     # prints $env:DW4ONLINE_TEST_MYSQL = ...
dotnet test tests\DW4Online.Integration.Tests            # skipped without DW4ONLINE_TEST_MYSQL
powershell -ExecutionPolicy Bypass -File scripts\local-db.ps1 stop
```

`local-db.ps1` uses XAMPP's MariaDB binaries (`-MySqlBin` for others) with its **own** data directory in
`server/DW4Online/.local` (gitignored), its own port and `--bind-address=127.0.0.1`; it never touches XAMPP's data,
other databases or services and stops only the `mysqld` it started (by PID). XAMPP ships MariaDB 10.4, whose native
AIO hung `DROP DATABASE` here (an InnoDB latch wait), so the script starts it with `--innodb-use-native-aio=0`.
The integration tests create a database `dw4online_it_<random>` per test class and drop it afterwards.

Run the service locally:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\local-db.ps1 app     # database 'dw4online' + user, prints the connection string
copy src\DW4Online.Server\appsettings.Local.example.json src\DW4Online.Server\appsettings.Local.json   # paste it there
dotnet run --project src\DW4Online.Server --environment Development   # http://127.0.0.1:5280, relay 127.0.0.1:52041
```

## Configuration

`appsettings.json` holds every setting with its default and no secrets. Secrets (the connection string, the relay
ticket key) go into `appsettings.Local.json` next to the executable (gitignored, never published) or, on the server,
a settings file outside the app folder passed with `--settings` and readable only by the service account. Environment variables (`Database__ConnectionString`)
and user secrets (Development) work as usual.

| Section | Keys (defaults) |
| --- | --- |
| `Database` | `ConnectionString`, `AutoMigrate` (true): run pending migrations at startup. |
| `Service` | `Name`, `Motd`, `Maintenance` (announced in the status), `RelayHost` / `RelayPort` (announced to clients). |
| `Accounts` | `Registration` (open / closed), `PasswordIterations` (220000), `MaxFailedLogins` (10), `LockoutMinutes` (15), `GameSessionDays` (30), `WebSessionDays` (14), `SessionTouchMinutes` (5), `MaxSessionsPerAccount` (20). |
| `OnlineCharacter` | `HistoryRevisions` (20), `Rules:MaxLevelsPerPlayHour` (120), `Rules:PlayTimeSlackSeconds` (600). |
| `CloudSave` | `MaxBytes` (1 MiB), `HistoryRevisions` (5). |
| `Play` | `RequireRoom` (true), `MinimumProtocol` / `CurrentProtocol` (netplay `kProtocol`), `MinimumClientVersion` / `CurrentClientVersion`. |
| `RateLimits` | `GlobalPerMinute` 300 (per IP), `LoginPerMinute` 5 (per IP, login + recovery), `RegisterPerHour` 3 (per IP), `CharacterWritesPerMinute` 12, `CloudWritesPerHour` 6, `AchievementsPerMinute` 60, `AccountChangesPerMinute` 10, `StaffPerMinute` 60 (per account). |
| `Relay` | `Enabled` (false; true in Development), `ListenAddress`, `Port` (52041), `TicketKey` (secret, base64 32 bytes: `DW4Online.Server gen-relay-key`), timeouts and byte budgets. |
| `Tls` | `StoreHostName`: take HTTPS certificates from the Windows store (renewals without a restart); `AcmeChallengeDirectory`: serve ACME http-01 files from a folder. |
| `Logging:File` | `Directory` (empty = off), `MinimumLevel`, `RetainDays`: daily UTC log files for the service. |
| `Kestrel` | Standard Kestrel endpoints (`http://127.0.0.1:5280` by default). |

CLI of the executable:

```
DW4Online.Server                          run in the console
DW4Online.Server --service                run under the Service Control Manager
DW4Online.Server migrate                  apply pending migrations and exit
DW4Online.Server set-role <user> <role>   player | moderator | admin (bootstraps the first admin)
DW4Online.Server reset-password <user>    print a temporary password the user must change (audit actor 0 = console)
DW4Online.Server gen-relay-key            print a new random Relay:TicketKey
... --settings <file>                      every form: load an extra settings file (the server's secrets live outside the app folder)
```

## Database

MariaDB 11.4 LTS is the target (developed on a private 10.4 instance; the schema avoids `INET6` / `UUID` so both
run it). InnoDB, `utf8mb4_unicode_ci` set explicitly (11.x changed the default), all times UTC `DATETIME(3)` written by
the service. The migrator applies `sql/NNNN_*.sql` in order, records each in `schema_version` with the SHA-256 of the
file and refuses to run when the database has tables but no `schema_version`, when an applied file changed, or when the
database is newer than the build. MariaDB commits DDL implicitly: **back up before an update** (deploy kit).

Tables: `accounts`, `sessions` (only SHA-256 of tokens), `email_tokens` (future SMTP), `account_bans`,
`online_characters` + `online_character_history`, `cloud_saves` + `cloud_save_history`, `achievements`,
`achievement_unlocks` (per ruleset), `player_stats`, `speedrun_times`, `rooms` + `room_members` (durable room record,
written by the room service), `audit_log` (pruned after 180 days).

## API (`/api/v1`, JSON, errors as problem details)

Errors are RFC 9457 problem details: `type` = `urn:dw4online:problem:<code>`, `code` repeats the stable machine code,
`title` is an English message; extra fields such as `currentRevision`, `issues` or `retryAfterSeconds` sit beside them.
Authenticated calls send `Authorization: Bearer dw4_<43 characters>`.

| Method and path | Auth | What |
| --- | --- | --- |
| `GET /status` | - | Service name and version, maintenance, motd, registration, `client.minimumVersion/currentVersion/minimumProtocol/currentProtocol`, relay address. |
| `POST /auth/register` | - | `{username, password, email?, deviceName?}` -> 201 `{account, recoveryCode, token, expiresAt}`. Show the recovery code once. |
| `POST /auth/login` | - | `{username, password, deviceName?, clientVersion?, protocol?}` -> `{token, expiresAt, account}`; 426 when the client is too old. |
| `POST /auth/logout` / `logout-all` | token | End this session / every session of the account. |
| `POST /auth/password` | token | `{currentPassword, newPassword}`; ends every other session. |
| `POST /auth/recover` | - | `{username, recoveryCode, newPassword}` -> `{recoveryCode}` (the next one); ends every session. |
| `POST /auth/recovery-code` | token | `{currentPassword}` -> a new recovery code. |
| `GET /me`, `PUT /me/email`, `GET /me/sessions`, `DELETE /me/sessions/{id}` | token | Account, optional e-mail, active sessions. |
| `GET /online-character` | token | `{revision, record (base64), summary, playSeconds, flagged, ...}` with `ETag`; 404 = start from zero. |
| `PUT /online-character` | token | `{record (base64 3316 bytes), playSeconds, context}`; `If-None-Match: *` creates, `If-Match: <revision>` updates (409 with `currentRevision`, 428 without either). Answers `{revision, summary, warnings, unlocked}`. |
| `GET /online-character/history`, `POST /online-character/reset` | token | Kept revisions; end the character (`{currentPassword}`). |
| `GET /cloud-save` (`?revision=N`) | token | The save bytes (`application/octet-stream`), `ETag`, `X-Sha256`, `X-Save-Format`. |
| `GET /cloud-save/meta` | token | `{latest, boxes, history}`. |
| `PUT /cloud-save` | token | Body = save bytes, `X-Sha256` (hex), `If-None-Match: *` or `If-Match: <revision>`, optional `X-Device-Name`, `X-Client-Version`. 413 above `MaxBytes`. |
| `POST /cloud-save/restore/{revision}`, `DELETE /cloud-save` | token | Make a kept revision the latest again; delete the backup and its history. |
| `GET /achievements` | optional | All definitions (hidden ones masked until unlocked); with a token the caller's unlocks and progress. |
| `POST /achievements/unlock` | token | `{code, context, evidence: {igtMs?, level?, stage?}}` -> `{code, ruleset, newlyUnlocked, newBestTime}`. |
| `POST /stats` | token | `{deltas: {enemies_defeated: 12, play_seconds: 60, ...}, context}` -> `{totals, unlocked}`. |
| `GET /admin/accounts?query=`, `POST /admin/accounts/{user}/reset-password`, `.../status`, `.../role` (admin), `.../online-character/history`, `.../online-character/restore/{rev}`, `.../online-character/reset` | staff | Moderation; every action is audited. |
| `GET /health` (no prefix) | - | `{status, schema}` or 503 when the database is unreachable. |

`context` (sent with uploads, unlocks and stats): `{roomCode, players, randomizer, cheats, rebalanced, saveStateLoaded,
difficulty, protocol, clientVersion}`.

## Rules in force

- **Passwords**: PBKDF2-HMAC-SHA512, 220,000 iterations (OWASP), about 150 ms per hash on the development PC, stored
  as `$pbkdf2-sha512$i=...$salt$hash`; stronger settings rehash at the next login. 10-128 characters, not the user name,
  not a common password. Unknown user names cost the same time as wrong passwords.
- **Lockout**: 10 failed logins or recovery attempts in a row lock the account for 15 minutes (423 with
  `retryAfterSeconds`); staff locks and bans are separate statuses.
- **Sessions**: 256-bit opaque tokens, only SHA-256 stored, kinds `game` (30 days) and `web` (14 days), sliding
  expiry (moved forward at most every 5 minutes), at most 20 live sessions per account.
- **Recovery**: a one-time recovery code (100 bits, shown at registration and after each use) or an admin reset
  (temporary password that must be changed before anything else works). E-mail is optional and unused until SMTP exists.
- **Online character**: one per account, starts from zero. The server decodes the record with its own port of the
  record layout and rejects structural errors (size, ISUSE, unknown partner model, name marker / characters, level
  1-999, HP / MP ranges, bits and bank bits 0-9,999,999, malformed device words, equipment indices) and a different
  character (UNIQUE) on update; plausibility findings (form levels dropping, forms lost, play time running backwards or
  faster than real time, unusually fast levelling) are **warnings**: the upload is stored, the character is flagged for
  moderators and an audit row is written. Never auto-bans. Summary columns come from the server's decode.
- **Cloud save**: one per account, latest + 5 older revisions, at most 1 MiB, SHA-256 checked, GCI (90176 bytes) or raw
  (90112 bytes) with a valid copy; private (never on the website, never used for achievements).
- **Achievements** (72 definitions, `sql/0002_achievements.sql`, stable ids 1-72): only online play counts, so unlocks
  and stats need a room the account is a member of (`Play:RequireRoom`); a report with cheats, rebalanced rates or a
  save-state load is refused (`unranked_*`), and so is an unranked room. Randomizer runs are their own ruleset (a second
  unlock row). `event` achievements come from the game (some checked against the goal or the room: speed limits on
  in-game time without loading, low-level limits, player counts capped by the room's members), `stat` achievements unlock
  when `player_stats` reaches the goal (deltas bounded per minute since the last batch), `state` achievements unlock from
  the server's decode of an online character upload (unlocks from a flagged upload are flagged too). Story clears with
  `evidence.igtMs` also keep the best time per board and ruleset (`speedrun_times`). Offline unlocks stay local in the game.

## Relay protocol (TCP, default port 52041)

Tickets are issued by the room API (not part of this round): `base64url("room|account|role|expiry|nonce")` + "." +
`base64url(HMAC-SHA256)`, single use, 60 s. Every connection starts with one ASCII line ending in `\n`:

1. Host control: `DW4R1 HOST <ticket>` -> `OK <hostKey>`. The relay then sends `GUEST <n> <account>` per arriving
   guest. Closing this socket closes the room (no host migration) and raises `RelayServer.HostDisconnected`.
2. Guest: `DW4R1 GUEST <ticket>` -> `OK` once the host's data socket arrived (within 10 s), then raw bytes.
3. Host data: `DW4R1 DATA <room> <n> <hostKey>` -> spliced with guest `n` byte for byte.

Failures answer `ERR <reason>` (`ticket`, `no_host`, `host_timeout`, `room_taken`, `full`, `data`, `protocol`) and close.
Limits: handshake 10 s, idle 60 s, 16 connections per IP, 200 rooms, 64 KB/s sustained per direction with a 1 MiB burst
(the START message carries about 90 KB). The relay does not parse the game protocol.

## Open ends (other lanes)

- Room API: create / list / join / heartbeat / leave / kick, writing `rooms` / `room_members`, issuing tickets with
  `RelayTickets.Issue`, closing rooms on `HostDisconnected` and rows left open at startup (`server_restart`).
- Website pages (account, profile, achievements, rooms, admin) on top of the Domain services; cookie authentication with
  `web` sessions (`AccountService.LoginAsync(..., SessionKind.Web, ...)`), anti-forgery, Data Protection keys.
- Game client: WinHTTP client, token storage, the online character in place of the card, the relay transport.
- Achievement goals that need calibration on real runs (speed limits, Minimalist level, stat rates, level-rate warning).
