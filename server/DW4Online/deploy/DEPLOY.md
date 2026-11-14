# Deploying DW4Online on the Windows Server

The target is the user's Windows Server 2025 (public address 88.198.52.221, no domain yet). Everything here runs
**without a Windows restart**: the database is a MariaDB service (a new MariaDB 11.4 MSI install, or an existing XAMPP
MariaDB made a service), the service is a self-contained publish (no .NET runtime install), Kestrel serves HTTPS
itself (no IIS). SQL Server Express and the Pangya servers (web port 88) stay untouched.

| What | Where |
| --- | --- |
| Service root | `D:\Dekirai\Servers\DW4Online` |
| Program (replaced on updates) | `D:\Dekirai\Servers\DW4Online\app` (previous version kept in `app.previous`) |
| Secrets and settings | `D:\Dekirai\Servers\DW4Online\config\appsettings.Production.json` |
| Logs | `D:\Dekirai\Servers\DW4Online\logs` |
| Database backups | `D:\Dekirai\Servers\DW4Online\backups` |
| ACME challenge folder (later, section 6.3 A2) | `D:\Dekirai\Servers\DW4Online\acme` |
| Website Data Protection keys | `D:\Dekirai\Servers\DW4Online\keys` (DPAPI-encrypted for the service account) |
| Service | `DW4Online`, virtual account `NT SERVICE\DW4Online`, start delayed-auto, depends on the database service |
| TLS certificate (now) | self-signed for `IP 88.198.52.221` in `LocalMachine\My`, pinned by the game client (sections 6.1, 6.4); target: a key made once and kept offline (the server installed on 31.10 has a server-made key, see below) |
| Kept once, never regenerated | the two TLS keys (in use, spare; offline), `Relay:TicketKey` (in the settings file and its encrypted copy) |
| Ports | 443 TCP (HTTPS: API and website; another port such as 8443 when 443 is taken, section 4.1), 52041 TCP (relay), 80 TCP only for a later domain's certificate validation; MariaDB 3306 on 127.0.0.1 only; plain HTTP on 127.0.0.1:5280 for checks on the server itself (`appsettings.json`'s endpoint, never reachable from outside) |

**The server as installed on 31.10.2026 (release 0.1.0)** uses the variants this document describes next to the
defaults: the existing **XAMPP MariaDB 10.4.32** (`C:\Users\Dekirai\xampp\mysql`) as the Windows service `mysql`
(section 1.1), **root as the only database login** (section 2.1), **HTTPS on port 8443** because a Traefik reverse
proxy owns 443 (section 4.1); relay 52041, loopback check endpoint 127.0.0.1:5280. Its TLS certificate was made the
earlier way, on a key the server made itself, with a spare PFX kept offline (section 6.1, "Without kept keys"); the
game client's pins are that certificate's and the spare's. Its first `Start-Service` failed:
release 0.1.0 ran the migrations before it connected to the Service Control Manager, and the log stopped after
"Applying migration 0001 initial" (twice: two start attempts). Migration 0001 takes 0.2 s on the same MariaDB build
elsewhere, so it did not run slowly: it **hung** in one of its `CREATE TABLE` statements, the SCM ended the process
after its 30 s, and every later start refused the half-migrated database. The likely cause is still configured on
that server: MariaDB before 10.6 on Windows with native asynchronous I/O (`innodb_use_native_aio`, on by default),
which XAMPP's my.ini leaves on. In the review's rehearsal on a private mysqld 10.4.32 with XAMPP's my.ini plus the
two lines the server got, three of four integration rounds went wrong: `SHUTDOWN` hung after a full run, mysqld died
in the middle of the migrator tests, and `CREATE TABLE speedrun_times` of 0001 stayed "Killed / Creating table" for
over 600 s and kept the migration lock; with `innodb_use_native_aio=0` every round passed (the setting
`scripts/local-db.ps1` has used since `DROP DATABASE` hung there). The hang does not come every time (four later
rounds with native AIO passed), which fits a server where the same migration then worked from the console. Section
1.1 therefore adds that line, a one-time change for the installed server (section 8). The way out on 31.10 was
`DROP DATABASE` / `CREATE DATABASE` and `DW4Online.Server.exe migrate` in a console. Later releases connect to the
SCM first and migrate while they report "start pending" (section 9), every refusal names its way out, and `migrate`
warns about native AIO on such a MariaDB; run `migrate` in a console before a start anyway (sections 5 and 8).

The scripts in this folder are meant to be read and then run by you: `publish.ps1`, `tls-key.ps1` and
`print-pins.ps1` on the development PC (sections 3 and 6.1), the others in an elevated PowerShell on the server. (They
were tested on the development PC as far as that is possible without changing its services, firewall or certificate
stores: `-DryRun`, the read-only checks, the backup and restore against a private XAMPP MariaDB 10.4, and
`new-selfsigned-cert.ps1` against a user certificate store, see the README's tests.)

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

### 1.1 Variant: an existing XAMPP MariaDB 10.4 as the database service

A XAMPP that already runs on the server brings MariaDB 10.4 (XAMPP 8.x: 10.4.32). DW4Online runs on it: the schema
avoids the types 10.4 lacks. Migrations 1 to 7 and their integration tests passed, round after round, against a
MariaDB 10.4.32 that runs with XAMPP's own `my.ini` plus the **three** lines of step 2 (so with its non-strict
`sql_mode`, `collation-server=utf8mb4_general_ci`, a 16 MB buffer pool and a 5 MB redo log; about 0.25 s on the
development PC). All migrations 1 to 10 and all 87 integration tests also pass on the private 10.4.32 mysqld of
`scripts/local-db.ps1` (`innodb_use_native_aio=0`; 1 to 10 in about 0.25 s), there also with XAMPP's `sql_mode`
and `collation_server` set at run time; XAMPP's buffer pool and redo log sizes could not be set that way. Without the third line, `innodb_use_native_aio=0`, the runs do
not all pass: MariaDB before 10.6 on Windows uses native asynchronous I/O by default, and with it DDL (`CREATE TABLE`, `DROP DATABASE`) can hang for good
in an InnoDB latch wait (the top of this document; the server's first start). A hung statement stays "Killed" in the
process list and keeps its locks, and `Restart-Service mysql` can hang behind it. What the line costs: InnoDB then does
its file I/O with its own threads instead of Windows' asynchronous I/O, which can make heavy write loads somewhat
slower for every database on that MariaDB (DW4Online's load is far too small to notice; for the others it depends on
how much they write). It is your decision because those databases share it; the alternative is a MariaDB 11.4 of its
own for DW4Online (section 1, then a backup and restore), where the line is not needed. `DW4Online.Server.exe
migrate` (and the service log) warn with `WARNING: MariaDB 10.4.32-MariaDB on Windows runs with
innodb_use_native_aio=ON ...` while the line is missing.

The other cost: MariaDB 10.4 reached its end of life in June 2024 and gets no security fixes any more. That is
acceptable only because it answers on 127.0.0.1 alone (step 3); move to a supported MariaDB (section 1, then a backup
and restore) when convenient.

1. Make XAMPP's MySQL a Windows service: start the **XAMPP Control Panel as administrator**, stop MySQL there if it
   runs, tick the **"Svc"** box in the MySQL row and confirm. The Control Panel installs the service **`mysql`**
   (`mysqld.exe --defaults-file=<XAMPP>\mysql\bin\my.ini mysql`, automatic start, LocalSystem). Start it once with the
   Control Panel's Start button or `Start-Service mysql`. From now on start and stop it as a service; with DW4Online
   installed, stop DW4Online first (it depends on `mysql`).
2. Add three lines at the **end of the `[mysqld]` section** of `<XAMPP>\mysql\bin\my.ini` (just above `[mysqldump]`;
   a later line wins over XAMPP's own `max_allowed_packet=1M` further up) and leave the rest as it is, since other
   databases on this MariaDB rely on it:

   ```ini
   bind-address=127.0.0.1
   max_allowed_packet=16M
   innodb_use_native_aio=0
   ```

3. `Restart-Service mysql` (other databases on it are unavailable for those seconds), then check that it listens on
   the loopback address only and runs without native AIO (`mysql.exe` asks for the root password; HeidiSQL:
   `SHOW VARIABLES LIKE 'innodb_use_native_aio'` shows `OFF`):

   ```powershell
   Get-NetTCPConnection -State Listen -LocalPort 3306 | Select-Object LocalAddress, OwningProcess   # only 127.0.0.1
   & '<XAMPP>\mysql\bin\mysql.exe' -h 127.0.0.1 -u root -p -N -e "SELECT @@innodb_use_native_aio"   # 0
   ```

4. XAMPP's root accounts (usually `root@localhost`, `root@127.0.0.1` and `root@::1`) have **no password** out of
   the box. Give every one of them the same strong password (HeidiSQL: Tools -> User manager, each root entry; or
   `ALTER USER 'root'@'localhost' IDENTIFIED BY '...'` and the same for the others; `SELECT user, host FROM mysql.user`
   lists them). XAMPP's phpMyAdmin then needs that password in `<XAMPP>\phpMyAdmin\config.inc.php` or stays unused;
   HeidiSQL asks for it.
5. Everywhere below: `-DatabaseService mysql` for `install-service.ps1`, `-MariaDbBin <XAMPP>\mysql\bin -DumpExe
   mysqldump.exe` for `backup-db.ps1`, `-MariaDbBin <XAMPP>\mysql\bin -ClientExe mysql.exe` for `restore-db.ps1`
   (XAMPP's 10.4 has `mysqldump.exe` and `mysql.exe`, no `mariadb-dump.exe` / `mariadb.exe`; both scripts also take the
   old names by themselves when the folder has no new ones), and `<XAMPP>\mysql\bin\mysql.exe` (or HeidiSQL) wherever
   a command below says `mariadb.exe`. No firewall rule exists for XAMPP's MySQL by default; if one was added, disable
   it as in section 1 step 6.

## 2. Database and users

Generate two long random passwords (for example `-join ((48..57)+(65..90)+(97..122) | Get-Random -Count 32 | % {[char]$_})`)
and run in `mariadb.exe -u root -p` (if PowerShell does not find `mariadb.exe`, add the client folder for this
session: `$env:PATH += ';C:\Program Files\MariaDB 11.4\bin'`; or use HeidiSQL's query tab as root):

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
mariadb.exe -h 127.0.0.1 -P 3306 -u dw4online -p -e "SELECT CURRENT_USER()"   # dw4online@localhost or dw4online@127.0.0.1
mariadb.exe -h 127.0.0.1 -P 3306 -u dw4backup -p -e "SELECT CURRENT_USER()"   # dw4backup@localhost or dw4backup@127.0.0.1
```

Which of the two accounts answers depends on MariaDB's name resolution (127.0.0.1 resolves to `localhost` unless
`skip-name-resolve` is set); both exist with the same password and rights, so either line is right. "Access denied"
is not: check the password and that both `CREATE USER` lines ran.

### 2.1 Variant: root as the only login

The operator may decide to run the service and the backup as MariaDB's `root` instead of the two users above (the
server installed on 31.10 does). Then only the database is created (as root, for example in HeidiSQL's query tab):

```sql
CREATE DATABASE dw4online CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
```

and both files name root: `User ID=root;Password=<root password>` in the connection string (section 4),
`user=root` / `password=<root password>` in `backup.cnf` (section 7). Use a root password of letters and digits only
(a `;` ends the connection string's value, a `"` the JSON string).

What it costs, plainly: the service then holds every right on the whole MariaDB instance, not only on `dw4online`. A
flaw in DW4Online, a leaked settings file or code running as `NT SERVICE\DW4Online` would reach every other database
on that instance (Pangya's, for example), the `mysql` system schema with its accounts and, through the `FILE`
privilege, files on the disk with the rights of the MariaDB service (XAMPP's runs as LocalSystem and leaves
`secure_file_priv` empty). The root password lies in two files instead of none, and the service could `DROP` its own
tables. Why it is acceptable here: the MariaDB answers on 127.0.0.1 only, so nothing outside the server can try that
password; both files are readable only by Administrators and SYSTEM (and the settings file by the service account);
DW4Online sends only parameterised SQL (Dapper), no SQL built from player input; and one person administers the
server. If the server ever hosts other people's services, or the MariaDB becomes reachable from the network, switch to
the two users above (create them, put them into both files, restart the service).

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
   password (root and its password in the root-only variant, section 2.1; the database's port), the relay ticket key
   (below), the IP address (the TLS settings of section 6.1), the HTTPS port (`Kestrel:Endpoints:Https:Url`, section
   4.1 when 443 is taken), and the `Web` section: `RequireHttps` true, `DataProtectionKeysDirectory` = the `keys`
   folder, and your name, postal address and a contact in `OperatorName` / `OperatorAddress` / `OperatorContact` (the
   privacy page names the controller, GDPR Art. 13). The relay is on by default (`Relay:Enabled`); it needs
   `ListenAddress` 0.0.0.0 and the ticket key, and `Service:RelayHost` tells clients where it is. This file holds the
   secrets: never copy it into Git or the app folder.

   **The relay ticket key is made once and kept.** Make it with `app\DW4Online.Server.exe gen-relay-key` (32 random
   bytes, base64) the first time only, put it into `Relay:TicketKey` and keep it with the encrypted copy of this file
   (section 7). Updates, restores and a moved server keep the same key: it signs the one-time relay tickets (a new key
   only breaks the tickets of the last 60 seconds) and, through HKDF, keys the security log's tags of unknown user names
   (`audit-tag`): with a new key the tags written before can no longer be matched, and account deletions leave them
   behind. Without the key a production start fails on purpose (a temporary key only in Development).
3. Register the service, folder permissions and firewall rules, in an **elevated** PowerShell (right-click the Start
   button, *Terminal (Admin)*, or PowerShell with *Run as administrator*; its title bar says Administrator). In a
   normal PowerShell the script stops with "This script needs administrator rights". The second line is for the MSI
   MariaDB with HTTPS on 443, the third for the XAMPP variant with HTTPS on 8443 (sections 1.1 and 4.1):

   ```powershell
   powershell -ExecutionPolicy Bypass -File D:\Dekirai\Servers\DW4Online\app\deploy\install-service.ps1 -DryRun   # shows the commands
   powershell -ExecutionPolicy Bypass -File D:\Dekirai\Servers\DW4Online\app\deploy\install-service.ps1
   powershell -ExecutionPolicy Bypass -File D:\Dekirai\Servers\DW4Online\app\deploy\install-service.ps1 -DatabaseService mysql -DatabasePort 3306 -HttpsPort 8443
   ```

   What it does (all reversible, no restart):
   - First it checks, and stops before changing anything if one check fails: the database service
     (`-DatabaseService`: `MariaDB`, or `mysql` for XAMPP) exists and its port (`-DatabasePort`, 3306) listens on
     127.0.0.1 only; the connection string names that port; the settings' HTTPS URL uses `-HttpsPort` and `Relay:Port`
     is `-RelayPort` (the firewall rules open exactly these); nothing else listens on the HTTPS, relay and loopback
     HTTP ports (a Traefik on 443 is named with its process and the hint to section 4.1).
   - `New-Service DW4Online` with the path `"...\app\DW4Online.Server.exe" --service --settings "...\config\appsettings.Production.json"`,
     then `sc.exe config DW4Online start= delayed-auto obj= "NT SERVICE\DW4Online"` (virtual account, no password),
     restart-on-failure actions (5 s, 30 s, 60 s) and `sc.exe config DW4Online depend= <database service>`: after a
     reboot Windows starts the database first, and the service itself waits up to 60 s
     (`Database:StartupWaitSeconds`) for the database to answer before its start fails (then the restart actions try
     again). Stopping the database service now stops DW4Online first; start both again with
     `Start-Service <database service>, DW4Online`.
   - ACLs: every folder drops the permissions inherited from the drive and keeps only Administrators and SYSTEM (full
     control) plus the service's rights: the root and `app` read and execute for `NT SERVICE\DW4Online` (a release
     folder made later in the root inherits that), `config` read, `logs`, `acme` and `keys` modify, `backups` nothing
     (only Administrators and SYSTEM: the dumps hold password hashes, e-mail addresses and cloud saves). Check with
     `icacls D:\Dekirai\Servers\DW4Online\backups`.
   - Inbound firewall rules `DW4Online TCP <port>` for the HTTPS port and 52041 (and 80 with `-OpenPort80`), limited
     to the service executable.
4. If the hoster has a network firewall in front of the server (for example a Hetzner firewall in the Robot / Cloud
   console), open the HTTPS port (443, or 8443 in section 4.1), 52041 (and 80 for certificate validation) there too.
5. Check that nothing else listens on the service's ports (a XAMPP Apache often holds 443 and 80, IIS or another
   http.sys user shows up as `System`, PID 4; `install-service.ps1` checks the same):

   ```powershell
   Get-NetTCPConnection -State Listen -LocalPort 443,52041,5280 -ErrorAction SilentlyContinue   # no output = free
   ```

   With HTTPS on another port (section 4.1) check that port in place of 443 (`-LocalPort 8443,52041,5280`). 5280 is the plain-HTTP endpoint on 127.0.0.1 that `appsettings.json` defines and the production settings keep
   (`Kestrel:Endpoints:Http`; `curl.exe http://127.0.0.1:5280/health` works on the server without the certificate).
   If another program uses 5280, give the endpoint another free loopback port in `appsettings.Production.json`; a
   port in use stops the service at its start.
6. Install the TLS certificate made on the kept key and write the client's pins (section 6.1, `tls-key.ps1`,
   `new-selfsigned-cert.ps1 -ImportPfx`, `print-pins.ps1`).

Kestrel binds the ports itself; on Windows a non-admin account may bind ports below 1024 with sockets (no URL ACL is
needed because http.sys is not used).

### 4.1 HTTPS on another port when 443 is taken (Traefik, IIS, Apache)

A reverse proxy that already serves port 443 on the server stays untouched; DW4Online serves HTTPS on a port of its
own instead (the server installed on 31.10: Traefik owns 443 and answers `404 page not found` with its "TRAEFIK DEFAULT
CERT", so DW4Online uses **8443**). Who holds 443:

```powershell
Get-NetTCPConnection -State Listen -LocalPort 443 | Select-Object LocalAddress, OwningProcess, @{n='Process';e={(Get-Process -Id $_.OwningProcess).ProcessName}}
```

1. `appsettings.Production.json`: `"Kestrel": { "Endpoints": { "Https": { "Url": "https://0.0.0.0:8443" }, "Http": { "Url": "http://127.0.0.1:5280" } } }`.
2. Windows firewall: a fresh install passes `-HttpsPort 8443` to `install-service.ps1` (rule `DW4Online TCP 8443`).
   On a service installed for 443, swap the rule by hand (elevated):

   ```powershell
   New-NetFirewallRule -DisplayName 'DW4Online TCP 8443' -Direction Inbound -Protocol TCP -LocalPort 8443 -Program D:\Dekirai\Servers\DW4Online\app\DW4Online.Server.exe -Action Allow -Profile Any
   Get-NetFirewallRule -DisplayName 'DW4Online TCP 443' -ErrorAction SilentlyContinue | Remove-NetFirewallRule
   ```

3. Hetzner firewall: allow inbound TCP 8443 (and keep 52041).
4. Certificate checks with the port: `new-selfsigned-cert.ps1 -PinServedBy 88.198.52.221 -PinServedPort 8443`
   (`-HttpsPort 8443` does the same and also puts 8443 into the settings the script prints when it imports or makes a
   certificate, section 6.1); `print-pins.ps1 -Server 88.198.52.221:8443`; `curl.exe -k https://88.198.52.221:8443/health`.
5. Clients: the game client's server address carries the port, **`https://88.198.52.221:8443`**; the pins do not
   change (they belong to the key, not the port), the relay stays on 52041 (`Service:RelayHost` without a port). The
   website is `https://88.198.52.221:8443/` (register, account, privacy page).

Port 443 can come back to DW4Online later through the proxy that holds it (section 6.2, case 2): the proxy then has to
pass the pinned IP certificate's connections through (Traefik: a TCP router with TLS passthrough) or serve that
certificate itself, or every client that pins it stops connecting.

## 5. First start

Create the tables in the console first, then start the service: `migrate` prints every migration with its time and,
if something is wrong, the reason and the way out on the screen (section 9), not only in the log.

```powershell
cd D:\Dekirai\Servers\DW4Online
.\app\DW4Online.Server.exe migrate --settings config\appsettings.Production.json   # ... "Migrating database 'dw4online' done in N s: schema 14"
Start-Service DW4Online
Get-ChildItem logs\dw4online-*.log | Sort-Object Name | Select-Object -Last 1 | Get-Content -Tail 30
curl.exe http://127.0.0.1:5280/health          # {"status":"ok","schema":14}, on the server itself
curl.exe -k https://88.198.52.221/health      # the same over HTTPS (:8443 in section 4.1); -k: the certificate is self-signed (section 6.1)
.\app\DW4Online.Server.exe tls-pin --host 88.198.52.221   # the certificate the service picks from the store, and its pin
powershell -ExecutionPolicy Bypass -File app\deploy\new-selfsigned-cert.ps1 -PinServedBy 88.198.52.221   # the pin of the certificate the HTTPS port really serves (-PinServedPort 8443 in section 4.1)
```

Both pins must equal the pin in use, the first pin of the client build's pins (comment lines aside) (section 6.1 step 5;
`new-selfsigned-cert.ps1` printed it too when it imported or made the certificate). `Start-Service` may print `WARNING: Waiting for service 'DW4Online' to start...` a few times
while the service waits for the database or migrates: that is the service reporting progress, not an error (for
minutes: section 9, "A migration or a start that hangs"). The log then says "Database schema is current" (or "Applied
migrations ..." if it migrated itself), "Now listening on: https://0.0.0.0:443" (`:8443` in section 4.1) and the TLS
certificate line; a ` FAIL ` line names what went wrong (section 9).

Register your own account on the website (`https://<host>/register`, keep the recovery code) or in the game, then make
it the admin from an elevated console:

```powershell
cd D:\Dekirai\Servers\DW4Online\app
.\DW4Online.Server.exe set-role <your name> admin --settings ..\config\appsettings.Production.json
```

Log in on the website again: the navigation now shows "Admin" (account search, locks, bans, temporary passwords,
unlock review, closing rooms, account deletion on request). Read the privacy page once (`/privacy`) and adjust the text
in `src/DW4Online.Web/Pages/Privacy.cshtml` if your setup differs (for example other log retention).

## 6. TLS: the IP address now, the domain later

### 6.1 Now: https://88.198.52.221 with a pinned self-signed certificate on a kept key

There is no domain yet, so no certificate authority vouches for the address. The game client therefore does not ask
the Windows root store: it pins the server's public key, the SHA-256 of the certificate's SubjectPublicKeyInfo ("SPKI
pin"), and refuses any other key before it sends a single byte of a request (the end-to-end test
`Pinned_self_signed_https_at_an_ip_address_below_a_path_base` checks exactly that with the WinHTTP test client).

**A key made once and kept.** The pin belongs to the key, not to the certificate. Two keys are made once on your PC and
kept offline: the key in use and a spare. Every client pins both. Every certificate made on a kept key has its pin, so
renewing the certificate, reinstalling or moving the server needs no client update. (A key the server makes itself, as
`New-SelfSignedCertificate` does, is new with every certificate: a new pin, so every renewal would need a client update
first.)

1. **The keys**, on the development PC in PowerShell (OpenSSL comes with Git for Windows):

   ```powershell
   cd server\DW4Online\deploy
   powershell -ExecutionPolicy Bypass -File tls-key.ps1 -NewKey E:\dw4online\server-key.pem
   powershell -ExecutionPolicy Bypass -File tls-key.ps1 -NewKey E:\dw4online\spare-key.pem
   ```

   `E:` stands for a USB stick or any place outside the repository; keep a second copy somewhere else. Each key is RSA
   3072, encrypted with AES-256 under a passphrase OpenSSL asks for; keep the passphrases apart (a password manager).
   [tls-key.ps1](tls-key.ps1) never overwrites a key file and prints each key's pin. The keys never go into Git, the
   repository or (as PEM files) onto the server.
2. **A certificate** on the key in use, as a PFX for the server:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tls-key.ps1 -Certificate -KeyFile E:\dw4online\server-key.pem -Out E:\dw4online\server.pfx
   ```

   It is self-signed for `IP 88.198.52.221` (subject alternative name, server authentication), valid 10 years
   (`-Years` changes it); OpenSSL asks for the key's passphrase and a password for the PFX.
3. **On the server**, after `install-service.ps1` (section 4; the service account must exist), in an elevated
   PowerShell:

   ```powershell
   cd D:\Dekirai\Servers\DW4Online\app\deploy
   powershell -ExecutionPolicy Bypass -File new-selfsigned-cert.ps1 -ImportPfx E:\dw4online\server.pfx
   ```

   (With HTTPS on 8443, section 4.1, add `-HttpsPort 8443`: the settings the script prints then name that port.)
   `E:` is wherever the PFX of step 2 is. On a rented server reached over Remote Desktop that is your own PC: with
   drive redirection on, read it there directly (`-ImportPfx \\tsclient\E\dw4online\server.pfx`), otherwise copy it to
   the server's desktop through the Remote Desktop clipboard. [new-selfsigned-cert.ps1](new-selfsigned-cert.ps1) asks
   for the PFX password, installs the certificate in `LocalMachine\My` without export permission, gives
   `NT SERVICE\DW4Online` read access to its key (the service runs the TLS handshakes) and prints its pin, checked
   against the service's own `DW4Online.Server.exe tls-pin`, and the settings of step 4. Delete the PFX from the server
   afterwards (`Remove-Item`): the kept PEM key is the master copy. `-DryRun` shows what it would do.
4. In `appsettings.Production.json` (the example file already holds these values; the port is 8443 in section 4.1):

   ```json
   "Kestrel": { "Endpoints": { "Https": { "Url": "https://0.0.0.0:443" } } },
   "Tls": { "StoreHostName": "88.198.52.221" },
   "Service": { "RelayHost": "88.198.52.221" },
   "Web": { "RequireHttps": true, "Hsts": false }
   ```

   then, if the service already runs, `Restart-Service DW4Online` (or wait: the service re-reads the store every 10
   minutes); at the first install it is not started yet, section 5 starts it after `migrate`. Its log names the
   certificate and its pin (`TLS certificate for 88.198.52.221: <thumbprint>, valid until ..., SPKI pin ...`).
5. **The pins for the game client**, on the development PC (the pin script):

   ```powershell
   powershell -ExecutionPolicy Bypass -File print-pins.ps1 -KeyFile E:\dw4online\server-key.pem,E:\dw4online\spare-key.pem -Out ..\..\..\local\online_pins.txt
   ```

   [print-pins.ps1](print-pins.ps1) prints both pins (in use first, then the spare) and writes them to
   `local/online_pins.txt` of the DW4_PC checkout, which the client build reads (section 6.4). It also reads
   certificates (`-CertFile`) and PFX files (`-Pfx`).
6. **Check** (the server's running service, after section 5): `print-pins.ps1 -Server 88.198.52.221`
   (`-Server 88.198.52.221:8443` in section 4.1; from any PC; a TLS handshake, nothing is sent) prints the key the
   server presents; together with the `-KeyFile` list of step 5 it checks that this is the key in use. On the server,
   `.\DW4Online.Server.exe tls-pin --host 88.198.52.221` (in the app folder) prints the certificate the service picks
   from the store (the same selection, read from the store, no connection) and its pin, and
   `new-selfsigned-cert.ps1 -PinServedBy 88.198.52.221` (with `-PinServedPort 8443` in section 4.1) connects to the
   HTTPS port as the game client does (no host name) and prints the pin of the certificate actually served; from
   another PC `curl.exe -k https://88.198.52.221/health` (`https://88.198.52.221:8443/health`) answers
   `{"status":"ok",...}`.

What players notice: browsers warn about the website's certificate (it is self-signed). Whoever wants to check it
before accepting compares the browser's SHA-256 fingerprint with the `Certificate fingerprint (SHA-256)` line of
`tls-pin`; you can publish that line where your players read news. HSTS stays off for the IP address (browsers ignore it
there anyway). The game itself shows no warning: it checks the pin.

**Renewal, a new or moved server.** Make a new certificate on the same kept key (step 2) and import it (step 3) with
`-Force`: the script refuses a second certificate for an address that already has a valid one, because with a new key
that would lock every client out; with the kept key the pin stays the same, so `-Force` is safe. The certificate lasts
10 years; by then the service runs under the domain.

**A key that leaked.** Make a certificate on the spare key and import it with `-Force`: every client accepts it, as it
pins the spare too. Then make a new spare key (step 1), write the pins again (step 5: the former spare in use first,
the new spare second) and ship a client update with them before the old key's certificate is gone for good.

**Without kept keys** (the earlier way, not recommended): `new-selfsigned-cert.ps1` without `-ImportPfx` makes the key on
the server, where it can never leave (`-SparePfx` writes a spare to a PFX, over Remote Desktop best straight to your own
PC through `\\tsclient\...`; `-SpareOnly` adds one later; `-DryRun` shows what it would do). It refuses a second
certificate for an address that already has a valid one unless `-Force` is given. Its pins come from
`print-pins.ps1 -Server` or `-Pfx` (or `DW4Online.Server.exe tls-pin --file x.pfx --password-env DW4_PFX`) and go into
the build the same way, but a renewal makes a new key and a new pin. A certificate made again this way before any
client pins it (for example after a failed first attempt) is fine: remove the first one with its key
(`Get-ChildItem Cert:\LocalMachine\My | Where-Object FriendlyName -eq 'DW4Online 88.198.52.221' | ForEach-Object {
Remove-Item -LiteralPath "Cert:\LocalMachine\My\$($_.Thumbprint)" -DeleteKey }`), run the script again and use only
the new pins. The server installed on 31.10 has a certificate made this way (its spare is the offline
`dw4online-spare.pfx`); moving it to kept keys means new pins, so a client update that carries them comes first.
If that server or its key is lost (or the key leaks), the spare takes over: on the new (or the same) server run
`new-selfsigned-cert.ps1 -ImportPfx <path>\dw4online-spare.pfx -HttpsPort 8443` (`-Force` on a server that still has
the old certificate; it asks for the PFX password and installs the key without export permission), restart the
service and check with `-PinServedBy ... -PinServedPort 8443` that the spare's pin is served. Every client keeps
working because it pins the spare too. Then ship a client update whose pins name the spare as the key in use plus a
new spare (made with `-SparePfx ... -SpareOnly` on that server), before the next loss can happen.

### 6.2 The domain: https://dekirai.crygod.de/dw4online

The service works below a path: `"Service": { "PathBase": "/dw4online" }` puts the website and the API under
`/dw4online` (links, redirects, forms, `Location` headers and the session cookie stay below it; `PathBaseTests` and
the pinned end-to-end test check it), and behind a reverse proxy that strips the prefix, the proxy's
`X-Forwarded-Prefix` naming that path does the same (case 2; `ReverseProxyTests` and `ProxyHeadersTests` check every
combination).
Requests without the prefix keep working at the root, so clients that still use `https://88.198.52.221`
(`https://88.198.52.221:8443` in section 4.1) go on working during the move.

**A path keeps the cookie away from other sites, not their scripts.** Browsers isolate by origin (scheme, host name,
port), never by path: any page of another site on `dekirai.crygod.de` runs in the same origin as
`dekirai.crygod.de/dw4online`, can send requests with the visitor's DW4Online cookie and read the answers (anti-forgery
tokens included). A cross-site-scripting flaw in any other site on that name is then an account takeover in DW4Online.
If other content lives on that host name (case 2), prefer a host name of its own for DW4Online
(`dw4.crygod.de`, an A record to 88.198.52.221) with `PathBase` empty or kept.

**Case 1: DW4Online keeps its HTTPS port on this server.** The domain's A record points to 88.198.52.221 and nothing
else on this machine serves that name on that port. Get a Let's Encrypt certificate for `dekirai.crygod.de` into
`LocalMachine\My` (6.3) and set:

```json
"Tls": { "StoreHostName": "88.198.52.221", "AdditionalHostNames": [ "dekirai.crygod.de" ] },
"Service": { "PathBase": "/dw4online", "RelayHost": "88.198.52.221" }
```

A connection that names the domain (SNI) gets the Let's Encrypt certificate and is validated normally; a connection to
the IP address names no host and keeps getting the pinned certificate. New clients use
`https://dekirai.crygod.de/dw4online` (with `:8443` while DW4Online serves that port, section 4.1), old ones keep
working until they update. (The domain's root then also shows the DW4Online website; if other content should live
there, see case 2.)

**Case 2: another web server serves dekirai.crygod.de on this machine** (Traefik, IIS, nginx, Caddy), for example
the server as it runs now: **Cloudflare -> Traefik (port 443 on this server) -> DW4Online `http://127.0.0.1:5280`**.
The proxy forwards the site's requests to the loopback endpoint; the game clients keep talking to DW4Online directly at
`https://88.198.52.221:8443` (section 4.1), so the `Https` endpoint and `Tls:StoreHostName` stay, and the relay keeps
its own port 52041. `Service:BehindProxy` makes DW4Online read the proxy's headers:

| Header | What DW4Online takes from it |
| --- | --- |
| `X-Forwarded-Proto` | the scheme the browser used (`https`: Secure cookies, no `Web:RequireHttps` redirect) |
| `X-Forwarded-Prefix` | the path prefix a proxy stripped (Traefik's `stripPrefix` sends it), **only when it is `Service:PathBase`**: links, redirects, forms, `Location` headers and cookie paths get it back |
| `X-Forwarded-For` | its last entry: the address the local proxy saw (a direct browser's, or a Cloudflare edge's) |
| `CF-Connecting-IP` | Cloudflare's header with the browser's address, only when that last entry lies in `Service:TrustedProxyNetworks` (Cloudflare's networks), see "The client address behind Cloudflare" below |

The address is used for the per-IP rate limits of the login, registration and recovery forms and of the API, and
recorded with the sessions and in the audit log. These headers count **only on plain-HTTP connections from this
machine** (the proxy's hop to `127.0.0.1:5280`): a direct client, such as the game at `https://88.198.52.221:8443` or
anyone who sends these headers to port 8443, never names an address, a scheme or a prefix; nor does a connection to the
HTTPS endpoint from 127.0.0.1 (a proxy that only passes TLS through, a TCP router, forwards the client's own bytes).
The last value of `X-Forwarded-For` and `X-Forwarded-Proto` counts (the one the local proxy added; entries further
left were written by whoever sent the request). A prefix counts only as `Service:PathBase` (letter case aside): any
other value, or a prefix without `PathBase`, could be a browser's own header that a proxy passed on, and is ignored.
Since release 0.1.8 that one prefix (`X-Forwarded-Prefix` equal to `Service:PathBase`, with `BehindProxy` on) counts on
every connection, also on the HTTPS endpoint and from other addresses: a Traefik whose service url is
`https://...:8443` (or a container) and which strips the prefix gets the right links without any change on its side.
It can only put the links below the path base, which serves them too; the address and the scheme still come only from
plain HTTP on this machine (so behind such a Traefik all browsers keep sharing the edge's address until its url is
`http://127.0.0.1:5280`).

**The path prefix, with or without `stripPrefix`.** Both Traefik variants work; set `"PathBase": "/dw4online"` in both
(without `stripPrefix` the path carries the prefix, with it the proxy's `X-Forwarded-Prefix` must match `PathBase`;
the direct addresses `https://88.198.52.221:8443/dw4online/...` work too, and the root keeps working in every case).
The prefix never doubles.

| Traefik | `Service:PathBase` | DW4Online receives | Links, redirects, cookie path |
| --- | --- | --- | --- |
| `stripPrefix` (sends `X-Forwarded-Prefix: /dw4online`) | `/dw4online` | `/account` and the header | `/dw4online/...` (the header names `PathBase`) |
| without `stripPrefix` | `/dw4online` | `/dw4online/account` | `/dw4online/...` (from `PathBase`) |
| a proxy that keeps the path and names the prefix as well | `/dw4online` | `/dw4online/account` and the header | `/dw4online/...` (the prefix is cut from the path once) |
| any, with a browser's own `X-Forwarded-Prefix: /evil` passed on | `/dw4online` | the header `/evil` | ignored: `/dw4online/...` as above |
| `stripPrefix` | empty | `/account` and the header | wrong: links at the root (set `PathBase`) |
| without `stripPrefix` | empty | `/dw4online/account` | wrong: links at the root (set `PathBase`) |

**The client address behind Cloudflare.** Every browser reaches Traefik from a Cloudflare edge, so with the defaults
DW4Online sees that edge's address: all web users share a handful of addresses and their per-IP limits (5 logins a
minute, 3 registrations an hour), and the sessions and the audit log record the edge. Cloudflare adds its own header,
`CF-Connecting-IP`, with the address of the browser that connected to it, to every request through the site's zone (it
replaces one the browser sent). Traefik passes it on unchanged, at its **default** settings: it does not trust
Cloudflare's forwarded headers, removes the incoming `X-Forwarded-For`, `X-Forwarded-Proto`, `X-Forwarded-Host` and
`X-Real-Ip` and writes its own, with the address it saw, the edge's, as `X-Forwarded-For`. One setting in DW4Online is
all it takes: `TrustedProxyNetworks` in `Service` (below) lists Cloudflare's networks
(<https://www.cloudflare.com/ips/>, as of 05.11.2026). When the last `X-Forwarded-For` entry (the address Traefik saw)
lies in one of them, `CF-Connecting-IP` is the browser's address, if it is exactly one public address: a missing or
unusable one (a list, loopback, a private, link-local or multicast address, a Cloudflare address itself) keeps the
edge's address, as without the setting.

**Leave Traefik's entry point as it is: no `forwardedHeaders.trustedIPs` (and no `insecure`) for Cloudflare.** That
setting holds for every router on the entry point, not only DW4Online's: Traefik would then pass a browser's own
`X-Real-Ip`, `X-Forwarded-Host` and the left part of `X-Forwarded-For` through Cloudflare to every site on
`websecure`, and a site that trusts them (a host name for reset links, `X-Real-Ip` as the client's address) would take
values an attacker chose. DW4Online does not need it. (Where it is set already for another site, DW4Online still works
the same, it reads only the last `X-Forwarded-For` entry, `CF-Connecting-IP` and a prefix equal to `PathBase`; check
the other sites, though.)

How requests are read (Traefik on 127.0.0.1 at its default, a browser 203.0.113.9, an edge 172.64.1.1):

| Request | At DW4Online | Address |
| --- | --- | --- |
| browser -> Cloudflare -> Traefik | `X-Forwarded-For: 172.64.1.1`, `CF-Connecting-IP: 203.0.113.9` | 203.0.113.9 |
| the same, the browser sent its own `X-Forwarded-For` or `CF-Connecting-IP` | the same (Traefik removed the first, Cloudflare replaced the second) | 203.0.113.9 |
| browser -> Traefik directly (88.198.52.221:443, not through Cloudflare), any headers | `X-Forwarded-For: 203.0.113.9` (no edge: `CF-Connecting-IP` is not read) | 203.0.113.9 |
| an edge with a `CF-Connecting-IP` no browser has (127.0.0.1, 10.0.0.1, a Cloudflare address, two addresses) | `X-Forwarded-For: 172.64.1.1` | 172.64.1.1 (the edge) |
| browser -> Cloudflare -> Traefik without `TrustedProxyNetworks` | `X-Forwarded-For: 172.64.1.1` | 172.64.1.1 (the edge, as before these settings) |
| anyone -> DW4Online's port 8443 (the game), any headers | (not read) | the connection's own address |
| a Cloudflare address -> port 8443 directly, any headers | (not read) | that address: an edge counts only behind Traefik |

DW4Online refuses to start when `TrustedProxyNetworks` holds something that is not a network in CIDR form, a network
wider than /8 (IPv4) or /16 (IPv6), an IPv4-mapped IPv6 network (`::ffff:198.51.100.0/120`: write it as
`198.51.100.0/24`) or a network with bits set after its prefix length (`198.51.100.1/24`: the error names
`198.51.100.0/24`), or when it is set without `BehindProxy`. Cloudflare changes its list rarely and announces it;
compare <https://www.cloudflare.com/ips/> with the list now and then. A network missing from it only makes the
browsers behind it share their edge's address again (as without the setting); it opens nothing.

**What the list cannot tell apart.** A request that comes from Cloudflare's network without passing through this
site's Cloudflare zone (a Worker of another Cloudflare account, or another Cloudflare service, that connects to
`88.198.52.221:443` with this site's host name) also arrives from an edge address. Where such a request can set its
own `CF-Connecting-IP`, it picks the address DW4Online counts and records (only one a browser could have: the ones
above are refused). That allows:

- unlimited attempts past the per-IP limits: registrations (3 an hour per address), logins and recoveries (5 a minute
  per address), each from a new made-up address;
- the account-wide lock (`Accounts:MaxFailedLoginsPerAccount`, 50 failures within `LockoutMinutes` = 15 minutes, at
  most 10 from one network) from a single machine: the account is then locked for 15 minutes for everyone, its owner
  included (a botnet can do that without any of this);
- a targeted lock of one player: knowing his address, 10 failed logins with his name "from" that address lock the pair
  of name and address (`Accounts:MaxFailedLogins`) for 15 minutes; he cannot log in from that address meanwhile (from
  any other he can), although the lock's promise is that strangers lock out only their own address;
- made-up addresses in the sessions and the audit log.

Passwords, sessions and the game's own connections (port 8443, the relay) are not affected. Cloudflare's
*Authenticated Origin Pulls* close it: Cloudflare then presents a client certificate on every connection to the
origin, and Traefik admits only connections with it (a `tls.options` entry with `clientAuth`, `clientAuthType:
RequireAndVerifyClientCert` and the origin-pull CA, on every router of `dekirai.crygod.de`: Traefik picks TLS options
by host name, and all sites of that name come through Cloudflare anyway). Use a certificate of your own for the zone
(Cloudflare's shared one proves only "some Cloudflare zone").

**Traefik's route** (dynamic configuration: the file provider's file, or labels). With `stripPrefix`:

```yaml
http:
  routers:
    dw4online:
      rule: "Host(`dekirai.crygod.de`) && (Path(`/dw4online`) || PathPrefix(`/dw4online/`))"
      entryPoints: [websecure]
      middlewares: [dw4online-strip]
      service: dw4online
      tls: {}
  middlewares:
    dw4online-strip:
      stripPrefix:
        prefixes: ["/dw4online"]
  services:
    dw4online:
      loadBalancer:
        servers:
          - url: "http://127.0.0.1:5280"
```

Without `stripPrefix`: the same without the `middlewares` lines. Traefik passes the host name on (`passHostHeader`, its
default), `CF-Connecting-IP` too, and sets `X-Forwarded-For` and `X-Forwarded-Proto`; nothing else is needed, and
Traefik's static configuration (the entry points) stays as it is. (The rule with `Path(...) || PathPrefix(.../)` keeps
`/dw4onlinex` off the route; a plain ``PathPrefix(`/dw4online`)`` works too.)

**DW4Online's settings** (`config\appsettings.Production.json`; the `Https` endpoint on 8443 and the `Tls` section stay
for the game clients), the same with and without `stripPrefix`:

```json
"Service": {
  "RelayHost": "88.198.52.221",
  "PathBase": "/dw4online",
  "BehindProxy": true,
  "TrustedProxyNetworks": [
    "173.245.48.0/20", "103.21.244.0/22", "103.22.200.0/22", "103.31.4.0/22", "141.101.64.0/18", "108.162.192.0/18",
    "190.93.240.0/20", "188.114.96.0/20", "197.234.240.0/22", "198.41.128.0/17", "162.158.0.0/15", "104.16.0.0/13",
    "104.24.0.0/14", "172.64.0.0/13", "131.0.72.0/22",
    "2400:cb00::/32", "2606:4700::/32", "2803:f800::/32", "2405:b500::/32", "2405:8100::/32", "2a06:98c0::/29", "2c0f:f248::/32"
  ]
},
"Web": {
  "RequireHttps": true,
  "Processors": [
    "Cloudflare, Inc., 101 Townsend St, San Francisco, CA 94107, USA: the content delivery network this website is reached through. Every request to the website passes through Cloudflare's servers with your IP address, the cookies and what you enter in the forms (passwords included); Cloudflare may process it outside the EU, under its data processing addendum, and may set technically necessary security cookies of its own (such as __cf_bm). The game's own connections do not pass through Cloudflare."
  ]
}
```

(`Web:Processors`: Cloudflare terminates TLS for the website and sees everything in transit, so the privacy page names
it under "Who receives data" beside the hosting provider. The text is yours as the operator: check it against
Cloudflare's current data processing addendum and your Cloudflare settings, and drop the entry if the website ever
stops going through Cloudflare. Without entries the page names the hosting provider only.)

`Restart-Service DW4Online` (it reads its settings at the start; its log then shows `Behind a proxy: X-Forwarded-For,
-Proto and -Prefix (= Service:PathBase) from plain-HTTP connections of this machine, CF-Connecting-IP from 22 trusted
edge network(s)`). Check: open `https://dekirai.crygod.de/dw4online/` (the links and the login stay below
`/dw4online`; the privacy page names Cloudflare), log in, and look at the newest audit rows in HeidiSQL (database
`dw4online`, Query tab):

```sql
SELECT id, event, INET6_NTOA(ip) AS ip, created_at FROM audit_log ORDER BY id DESC LIMIT 5;
```

The `login_ok` row must show your own public address (<https://www.cloudflare.com/cdn-cgi/trace> shows it as `ip=`),
not one of Cloudflare's. A Cloudflare address there means `TrustedProxyNetworks` is missing (or lacks that edge's
network) or the service was not restarted.

Other proxies: IIS (ARR), nginx and Caddy forward `X-Forwarded-For` and `X-Forwarded-Proto` the same way (DW4Online
reads the last `X-Forwarded-For` entry, so a proxy that appends to the header works like one that replaces it); send
`X-Forwarded-Prefix` only when the proxy strips the prefix (with `PathBase`). `TrustedProxyNetworks` is for Cloudflare
only: another edge in front of the local proxy has no `CF-Connecting-IP`, and its browsers keep sharing the edge's
address. Should port 443 itself ever have to serve the pinned IP certificate to the game (clients of
`https://88.198.52.221` without a port), the proxy must pass those connections through to DW4Online's HTTPS port
(Traefik: a TCP router with TLS passthrough for connections without SNI; IIS: an https binding on 88.198.52.221:443
without host name using the same certificate from `LocalMachine\My`, which ARR forwards like the website). With TLS
passthrough, forwarded headers never count on DW4Online's HTTPS port, so such clients cannot name an address, but they
all appear as 127.0.0.1 there (one per-IP limit for all of them); the game clients of
`https://88.198.52.221:8443` are not affected.

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
against the Windows root store (WinHTTP + Schannel) and do not pin it, so renewals with new keys are fine. (Port 80
may be taken by the same proxy as 443; then the proxy has to answer or forward the challenge.)

Not chosen: a Let's Encrypt certificate for the IP address (6-day certificates through the ACME `shortlived` profile;
the client would have to pin a key that changes with every renewal unless the key is reused) and plain HTTP (passwords
and tokens would travel unencrypted).

### 6.4 The pins in the game client (a build step)

The client takes the server's pins from its build, never from a file at run time and never from a key
(`src/game/online/server_pins.hpp`; SEAMLESS.md 10):

- `local/online_pins.txt` in the DW4_PC checkout: one pin per line, the key in use first, then the spare; `#` starts a
  comment. `print-pins.ps1 -Out` writes it (section 6.1, step 5). `local/` is ignored by Git. The pins are public values
  (hashes of public keys), the keys stay offline.
- or `-DDW4_ONLINE_PINS=<in use>,<spare>` on the CMake command line (it wins over the file; `DW4_ONLINE_PINS_FILE` names
  another file).

CMake reads them at its configure step: `scripts/build.ps1` runs it on every build; in a build folder of your own run
`cmake -S . -B <folder>` once after the file first exists (later changes of the file reconfigure by themselves). It
checks each pin (base64 SHA-256, 44 characters, at most two) and prints `DW4Online server pins: 2 (...)`; without pins
it prints `none`, and that client refuses the server (fail closed: "the server's key matched no pin"). For a release
package configure with `-DDW4_ONLINE_REQUIRE_PINS=ON`: a missing or broken pin then fails the configure step. The ctest
`online_api` checks that the build carries the configured pins ("server pins in this build: 2"). A client built with
`DW4_ONLINE_USE_DOMAIN` (section 6.2) validates the domain's certificate chain instead and needs no pins.

## 7. Backups

[backup-db.ps1](backup-db.ps1) runs `mariadb-dump --single-transaction --quick --hex-blob --routines --events
--no-tablespaces dw4online`, compresses the dump (gzip) into `backups\daily`, keeps 14 daily and 8 weekly (Sunday)
copies and can copy each new file to a second folder (`-CopyTo`, for example a mounted share). The credentials come
from an option file in the `config` folder (Administrators and SYSTEM; the service account may read it as well, it
holds a stronger password of its own anyway), so no password appears on a command line:

```ini
# D:\Dekirai\Servers\DW4Online\config\backup.cnf
[client]
user=dw4backup
password=<backup password>
host=127.0.0.1
port=3306
```

(Root-only variant, section 2.1: `user=root` and the root password.)

Schedule it nightly as SYSTEM (elevated PowerShell):

```powershell
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument '-NoProfile -ExecutionPolicy Bypass -File D:\Dekirai\Servers\DW4Online\app\deploy\backup-db.ps1'
$trigger = New-ScheduledTaskTrigger -Daily -At 03:30
Register-ScheduledTask -TaskName 'DW4Online DB backup' -Action $action -Trigger $trigger -User 'SYSTEM' -RunLevel Highest
```

With XAMPP's MariaDB (section 1.1) the task passes its bin folder and dump program (put the XAMPP path of your server
in place of `C:\xampp`; a path with spaces needs `\"...\"` around it inside the argument string):

```powershell
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument '-NoProfile -ExecutionPolicy Bypass -File D:\Dekirai\Servers\DW4Online\app\deploy\backup-db.ps1 -MariaDbBin C:\xampp\mysql\bin -DumpExe mysqldump.exe'
```

Copy the backups off the server as well (for example to your own FTPS server with WinSCP scripting, or a storage box).
Also keep a copy of `config\appsettings.Production.json` (it holds the database password and the relay ticket key, which
stays the same for good, section 4), stored encrypted. The TLS keys are kept offline anyway (section 6.1); the
certificate in the store needs no backup, as a new one is made on the kept key. (Without kept keys, the way of the
server installed on 31.10, its key cannot leave the server: the offline spare PFX is what clients accept after a lost
server, section 6.1.) The `keys` folder needs no backup: its keys are DPAPI-encrypted for this machine's service
account and only protect anti-forgery tokens; if it is lost, open website forms simply have to be reloaded.

Run the backup once by hand and check the file (elevated PowerShell; `Start-ScheduledTask 'DW4Online DB backup'` runs
it as SYSTEM the same way):

```powershell
powershell -ExecutionPolicy Bypass -File D:\Dekirai\Servers\DW4Online\app\deploy\backup-db.ps1   # "Backup written: ...sql.gz (.. KB)"
powershell -ExecutionPolicy Bypass -File D:\Dekirai\Servers\DW4Online\app\deploy\backup-db.ps1 -MariaDbBin C:\xampp\mysql\bin -DumpExe mysqldump.exe   # XAMPP
```

Restore drill (once, and after schema changes) with [restore-db.ps1](restore-db.ps1), which asks for the MariaDB root
password once (or uses `backup.cnf` with `-OptionFile` when that holds a root login) and prints the restored
database's account count and schema version:

```powershell
cd D:\Dekirai\Servers\DW4Online
$latest = (Get-ChildItem backups\daily\*.sql.gz | Sort-Object Name | Select-Object -Last 1).FullName
powershell -ExecutionPolicy Bypass -File app\deploy\restore-db.ps1 -Backup $latest     # into dw4online_restore
mariadb.exe -u root -p -e "DROP DATABASE dw4online_restore"
```

With XAMPP's MariaDB: `restore-db.ps1 -Backup $latest -MariaDbBin C:\xampp\mysql\bin -ClientExe mysql.exe`, and drop
`dw4online_restore` afterwards in HeidiSQL or with `C:\xampp\mysql\bin\mysql.exe -u root -p -e "DROP DATABASE dw4online_restore"`.

Do not pipe a dump into the client in Windows PowerShell 5.1 (`7z e -so x.sql.gz | mariadb.exe ...`): the pipe
re-encodes the text and silently turns every non-ASCII character into `?` (the rehearsal restored "PC von Jürgen" as
"PC von J??rgen"). `restore-db.ps1` decompresses to a file and lets cmd.exe feed it unchanged; by hand that is
`cmd /c "mariadb.exe -u root -p dw4online_restore < dump.sql"`. HeidiSQL's "Run SQL file" is no substitute either.

## 8. Updates and rollback

1. Announce the update and pick a quiet time: stopping the service ends running co-op sessions and rooms (they close
   with `server_restart` at the next start). The service reads its settings only when it starts, so `Service:Motd` or
   `Service:Maintenance` (no new rooms) take effect only after a restart: announce elsewhere, or set them for a
   restart some time before the update.
2. In an elevated PowerShell, with the new zip on the server (for example `C:\Users\Administrator\Desktop\DW4Online-<version>.zip`;
   XAMPP's MariaDB: add `-MariaDbBin <XAMPP>\mysql\bin -DumpExe mysqldump.exe` to the backup line; HTTPS on 8443: the
   last line with `:8443`):

   ```powershell
   cd D:\Dekirai\Servers\DW4Online
   Stop-Service DW4Online
   powershell -ExecutionPolicy Bypass -File app\deploy\backup-db.ps1      # migrations commit DDL implicitly: this backup is the way back
   if (Test-Path app.previous) { Remove-Item -Recurse -Force app.previous }
   Rename-Item app app.previous
   Expand-Archive C:\Users\Administrator\Desktop\DW4Online-<version>.zip -DestinationPath app
   .\app\DW4Online.Server.exe migrate --settings config\appsettings.Production.json   # "The database schema is current." or "... done in N s: schema N"
   Start-Service DW4Online
   Get-Content (Get-ChildItem logs\dw4online-*.log | Sort-Object Name | Select-Object -Last 1).FullName -Tail 20   # no " FAIL " line
   curl.exe http://127.0.0.1:5280/health
   curl.exe http://127.0.0.1:5280/api/v1/status   # "version":"<version>": the release that runs now
   curl.exe -k https://88.198.52.221/health
   ```

   `migrate` runs the new release's migrations in the console, before the service starts (section 9): it prints each
   one with its time, and if it refuses or fails, nothing starts and the screen says why and what to do (usually: the
   rollback below). If it prints `WARNING: MariaDB ... runs with innodb_use_native_aio=ON`, stop there: the line of
   section 1.1 step 2 is missing (a server installed with release 0.1.0 needs it once; add it while DW4Online is
   stopped, `Restart-Service mysql`, then run `migrate` again). Each migration takes well under a second; if nothing
   follows `Applying migration ...` for minutes, see "A migration or a start that hangs" (section 9). The new `app`
   folder inherits the root's permissions (service read and execute), and the firewall rules and the service name the
   same path, so nothing else changes. Settings that a release asks for go into `config\appsettings.Production.json`
   before the start: an update from release 0.1.0 (its example file had 43) sets `Play:MinimumProtocol` and
   `Play:CurrentProtocol` to the game client's protocol (45 since round S2, 46 since round 63, 47 since round G-core,
   48 since release 0.1.6: the READY check at the check tick of round J's joins, 49 since release 0.1.7, the online world, section 9.2,
   50 since release 0.2.0, round I1b, section 9.3: the code refuses anything below the current floor whatever the file says); the room story settings work with
   their defaults (section 10). A release that raises the protocol goes on the server first and the game client after
   it: an older relay (0.1.5, minimum 47) admits a 48 client but checks its READY at the check tick only when the
   donor's report happens to be there already; a relay with `Play:MinimumProtocol` 48 asks older clients to update.
   Release 0.1.7 (the world, protocol 49) is different: its relay speaks only 49, so a 48 client cannot play until the
   players have the new game client (section 9.2: the server first, the client release right after it). Release 0.2.0
   (protocol 50, section 9.3) is the same: its relay speaks only 50.

Rollback: stop the service, swap `app` and `app.previous` back; if the update applied a migration (or one failed half
way), restore the backup from step 2 into `dw4online` first (the old build refuses a database that is newer than
itself). The swap only runs when the service is really stopped (`Stop-Service` cannot stop a start that is still
pending, and a failed rename would leave the next lines working on the new release):

```powershell
cd D:\Dekirai\Servers\DW4Online
Stop-Service DW4Online
if ((Get-Service DW4Online).Status -ne 'Stopped') { 'DW4Online is not stopped (section 9, "A migration or a start that hangs"); nothing was changed.' } else {
    if (Test-Path app.failed) { Remove-Item -Recurse -Force app.failed }
    Rename-Item app app.failed -ErrorAction Stop
    Rename-Item app.previous app -ErrorAction Stop
    'The previous release is in app again.'
}
```

Then, only after that last line appeared (XAMPP: add `-MariaDbBin <XAMPP>\mysql\bin -ClientExe mysql.exe` to the
restore; the backup file is `dw4online-<yyyyMMdd>-<HHmmss>.sql.gz`, UTC):

```powershell
powershell -ExecutionPolicy Bypass -File app\deploy\restore-db.ps1 -Backup backups\daily\dw4online-<date>-<time>.sql.gz -Database dw4online -Replace   # only after a migration
.\app\DW4Online.Server.exe migrate --settings config\appsettings.Production.json   # "The database schema is current."
Start-Service DW4Online
```

**A release that raised the protocol** (0.1.7: 49, 0.2.0: 50): before that `Start-Service`, set `Play:MinimumProtocol` and
`Play:CurrentProtocol` in `config\appsettings.Production.json` back to the old release's protocol (49 for 0.1.9). The old
release takes the larger of the file's value and its own, so with 50 left in the file it answers every 49 client
`client_outdated` (426), and its relay speaks only 49: nobody could play. Then check `/api/v1/status` (`"minimumProtocol":49`).
A client release of the new protocol that was published already cannot play on the old server either (`ERR protocol`):
withdraw it with the rollback. That is why the client release follows the server only after the server runs and was
checked (section 9.3).

Database updates and restarts: DW4Online depends on the database service (`sc.exe config DW4Online depend= MariaDB`,
or `mysql`), so `Stop-Service` and `Restart-Service` of that service refuse while DW4Online runs (without `-Force`,
which stops DW4Online too), and the MariaDB MSI cannot replace a running server. Stop DW4Online first:
`Stop-Service DW4Online; Restart-Service MariaDB; Start-Service DW4Online` (`mysql` for XAMPP; for an MSI update: stop
both, run the MSI, start MariaDB, then DW4Online). The same holds for a `my.ini` change (section 1). With XAMPP, use
these commands rather than the Control Panel's Stop button for MySQL: the Control Panel does not stop DW4Online first.

## 9. Migrations

Every release carries its database migrations (`sql/NNNN_*.sql`, inside the executable). They run

- **in the console**, `DW4Online.Server.exe migrate --settings config\appsettings.Production.json`, which is the way
  this document uses before every start that brings new migrations (sections 5 and 8): it shows the database it
  talks to, each migration with its time (`Applied migration 0003 rooms in 0.01 s`) and the total
  (`Migrating database 'dw4online' done in 0.25 s: schema 14`), and exits with 1 and a `FAIL:` message if it refuses or
  fails;
- **at the service start** (`Database:AutoMigrate`, on by default), so a start after a forgotten `migrate` still
  works. The service connects to the Service Control Manager first and reports "start pending" with a new checkpoint
  every 5 s while it waits for the database (up to `Database:StartupWaitSeconds`, 60 s) and migrates, however long
  that takes; it reports "running" only once it listens. (Release 0.1.0 migrated before it connected; when migration
  0001 hung, the SCM ended it after 30 s, see the top of this document.) The checkpoints count time, not progress:
  a migration that hangs keeps the start pending without end (see below).

Both warn first when the MariaDB is one whose DDL can hang (`WARNING: MariaDB 10.4.32-MariaDB on Windows runs with
innodb_use_native_aio=ON ...`, a WARN line in the service log): add the line of section 1.1 step 2 and restart the
MariaDB before you go on.

Migrations change the schema with DDL, and MariaDB commits every DDL statement at once: a migration stopped half way
leaves the database between two versions, which no build can work with. The migrator therefore marks each migration
in `schema_version` before it runs it ("<name> (started, not finished)", checksum all zero) and replaces the mark when
it is done. It never repairs or drops anything itself; it refuses, and its message (on screen, and as a ` FAIL ` line
in the service log) names the state and the way out:

| Message begins with | State | Way out |
| --- | --- | --- |
| `Database 'dw4online' has an empty schema_version: migration 0001 (initial) ... was interrupted` + `None of its N tables holds a row` | the very first migration stopped half way (the server's first start on 31.10) | nothing to lose: drop and re-create the database empty (as root, for example in HeidiSQL: `DROP DATABASE dw4online; CREATE DATABASE dw4online CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;`), then `migrate` in the console, then `Start-Service` |
| the same + `yet tables hold rows (...)` | not what an interrupted first migration leaves | do **not** drop it: restore the last backup (`restore-db.ps1 -Database dw4online -Replace`) |
| `Migration 000N (...) was started at ... and did not finish` / `Migration 000N (...) failed after ...: <MariaDB error>` | migration N stopped half way (a failing statement, a killed process) | restore the backup made before the update (`restore-db.ps1 -Backup <file> -Database dw4online -Replace`), remove the cause (the MariaDB error says what; tell the developers), then `migrate` again, or go back to the old release (section 8, rollback) |
| `Database 'dw4online' has tables but no schema_version; refusing to touch it` | a database that is not (or no longer) DW4Online's | rows in it: point `Database:ConnectionString` at DW4Online's own database, or restore DW4Online's backup; no rows: drop and re-create it as above if it is meant for DW4Online |
| `The database is at schema N, newer than this build` | an older release on a newer database (a rollback without the restore) | restore the backup that matches the release, or start the newer release |
| `Migration N (...) changed after it was applied` | a build whose migration file differs from the applied one | do not use that build; tell the developers |
| `Another process is migrating this database` | a second `migrate` or a starting service at the same time | wait until the other one is done, then again |
| `The database did not answer within 60 s` (service log) | the database service is not running, or the port in the connection string is wrong | start it (`Start-Service mysql` / `MariaDB`); the service's restart actions try again by themselves |
| nothing after `Applying migration 000N ...` for minutes | a DDL statement hangs inside MariaDB (native AIO on a MariaDB before 10.6, section 1.1) | "A migration or a start that hangs" below |

How a service start ends, and what the log's last ` FAIL ` line of `DW4Online.Server.Startup` says (the lines of the
host itself, such as `Hosting failed to start` or `BackgroundService failed`, come before it with the details):

| Last FAIL line | `Start-Service` / System event log | Restart actions |
| --- | --- | --- |
| `Start failed: <the migrator's refusal or failure>`, or `Start failed: Relay:TicketKey ...` / `Access denied ...` (a setting the start checks) | "Failed to start service"; 7024 "The DW4Online service terminated with the following service-specific error: The data is invalid." (code 13; Windows supplies that text, the log says what is wrong) | none: an operator has to act |
| `Start failed: The database did not answer within 60 s ...` | "Failed to start service"; 7034 / 7031 "terminated unexpectedly" | 5 s, 30 s, 60 s |
| `The host failed: Failed to bind to address https://0.0.0.0:8443: address already in use.` (Kestrel) or `The host failed: Failed to bind the relay to 0.0.0.0:52041 (Relay:ListenAddress, Relay:Port): address already in use.` | as above | 5 s, 30 s, 60 s (they help once the other program lets the port go) |
| `The host failed: ...` with anything else Kestrel or a background service refuses while the host starts (an invalid `Kestrel:Endpoints` URL, for example) | as above | they run, and fail the same way until the setting is fixed |
| `The host stopped although the Service Control Manager did not ask it to ...` (after RUNNING; a background service failed, its FAIL / CRIT lines come first) | the service was running; 7034 / 7031 | 5 s, 30 s, 60 s |
| `The host failed while it stopped: ...` | the stop you asked for ends as stopped | none (you stopped it) |

### A migration or a start that hangs

Each migration takes well under a second. When `migrate` prints nothing after `Applying migration 000N ...` for
minutes, or `Start-Service DW4Online` keeps printing "Waiting for service 'DW4Online' to start..." for minutes (the
service log then ends with that `Applying migration` line), a statement hangs inside MariaDB; on XAMPP's 10.4 that is
the native AIO hang of section 1.1. The migrator gives up on a statement only after 10 minutes (then `Migration 000N
(...) failed after 600... s: The Command Timeout expired ...`, while MariaDB may keep the killed statement hanging and
its locks held), and the service accepts no STOP while its start is pending (stopping in the middle of a migration is
the one thing the start never risks), so `Stop-Service` fails with "cannot accept control messages". Do not wait:

1. End DW4Online: in the console Ctrl+C; the service by its process id (an elevated PowerShell):
   `Stop-Process -Id (Get-CimInstance Win32_Service -Filter "Name='DW4Online'").ProcessId -Force`. The SCM counts that
   as a crash and its restart actions start the service again after 5 s; that start refuses within a minute
   (`Another process is migrating this database` while the hung statement keeps the lock, or the migration's
   "started, not finished" mark) and stops for good (7024).
2. Restart MariaDB: `Restart-Service mysql` (or `MariaDB`). A hung statement shows in HeidiSQL's process list as
   "Killed" with a state such as "Creating table"; MariaDB may not stop while it hangs. If the restart has not finished
   after two minutes, end it by its process id in a second elevated PowerShell,
   `Stop-Process -Id (Get-CimInstance Win32_Service -Filter "Name='mysql'").ProcessId -Force`, then
   `Start-Service mysql`: InnoDB recovers at its next start (its error log, `<XAMPP>\mysql\data\mysql_error.log`,
   shows the recovery). Every database on that MariaDB is unavailable meanwhile. Before the restart, add the line of
   section 1.1 step 2 if it is missing. (In the rehearsal the first start after such a crash ended again within a
   second, a failed redo log write; `Start-Service mysql` once more recovered and ran from then on.)
3. The interrupted migration's mark stays in `schema_version`: go the way out of the table above (the backup from
   before the update, or for 0001 on a fresh database DROP / CREATE), then `migrate` in the console again.

A start that fails at once with System events 7009 "A timeout was reached (30000 milliseconds) while waiting for the
DW4Online service to connect." and 7000 "... did not respond to the start or control request in a timely fashion" is
a different case: the process did not even reach the SCM within 30 s. Release 0.1.0 did that while it migrated; later
releases connect before the database step, so only a very slow first start of freshly unpacked files (an antivirus
scan of the release) is left, and the `migrate` run before the start (sections 5 and 8) has already loaded them once.
`Start-Service DW4Online` again.

### 9.1 Migration 0012 `friends` (round I2-social): what it adds and the HeidiSQL check

The release with migration 0012 (DW4Online **0.1.6**, schema 12) adds friends and blocks on the website and in the API
(the game shows them from a later release on). It creates three tables and changes nothing that exists:

| Table | One row per | Columns |
| --- | --- | --- |
| `friendships` | pair of friends | `account_lo`, `account_hi` (the two account ids, the smaller first), `created_at` |
| `friend_requests` | open friend request | `from_account_id`, `to_account_id`, `state` (`pending` / `declined`), `delivered` (0: the receiver blocks the sender, the request never reaches him), `created_at`, `decided_at`, `cancelled_at` |
| `account_blocks` | block | `account_id` (who blocks), `blocked_account_id`, `created_at` |

Every table and column carries a comment (HeidiSQL shows it in the table editor's *Comment* column); there are no
binary columns. The migration needs no new right: the service user of section 2 (and root, section 2.1) can run it,
so the update is section 8 as always, with the backup in step 2 as the way back. No setting has to change: the new
`Friends:*` and `RateLimits:SocialPerMinute` / `FriendListSeconds` / `WebSocialPerMinute` keys have their defaults
(section 10). Requests expire 30 days after they were sent; the hourly housekeeping deletes them then (the log line
`Pruned ... expired friend requests`).

The check in HeidiSQL after the update (connect as root or as `dw4backup`, select the database `dw4online`, open a
*Query* tab, paste a query and press F9):

```sql
-- 1. The migration is applied: the top row is 12 "friends".
SELECT version, description, applied_at FROM schema_version ORDER BY version DESC LIMIT 3;

-- 2. The three tables exist (right after the update all three counts are 0).
SELECT (SELECT COUNT(*) FROM friendships) AS friendships, (SELECT COUNT(*) FROM friend_requests) AS requests,
       (SELECT COUNT(*) FROM account_blocks) AS blocks;

-- 3. Later, with user names: friendships, requests and blocks.
SELECT a.username AS player, b.username AS friend, f.created_at
  FROM friendships f JOIN accounts a ON a.id = f.account_lo JOIN accounts b ON b.id = f.account_hi
  ORDER BY f.created_at DESC;
SELECT a.username AS from_player, b.username AS to_player, r.state, r.delivered, r.created_at, r.decided_at, r.cancelled_at
  FROM friend_requests r JOIN accounts a ON a.id = r.from_account_id JOIN accounts b ON b.id = r.to_account_id
  ORDER BY r.created_at DESC;
SELECT a.username AS player, b.username AS blocks_player, k.created_at
  FROM account_blocks k JOIN accounts a ON a.id = k.account_id JOIN accounts b ON b.id = k.blocked_account_id
  ORDER BY k.created_at DESC;
```

Look, do not edit: the website and the API keep the rules (a declined request stays "sent" to its sender, a block is
never revealed), and a hand-made row can break them (a `delivered = 1` request from a blocked player reaches the
blocker). `delivered = 0` on a request from a player who is not blocked means it waits for a place among the
receiver's 100 open requests; the next answer, cancel or block of that receiver, or the hourly housekeeping, delivers it.
Deleting a whole row by hand (a friendship, a request, a block) is harmless. A deleted account (`deleted_<id>`)
has no rows in any of the three tables. The views with user names (`v_friends`, `v_friend_requests`, `v_blocks`) come
with a later release that also grants `CREATE VIEW`; until then the queries above stand in for them.

### 9.2 Migration 0013 `world` (round I1): the rooms give way to the online world

Which releases carry which migration: 0.1.6 brought 0012 (section 9.1, schema 12), 0.1.7 brings 0013 (schema 13); 0.1.8
and 0.1.9 add none; 0.2.0 brings 0014 (section 9.3, schema 14). The project's server went 0.1.4 (schema 11) -> 0.1.6
(0012 applied at its start) -> 0.1.7 (0013) -> 0.1.8 -> 0.1.9 -> 0.2.0 (0014); a server that skips releases applies
the missing migrations in one start (all with the same backup first, section 8, and the `CREATE VIEW` right below
before it).

The release with migration 0013 (DW4Online 0.1.7) replaces rooms by the online world (SEAMLESS.md 19): the game's *Play* enters the
world, a player's lobby is always his own, and taking the gate to an area where a visible instance runs puts him into
it. Rooms are gone after this update: `/api/v1/rooms` answers 410, the website's *Rooms* page became *Instances*, and
the server refuses game clients older than protocol **49** (the client release of the same round). Server and client
are released together; install the server first, then publish the client.

What the migration does (it runs at the start, or with `migrate` in the console, section 8):

| Object | What |
| --- | --- |
| `online_prefs` (new table) | per player: the visibility his instances start with, invites, whereabouts, friend toasts, and the host settings (camera, light, fast loading, randomizer seed and modes, last box); a player without a row has the defaults |
| `instance_history`, `instance_history_members` (new tables) | every instance (host, first stage, visibility, difficulty, settings, start, end and why) and who was in it when; kept 90 days (`World:HistoryDays`) |
| `accounts.last_seen_at`, `achievement_unlocks.instance_id`, `speedrun_times.instance_id` (new columns) | "last online" for friends; the instance an unlock or a best time came from (older rows keep `room_id`) |
| `online_story_history.reason` | gains the value `mirror` (shared progress, a later round) |
| data | each room host's newest room settings become his `online_prefs` (camera, light, fast loading, randomizer); nothing of a room's story is copied to its guests (a player who was only a guest starts the world with an empty story, as decided) |
| views `v_friends`, `v_friend_requests`, `v_blocks`, `v_instance_history`, `v_staff_audit` | read-only tables with user names for HeidiSQL |
| `rooms`, `room_members` | stay as they are, read-only; a later release drops them |

**The one new right.** The views need the right `CREATE VIEW`. Root has it (section 2.1: nothing to do). The service
user of section 2 does not: give it **before** the update, as root, in HeidiSQL (connect as root, select the database
`dw4online`, open a *Query* tab, paste, press F9):

```sql
GRANT CREATE VIEW ON dw4online.* TO 'dw4online'@'localhost';
GRANT CREATE VIEW ON dw4online.* TO 'dw4online'@'127.0.0.1';
SHOW GRANTS FOR 'dw4online'@'localhost';   -- the second line now ends with ", CREATE VIEW ON `dw4online`.* TO ..."
```

Without it the migrator stops before it changes anything: `migrate` prints `FAIL: Migration 0013 (world) needs the
right CREATE VIEW on database 'dw4online', which the user ... does not have. Nothing was changed (the database stays at
schema 12). Grant it as root ...`; the service log shows the same as a ` FAIL ` line. Run the two `GRANT` lines and
`migrate` again (no backup restore is needed: nothing was changed). The backup user of section 2 already has
`SHOW VIEW`, so the nightly dump keeps the views. The views run with the rights of whoever reads them (`SQL SECURITY
INVOKER`), so a restored backup works on a server without the `dw4online` user too.

**The settings.** In `config\appsettings.Production.json`, before the start:

1. `Play:MinimumProtocol` to **49** and `Play:CurrentProtocol` to **49**. (The server refuses anything below 49 anyway,
   whatever the file says: the floor is in the code. Set it so the file says what happens.)
2. `Play:RequireRoom` renamed to `Play:RequireWorld` (same value, `true`). The old name is still read for this release.
3. The `Rooms` section can go (its `StoryWhileOwnerAway` is no longer used; the service writes one warning line if it is
   still there). `Rooms:RankedDataHashes`, if you set it, becomes `World:RankedDataHashes` (the old place is still read
   for this release).
4. Optional, with their defaults: `World:DataHashes` (empty: any game data may play; a list of hashes admits only
   those), `World:RankedDataHashes` (empty: every game data counts for achievements), `World:HistoryDays` (90).

**The update** is section 8 with these steps in this order: (1) the backup in HeidiSQL or `backup-db.ps1` (section 7;
the migration's way back), (2) the `GRANT CREATE VIEW` lines above (only with the service user of section 2), (3) the
settings above, (4) the server release (`migrate`, `Start-Service`), (5) the client release for the players. Rooms
that were open close at the start (`server_restart`). The way back is section 8's rollback with the backup of step 1
(the old release refuses schema 13).

The check in HeidiSQL after the update (as root or `dw4backup`, database `dw4online`, a *Query* tab, F9):

```sql
-- 1. The migration is applied: the top row is 13 "world".
SELECT version, description, applied_at FROM schema_version ORDER BY version DESC LIMIT 3;

-- 2. The room hosts' settings arrived (one row per host who ever opened a room with settings).
SELECT a.username, p.default_visibility, p.camera_mode, p.light_mode, p.fast_loading, p.randomizer_seed, p.randomizer_modes
  FROM online_prefs p JOIN accounts a ON a.id = p.account_id ORDER BY a.username;

-- 3. Later: who played where (the views show user names; open one in the tree and use its Data tab, or query it).
SELECT * FROM v_instance_history ORDER BY started_at DESC LIMIT 50;   -- host, area, players, minutes, end_reason
SELECT * FROM v_staff_audit ORDER BY at DESC LIMIT 50;               -- what moderators did and looked at
SELECT * FROM v_friends ORDER BY friends_since DESC;
SELECT * FROM v_friend_requests ORDER BY created_at DESC;
SELECT * FROM v_blocks ORDER BY created_at DESC;
```

Look, do not edit: the relay writes `instance_history` and its members through a queue while instances run, and the
website shows them. Editing `online_prefs` by hand is harmless (the next login reads it); keep `randomizer_seed` empty
or between 1 and 4294967295 and `randomizer_modes` as the table comment says (the table refuses other values).
A deleted account (`deleted_<id>`) has no `online_prefs` and no member rows; instances it hosted keep their row.

### 9.3 Migration 0014 `reports` (round I1b, DW4Online 0.2.0): the arrival prompt, go along, removal and reports

Release 0.2.0 carries round I1b of the online world (SEAMLESS.md 19.25): the arrival prompt at the gate and the return
mat (choosing among the area's instances, *Arriving now* included), *Go along* into a dungeon, the host's *Remove* that
binds at every decision, and *Report player*. Its relay speaks only protocol **50** (the client's stage snapshot keeps
one more engine value, so 49 and 50 engines would play apart): **install the server first, then publish the client
release of the same round**; until the players have it, their game says the client is outdated.

What migration 0014 adds (it runs at the start, or with `migrate` in the console, section 8; well under a second):

| Object | What |
| --- | --- |
| `player_reports` (new table) | *Report player*: who reported whom, the reason from a fixed list (griefing, cheating, name, other; no free text), from the game or the API (`source`), the instance both played in (NULL when none, or after the history was pruned), open or done and by which moderator and when; a comment on every column; a reporter's rows go with his account, rows about a deleted account keep its id |
| view `v_player_reports` | the reports with user names for HeidiSQL |

No new right: the view needs `CREATE VIEW`, which section 9.2 granted for 0013 (the migrator checks it first and changes
nothing without it; then run section 9.2's two `GRANT` lines and `migrate` again). 0013 stays exactly as it went live
with 0.1.7 (its checksum is unchanged), and `v_staff_audit` stays 0013's.

**The settings.** In `config\appsettings.Production.json`, before the start: `Play:MinimumProtocol` to **50** and
`Play:CurrentProtocol` to **50** (the server refuses anything below 50 anyway; set it so the file says what happens).
The new relay options have code defaults and need no line: `World:ArrivalPrompt` (true), `World:OptionsRefreshMs`
(1000), `World:OfferReopenSeconds` (120), `World:AnswerNoticeSeconds` (20), `World:GoAlongWaitSeconds` (60),
`World:RemovedBarMinutes` (10), `Reports:PerDay` (10), `Reports:RepeatHours` (24), `Reports:SharedInstanceHours` (24),
`Reports:DoneDays` (180: a handled report is deleted that long after it was handled, by the hourly housekeeping; the privacy
page shows the configured period), `RateLimits:ReportsPerMinute` (10).
Never set the test options (`World:TestHooks`, `World:TestGoHook*`, `World:TestGoBarrierHoldMs`, `World:TestJoinHoldMs`,
`World:TestDropPlans`) on the live server.

**The update** is section 8 with these steps in this order: (1) the backup (section 7; the migration's way back), (2)
the settings above, (3) the server release 0.2.0 (`migrate`: "... done in N s: schema 14", `Start-Service`;
`/api/v1/status` says `"version":"0.2.0"` and `"minimumProtocol":50`), (4) only then, with the server running and checked, the
client release for the players. The way back is section 8's rollback with the backup of step 1 (0.1.9 refuses schema 14)
**and the two settings back to 49** before the start (section 8, "A release that raised the protocol"); a client release of
0.2.0 that went out already is withdrawn with it (it cannot play on 0.1.9).

The check in HeidiSQL after the update (as root or `dw4backup`, database `dw4online`, a *Query* tab, F9):

```sql
-- 1. The migration is applied: the top row is 14 "reports".
SELECT version, description, applied_at FROM schema_version ORDER BY version DESC LIMIT 3;

-- 2. Later: the players' open reports, newest first (user names; handle them on the website, not here).
SELECT * FROM v_player_reports WHERE state = 'open' ORDER BY created_at DESC;
```

Reports are handled on the website (*Admin > Reports*, *Done* / *Reopen*), not in HeidiSQL: the website writes the audit
row (`report_handled`, `report_reopened`) and the handler's name with it. A report about a moderator's own account is
shown to him without its reporter and instance, and only another moderator can close it.

## 10. Operating notes

- The online world (section 9.2, SEAMLESS.md 19): `World:DataHashes` (empty: any game data plays), `World:RankedDataHashes`
  (empty: every game data counts for achievements), `World:HistoryDays` (90: how long `instance_history` keeps an
  instance); the relay's own `World:*` keys (members, instances, the host's grace, the presence budget) have the defaults of
  SEAMLESS.md 19.17.5. Achievements count only while the player is in the world (`Play:RequireWorld`, true). Staff see
  every instance on the website's *Admin > Instances* page and can end one or send a player home there; both are audited
  (`v_staff_audit`). Since release 0.1.9 an instance there is its relay run and wid (the relay numbers wids from 1 again
  at every start): a page loaded before a server restart is refused ("The server restarted since this list was loaded";
  reload it), and `POST /api/v1/admin/instances/{wid}/end` and `.../remove` need the `run` of `GET /api/v1/admin/instances`
  as `?run=`. The hourly housekeeping deletes instances older than `World:HistoryDays`.
- Removal and *Report player* (round I1b, SEAMLESS.md 19.6.6, 19.16): a host removes a guest from the game's *Instance*
  page (the guest goes home and cannot see or join that instance for `World:RemovedBarMinutes`, 10; a host who blocks a
  guest removes him the same way). Players report others from the game's player card or `POST /api/v1/reports`: at most
  `Reports:PerDay` (10) a day each, a second report about the same player within `Reports:RepeatHours` (24) adds nothing.
  The website's *Admin* page shows how many are open; *Admin > Reports* lists them with the reported player's account
  page (his instances, story and achievements; lock or ban there) and *Done* / *Reopen* (audited `report_handled`,
  `report_reopened`). An account's deletion removes its own reports; reports about it keep the anonymous account.
- Until release 0.1.6 (rooms): the room story while the owner is away (`Rooms:StoryWhileOwnerAway` in `appsettings.Production.json`, SEAMLESS.md
  8.6): `drop` (the default: progress the guests make while the room's owner is away is not saved and leaves their
  games at the next stage change, with a notice), `hold` (kept on the server and merged when the owner returns) or
  `apply` (saved at once). The story's plausibility per visit (`Instances:StoryMinVisitSeconds`, 10;
  `StoryBurst` 10, `StoryMessagesPerMinute` 60) rarely needs a change. Every saved revision of an account's online
  story is kept (`OnlineCharacter:StoryHistoryRevisions`, 100): the admin page of an account lists them and puts one
  back (*Put back*) when misuse or an error damaged it; resetting the online character empties the story.
- `Play:MinimumProtocol` and `Play:CurrentProtocol` follow the game client (45 since round S2, 46 since round 63: a
  member's own menus, 47 since round G-core: flag 503 as story and the reason queued, 48 since the READY check of a join
  from a checkpoint at its check tick, SEAMLESS.md 6.13.5, 49 since release 0.1.7: the online world, section 9.2): older
  clients are asked to update. From 49 on the code refuses anything below 49 whatever the file says (`/api/v1/status`
  announces the effective minimum); keep both at 49 (the production example's values) until a release names a higher one.
- Friends and blocks (section 9.1, SEAMLESS.md 19.11): the limits per account in `Friends:*` (`RequestsPerHour` 20,
  `RequestsPerDay` 100, `MaxPendingOut` 50, `MaxPendingIn` 100, `MaxFriends` 200, `MaxBlocks` 500, `RequestDays` 30)
  and `RateLimits:SocialPerMinute` (60), `FriendListSeconds` (one `GET /friends` per 2 s), `WebSocialPerMinute` (60)
  rarely need a change. A request refused by one of them writes an audit row `social_limit` with the limit's name (at
  most once per account and limit an hour); friend and block changes themselves are not audited (the tables are the
  record). `MaxPendingIn` never refuses: requests beyond it wait and are delivered as places free.
- Logs: `logs\dw4online-yyyyMMdd.log` (UTC, 30 days) when `Logging:File:Directory` is set; errors are ` FAIL ` lines
  (with the exception below them), crashes also appear in the Windows Application event log (source ".NET Runtime").
- Staff tools without the website: `DW4Online.Server.exe reset-password <user> --settings ...`, the `/api/v1/admin/...`
  endpoints with an admin token.
- Privacy (GDPR, the server is in Germany): the database holds user names, optional e-mail addresses, who is friends
  with whom, friend requests and blocks (section 9.1), IP addresses in `sessions.created_ip` and `audit_log.ip` (audit rows are deleted after 180 days, old sessions after 30 days). The
  website has a privacy page (`/privacy`, fill in the `Web:Operator*` settings) and account deletion (by the player in
  the account settings, or by an admin on request on the admin page); deleting removes the personal records at once
  and leaves an anonymous `deleted_<id>` row. Backups keep deleted data until they roll over (14 daily + 8 weekly).
- Ports 80 / 443 must not collide with other software on the server (section 4.1); the Pangya web port 88 is not used.
