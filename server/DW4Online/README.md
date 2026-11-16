# DW4Online

The online backend of the Digimon World 4 PC port: accounts, the online character, the private cloud backup of the
local save, achievements, the online world (instances instead of rooms since round I1), the DW4R2 relay with its lockstep instances and the website, in **one** ASP.NET Core process
(API, website and relay share memory) backed by MariaDB. It runs as a Windows service on the user's Windows Server;
players never forward ports.

Status: the service side runs on the project's server and is tested on loopback: core (accounts, online character,
cloud save, achievements), friends and blocks, the world (instances, their history, the staff's moderation) with the
relay, and the website. The game client speaks it (`src/game/online`, see "Open ends"); `tests/winhttp_client` shows the
client side of every call over WinHTTP. The room service of the releases before 0.1.7 is retired (its endpoints answer
410, its tables are read-only).

## Layout

| Path | What |
| --- | --- |
| `src/DW4Online.Domain` | Pure rules, no ASP.NET Core and no database: the 3316-byte character record codec (C# port of `src/save_data.cpp`), validation, summaries, password hashing, tokens, recovery codes, achievement rules, the services (accounts, online character, cloud save, achievements) and the repository interfaces. |
| `src/DW4Online.Data` | MariaDB through MySqlConnector + Dapper: repositories and the startup migrator. The SQL migrations are embedded. |
| `src/DW4Online.Api` | The JSON API `/api/v1`: endpoints, bearer-token authentication, rate limits, problem details. |
| `src/DW4Online.Relay` | The DW4R2 relay (port 52041, SEAMLESS.md 6): `Protocol` (frames and messages), `Hub` (one hub for the world, one connection per member, stage groups as solo-led and sealed lockstep groups, server-paced sealing on a dedicated high-resolution timer thread, barrier decisions, forks, the tick log, state delivery, the presence fan-out: PRESENCE and since round 60 PRESENCE2 for the ghost figures of the lobby, with its bounds and token bucket, SEAMLESS.md 12.1), `World` (the user's instances: matching, seats, the end with HOME, the host's grace, SEAMLESS.md 19), HMAC world tickets. |
| `src/DW4Online.Web` | The Razor Pages website (English, server-rendered, no scripts): home, register, login, recovery, account settings, friends (with the online preferences and "Recently played with"), profiles, achievements, instances, admin (accounts, instances, reports), privacy. |
| `src/DW4Online.Server` | The executable: composition root, configuration, Windows-service mode, CLI tools, TLS certificate selector, file log. |
| `sql/` | Numbered migrations `NNNN_description.sql` (0001 schema, 0002 achievement definitions, 0003-0014 see "Database"). |
| `tests/DW4Online.Tests` | Unit tests (no database). |
| `tests/DW4Online.Integration.Tests` | HTTP + MariaDB tests against a private local instance (API, the world with the real relay and fake clients, website forms, end to end on real Kestrel). |
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

A fresh data directory of XAMPP's `mysql_install_db` came up on 05.11 with its Aria system tables (`mysql.db`,
`mysql.global_priv`) marked as crashed, which only `MigratorTests.Migrations_need_only_the_restricted_service_users_grants`
notices (it creates a user): `mysqlcheck --repair --databases mysql` (and `REPAIR TABLE mysql.global_priv EXTENDED`)
as root on the private instance, then `local-db.ps1 stop` and `start`, and it works. A `mysqld` that is killed instead
of shut down (an agent shell that ends takes the processes it started with it, also `local-db.ps1 start`'s hidden one)
can leave `mysql.global_priv`'s index broken so that the next start aborts ("Can't open and lock privilege tables"):
start that `mysqld` once with the same arguments plus `--skip-grant-tables` (still bound to 127.0.0.1), run `REPAIR
TABLE mysql.global_priv USE_FRM` and `REPAIR TABLE` for the other system tables `CHECK TABLE` names, `SHUTDOWN`, and
start it normally. Run it from a shell that outlives the tests and stop it with `local-db.ps1 stop` (SQL `SHUTDOWN`).

`local-db.ps1` uses XAMPP's MariaDB binaries (`-MySqlBin` for others) with its **own** data directory in
`server/DW4Online/.local` (gitignored), its own port and `--bind-address=127.0.0.1`; it never touches XAMPP's data,
other databases or services and stops only the `mysqld` it started (by PID). XAMPP ships MariaDB 10.4, whose native
AIO hung `DROP DATABASE` here (an InnoDB latch wait), so the script starts it with `--innodb-use-native-aio=0`. The
same hang is the likely cause of the server's failed first start on 31.10 (migration 0001 stuck in a `CREATE TABLE`),
so the XAMPP variant of the deploy kit sets that line in the server's `my.ini` too (DEPLOY.md section 1.1), and the
migrator warns when it talks to a MariaDB before 10.6 on Windows with native AIO on.
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
| `Database` | `ConnectionString`, `AutoMigrate` (true): run pending migrations at startup, `StartupWaitSeconds` (60): how long the start waits for a database that does not answer yet. |
| `Service` | `Name`, `Motd` (game menu and home page), `Maintenance` (announced; no new rooms), `RelayHost` (empty = the request's host name) / `RelayPort` (default 0 = the port the relay listens on, `Relay:Port`; set it only when clients reach the relay through another public port), announced to clients; `PathBase` (empty; for example `/dw4online`: website and API below that path, see "Website"), `BehindProxy` (false; true behind a reverse proxy on the same machine: client address from the last `X-Forwarded-For` entry, scheme from `X-Forwarded-Proto`, and a stripping proxy's `X-Forwarded-Prefix` when it is `PathBase`, on plain-HTTP connections from this machine only; never doubled), `TrustedProxyNetworks` (empty; with `BehindProxy` Cloudflare's CIDR ranges: when the local proxy saw such an address, Cloudflare's `CF-Connecting-IP` is the client's, if it is one public address; the local proxy keeps its default; deploy/DEPLOY.md 6.2). |
| `Accounts` | `Registration` (open / closed), `PasswordIterations` (220000), `MaxFailedLogins` (10, per user name and address), `LockoutMinutes` (15), `MaxFailedLoginsPerAccount` (50), `KnownAddressDays` (30), `GameSessionDays` (30), `WebSessionDays` (14), `SessionTouchMinutes` (5), `MaxSessionsPerAccount` (20). |
| `OnlineCharacter` | `HistoryRevisions` (20), `ResetHistoryDays` (30: the revisions of a reset character stay that long), `StoryHistoryRevisions` (100: the newest revisions of an account's online story kept in full, online_story_history), `StoryHistoryMinutes` (10: older room revisions are kept as the first of every this many minutes), `StoryHistoryDays` (90: how long those and the session starts, resets and restores stay), `StoryHistorySessions` (20: the newest session starts, resets and restores stay for good), `StoryCounterSaveSeconds` (60: a room story revision that changed only counters is written at most this often per account), `Rules:MaxLevelsPerPlayHour` (120), `Rules:PlayTimeSlackSeconds` (600). |
| `CloudSave` | `MaxBytes` (1 MiB), `HistoryRevisions` (5). |
| `World` | (round I1, SEAMLESS.md 19.17.5) `DataHashes` (empty = any game data; a list admits only those: `/world/enter` 409 `unsupported_data`, the relay `ERR data`), `RankedDataHashes` (empty = every data hash counts for achievements; `Rooms:RankedDataHashes` is read as it for one release), `HistoryDays` (90: `instance_history`), `RecentlyPlayedDays` (30); the relay's own keys of the same section (members, instances, the host's grace, presence) are the relay's (`WorldHubOptions`). |
| `Play` | `RequireWorld` (true: unlocks, stats and state achievements need the account in the world within the last minute, the relay's play context; `RequireRoom` is read as it for one release), `MinimumProtocol` / `CurrentProtocol` (netplay `kProtocol`; the code refuses below the world's protocol 50 (DW4Online 0.2.0, round I1b; 49 before) whatever `MinimumProtocol` says, and `/status` announces the effective minimum), `MinimumClientVersion` / `CurrentClientVersion`, `SpeedrunFloorSeconds` (per board `story_normal` / `story_hard` / `story_veryhard`, 2700 each: see "Speedrun floor"), `ClearStoryWaitSeconds` (30: a story clear reported in a room counts when the room's story holds that difficulty's clear flag; the server waits this long for it). |
| `RateLimits` | `GlobalPerMinute` 300 (per IP), `LoginPerMinute` 5 (per IP, login + recovery), `RegisterPerHour` 3 (per IP), `CharacterWritesPerMinute` 12, `CloudWritesPerHour` 6, `AchievementsPerMinute` 60, `AccountChangesPerMinute` 10, `StaffPerMinute` 60, `RoomCreatesPerMinute` 5, `RoomJoinsPerMinute` 20, `StageChangesPerMinute` 30 (stage enter + leave) (per account); `WebLoginPerMinute` 5 (website login + recovery), `WebRegisterPerHour` 3 (per IP, POSTs only), `WebAccountChangesPerMinute` 10 (per account, the account page's POSTs); `SocialPerMinute` 60 (per account: friend requests and answers, blocks, player cards), `FriendListSeconds` 2 (one `GET /friends` per account per this many seconds; 0 = no limit), `WebSocialPerMinute` 60 (per account, the friend and block buttons of the website, POSTs only); `ReportsPerMinute` 10 (per account, `POST /reports`). |
| `Reports` | (round I1b, *Report player*) `PerDay` (10: reports a player may send within 24 hours, counted in `player_reports`), `RepeatHours` (24: a second report about the same player within this adds no row), `SharedInstanceHours` (24: how far back a report looks for an instance both played in), `PageSize` (50: the admin list), `DoneDays` (180: a handled report stays this long, then the hourly housekeeping deletes it; open reports stay until staff handle them; the privacy page names the period). |
| `Friends` | `RequestsPerHour` (20) and `RequestsPerDay` (100) per sender (counted in memory), `MaxPendingOut` (50: open requests a player sees as sent), `MaxPendingIn` (100: open requests that reach a player at a time; further ones wait their turn), `MaxFriends` (200), `MaxBlocks` (500), `RequestDays` (30: pending and declined requests expire, then the hourly housekeeping deletes them). See "Friends and blocks". |
| `Rooms` | `MaxRooms` (200), `PlayersPerInstance` (4), `HeartbeatSeconds` (10), `MemberTimeoutSeconds` (35), `RoomsPerOwner` (1), `PasswordIterations` (20000, room passwords live in memory only), `SettingsMaxBytes` (4096), `RankedDataHashes` (empty = every data hash is ranked; decimal or 0x-hex). |
| `Relay` | `Enabled` (true), `ListenAddress` (127.0.0.1; 0.0.0.0 on the server), `Port` (52041), `TicketKey` (secret, base64 32 bytes: `DW4Online.Server gen-relay-key`; required outside Development), `TicketLifetimeSeconds` (60), `HandshakeTimeoutSeconds` (10), `IdleTimeoutSeconds` (60), `MaxConnectionsPerIp` (16), `MaxRooms` (200), inbound limits per connection `BytesPerSecond` (65536) / `BurstBytes` (1 MiB) / `FramesPerSecond` (400) / `FrameBurst` (2000), state transfers `StateBytesPerSecond` (4 MB/s) / `StateBurstBytes` (16 MiB), `MaxQueuedBytes` (32 MiB: a connection that does not read is closed), `ReconnectSeconds` (10), `LagLeaveSeconds` (10; also a host's time to catch up after his grace, and with `World:HostGraceSeconds` the limit of his absence debt for a link that keeps dropping: a longer value is more lenient there too, SEAMLESS.md 19.6.7), `DiagnosticsDirectory` (empty; a folder gets one JSON-lines membership log per room). |
| `Sealing` | `MaxDelayTicks` (15), `LateTarget` (0.02: d rises by one when more than this share of the last 600 paced pads, ten seconds, came late in at least three different seconds; SEAMLESS.md 6.4), `EventMarginTicks` (2: added to the largest member horizon for JOIN, LEAVE, INTENT). |
| `Instances` | `CoArrivalHoldMs` (3000), `JoinHoldSeconds` (30), `MaxCatchUpSeconds` (30), `DefaultReplayRate` (3000 steps/s for a member that sent no RATE), `HeadingValidMs` (5000), `BarrierReportSeconds` (5), `StoryBurst` (10) and `StoryMessagesPerMinute` (60: a member's STORY rate), `StoryMinVisitSeconds` (10: phase steps, flags, key items and counter growth count only from a visit at least this old; a visit gives one phase step and one key item, SEAMLESS.md 8.7), `StoryPhaseStepSeconds` (300) and `StoryKeySeconds` (60: a guest's phase steps and key items count at least this far apart in a room session, the owner's are not spaced; a step that waits only for these or for the one-per-visit rule is earned and merged by the relay when due), `StoryFlagBurst` (8) and `StoryFlagSeconds` (30: a guest's persistent flags count this many at once, then one per this many seconds in a room session; the rest is earned likewise; not the owner's, not the room's clear flag; 0 seconds: no spacing), `StoryLinesPerMember` (256: the counter lines a member may open in a room session; a guest also gets at most 1,000 of COUNTER 7 and 20,000,000 of COUNTER 1 merged per room session, `RoomStoryKeeper.MaxSessionGrowth`; the owner's own counters are not capped). `Rooms:PlayersPerInstance` (4) caps every instance. `Rooms:StoryWhileOwnerAway` (`drop`, `hold` or `apply`; default drop): story progress made while the room owner is away (SEAMLESS.md 8.6). |
| `Web` | `RequireHttps` (false; true on the server: website pages redirect to https, cookies are Secure), `Hsts`, `DataProtectionKeysDirectory` (anti-forgery keys, DPAPI-encrypted on Windows; empty = the service account's profile, `memory` = tests), `OperatorName` / `OperatorAddress` / `OperatorContact` (the controller on the privacy page), `Processors` (service providers the privacy page names beside the hosting provider, e.g. Cloudflare in front of the website, deploy/DEPLOY.md 6.2), `DigimonNames` (display names by model, overriding the built-in list). |
| `Tls` | `StoreHostName`: take HTTPS certificates from the Windows store (renewals without a restart); its certificate serves every connection that names no other host (an IP address sends no name: the IP phase's pinned self-signed certificate). `AdditionalHostNames` (empty): host names with their own certificate, chosen by the name the client asks for (SNI), for example the later domain next to the pinned IP certificate. `StoreName` (My), `StoreLocation` (LocalMachine), `RefreshMinutes` (10); `AcmeChallengeDirectory`: serve ACME http-01 files from a folder. |
| `Logging:File` | `Directory` (empty = off), `MinimumLevel`, `RetainDays`: daily UTC log files for the service. |
| `Kestrel` | Standard Kestrel endpoints (`http://127.0.0.1:5280` by default). |

CLI of the executable:

```
DW4Online.Server                          run in the console
DW4Online.Server --service                run under the Service Control Manager (connects first, then database wait and migrations)
DW4Online.Server migrate                  apply pending migrations and exit (each with its time; exit 1 with the way out on a refusal)
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
run it; the server installed on 31.10 runs XAMPP's 10.4.32, and the integration tests pass round after round on a
private mysqld with XAMPP's own `my.ini` plus the three lines of DEPLOY.md section 1.1: bind-address,
max_allowed_packet and `innodb_use_native_aio=0`, without which DDL can hang). InnoDB, `utf8mb4_unicode_ci` set
explicitly (11.x changed the default), all times UTC
`DATETIME(3)` written by the service. The migrator applies `sql/NNNN_*.sql` in order and records each in
`schema_version` with the SHA-256 of the file. MariaDB commits DDL implicitly, so before it runs a migration it marks
it in `schema_version` as started (an all-zero checksum, description "... (started, not finished)") and replaces the
mark when the migration is done. It refuses to run, with the state and the way out in its message (DEPLOY.md
section 9), when a migration is marked as started (interrupted or failed half way: restore the backup from before the
update), when `schema_version` is empty (the first migration was interrupted; when no table holds a row, drop and
re-create the database), when the database has tables but no `schema_version`, when an applied file changed, or when
the database is newer than the build. It never drops or repairs anything itself. **Back up before an update** (deploy
kit) and run `migrate` in a console before the service starts.

Startup (`ServerStartup`): as a Windows service the process enters the Service Control Manager's dispatcher first and
reports START_PENDING with a new checkpoint every 5 s (`WindowsServiceLifetime`), then waits for the database (up to
`Database:StartupWaitSeconds`) and migrates, then starts the host and reports RUNNING. A refused or failed migration,
or a setting the start checks first (`Relay:TicketKey`), is a FAIL line in the log and STOPPED with service-specific
error 13 (no restart); a database that never answered, a host that could not start (a port in use: Kestrel's or the
relay's, which binds in `StartAsync` so the error names the port) or a host that stopped after RUNNING without a STOP
from the SCM (a failed background service) ends the process without STOPPED, so the service's restart actions run.
The host is started and awaited by `ServerStartup` itself (not `RunAsync`, which disposed the log providers before
the FAIL line could be written) and disposed only after that line. A host whose start failed is stopped before that
line is written: the hosted services that had started end quietly, so the start's FAIL line is the log's last (only
disposed, their cancellation came as a later "BackgroundService failed" line at times). Release 0.1.0 migrated before it connected; when
migration 0001 hung on the server, the SCM ended it after 30 s. `tests/DW4Online.Tests/ServiceStartTests.cs` runs
this against a fake SCM, `tests/DW4Online.Integration.Tests/ServiceHostTests.cs` runs the real host under it.

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
"Achievements"); 0010 `online_story_history` (every revision written to `online_story` with its reason: a room's
merge, the reset that goes with an online character's reset, or a staff restore; the last
`OnlineCharacter:StoryHistoryRevisions` per account, removed with the account); 0011 the history's reason `session` (the
story a room session started from, kept when its owner opens a room; the history is kept by time and session since,
see `OnlineCharacter:StoryHistory*`); 0012 `friendships`, `friend_requests` and `account_blocks` (round I2-social,
SEAMLESS.md 19.11; plain columns with a comment on each for HeidiSQL); 0013 `world` (round I1, SEAMLESS.md 19.17.1):
`online_prefs` (the visibility a player's instances start with, invites, whereabouts, toasts and the host settings; the
room hosts' newest room settings carried over), `instance_history` + `instance_history_members` (written by the relay's
queue, 90 days), `accounts.last_seen_at`, `instance_id` on `achievement_unlocks` and `speedrun_times`, the story history
reason `mirror`, and the views `v_friends`, `v_friend_requests`, `v_blocks`, `v_instance_history`, `v_staff_audit`;
0014 `reports` (round I1b, DW4Online 0.2.0, DEPLOY.md 9.3): `player_reports` (*Report player*) and the view
`v_player_reports` (0013 went live with 0.1.7 and keeps its checksum). The
views need `CREATE VIEW` (DEPLOY.md 9.2): a migration's `-- Requires:` line names such rights, and the migrator refuses
before it changes anything when the database user lacks one. `rooms` and `room_members` are read-only since 0013.

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
| `GET /online-character/history`, `POST /online-character/reset` | token | Kept revisions; end the character and empty the account's online story (`{currentPassword}`; also an account with a story but no character; its revisions stay 30 days for staff, then go). A character the last reset ended does not come back as a new one (409 `character_was_reset`). |
| `GET /cloud-save` (`?revision=N`) | token | The save bytes (`application/octet-stream`), `ETag`, `X-Sha256`, `X-Save-Format`. |
| `GET /cloud-save/meta` | token | `{latest, boxes, history}`. |
| `PUT /cloud-save` | token | Body = save bytes, `X-Sha256` (hex), `If-None-Match: *` or `If-Match: <revision>`, optional `X-Device-Name`, `X-Client-Version`. 413 above `MaxBytes`. |
| `POST /cloud-save/restore/{revision}` | token | Make a kept revision the latest again. |
| `POST /cloud-save/delete` | token | `{currentPassword}`: delete the backup and its history (a wrong password counts as a failed login). |
| `GET /achievements` | optional | All definitions (hidden ones masked until unlocked); with a token the caller's unlocks and progress. |
| `POST /achievements/unlock` | token | `{code, context: {roomCode, players, roomPlayers, randomizer, cheats, rebalanced, saveStateLoaded, difficulty, protocol, clientVersion}, evidence: {igtMs?, level?, stage?}}` -> `{code, ruleset, newlyUnlocked, newBestTime, timeRejected}`. |
| `POST /stats` | token | `{deltas: {enemies_defeated: 12, play_seconds: 60, ...}, context}` -> `{totals, unlocked}`. |
| `GET /friends` | token | `{friends: [{username, since, presence}], requestsIn: [{username, sentAt, expiresAt}], requestsOut: [...], blocks: [{username, since}], limits: {maxFriends, maxPendingOut, maxPendingIn, maxBlocks, requestDays}}`; `presence` is `{online, status ("In the lobby", "Playing in Dry Land", "Playing" in a Private instance, "Offline"), area, stage, lastSeenAt}`, or null when the friend's whereabouts preference is "nobody". One call per `RateLimits:FriendListSeconds` (2). |
| `POST /friends/requests` | token | `{username}` (the exact user name, any case) -> `{result: "sent" or "friends", message}`; "friends" when the other player's request to the caller was open. 404 `player_not_found`, 422 `own_account`, 409 `you_blocked` / `already_friends` / `friend_limit` / `other_friend_limit` / `outgoing_limit`, 429 `friend_request_rate` with `retryAfterSeconds`. A request to a player who blocks the caller, whose open requests are full (it waits) or a repeat answers exactly like any other. Every write here may answer 503 `social_busy` when it waited 10 s for the accounts' lock. |
| `POST /friends/requests/{username}/accept`, `.../decline`, `DELETE /friends/requests/{username}` | token | Accept (-> `{result: "friends", message}`), decline (204, silent for the sender), cancel the caller's own request (204). 404 `request_not_found`. |
| `DELETE /friends/{username}` | token | End a friendship (204; 404 `not_friends`). |
| `GET /blocks`, `POST /blocks` `{username}`, `DELETE /blocks/{username}` | token | The caller's blocks; block (204, repeating changes nothing, 409 `block_limit`); unblock (204, 404 `not_blocked`). |
| `GET /players/{username}` | token | The public card: `{username, memberSince, hidden, character: {digimon, playerName, level} or null, relation}`; `relation` is `self`, `friends`, `request_sent`, `request_received`, `blocked` (the caller blocks him) or `none` (never the other side's block). A private or banned profile is `hidden` with `memberSince` and `character` null (nothing but the name and the relation), as on the website. |
| `POST /world/enter`, `POST /world/ticket` | token | `{box?, protocol?, dataHash?, clientVersion?}` -> `{relayTicket (= ticket), relay: {host, port, protocol}, protocol (50), role (the account's), relayRole (member / staff), box (1), heldSeat (null until round I5), prefs: {defaultVisibility, invites, whereabouts, friendToasts}, settings: {cameraMode, lightMode, fastLoading, randomizer: {enabled, seed, drops, enemies, zones}}}`: the world session's one-time relay ticket (SEAMLESS.md 19.17.3; the newest per account counts; `/world/enter` also keeps the story the session starts from). 426 `client_outdated` (a protocol below 50 when one is given), 503 `maintenance` / `relay_unavailable`, 403 `account_inactive`, 409 `unsupported_data`. |
| `POST /world/leave` | token | 204: the relay closes the account's world connection. |
| `GET /world` | - | `{players, playersOnline, instances, areas: {death_valley: n, ...}, areaList: [{area, name, instances, players}]}`, no names. |
| `GET /instances?area=` | token | The instances the caller may see (Visible(), SEAMLESS.md 19.7.2: Public, Friends for the host's friends, never Private; a block either way hides; staff see all with the visibility): `{instances: [{wid, host, area, areaName, stage, stageName, players, maxPlayers, visibility, difficulty, randomizer, storyLabel, ageSeconds, yours}]}`. |
| `GET /me/online-prefs`, `PUT /me/online-prefs` | token | `{defaultVisibility, invites, whereabouts, friendToasts, cameraMode, lightMode, fastLoading, randomizer: {enabled, seed, drops, enemies, zones}, lastBox}`; PUT takes any subset (422 `invalid_prefs`). |
| `GET /rooms`, `POST /rooms`, `/rooms/{code}` and everything below it (join, ticket, story, heartbeat, leave, kick, `DELETE`) | - | **Retired** since round I1 (0.1.7): 410 `rooms_retired` (rooms gave way to the world). The clients of 0.1.6 and earlier used them. |
| `GET /admin/instances`, `POST /admin/instances/{wid}/end?run=`, `POST /admin/instances/{wid}/remove?run=` `{username}` | staff | `{run, instances}`: every instance with members and spectators, and the relay run they are of; End (the instance ends, audited `staff_end_instance`) and Remove (the member goes home under the 10-minute removal bar, `staff_remove`). An instance is its run and wid: the relay numbers wids from 1 again after every restart, so End and Remove take the listing's `run` (422 `run_required` without it, 409 `instance_list_outdated` for another run: reload the list) and 404 `instance_not_found` / `member_not_found`. |
| `POST /reports` `{username, reason}` | token | *Report player* (round I1b, SEAMLESS.md 19.16): `reason` griefing, cheating, name or other (no free text) -> `{result: "sent", message: "Report sent. Thank you."}` (also for a repeat about the same player within `Reports:RepeatHours`, which adds nothing); 404 `player_not_found`, 422 `own_account` (as `/friends/requests`), 422 `invalid_reason`, 429 `report_limit` (`Reports:PerDay`, with `retryAfterSeconds`: when the oldest report of the 24 hours leaves them; once reached the service answers it without a query for 10 minutes, or until that time when it comes sooner, a repeat included); `RateLimits:ReportsPerMinute` (10). The game reports through the relay (REPORT 0x1E: 3 at once, then 10 a minute; only a member the reporting client was told about, any other id is "not available" without a name) into the same rules. |
| `GET /admin/reports?state=open\|done\|all&page=`, `POST /admin/reports/{id}/done`, `POST /admin/reports/{id}/reopen` | staff | The players' reports, newest first (`{reports: [{id, reporter, reporterId, reported, reportedId, reason, source, instanceId, instanceHost, instanceStage, instanceStarted, createdAt, state, handledBy, handledAt, reportsAboutReported}], total, page, pageSize}`; the instance's host name, first stage and start are null without an instance; a report about the caller's own account has `reporter`, `reporterId` and the four instance fields null: staff never learn who reported them); Done (204, 409 `report_done`, audited `report_handled`) and Reopen (204, 409 `report_open`, `report_reopened`); never a report about the caller's own account (403 `own_report`: another moderator handles it). |
| `GET /admin/accounts?query=`, `POST /admin/accounts/{user}/reset-password` (admin), `.../status`, `.../role` (admin), `.../online-character/history`, `.../online-character/restore/{rev}`, `.../online-character/reset` | staff | Moderation; every action is audited. A locked, banned or demoted account is re-checked by the relay at once (`IWorldNotifier.AccountChanged`). `status` is active, locked or banned (deleting is `DELETE /admin/accounts/{user}`, admins only); a temporary password only from an admin (whoever knows it owns the account). |
| `POST /admin/accounts/{user}/achievements/{code}/flag` `{ruleset?, flagged}`, `DELETE /admin/accounts/{user}/achievements/{code}?ruleset=`, `DELETE /admin/accounts/{user}` (admin) | staff | Flag / unflag or revoke an unlock, erase an account on request. |
| `GET /health` (no prefix) | - | `{status, schema}` or 503 when the database is unreachable. |

Until round I1 the room state (create, join, heartbeat, `GET` for members; retired) was `{room, settings, settingsHash, members: [{accountId,
username, slot, owner, instance, joinedAt}], instances: [{id, stage, visit, mode (hub / solo / sealed), members:
[{accountId, username, slot, connected, lagging}], departing, bossLock, joinable, ticks, parent}], you,
heartbeatSeconds, relay: {host, port, protocol}, relayTicket}` (`relayTicket` only after create, join and /ticket;
`settingsHash` was the 16-hex-digit hash the room's DW4R2 handshake carried).

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
- **Friends and blocks** (SEAMLESS.md 19.11): a request names the exact user name (no search); the other player
  accepts or declines; a request to a player whose own request to the caller is open makes both friends at once. A
  decline is silent: the request stays "sent" for its sender until it expires (30 days), a new request from him
  meanwhile answers "Request sent." and changes nothing, and his cancel only hides it from him (the decline holds).
  Either side removes a friendship without the other being told. A block ends the friendship and the blocker's own
  request; the blocked player's request to the blocker stays "sent" for him but no longer reaches the blocker
  (`delivered = 0`), and so do his new ones: a block is never revealed. At most `Friends:MaxPendingIn` (100) requests
  reach a player at a time; a request beyond it answers "Request sent." like any other and waits (`delivered = 0`)
  until a place frees (an answer, a cancel, a block or an expiry), oldest first, so the cap never tells a sender
  anything about a block or a decline; an unblock lets the formerly blocked player's request wait its turn the same way.
  Limits per account in `Friends:*` (only the sender's own limits refuse a request); a refusal by a limit writes an
  audit row `social_limit` (which limit) at most once per account and limit an hour, the changes themselves are not
  audited. Every social write holds the locks of both accounts (striped by account id), so crossing requests and the
  caps cannot race while other accounts' writes run on; after 10 s of waiting it answers 503 `social_busy`. Account
  deletion removes the account's rows in all three tables, in both directions.
  `IWorldNotifier` (`FriendsChanged`, `BlocksChanged`, ...) tells the relay in-process; `FriendsChanged` names who may
  learn of a change (a decline and a delivered waiting request are the receiver's alone); until round I1 nobody listens
  (`NullWorldNotifier`).
- **The world** (round I1, SEAMLESS.md 19): rooms are gone; `/world/enter` hands out the world session's relay ticket,
  the relay groups players into instances per area (`IWorldRelay`, implemented by the relay; `WorldService` is the
  service side: admission, counts, `Visible()` for the lists, whereabouts, the play context, End and Remove).
- **Rooms** (until round I1, read-only since migration 0013): a name (1-32 characters), an optional password (4-64, PBKDF2 in memory only, never stored), 2..10
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
  character and its history, cloud save and history, unlocks, stats, best times, room memberships, online preferences,
  instance memberships, bans and e-mail
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
  and stats need the account in the world within the last minute (the relay's play context, `Play:RequireWorld`); a
  report with cheats, rebalanced rates or a save-state load is refused (`unranked_*`), and so is unranked game data
  (`World:RankedDataHashes`). Since round I1 the instance decides: its randomizer the ruleset, the relay's count its
  players, the host gets the story challenges, a guest `online_join`, a clear needs the reporter's own story to hold it. Randomizer runs are their own ruleset (a second
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

- **One connection per member** to the one world hub, across all its stage groups and instances (SEAMLESS.md
  19.17). Tickets come from `POST /world/enter` (Play) and `POST /world/ticket` (a reconnect): the fixed world channel,
  the account, the role (member or staff), expiry and nonce, HMAC-SHA256 signed, single use, 60 s. The relay asks the
  world service before it accepts one (the account's live status and role, its friends and blocks, the newest ticket
  only). A newer connection of the same account replaces the older one (CLOSE replaced).
- **Handshake**: `DW4R2 <ticket> 50 <dataHash> <ignored>` -> `OK <member> <serverMs>` or `ERR <code>` (`protocol`,
  `ticket`, `data`, `full`); then frames of type u8 + length u32 LE + payload, at most 1 MiB, WELCOME first.
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
- **Leaving**: BYE, a leave of the world or a timeout stamp LEAVE at once; a closed connection keeps its slot filled for
  10 s (`Relay:ReconnectSeconds`), 10 s of continuous filling (`Relay:LagLeaveSeconds`) end it too.
- **Limits**: handshake 10 s, idle 60 s, 16 connections per IP, inbound 64 KB/s with a 1 MiB burst and
  400 frames/s with a burst of 2000 per connection (a flood closes with CLOSE abuse), malformed frames close with
  CLOSE protocol, state transfers 4 MB/s with a 16 MiB burst, 32 MiB of unsent frames close a connection that does
  not read.

## Website

Razor Pages in `src/DW4Online.Web`, English, one embedded stylesheet (`/css/site.css`, light and dark), no scripts.

| Page | What |
| --- | --- |
| `/` | Players online, instances, accounts, the message of the day. |
| `/register` | User name, password twice, optional e-mail, a honeypot field; shows the recovery code once and signs in. |
| `/login`, `/logout`, `/recover` | Sign in (optionally persistent), sign out (POST), reset a forgotten password with the recovery code (shows the next one). |
| `/account` | Overview, online character, password (ends every other session), e-mail, new recovery code, public / private profile, cloud save info, download and deletion (with the password), sessions (log out one or all others), account deletion. A temporary password from an admin must be changed first; every other page redirects here until then. |
| `/u/{name}` | Profile: online character (Digimon, name, level, play time, forms, story clears) and achievements with unlock dates, rulesets, player counts and progress bars. Private profiles show only to the owner and staff; unlocks under review only to them. Signed-in visitors get the friend and block buttons of their relation (*Add friend*, *Request sent* / *Cancel request*, *Accept request* / *Decline*, *Friends* / *Remove friend*, *Block* / *Unblock*), on private profiles too; the other side's block never shows. |
| `/account/friends` | Friends (Remove, Block), friend requests (Accept, Decline), sent requests (Cancel; declined ones stay listed until they expire), blocked players (Unblock), a request or a block by exact user name. |
| `/achievements` | Every definition by category with tier, points and rarity (share of players holding it); hidden ones masked unless the visitor holds them. |
| `/instances` | The world's instances: signed-in players see those open to them by area (host, stage, players, story point, age; Visible(), SEAMLESS.md 19.7.2), staff all with their visibility, signed-out visitors the counts per area. `/rooms` (retired) redirects here. |
| `/admin`, `/admin/account/{name}` | Staff: account search, lock / unlock, ban / unban, temporary password, flag / unflag / revoke unlocks, the account's instance history; admins also set roles and erase accounts. |
| `/admin/instances` | Staff: every instance, Private ones included, with *End* and *Remove* (audited); the forms name the relay run with the wid, so a page from before a server restart is refused. |
| `/admin/audit` | Staff: the staff rows of the audit log (a moderator his own, an admin all). |
| `/privacy` | What is stored, why, how long, cookies, recipients, rights, deletion (GDPR, server in Germany); the controller from `Web:Operator*`. |

Below a path (`Service:PathBase`, for example `/dw4online` for `https://dekirai.crygod.de/dw4online`) every link,
redirect, form, `Location` header and cookie stays below it (the session cookie is then `__Secure-dw4_session` with
`Path=/dw4online` on https instead of `__Host-dw4_session`, so other sites on the host never receive it, and the login
returns only to pages below the prefix); requests without the prefix keep working at the root, so clients of an
earlier root address go on working during a move. The browser's same-origin rule knows no paths, though: pages of
other sites on the same host name (`https://dekirai.crygod.de/...`) can script DW4Online with the visitor's cookie,
so a script flaw there would be an account takeover here. Give DW4Online its own host name (a subdomain such as
`dw4.crygod.de`) when other sites share the name (deploy/DEPLOY.md 6.2). Behind a reverse proxy that strips the
prefix (Traefik's `stripPrefix`), `Service:BehindProxy` takes it back from the proxy's `X-Forwarded-Prefix`, which
counts only when it names `PathBase` (so `PathBase` is set in both variants; any other value could be a browser's own
header passed on). `ReverseProxyTests` covers no proxy, `PathBase` only, a stripping and a non-stripping proxy, one that
names the prefix too, a foreign prefix and a stripping proxy without `PathBase`.

Security: sign-in is a cookie holding an opaque `web` session token (same sessions table as the game: revocable,
14 days sliding), HttpOnly, SameSite=Lax, Secure and `__Host-` prefixed on https. Every POST carries an anti-forgery
token (Razor Pages validate it; a missing one gives the 400 error page). Login and recovery POSTs are limited to 5 per
minute per IP, registrations to 3 per hour, on top of the account lockout. Website responses carry a strict CSP
(`default-src 'none'; style-src 'self'; form-action 'self'; frame-ancestors 'none'`), `X-Frame-Options: DENY`,
`nosniff`, `Referrer-Policy: same-origin`, `Cache-Control: no-store`; `Web:RequireHttps` redirects to https and
`Web:Hsts` adds HSTS. API paths keep bearer tokens and problem details.

## Open ends (other lanes)

- Game client: round S1 built the WinHTTP client with pinning, the DPAPI token and the DW4R2 client
  (`src/game/online`); round I1a's client lane its world session (*Play*, the own lobby, instances, *Leave instance*;
  SEAMLESS.md 19.21); round I1b (DW4Online 0.2.0, protocol 50) the arrival prompt (PLAN / OPTIONS / CHOICE, *Arriving
  now*), go along (OFFER / INTENT 4 / GO), the removal binding at every decision and *Report player* (REPORT). Next
  rounds (19.25): I2 (friends in the game), I3 (the instance browser and travel), I4 (parties); UDP for INPUT / SEALED
  stays open (the S1 loss matrix, SEAMLESS.md 6.4).
  For the IP phase it talks to `https://88.198.52.221:8443` (a Traefik holds 443 there, DEPLOY.md 4.1) and pins the
  two SPKI pins of the server's TLS keys (in use and
  spare; `deploy/tls-key.ps1` makes the keys and their certificates, `deploy/print-pins.ps1` writes the pins for the
  client build, DEPLOY.md 6.1 and 6.4), checked in WinHTTP's SENDING_REQUEST callback exactly like `dw4online_client --pin`;
  for the later domain (`https://dekirai.crygod.de/dw4online`) it validates normally and puts every API path below
  the base URL's path.
- Achievement goals that need calibration on real runs (speed limits, Minimalist level, stat rates, level-rate warning).
- Partner display names: `p_angelr` has no built-in English name yet (the website shows the model name); set
  `Web:DigimonNames` or check the built-in list in `src/DW4Online.Web/WebOptions.cs`.
