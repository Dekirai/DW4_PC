# Deploying DW4Online on the Windows Server

The target is the user's Windows Server 2025 (public address 88.198.52.221, no domain yet). Everything here runs
**without a Windows restart**: MariaDB is an MSI with its own service, the service is a self-contained publish (no
.NET runtime install), Kestrel serves HTTPS itself (no IIS). SQL Server Express and the Pangya servers (web port 88)
stay untouched.

| What | Where |
| --- | --- |
| Service root | `D:\Dekirai\Servers\DW4Online` |
| Program (replaced on updates) | `D:\Dekirai\Servers\DW4Online\app` (previous version kept in `app.previous`) |
| Secrets and settings | `D:\Dekirai\Servers\DW4Online\config\appsettings.Production.json` |
| Logs | `D:\Dekirai\Servers\DW4Online\logs` |
| Database backups | `D:\Dekirai\Servers\DW4Online\backups` |
| ACME challenge folder (later, section 6.3 A2) | `D:\Dekirai\Servers\DW4Online\acme` |
| Website Data Protection keys | `D:\Dekirai\Servers\DW4Online\keys` (DPAPI-encrypted for the service account) |
| Service | `DW4Online`, virtual account `NT SERVICE\DW4Online`, start delayed-auto |
| TLS certificate (now) | self-signed for `IP 88.198.52.221` in `LocalMachine\My`, pinned by the game client (section 6.1) |
| Ports | 443 TCP (HTTPS: API and website), 52041 TCP (relay), 80 TCP only for a later domain's certificate validation; MariaDB 3306 on 127.0.0.1 only |

The scripts in this folder are written for these steps but were **not run** on the server: read them, then run them
yourself in an elevated PowerShell. (`new-selfsigned-cert.ps1` was run end to end against a user certificate store
on the development PC, see the README's tests.)

## 1. MariaDB 11.4 LTS

1. Download the current **MariaDB 11.4.x** Windows MSI (x64) from <https://mariadb.org/download/> (11.4 is an LTS
   line, community support to May 2029).
2. Check that port 3306 is free (another MySQL / MariaDB, for example a XAMPP or a game server's database, may hold it):

   ```powershell
   Get-NetTCPConnection -State Listen -LocalPort 3306 -ErrorAction SilentlyContinue   # no output = free
   ```

   If something listens there, leave it alone and give MariaDB another port in the installer (for example 3307); then
   use that port everywhere below: `Port=3307` in the connection string, `port=3307` in `backup.cnf` (section 7) and
   `install-service.ps1 -DatabasePort 3307` (section 4).
3. Install: service name `MariaDB`, that TCP port, a strong root password, leave "Enable access from remote machines
   for 'root' user" **off**. A data folder on D: is fine (for example `D:\Dekirai\Servers\MariaDB\data`).
4. Add to the `[mysqld]` section of the instance's `my.ini` (see [mariadb-my.ini.example](mariadb-my.ini.example)),
   then restart only the MariaDB service (`Restart-Service MariaDB`):

   ```ini
   bind-address=127.0.0.1
   character-set-server=utf8mb4
   collation-server=utf8mb4_unicode_ci
   default-time-zone='+00:00'
   max_allowed_packet=16M
   innodb_buffer_pool_size=512M
   ```

5. Check that it now listens on the loopback address only (`install-service.ps1` refuses to run otherwise):

   ```powershell
   Get-NetTCPConnection -State Listen -LocalPort 3306 | Select-Object LocalAddress, OwningProcess   # only 127.0.0.1
   ```

6. Port 3306 is **not** opened in the firewall. If the installer added an inbound rule for MariaDB, disable it:
   `Get-NetFirewallRule -Direction Inbound | Where-Object DisplayName -like '*MariaDB*' | Disable-NetFirewallRule`.

## 2. Database and users

Generate two long random passwords (for example `-join ((48..57)+(65..90)+(97..122) | Get-Random -Count 32 | % {[char]$_})`)
and run in `mariadb.exe -u root -p`:

```sql
CREATE DATABASE dw4online CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- The service: data access plus the DDL its migrations need. No DROP, no access to other databases.
CREATE USER 'dw4online'@'localhost' IDENTIFIED BY '<service password>';
CREATE USER 'dw4online'@'127.0.0.1' IDENTIFIED BY '<service password>';
GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, ALTER, INDEX, REFERENCES ON dw4online.* TO 'dw4online'@'localhost';
GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, ALTER, INDEX, REFERENCES ON dw4online.* TO 'dw4online'@'127.0.0.1';

-- The nightly backup: read only. backup.cnf connects to 127.0.0.1 (TCP); a name that resolves to localhost is a
-- different account in MariaDB, so both exist.
CREATE USER 'dw4backup'@'localhost' IDENTIFIED BY '<backup password>';
CREATE USER 'dw4backup'@'127.0.0.1' IDENTIFIED BY '<backup password>';
GRANT SELECT, SHOW VIEW, TRIGGER, EVENT, LOCK TABLES ON dw4online.* TO 'dw4backup'@'localhost';
GRANT SELECT, SHOW VIEW, TRIGGER, EVENT, LOCK TABLES ON dw4online.* TO 'dw4backup'@'127.0.0.1';
```

Check both logins over TCP before you go on (each asks for its password):

```powershell
mariadb.exe -h 127.0.0.1 -P 3306 -u dw4online -p -e "SELECT CURRENT_USER()"   # dw4online@127.0.0.1
mariadb.exe -h 127.0.0.1 -P 3306 -u dw4backup -p -e "SELECT CURRENT_USER()"   # dw4backup@127.0.0.1
```

## 3. Publish on the development PC

```powershell
cd server\DW4Online
powershell -ExecutionPolicy Bypass -File deploy\publish.ps1
```

This runs the tests, publishes `DW4Online.Server` self-contained for `win-x64` (Release) and writes
`dist\DW4Online-<version>.zip` at the repository root (gitignored). Use a GA .NET SDK for release builds (the .NET 10
SDK; the development PC currently only has the 11.0 RC SDK). The publish restores the .NET 10 runtime packs
(Microsoft.NETCore.App / Microsoft.AspNetCore.App runtime for win-x64) from nuget.org on the first run.
`appsettings.Development.json` and `appsettings.Local.json` are never part of the publish. The script builds without
the shared compiler server (`--disable-build-servers`): the 11.0 RC SDK's server generated broken Razor code on the
second build of `DW4Online.Web` in a row (see the README) and stopped the publish at the integration tests.

What an RC SDK build means: the compiler and build tools are a release candidate, the program itself is not. The zip
carries the GA .NET 10 runtime it was published with (`DW4Online.Server.runtimeconfig.json` names it, 10.0.14 for the
first release) and the server needs no .NET installation. That runtime does not update itself (Windows Update does not
patch a self-contained app): a .NET security release means a new publish and an update (section 8).

## 4. Install on the server

1. Copy the zip to the server and extract it to `D:\Dekirai\Servers\DW4Online\app`.
2. Create `D:\Dekirai\Servers\DW4Online\config\appsettings.Production.json` from
   [appsettings.Production.example.json](appsettings.Production.example.json): the connection string with the service
   password, a relay ticket key (`app\DW4Online.Server.exe gen-relay-key`), the IP address (the TLS settings of
   section 6.1), and the `Web` section: `RequireHttps` true, `DataProtectionKeysDirectory` = the `keys` folder, and your
   name, postal address and a contact in `OperatorName` / `OperatorAddress` / `OperatorContact` (the privacy page names
   the controller, GDPR Art. 13). The relay is on by default (`Relay:Enabled`); it needs `ListenAddress` 0.0.0.0 and
   the ticket key, and `Service:RelayHost` tells clients where it is. This file holds the secrets: never copy it into
   Git or the app folder.
3. Register the service, folder permissions and firewall rules:

   ```powershell
   powershell -ExecutionPolicy Bypass -File D:\Dekirai\Servers\DW4Online\app\deploy\install-service.ps1 -DryRun   # shows the commands
   powershell -ExecutionPolicy Bypass -File D:\Dekirai\Servers\DW4Online\app\deploy\install-service.ps1
   ```

   What it does (all reversible, no restart):
   - First it checks that the `MariaDB` service exists and that its port (3306, `-DatabasePort`) listens on 127.0.0.1
     only; it stops before changing anything otherwise.
   - `New-Service DW4Online` with the path `"...\app\DW4Online.Server.exe" --service --settings "...\config\appsettings.Production.json"`,
     then `sc.exe config DW4Online start= delayed-auto obj= "NT SERVICE\DW4Online"` (virtual account, no password),
     restart-on-failure actions (5 s, 30 s, 60 s) and `sc.exe config DW4Online depend= MariaDB` (`-DatabaseService`):
     after a reboot Windows starts MariaDB first, and the service itself waits up to 20 s for the database to answer
     before its start fails (then the restart actions try again). Stopping MariaDB now stops DW4Online first; start
     both again with `Start-Service MariaDB, DW4Online`.
   - ACLs: every folder drops the permissions inherited from the drive and keeps only Administrators and SYSTEM (full
     control) plus the service's rights: the root and `app` read and execute for `NT SERVICE\DW4Online` (a release
     folder made later in the root inherits that), `config` read, `logs`, `acme` and `keys` modify, `backups` nothing
     (only Administrators and SYSTEM: the dumps hold password hashes, e-mail addresses and cloud saves). Check with
     `icacls D:\Dekirai\Servers\DW4Online\backups`.
   - Inbound firewall rules for TCP 443 and 52041 (and 80 with `-OpenPort80`), limited to the service executable.
4. If the hoster has a network firewall in front of the server (for example a Hetzner firewall in the Robot / Cloud
   console), open TCP 443, 52041 (and 80 for certificate validation) there too.
5. Check that nothing else listens on 443 / 80: `Get-NetTCPConnection -State Listen -LocalPort 80,443`.
6. Create the TLS certificate for the IP address and note its pins (section 6.1, `new-selfsigned-cert.ps1`).

Kestrel binds the ports itself; on Windows a non-admin account may bind ports below 1024 with sockets (no URL ACL is
needed because http.sys is not used).

## 5. First start

```powershell
Start-Service DW4Online
Get-Content D:\Dekirai\Servers\DW4Online\logs\dw4online-*.log -Tail 30
curl.exe -k https://88.198.52.221/health      # {"status":"ok","schema":6}; -k: the certificate is self-signed (section 6.1)
D:\Dekirai\Servers\DW4Online\app\DW4Online.Server.exe tls-pin --host 88.198.52.221   # the served certificate and its pin
```

The first start creates the tables (startup migrator). Register your own account on the website
(`https://<host>/register`, keep the recovery code) or in the game, then make it the admin from an elevated console:

```powershell
cd D:\Dekirai\Servers\DW4Online\app
.\DW4Online.Server.exe set-role <your name> admin --settings ..\config\appsettings.Production.json
```

Log in on the website again: the navigation now shows "Admin" (account search, locks, bans, temporary passwords,
unlock review, closing rooms, account deletion on request). Read the privacy page once (`/privacy`) and adjust the text
in `src/DW4Online.Web/Pages/Privacy.cshtml` if your setup differs (for example other log retention).

## 6. TLS: the IP address now, the domain later

### 6.1 Now: https://88.198.52.221 with a pinned self-signed certificate

There is no domain yet, so no certificate authority vouches for the address. The game client therefore does not ask
the Windows root store: it pins the server's public key, the SHA-256 of the certificate's SubjectPublicKeyInfo ("SPKI
pin"), and refuses any other key before it sends a single byte of a request (the end-to-end test
`Pinned_self_signed_https_at_an_ip_address_below_a_path_base` checks exactly that with the WinHTTP test client).

1. After `install-service.ps1` (section 4; the service account must exist), in an elevated PowerShell:

   ```powershell
   cd D:\Dekirai\Servers\DW4Online\app\deploy
   powershell -ExecutionPolicy Bypass -File new-selfsigned-cert.ps1 -SparePfx E:\dw4online-spare.pfx
   ```

   `E:` stands for a USB stick or any place that is not this server. [new-selfsigned-cert.ps1](new-selfsigned-cert.ps1)
   - creates a certificate for `IP 88.198.52.221` (RSA 3072, server authentication, valid 10 years) in
     `LocalMachine\My` whose key cannot be exported: it never leaves the server;
   - gives `NT SERVICE\DW4Online` read access to that key (the service runs the TLS handshakes);
   - with `-SparePfx`, creates a second, spare key (in the current user's store, where the service never looks),
     writes it to that PFX file (it asks for a password twice) and removes it from the server again;
   - prints both pins, checked against the service's own `DW4Online.Server.exe tls-pin`, and the settings of step 2.

   It refuses to make a second certificate for an address that already has a valid one: a new key means a new pin,
   and every client without that pin stops connecting (`-Force` overrides this; only use it when a client update with
   the new pin is out). `-DryRun` shows what it would do. A spare can follow later without a new key:
   `new-selfsigned-cert.ps1 -SparePfx E:\dw4online-spare.pfx -SpareOnly` keeps the certificate in use and prints its
   pin and the spare's. `DW4Online.Server.exe tls-pin --file E:\dw4online-spare.pfx --password-env DW4_PFX` prints a
   PFX's pin with the password from the environment variable `DW4_PFX` (`$env:DW4_PFX = Read-Host` first).
2. In `appsettings.Production.json` (the example file already holds these values):

   ```json
   "Kestrel": { "Endpoints": { "Https": { "Url": "https://0.0.0.0:443" } } },
   "Tls": { "StoreHostName": "88.198.52.221" },
   "Service": { "RelayHost": "88.198.52.221" },
   "Web": { "RequireHttps": true, "Hsts": false }
   ```

   then `Restart-Service DW4Online` (or wait: the service re-reads the store every 10 minutes). Its log names the
   certificate and its pin (`TLS certificate for 88.198.52.221: <thumbprint>, valid until ..., SPKI pin ...`).
3. Both pins go into the game client's online settings (the first one is in use, the second is the spare). Keep the
   spare PFX offline and its password somewhere else (for example in your password manager).
4. Check: `.\DW4Online.Server.exe tls-pin --host 88.198.52.221` (in the app folder) prints the certificate the service
   serves and its pin; from another PC `curl.exe -k https://88.198.52.221/health` answers `{"status":"ok",...}`.

What players notice: browsers warn about the website's certificate (it is self-signed). Whoever wants to check it
before accepting compares the browser's SHA-256 fingerprint with the `Certificate fingerprint (SHA-256)` line of
`tls-pin`; you can publish that line where your players read news. HSTS stays off for the IP address (browsers ignore it
there anyway). The game itself shows no warning: it checks the pin.

If the server or its key is lost: install the spare on the new server with
`new-selfsigned-cert.ps1 -ImportPfx E:\dw4online-spare.pfx` (it asks for the password, installs the key without
export permission and grants the service read access). Every client keeps working because it pins the spare too; ship
a client update with a new spare pin afterwards (`new-selfsigned-cert.ps1 -Force -SparePfx ...` on the new server
makes one, but its own new key is then a third pin: the update must carry it before you switch to it). The certificate
lasts 10 years; by then the service runs under the domain.

### 6.2 Later: https://dekirai.crygod.de/dw4online

The service already works below a path: `"Service": { "PathBase": "/dw4online" }` puts the website and the API under
`/dw4online` (links, redirects, forms, `Location` headers and the session cookie stay below it; `PathBaseTests` and
the pinned end-to-end test check it). Requests without the prefix keep working at the root, so clients that still use
`https://88.198.52.221` go on working during the move.

**A path keeps the cookie away from other sites, not their scripts.** Browsers isolate by origin (scheme, host name,
port), never by path: any page of another site on `dekirai.crygod.de` runs in the same origin as
`dekirai.crygod.de/dw4online`, can send requests with the visitor's DW4Online cookie and read the answers (anti-forgery
tokens included). A cross-site-scripting flaw in any other site on that name is then an account takeover in DW4Online.
If other content lives on that host name (case 2), prefer a host name of its own for DW4Online
(`dw4.crygod.de`, an A record to 88.198.52.221) with `PathBase` empty or kept.

**Case 1: DW4Online keeps port 443 on this server.** The domain's A record points to 88.198.52.221 and nothing else
on this machine serves that name. Get a Let's Encrypt certificate for `dekirai.crygod.de` into `LocalMachine\My` (6.3)
and set:

```json
"Tls": { "StoreHostName": "88.198.52.221", "AdditionalHostNames": [ "dekirai.crygod.de" ] },
"Service": { "PathBase": "/dw4online", "RelayHost": "88.198.52.221" }
```

A connection that names the domain (SNI) gets the Let's Encrypt certificate and is validated normally; a connection to
the IP address names no host and keeps getting the pinned certificate. New clients use
`https://dekirai.crygod.de/dw4online`, old ones keep working until they update. (The domain's root then also shows
the DW4Online website; if other content should live there, see case 2.)

**Case 2: another web server serves dekirai.crygod.de on this machine** (IIS, nginx, Caddy). It forwards
`/dw4online/...` with the prefix kept to `http://127.0.0.1:5280` and sends `X-Forwarded-For` and `X-Forwarded-Proto`;
DW4Online then listens only there (remove its `Https` endpoint and `Tls:StoreHostName`):

```json
"Kestrel": { "Endpoints": { "Http": { "Url": "http://127.0.0.1:5280" } } },
"Service": { "PathBase": "/dw4online", "BehindProxy": true, "RelayHost": "88.198.52.221" }
```

`BehindProxy` believes those headers only from a proxy on this machine. Port 443 then belongs to that web server, so
it must also serve the pinned IP certificate to connections without a name (IIS: an https binding on 88.198.52.221
port 443 without host name, using the same certificate from `LocalMachine\My`, no export needed) and forward them to
DW4Online as well, or the clients that still pin the IP certificate stop working. The relay keeps its own port 52041
in both cases.

### 6.3 Let's Encrypt certificates with win-acme (the domain)

Use win-acme (<https://www.win-acme.com/>) to get a Let's Encrypt certificate into `LocalMachine\My`:

- A1, win-acme answers the http-01 challenge itself (its self-hosting validation uses port 80 through http.sys).
  Kestrel then does not listen on port 80; open port 80 in the firewall for the validation.
- A2, Kestrel answers the challenge: set `Tls:AcmeChallengeDirectory` to `D:\Dekirai\Servers\DW4Online\acme`, add an
  HTTP endpoint on port 80 to Kestrel and choose win-acme's filesystem validation into that folder.

In both cases choose the certificate store `My` (LocalMachine) and give `NT SERVICE\DW4Online` read access to the
private key (win-acme's certificate store plugin can set key ACLs, check `wacs.exe --help` for the options of your
version; otherwise certlm.msc -> the certificate -> All Tasks -> Manage Private Keys). The service re-reads the store
every 10 minutes and uses the newest valid certificate for each name, so win-acme's scheduled renewals take effect
**without** restarting the service (a restart would end running co-op sessions). Clients of the domain validate it
against the Windows root store (WinHTTP + Schannel) and do not pin it, so renewals with new keys are fine.

Not chosen: a Let's Encrypt certificate for the IP address (6-day certificates through the ACME `shortlived` profile;
the client would have to pin a key that changes with every renewal unless the key is reused) and plain HTTP (passwords
and tokens would travel unencrypted).

## 7. Backups

[backup-db.ps1](backup-db.ps1) runs `mariadb-dump --single-transaction --quick --hex-blob --routines --events
--no-tablespaces dw4online`, compresses the dump (gzip) into `backups\daily`, keeps 14 daily and 8 weekly (Sunday)
copies and can copy each new file to a second folder (`-CopyTo`, for example a mounted share). The credentials come
from an option file readable only by Administrators and SYSTEM:

```ini
# D:\Dekirai\Servers\DW4Online\config\backup.cnf
[client]
user=dw4backup
password=<backup password>
host=127.0.0.1
port=3306
```

Schedule it nightly as SYSTEM (elevated PowerShell):

```powershell
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument '-NoProfile -ExecutionPolicy Bypass -File D:\Dekirai\Servers\DW4Online\app\deploy\backup-db.ps1'
$trigger = New-ScheduledTaskTrigger -Daily -At 03:30
Register-ScheduledTask -TaskName 'DW4Online DB backup' -Action $action -Trigger $trigger -User 'SYSTEM' -RunLevel Highest
```

Copy the backups off the server as well (for example to your own FTPS server with WinSCP scripting, or a storage box).
Also keep a copy of `config\appsettings.Production.json` (it holds the database password and the relay key), stored
encrypted. The `keys` folder needs no backup: its keys are DPAPI-encrypted for this machine's service account and only
protect anti-forgery tokens; if it is lost, open website forms simply have to be reloaded.

Restore drill (once, and after schema changes):

```powershell
mariadb.exe -u root -p -e "CREATE DATABASE dw4online_restore CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci"
& 'C:\Program Files\7-Zip\7z.exe' e -so backups\daily\dw4online-<date>.sql.gz | mariadb.exe -u root -p dw4online_restore
```

(Any gzip tool works; the dump is plain SQL.) Point a test instance's connection string at `dw4online_restore` and
check `/health`, then drop the database.

## 8. Updates and rollback

1. Announce the update (set `Service:Motd`, optionally `Service:Maintenance` true so no new rooms open), pick a quiet
   time: stopping the service ends running co-op sessions and rooms (they close with `server_restart` at the next start).
2. `Stop-Service DW4Online`
3. Run `deploy\backup-db.ps1` once by hand (migrations commit DDL implicitly; a failed one leaves the schema between
   versions and the backup is the way back).
4. `Rename-Item app app.previous` (remove the older `app.previous` first), extract the new zip to `app`.
5. `Start-Service DW4Online`; the startup migrator applies new migrations; check the log and `/health`.

Rollback: stop the service, swap `app` and `app.previous` back; if the update applied a migration, restore the backup
from step 3 into `dw4online` first (the old build refuses a database that is newer than itself).

MariaDB updates and restarts: DW4Online depends on the MariaDB service (`sc.exe config DW4Online depend= MariaDB`), so
`Stop-Service MariaDB` and `Restart-Service MariaDB` refuse while DW4Online runs (without `-Force`, which stops
DW4Online too), and the MariaDB MSI cannot replace a running server. Stop DW4Online first:
`Stop-Service DW4Online; Restart-Service MariaDB; Start-Service DW4Online` (for an MSI update: stop both, run the MSI,
start MariaDB, then DW4Online). The same holds for a my.ini change (section 1).

## 9. Operating notes

- Logs: `logs\dw4online-yyyyMMdd.log` (UTC, 30 days) when `Logging:File:Directory` is set; crashes also appear in the
  Windows Application event log (source ".NET Runtime").
- Staff tools without the website: `DW4Online.Server.exe reset-password <user> --settings ...`, the `/api/v1/admin/...`
  endpoints with an admin token.
- Privacy (GDPR, the server is in Germany): the database holds user names, optional e-mail addresses, IP addresses in
  `sessions.created_ip` and `audit_log.ip` (audit rows are deleted after 180 days, old sessions after 30 days). The
  website has a privacy page (`/privacy`, fill in the `Web:Operator*` settings) and account deletion (by the player in
  the account settings, or by an admin on request on the admin page); deleting removes the personal records at once
  and leaves an anonymous `deleted_<id>` row. Backups keep deleted data until they roll over (14 daily + 8 weekly).
- Ports 80 / 443 must not collide with other software on the server; the Pangya web port 88 is not used.
