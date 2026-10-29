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
| ACME challenge folder (option A2) | `D:\Dekirai\Servers\DW4Online\acme` |
| Website Data Protection keys | `D:\Dekirai\Servers\DW4Online\keys` (DPAPI-encrypted for the service account) |
| Service | `DW4Online`, virtual account `NT SERVICE\DW4Online`, start delayed-auto |
| Ports | 443 TCP (HTTPS: API and website), 52041 TCP (relay), 80 TCP only for certificate validation; MariaDB 3306 on 127.0.0.1 only |

The scripts in this folder are written for these steps but were **not run** on the server: read them, then run them
yourself in an elevated PowerShell.

## 1. MariaDB 11.4 LTS

1. Download the current **MariaDB 11.4.x** Windows MSI (x64) from <https://mariadb.org/download/> (11.4 is an LTS
   line, community support to May 2029).
2. Check that port 3306 is free (another MySQL / MariaDB, for example a XAMPP or a game server's database, may hold it):

   ```powershell
   Get-NetTCPConnection -State Listen -LocalPort 3306 -ErrorAction SilentlyContinue   # no output = free
   ```

   If something listens there, leave it alone and give MariaDB another port in the installer (for example 3307); then
   use that port everywhere below: `Port=3307` in the connection string, `port=3307` in `backup.cnf` (step 7) and
   `install-service.ps1 -DatabasePort 3307` (step 4).
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
`appsettings.Development.json` and `appsettings.Local.json` are never part of the publish.

## 4. Install on the server

1. Copy the zip to the server and extract it to `D:\Dekirai\Servers\DW4Online\app`.
2. Create `D:\Dekirai\Servers\DW4Online\config\appsettings.Production.json` from
   [appsettings.Production.example.json](appsettings.Production.example.json): the connection string with the service
   password, a relay ticket key (`app\DW4Online.Server.exe gen-relay-key`), the host name or IP, the TLS option of
   step 6, and the `Web` section: `RequireHttps` true, `DataProtectionKeysDirectory` = the `keys` folder, and your
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

Kestrel binds the ports itself; on Windows a non-admin account may bind ports below 1024 with sockets (no URL ACL is
needed because http.sys is not used).

## 5. First start

```powershell
Start-Service DW4Online
Get-Content D:\Dekirai\Servers\DW4Online\logs\dw4online-*.log -Tail 30
Invoke-RestMethod https://<host>/health        # {"status":"ok","schema":5}
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

## 6. TLS options

The game client validates certificates against the Windows root store (WinHTTP + Schannel), so a publicly trusted
certificate is the normal case.

**A. With a domain (recommended).** Create an A record (for example `dw4.<domain>`) pointing to 88.198.52.221.
Use win-acme (<https://www.win-acme.com/>) to get a Let's Encrypt certificate into `LocalMachine\My`:

- A1, win-acme answers the http-01 challenge itself (its self-hosting validation uses port 80 through http.sys).
  Kestrel then does not listen on port 80; open port 80 in the firewall for the validation.
- A2, Kestrel answers the challenge: set `Tls:AcmeChallengeDirectory` to `D:\Dekirai\Servers\DW4Online\acme`, add an
  HTTP endpoint on port 80 to Kestrel and choose win-acme's filesystem validation into that folder.

In both cases choose the certificate store `My` (LocalMachine) and give `NT SERVICE\DW4Online` read access to the
private key (win-acme's certificate store plugin can set key ACLs, check `wacs.exe --help` for the options of your
version; otherwise certlm.msc -> the certificate -> All Tasks -> Manage Private Keys). Then set:

```json
"Kestrel": { "Endpoints": { "Https": { "Url": "https://0.0.0.0:443" } } },
"Tls": { "StoreHostName": "dw4.<domain>" },
"Service": { "RelayHost": "dw4.<domain>" }
```

The service re-reads the store every 10 minutes and uses the newest valid certificate for that name, so win-acme's
scheduled renewals take effect **without** restarting the service (a restart would end running co-op sessions).

**B. Without a domain, an IP certificate.** Let's Encrypt issues certificates for IP addresses since January 2026, but
only as 6-day certificates through the ACME `shortlived` profile; whether win-acme supports that profile is not
verified. With a client that does, the same store setup works with `"Tls": { "StoreHostName": "88.198.52.221" }`
(the selector matches IP addresses in the certificate's SAN). Renewal every few days must be automatic.

**C. Without a domain, a self-signed certificate.** For a closed test group:

```powershell
New-SelfSignedCertificate -Subject "CN=88.198.52.221" -TextExtension @("2.5.29.17={text}IPAddress=88.198.52.221") `
  -CertStoreLocation Cert:\LocalMachine\My -NotAfter (Get-Date).AddYears(2) -KeyExportPolicy NonExportable
```

Browsers warn on the website, and the game would have to pin the certificate's SHA-256 (client work, not done yet).

**D. Plain HTTP** on a non-standard port is only for a short closed test: passwords and tokens travel unencrypted.

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
