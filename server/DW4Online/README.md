# DW4Online

The online backend of the Digimon World 4 PC port: accounts, the online character, the private cloud backup of the
local save, achievements, rooms, the DW4R2 relay with its lockstep instances and the website, in **one** ASP.NET Core process
(API, website and relay share memory) backed by MariaDB. It runs as a Windows service on the user's Windows Server;
players never forward ports.

Status (feature package "online"): the service side is complete and tested on loopback: core (accounts, online
character, cloud save, achievements), the room service and relay, and the website. The game client integration is
separate work (see "Open ends"); `tests/winhttp_client` shows the client side of every call over WinHTTP.

## Layout

| Path | What |
| --- | --- |
| `src/DW4Online.Domain` | Pure rules, no ASP.NET Core and no database: the 3316-byte character record codec (C# port of `src/save_data.cpp`), validation, summaries, password hashing, tokens, recovery codes, achievement rules, the services (accounts, online character, cloud save, achievements) and the repository interfaces. |
| `src/DW4Online.Data` | MariaDB through MySqlConnector + Dapper: repositories and the startup migrator. The SQL migrations are embedded. |
| `src/DW4Online.Api` | The JSON API `/api/v1`: endpoints, bearer-token authentication, rate limits, problem details. |
| `src/DW4Online.Relay` | The DW4R2 relay (port 52041, SEAMLESS.md 6): `Protocol` (frames and messages), `Hub` (one connection per member and room, instances as solo-led and sealed lockstep groups, server-paced sealing on a dedicated high-resolution timer thread, barrier decisions, forks, the tick log, state delivery), HMAC room tickets. |
| `src/DW4Online.Web` | The Razor Pages website (English, server-rendered, no scripts): home, register, login, recovery, account settings, profiles, achievements, rooms, admin, privacy. |
| `src/DW4Online.Server` | The executable: composition root, configuration, Windows-service mode, CLI tools, TLS certificate selector, file log. |
| `sql/` | Numbered migrations `NNNN_description.sql` (0001 schema, 0002 achievement definitions, 0003-0009 see "Database"). |
| `tests/DW4Online.Tests` | Unit tests (no database). |
| `tests/DW4Online.Integration.Tests` | HTTP + MariaDB tests against a private local instance (API, rooms with the real relay and fake clients, website forms, end to end on real Kestrel). |
| `tests/DW4Online.FakeClient` | Fake DW4R2 clients (a deterministic stand-in engine that speaks the protocol as the game will), the latency / jitter / loss proxy and the loss matrix; a console tool (`client`, `proxy`, `matrix`) and a library for the tests. |
| `tests/winhttp_client` | `dw4online_client.cpp`, a C++20 WinHTTP test tool (the HTTP stack the game will use), and `build.cmd` (MSVC). |
| `tests/e2e/relay_spike_test.py` | The groundwork spike's Python test on the real API and relay: an independent DW4R2 client (standard library only) that drives a co-arrival and checks the first sealed tick on both machines. |
| `scripts/local-db.ps1` | Starts / stops the private MariaDB for development and tests. |
| `scripts/e2e.ps1` | Builds the WinHTTP client, starts the private MariaDB and runs the end-to-end tests. |
| `deploy/` | Deployment kit for the server ([DEPLOY.md](deploy/DEPLOY.md)): publish, service, firewall, backups, TLS. |

## Build and test

Target framework: **.NET 10 LTS** (`net10.0`, supported to November 2028). Any .NET SDK 10.0.100 or newer builds it;
this round was built with the installed SDK 11.0.100-rc.3 (no .NET 10 SDK is installed on the development PC; the .NET
10.0.14 runtimes and targeting packs are). Release builds for the server should use a GA SDK (install the .NET 10 SDK
or wait for the .NET 11 SDK GA). Packages: MySqlConnector 2.6.1, Dapper 2.1.79 (runtime), xUnit 2.9.3 and
Microsoft.AspNetCore.TestHost 10.0.0 (tests); everything else comes from the shared framework.

With the 11.0 RC SDK the shared compiler server sometimes generates broken Razor code when it rebuilds
`DW4Online.Web` a second time (unchanged pages fail with errors such as "IndexModel has no definition for asp" or
"newline in constant"). Build with `--disable-build-servers` (or run `dotnet build-server shutdown` once) and it
compiles again; the sources are fine.

```powershell
cd server\DW4Online
dotnet build DW4Online.slnx
dotnet test tests\DW4Online.Tests                       # unit tests, no database needed

powershell -ExecutionPolicy Bypass -File scripts\local-db.ps1 start   # private MariaDB on 127.0.0.1:3427
powershell -ExecutionPolicy Bypass -File scripts\local-db.ps1 env     # prints $env:DW4ONLINE_TEST_MYSQL = ...
dotnet test tests\DW4Online.Integration.Tests            # skipped without DW4ONLINE_TEST_MYSQL
powershell -ExecutionPolicy Bypass -File scripts\local-db.ps1 stop
```

End to end (real Kestrel on 127.0.0.1 with a free port, the relay on a free loopback port, a throw-away database):

```powershell
powershell -ExecutionPolicy Bypass -File scripts\e2e.ps1 -Python C:\path\to\python.exe
```

It registers an account through the website form, runs `dw4online_client.exe` (login, cloud save upload and download
with SHA-256 check, online character write and read, room with its relay ticket and settings hash, the DW4R2
handshake up to WELCOME, achievement unlock, leave, logout) and checks that the profile page shows the character and
the achievement; then it runs `relay_spike_test.py` (register, login, rooms, password join, the DW4R2 handshake, a
co-arrival with the snapshot, the co-arrival's state, hashes, pads and the first sealed tick, the owner leaving with
the story frozen, the login rate limit). Both tests are skipped when the client is not built
(`tests\winhttp_client\build.cmd`) or no Python is given (`DW4ONLINE_PYTHON`). With `DW4ONLINE_TEST_PORTS=53100-53199`
every listener of the tests takes a free port of that range instead of one the system picks (parallel work on one
machine keeps to its own range).

The whole online stack with fake clients (SEAMLESS.md 16) runs from `tools/online_test/run.py` (a private MariaDB,
the server over HTTPS with a throw-away pinned certificate, the relay, accounts, rooms, hidden clients with input
scripts, fingerprint and membership-log comparison per instance; `--list` shows the scenarios, `--impair OW,JITTER,LOSS`
puts every client behind the proxy, `run.py matrix` measures the loss and jitter matrix). `run.py G1 --game-exe EXE
--data DATA` runs two hidden real game clients (a `DW4_ONLINE_DEV` build) instead: both log in over the pinned HTTPS
API, play a new game with the campaign bot from the title, meet in the first field (the second joins by catch-up) and
must keep equal net fingerprints in the sealed instance (`DW4_ONLINE_TRACE` netfp lines) for `--sealed-ticks` ticks.

The IP phase is tested the same way: `Pinned_self_signed_https_at_an_ip_address_below_a_path_base` runs the service on
`https://127.0.0.1` with a self-signed IP certificate below `/dw4online`; the client must refuse the certificate without
`--pin`, send nothing with a wrong pin (not even a failed login reaches the server) and pass every step with the right
one, and the website must work over it below the path base and at the root. That test feeds the certificate selector
from memory (`TlsCertificateSource`), never from a certificate store. With `DW4ONLINE_TEST_CERT_STORE=1`,
`Deploy_kit_certificate_from_the_store_and_the_pinned_client` also runs `deploy\new-selfsigned-cert.ps1` against the
current user's store (a `CN=127.0.0.2` test certificate whose key cannot be exported, removed with its key
afterwards), serves it from the store as the service does and runs the client with the pin the script printed.

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
| `Service` | `Name`, `Motd` (game menu and home page), `Maintenance` (announced; no new rooms), `RelayHost` (empty = the request's host name) / `RelayPort` (default 0 = the port the relay listens on, `Relay:Port`; set it only when clients reach the relay through another public port), announced to clients; `PathBase` (empty; for example `/dw4online`: website and API below that path, see "Website"), `BehindProxy` (false; true behind a reverse proxy on the same machine: client address and scheme from `X-Forwarded-For` / `X-Forwarded-Proto` of a loopback proxy only). |
| `Accounts` | `Registration` (open / closed), `PasswordIterations` (220000), `MaxFailedLogins` (10, per user name and address), `LockoutMinutes` (15), `MaxFailedLoginsPerAccount` (50), `KnownAddressDays` (30), `GameSessionDays` (30), `WebSessionDays` (14), `SessionTouchMinutes` (5), `MaxSessionsPerAccount` (20). |
| `OnlineCharacter` | `HistoryRevisions` (20), `ResetHistoryDays` (30: the revisions of a reset character stay that long), `Rules:MaxLevelsPerPlayHour` (120), `Rules:PlayTimeSlackSeconds` (600). |
| `CloudSave` | `MaxBytes` (1 MiB), `HistoryRevisions` (5). |
| `Play` | `RequireRoom` (true), `MinimumProtocol` / `CurrentProtocol` (netplay `kProtocol`), `MinimumClientVersion` / `CurrentClientVersion`, `SpeedrunFloorSeconds` (per board `story_normal` / `story_hard` / `story_veryhard`, 2700 each: see "Speedrun floor"). |
| `RateLimits` | `GlobalPerMinute` 300 (per IP), `LoginPerMinute` 5 (per IP, login + recovery), `RegisterPerHour` 3 (per IP), `CharacterWritesPerMinute` 12, `CloudWritesPerHour` 6, `AchievementsPerMinute` 60, `AccountChangesPerMinute` 10, `StaffPerMinute` 60, `RoomCreatesPerMinute` 5, `RoomJoinsPerMinute` 20, `StageChangesPerMinute` 30 (stage enter + leave) (per account); `WebLoginPerMinute` 5 (website login + recovery), `WebRegisterPerHour` 3 (per IP, POSTs only), `WebAccountChangesPerMinute` 10 (per account, the account page's POSTs). |
| `Rooms` | `MaxRooms` (200), `PlayersPerInstance` (4), `HeartbeatSeconds` (10), `MemberTimeoutSeconds` (35), `RoomsPerOwner` (1), `PasswordIterations` (20000, room passwords live in memory only), `SettingsMaxBytes` (4096), `RankedDataHashes` (empty = every data hash is ranked; decimal or 0x-hex). |
| `Relay` | `Enabled` (true), `ListenAddress` (127.0.0.1; 0.0.0.0 on the server), `Port` (52041), `TicketKey` (secret, base64 32 bytes: `DW4Online.Server gen-relay-key`; required outside Development), `TicketLifetimeSeconds` (60), `HandshakeTimeoutSeconds` (10), `IdleTimeoutSeconds` (60), `MaxConnectionsPerIp` (16), `MaxRooms` (200), inbound limits per connection `BytesPerSecond` (65536) / `BurstBytes` (1 MiB) / `FramesPerSecond` (400) / `FrameBurst` (2000), state transfers `StateBytesPerSecond` (4 MB/s) / `StateBurstBytes` (16 MiB), `MaxQueuedBytes` (32 MiB: a connection that does not read is closed), `ReconnectSeconds` (10), `LagLeaveSeconds` (10), `DiagnosticsDirectory` (empty; a folder gets one JSON-lines membership log per room). |
| `Sealing` | `MaxDelayTicks` (15), `LateTarget` (0.001: d rises when more of the last 3,600 pads were late), `EventMarginTicks` (2: added to the largest member horizon for JOIN, LEAVE, INTENT). |
| `Instances` | `CoArrivalHoldMs` (3000), `JoinHoldSeconds` (30), `MaxCatchUpSeconds` (30), `DefaultReplayRate` (3000 steps/s for a member that sent no RATE), `HeadingValidMs` (5000), `BarrierReportSeconds` (5), `StoryBurst` (10) and `StoryMessagesPerMinute` (60: a member's STORY rate). `Rooms:PlayersPerInstance` (4) caps every instance. `Rooms:StoryWhileOwnerAway` (`drop`, `hold` or `apply`; default drop): story progress made while the room owner is away (SEAMLESS.md 8.6). |
| `Web` | `RequireHttps` (false; true on the server: website pages redirect to https, cookies are Secure), `Hsts`, `DataProtectionKeysDirectory` (anti-forgery keys, DPAPI-encrypted on Windows; empty = the service account's profile, `memory` = tests), `OperatorName` / `OperatorAddress` / `OperatorContact` (the controller on the privacy page), `DigimonNames` (display names by model, overriding the built-in list). |
| `Tls` | `StoreHostName`: take HTTPS certificates from the Windows store (renewals without a restart); its certificate serves every connection that names no other host (an IP address sends no name: the IP phase's pinned self-signed certificate). `AdditionalHostNames` (empty): host names with their own certificate, chosen by the name the client asks for (SNI), for example the later domain next to the pinned IP certificate. `StoreName` (My), `StoreLocation` (LocalMachine), `RefreshMinutes` (10); `AcmeChallengeDirectory`: serve ACME http-01 files from a folder. |
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
DW4Online.Server audit-tag <name>         print the security log's tag of a user name (failed logins with unknown names)
DW4Online.Server tls-pin --host <IP or name> | --thumbprint <hex> | --file <certificate> [--password-env NAME] [--store My] [--location LocalMachine]
                                          print a certificate's SHA-256 SPKI pin (what the game client pins in the IP
                                          phase) and SHA-256 fingerprint; --host = the certificate the service would serve
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
written by the room service before its memory changes), `audit_log` (pruned after 180 days). Migration 0003 adds
`rooms.randomizer`, `rooms.story_frozen_at` and the close reason `host_closed`; 0004 `accounts.online_reset_at`;
0005 drops the user names old `login_unknown` audit rows held; 0006 `online_character_history.ended_at` (the reset
that ended a revision's character, filled in for earlier resets); 0007 `sessions.last_used_ip` (the address a session
was last used from, for the account-wide lock) and `online_character_history.flagged` (each revision's flag, for staff
restores; earlier revisions take their live character's flag or the one their reset noted); 0008 `online_story` (an
account's own online story: BASE_FLAG, BASE_FLAGFOLDER, BASE_COUNTER and a revision; a room's story is its owner's row,
only the relay writes it, newer revisions only; SEAMLESS.md 8); 0009 the game client's achievement catalogue (see
"Achievements").

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
| `PUT /me/profile` | token | `{public}`: show the online character and achievements on the website profile. |
| `POST /me/delete` | token | `{currentPassword}`: GDPR erasure of the account (see "Rules in force"). |
| `GET /online-character` | token | `{revision, record (base64), summary, playSeconds, flagged, ...}` with `ETag`; 404 = start from zero. The record's story bytes (BASE_FLAG, BASE_FLAGFOLDER, BASE_COUNTER) are the account's own online story (`online_story`). |
| `PUT /online-character` | token | `{record (base64 3316 bytes), playSeconds, context}`; `If-None-Match: *` creates, `If-Match: <revision>` updates (409 with `currentRevision`, 428 without either). Answers `{revision, summary, warnings, unlocked}`. The uploaded story bytes are ignored: the stored record carries the account's own online story (a guest plays with the room owner's story; its clears never count). |
| `GET /online-character/history`, `POST /online-character/reset` | token | Kept revisions; end the character (`{currentPassword}`; its revisions stay 30 days for staff, then go). |
| `GET /cloud-save` (`?revision=N`) | token | The save bytes (`application/octet-stream`), `ETag`, `X-Sha256`, `X-Save-Format`. |
| `GET /cloud-save/meta` | token | `{latest, boxes, history}`. |
| `PUT /cloud-save` | token | Body = save bytes, `X-Sha256` (hex), `If-None-Match: *` or `If-Match: <revision>`, optional `X-Device-Name`, `X-Client-Version`. 413 above `MaxBytes`. |
| `POST /cloud-save/restore/{revision}` | token | Make a kept revision the latest again. |
| `POST /cloud-save/delete` | token | `{currentPassword}`: delete the backup and its history (a wrong password counts as a failed login). |
| `GET /achievements` | optional | All definitions (hidden ones masked until unlocked); with a token the caller's unlocks and progress. |
| `POST /achievements/unlock` | token | `{code, context: {roomCode, players, roomPlayers, randomizer, cheats, rebalanced, saveStateLoaded, difficulty, protocol, clientVersion}, evidence: {igtMs?, level?, stage?}}` -> `{code, ruleset, newlyUnlocked, newBestTime, timeRejected}`. |
| `POST /stats` | token | `{deltas: {enemies_defeated: 12, play_seconds: 60, ...}, context}` -> `{totals, unlocked}`. |
| `GET /rooms?protocol=&dataHash=` | - | Open rooms (compatible ones only when filtered, like the HELLO check): code, name, owner, `ownerPresent`, `storyFrozen`, `hasPassword`, players / maxPlayers, ranked, randomizer, difficulty, instances. |
| `POST /rooms` | token | `{name, password?, maxPlayers (2..10, default 4), protocol, dataHash, clientVersion, settings?}` -> 201 room state with `relayTicket`. `settings` is a JSON object: `difficulty` 0..2, `cameraMode` 0..1, `lightMode` 0..1 (default 1), `fastLoading`, `randomizer: {enabled, seed (u32), drops, enemies 0..2, zones 0..2}`; `cheats` / `rebalanced` are refused. One owned room per account. |
| `GET /rooms/{code}` | token | Members get the room state, others the summary. |
| `POST /rooms/{code}/join` | token | `{password?, protocol, dataHash, clientVersion}` -> room state with `relayTicket`; repeating it is a heartbeat; the owner always gets back in (no password, place kept) and unfreezes the story; joining leaves any other room. 403 `wrong_room_password` / `kicked`, 409 `room_full` / `incompatible_room`. |
| `POST /rooms/{code}/ticket` | token | A fresh one-time relay ticket (`relayTicket`, a reconnect); it replaces the member's older ticket. 503 `relay_unavailable` with the relay off. |
| `GET /rooms/{code}/story` | token | Members: the room story the relay keeps (SEAMLESS.md 8): `{revision, phase (0-7), flags, folder, counters (16), policy, ownerPresent, droppedFlags, droppedFolder, droppedCounters, held}`. 404 `not_in_room`, 404 `no_story` before the room's first relay connection. |
| `POST /rooms/{code}/heartbeat` | token | Every `heartbeatSeconds` (10) while the relay connection is down (a live relay connection counts as the heartbeat); silent members leave after 35 s. Answers the room state; 404 `not_in_room` means join again. |
| `POST /rooms/{code}/leave`, `POST /rooms/{code}/kick` `{username}`, `DELETE /rooms/{code}` | token | Leave; the owner (present) kicks a guest (no re-join); the owner closes the room for everyone. |
| `GET /admin/accounts?query=`, `POST /admin/accounts/{user}/reset-password` (admin), `.../status`, `.../role` (admin), `.../online-character/history`, `.../online-character/restore/{rev}`, `.../online-character/reset` | staff | Moderation; every action is audited. A locked or banned account leaves its room. `status` is active, locked or banned (deleting is `DELETE /admin/accounts/{user}`, admins only); a temporary password only from an admin (whoever knows it owns the account). |
| `POST /admin/accounts/{user}/achievements/{code}/flag` `{ruleset?, flagged}`, `DELETE /admin/accounts/{user}/achievements/{code}?ruleset=`, `POST /admin/rooms/{code}/close`, `DELETE /admin/accounts/{user}` (admin) | staff | Flag / unflag or revoke an unlock, close a room, erase an account on request. |
| `GET /health` (no prefix) | - | `{status, schema}` or 503 when the database is unreachable. |

The room state (create, join, heartbeat, `GET` for members) is `{room, settings, settingsHash, members: [{accountId,
username, slot, owner, instance, joinedAt}], instances: [{id, stage, visit, mode (hub / solo / sealed), members:
[{accountId, username, slot, connected, lagging}], departing, bossLock, joinable, ticks, parent}], you,
heartbeatSeconds, relay: {host, port, protocol}, relayTicket}` (`relayTicket` only after create, join and /ticket;
`settingsHash` is the 16-hex-digit hash the DW4R2 handshake must carry).

`context` (sent with uploads, unlocks and stats): `{roomCode, players, randomizer, cheats, rebalanced, saveStateLoaded,
difficulty, protocol, clientVersion}`. With `Play:MinimumProtocol` / `Play:MinimumClientVersion` set, uploads, unlocks
and stats must carry `protocol` / `clientVersion` (426 `client_outdated` otherwise); a login checks them when sent.

## Rules in force

- **Passwords**: PBKDF2-HMAC-SHA512, 220,000 iterations (OWASP), about 150 ms per hash on the development PC, stored
  as `$pbkdf2-sha512$i=...$salt$hash`; stronger settings rehash at the next login. 10-128 characters, not the user name,
  not a common password. Unknown user names cost the same time as wrong passwords.
- **Lockout** (both answers are 423 `account_locked_temporarily` with `retryAfterSeconds`; staff locks and bans are
  separate statuses, reported only after a correct password). User names are public (room list, profiles), so a lock
  must not let strangers lock the owner out:
  - per user name and client address (IPv4 exactly, IPv6 by its /64 network, in memory): 10 failed logins, recovery
    codes or password confirmations within 15 minutes refuse that address for that name for 15 minutes, before any
    password check. A stranger locks out only their own address; a name without an account locks the same way, so the
    answer does not tell whether an account exists. A correct password from the address clears its count. The name
    counts and is looked up only in its canonical form (trimmed, lower case, the user name shape): the database
    collation folds accents and full-width letters ("álice" and "ａlice" would find "alice"), so such spellings are
    unknown names without a database query, and all typed text that cannot be a user name shares one count per address
    (nothing longer than 20 characters of a typed name is kept in memory).
  - per account, against guessing from many addresses: 50 failures from all addresses together within 15 minutes (a
    success clears them) lock the account for 15 minutes. One network adds at most 10 failures per window (IPv4: the
    address, IPv6: its /48, the most a home connection gets, whose 256 or more /64 networks would otherwise each add
    10), so neither slow guessing nor one connection adds up to it. During the lock an address the account created a
    session from or last used one from in the last 30 days (IPv6: the same /64) may still log in (its success ends the
    lock, its wrong password does not); other addresses wait. Only during such a lock does the answer show that the
    name exists.
  Why not "check the password first, then lock": a lock that only applies after a correct password protects nothing
  against guessing, and one that applies before it lets anyone lock a public name. Counting per name and address
  keeps strangers to their own address; the account-wide lock spares the addresses the owner really uses.
  Logins are also limited to 5 per minute per address (rate limit; all per-address rate limits, the website's
  included, count an IPv6 /64 network as one address).
- **Sessions**: 256-bit opaque tokens, only SHA-256 stored, kinds `game` (30 days) and `web` (14 days), sliding
  expiry (moved forward at most every 5 minutes), at most 20 live sessions per account.
- **Recovery**: a one-time recovery code (100 bits, shown at registration and after each use) or an admin reset
  (temporary password that must be changed before anything else works). E-mail is optional and unused until SMTP exists.
- **Online character**: one per account, starts from zero. The server decodes the record with its own port of the
  record layout and rejects structural errors (size, ISUSE, unknown partner model, name marker / characters, level
  1-999, HP / MP ranges, bits and bank bits 0-9,999,999, malformed device words, equipment indices) and a different
  character (UNIQUE) on update; plausibility findings (form levels dropping, forms lost, play time running backwards or
  faster than real time, unusually fast levelling) are **warnings**: the upload is stored, the character is flagged for
  moderators and an audit row is written. A new online character starts from zero, so its first upload's play time must
  fit into the time since the account was made or since the last reset (`accounts.online_reset_at`), and the levelling
  rate counts play time only as far as real time allows: an uploaded local or edited character gets flagged. Never
  auto-bans. Summary columns come from the server's decode. A reset (by the player with the password, or by staff)
  ends the character: its revisions stay in the history for `OnlineCharacter:ResetHistoryDays` (30, counted from that
  reset, also when the next character is reset within those days; staff can restore one after a mistaken reset or
  look at a flagged character), then the hourly housekeeping deletes them; the audit row
  `character_reset` keeps the revision and whether the character was flagged. The next character starts unflagged.
  A staff restore writes the kept revision with its own play time and flag (an honest character restored after a
  mistaken reset is not "too fast" at its next upload, a flagged one stays flagged); restoring a reset character's
  revision undoes that reset (its revisions are kept again, a newer live character it replaces ends like a reset, and
  `accounts.online_reset_at` goes back to the reset before).
- **Cloud save**: one per account, latest + 5 older revisions, at most 1 MiB, SHA-256 checked, GCI (90176 bytes) or raw
  (90112 bytes) with a valid copy; private (never on the website, never used for achievements).
- **Rooms**: a name (1-32 characters), an optional password (4-64, PBKDF2 in memory only, never stored), 2..10
  players, the netplay protocol and data hash of the host (only compatible clients join). The owner is the host: the
  room's story is the owner's online save. When the owner leaves (or times out), the room **keeps running with the
  story frozen** (`storyFrozen`, `rooms.story_frozen_at`) until the owner returns; the last member leaving closes it
  (`empty`), the owner can close it (`host_closed`), staff too (`admin`); rooms left open by a previous process close at
  start (`server_restart`). Members in the same stage with the same session context share an **instance** of at most
  four (the engine's player slots) on the relay; a fifth, another quest or a catch-up longer than 30 s opens another
  (SEAMLESS.md 3). There is no instance host: the relay seals the ticks, a leaving member is an ordinary LEAVE. The
  room settings' hash makes sure every member's engine runs the room's difficulty, camera and light mode, randomizer
  and fast loading. A room's randomizer setting puts every unlock in it into the randomizer ruleset; unknown data hashes
  (with `Rooms:RankedDataHashes` set) make a room unranked.
- **Account deletion** (GDPR erasure, by the user with the password or by an admin on request): sessions, online
  character and its history, cloud save and history, unlocks, stats, best times, room memberships, bans and e-mail
  tokens are deleted, the account row stays as `deleted_<id>` (no e-mail, no usable password, status `deleted`) so
  rooms and audit rows keep a valid reference, and the account's audit rows lose their IP addresses (so do the failed
  logins whose name tag is the account's name, see the next point).
- **Security log of unknown names**: a failed login with a user name that matches no account (often a password typed
  into the name field) stores only a tag of the typed name: HMAC-SHA256 with a key derived (HKDF) from
  `Relay:TicketKey`, of the trimmed lower-case name, 32 hex characters (`audit_log.detail.name`). Repeated attempts on
  one name stay visible without the name; `DW4Online.Server audit-tag <name> --settings ...` prints a name's tag to
  search for. A new `Relay:TicketKey` starts new tags. Migration 0005 removed the names older rows held. Every password
  confirmation (password, e-mail, recovery code, deletion, online character reset, cloud save deletion) counts a wrong password like a
  failed login, so the lockout applies. The name prefix
  `deleted_` cannot be registered.
- **Achievements** (72 active definitions: the game client's catalogue, SEAMLESS.md 11): `sql/0002_achievements.sql` made
  ids 1-72, `sql/0009_achievement_catalogue.sql` adopts the client's ids, titles, descriptions and thresholds (52 codes
  keep their ids, the client's 20 others are ids 73-92, the 20 codes the client does not have are retired with
  `active = 0`; ids and codes are never reused). The client exports its table as `data/online/achievements.json`;
  `AchievementRulesTests` compare 0009 with that file and the rules with 0009, the client's ctest compares the file
  with its table, so a change on one side fails both until the other follows. Only online play counts, so unlocks
  and stats need a room the account is a member of (`Play:RequireRoom`); a report with cheats, rebalanced rates or a
  save-state load is refused (`unranked_*`), and so is an unranked room. Randomizer runs are their own ruleset (a second
  unlock row). `event` achievements come from the game (some checked against the goal or the room: speed limits on
  in-game time without loading, low-level limits, player counts capped by the room's members: `context.players` is the
  reporter's stage instance, `context.roomPlayers` the room's members for Room Host and Raid Night; the story challenges
  `deathless_clear`, `solo_veryhard`, `speed_normal`, `speed_fast` and `rookie_clear` only from the room owner, whose
  story it is), `stat` achievements unlock
  when `player_stats` reaches the goal (deltas bounded per counter by a token bucket: the counter's rate per minute of
  real time since the last batch - for the first batch since the account was made -, at most 15 minutes saved up; one
  account's batches are checked one at a time), `state` achievements unlock
  from the server's decode of an online character upload made in a ranked room the account is a member of (unlocks from
  a flagged upload are flagged too). Event, stat and state unlocks all take the room's ruleset. Story clears with
  `evidence.igtMs` also keep the best time per board and ruleset (`speedrun_times`), unless the time is below the
  board's floor (next point). Offline unlocks stay local in the game.
- **Speedrun floor**: a story clear takes at least 45 minutes of in-game time (loading excluded) on every board. The
  campaign bot plays the whole Normal story with god mode, one-hit defeats and infinite MP and skips every text box at
  the fastest rate (`work/campaign`, checkpoint sets r25c and r25t, four runs, 59.94 fields per second, from "stage
  started" to the next stage change, loads and `stagis*` menu stages left out): 6.93 to 7.52 hours over the 64 story
  stages, 6.61 hours for the best time of every stage over those runs added up, the six story boss stages alone 9.6 to
  13.3 minutes. 45 minutes is about a ninth of that sum and a quarter of Speed Demon's 3-hour goal, so no real run gets
  near it even with a far better route than the bot's; Hard and Very Hard play the same stages. A clear reported faster keeps no best time, its new unlock is flagged for review
  (answer `timeRejected: true`, audit `speedrun_implausible`), and `speed_normal` / `speed_fast` (8 h / 4 h) refuse it
  (`requirement_not_met`). `Play:SpeedrunFloorSeconds` sets the floor per board (calibrate with real online runs).

## Relay protocol (DW4R2, TCP, default port 52041)

The design is SEAMLESS.md section 6; the exact wire layout is its appendix "DW4R2 wire layout", the one layout of the
relay, the fake clients, the Python client and the game client (`src/game/online/wire.hpp`); golden byte vectors in
`tests/DW4Online.Tests/WireGoldenTests.cs` and the game's `tests/online_wire_tests.cpp` hold both sides to it. In short:

- **One connection per member and room**, across all its instances. Tickets come from `POST /rooms`, `POST
  /rooms/{code}/join` and `POST /rooms/{code}/ticket`: `base64url("room|account|member|expiry|nonce")` + "." +
  `base64url(HMAC-SHA256)`, single use, 60 s. The relay asks the room service before it accepts one: the account must
  be a member of the open room and the ticket its newest (a new ticket replaces the older one), so tickets cannot be
  hoarded for after a kick. A newer connection of the same member replaces the older one (CLOSE replaced).
- **Handshake**: `DW4R2 <ticket> <protocol> <dataHash> <settingsHash hex>` -> `OK <member> <serverMs>` or `ERR
  <code>` (`protocol`, `ticket`, `data`, `settings`, `full`); then frames of type u8 + length u32 LE + payload, at most
  1 MiB, WELCOME first.
- **Instances**: every member is in exactly one: a hub instance (hub and UI stages, solo, not logged), a solo-led one
  (its member steps on its own clock and streams LOG) or a sealed one (two to four members: the relay seals every
  tick at 59.94 Hz on its own timer thread with each member's pad, filling a missing pad at once with that member's
  last pad, six times, then neutral). The delay of each member follows its arrival margins (DELAY events). JOIN,
  LEAVE and INTENT are stamped at the largest member horizon plus two ticks. The relay never simulates the game.
- **Barriers** (a NEXTSTAGE): DISSOLVE into solo hubs for a hub target; a co-arrival when another group announced
  (HEADING) the same target with the same session context (the first holds up to 3 s); JOIN into an instance of the
  target with room, the same context and a catch-up within 30 s by the slowest arriving member's reported replay
  rate (held up to 15 s while it is not joinable; a boss lock asks *wait* or *alone*); NEW otherwise (same instance id,
  next visit, slot 0 refilled, preferred slots kept), with a notice when an instance of the stage was there.
- **Joins**: STATE_DATA (the visit's snapshot as its leader uploaded it, the fork list, every tick since) on its own
  budget, LIVE frames after it, READY with the net fingerprint and replay rate, then the JOIN (sealed: stamped;
  solo-led: JOIN_REQ to the leader, which answers JOIN_AT and is sealed from J).
- **Forks** (SPLIT): the leaver side becomes a child instance (departing for an exit fork, joinable for an in-stage
  fork) that shares the parent's snapshot and every tick sealed so far; its members get REASSIGN; a side left with
  one member leads alone (LEAD_FROM).
- **Log**: per visit in blocks of 600 ticks, each full block compressed with Deflate (about 240 KB for 20 minutes of
  four players); snapshots kept with every member's canonical hash; net fingerprints every 60 ticks with a majority
  vote (two members: the longer member wins) and RESYNC for the minority.
- **Leaving**: BYE, a room leave, kick or timeout stamp LEAVE at once; a closed connection keeps its slot filled for
  10 s (`Relay:ReconnectSeconds`), 10 s of continuous filling (`Relay:LagLeaveSeconds`) end it too.
- **Limits**: handshake 10 s, idle 60 s, 16 connections per IP, 200 rooms, inbound 64 KB/s with a 1 MiB burst and
  400 frames/s with a burst of 2000 per connection (a flood closes with CLOSE abuse), malformed frames close with
  CLOSE protocol, state transfers 4 MB/s with a 16 MiB burst, 32 MiB of unsent frames close a connection that does
  not read.

## Website

Razor Pages in `src/DW4Online.Web`, English, one embedded stylesheet (`/css/site.css`, light and dark), no scripts.

| Page | What |
| --- | --- |
| `/` | Players online, open rooms, accounts, the message of the day. |
| `/register` | User name, password twice, optional e-mail, a honeypot field; shows the recovery code once and signs in. |
| `/login`, `/logout`, `/recover` | Sign in (optionally persistent), sign out (POST), reset a forgotten password with the recovery code (shows the next one). |
| `/account` | Overview, online character, password (ends every other session), e-mail, new recovery code, public / private profile, cloud save info, download and deletion (with the password), sessions (log out one or all others), account deletion. A temporary password from an admin must be changed first; every other page redirects here until then. |
| `/u/{name}` | Profile: online character (Digimon, name, level, play time, forms, story clears) and achievements with unlock dates, rulesets, player counts and progress bars. Private profiles show only to the owner and staff; unlocks under review only to them. |
| `/achievements` | Every definition by category with tier, points and rarity (share of players holding it); hidden ones masked unless the visitor holds them. |
| `/rooms` | Open rooms, read-only (host away = story paused). |
| `/admin`, `/admin/account/{name}` | Staff: account search, open rooms (close), lock / unlock, ban / unban, temporary password, flag / unflag / revoke unlocks; admins also set roles and erase accounts. |
| `/privacy` | What is stored, why, how long, cookies, recipients, rights, deletion (GDPR, server in Germany); the controller from `Web:Operator*`. |

Below a path (`Service:PathBase`, for example `/dw4online` for `https://dekirai.crygod.de/dw4online`) every link,
redirect, form, `Location` header and cookie stays below it (the session cookie is then `__Secure-dw4_session` with
`Path=/dw4online` on https instead of `__Host-dw4_session`, so other sites on the host never receive it, and the login
returns only to pages below the prefix); requests without the prefix keep working at the root, so clients of an
earlier root address go on working during a move. The browser's same-origin rule knows no paths, though: pages of
other sites on the same host name (`https://dekirai.crygod.de/...`) can script DW4Online with the visitor's cookie,
so a script flaw there would be an account takeover here. Give DW4Online its own host name (a subdomain such as
`dw4.crygod.de`) when other sites share the name (deploy/DEPLOY.md 6.2).

Security: sign-in is a cookie holding an opaque `web` session token (same sessions table as the game: revocable,
14 days sliding), HttpOnly, SameSite=Lax, Secure and `__Host-` prefixed on https. Every POST carries an anti-forgery
token (Razor Pages validate it; a missing one gives the 400 error page). Login and recovery POSTs are limited to 5 per
minute per IP, registrations to 3 per hour, on top of the account lockout. Website responses carry a strict CSP
(`default-src 'none'; style-src 'self'; form-action 'self'; frame-ancestors 'none'`), `X-Frame-Options: DENY`,
`nosniff`, `Referrer-Policy: same-origin`, `Cache-Control: no-store`; `Web:RequireHttps` redirects to https and
`Web:Hsts` adds HSTS. API paths keep bearer tokens and problem details.

## Open ends (other lanes)

- Game client: round S1 built the WinHTTP client with pinning, the DPAPI token, the command-line room login and the
  DW4R2 client (`src/game/online`, speaking the relay's layout since the round S1 integration); round S2 adds the title /
  F1 room UI and the online character in place of the card.
- Round S2: STORY / STORY_ACK and the room story; round S4: the resync path completed, RESUME with missing ticks after
  a crash-free reconnect, UDP for INPUT / SEALED (the S1 loss matrix, SEAMLESS.md 6.4).
  For the IP phase it talks to `https://88.198.52.221` and pins the two SPKI pins of the kept TLS keys (in use and
  spare; `deploy/tls-key.ps1` makes the keys and their certificates, `deploy/print-pins.ps1` writes the pins for the
  client build, DEPLOY.md 6.1 and 6.4), checked in WinHTTP's SENDING_REQUEST callback exactly like `dw4online_client --pin`;
  for the later domain (`https://dekirai.crygod.de/dw4online`) it validates normally and puts every API path below
  the base URL's path.
- Achievement goals that need calibration on real runs (speed limits, Minimalist level, stat rates, level-rate warning).
- Partner display names: `p_angelr` has no built-in English name yet (the website shows the model name); set
  `Web:DigimonNames` or check the built-in list in `src/DW4Online.Web/WebOptions.cs`.
