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
| TLS certificate (now) | self-signed for `IP 88.198.52.221` in `LocalMachine\My`, pinned by the game client (section 6.1) |
| Ports | 443 TCP (HTTPS: API and website; another port such as 8443 when 443 is taken, section 4.1), 52041 TCP (relay), 80 TCP only for a later domain's certificate validation; MariaDB 3306 on 127.0.0.1 only; plain HTTP on 127.0.0.1:5280 for checks on the server itself (`appsettings.json`'s endpoint, never reachable from outside) |

**The server as installed on 31.10.2026 (release 0.1.0)** uses the variants this document describes next to the
defaults: the existing **XAMPP MariaDB 10.4.32** (`C:\Users\Dekirai\xampp\mysql`) as the Windows service `mysql`
(section 1.1), **root as the only database login** (section 2.1), **HTTPS on port 8443** because a Traefik reverse
proxy owns 443 (section 4.1); relay 52041, loopback check endpoint 127.0.0.1:5280. Its first `Start-Service` failed:
release 0.1.0 ran the migrations before it connected to the Service Control Manager, migration 0001 took longer than
the 30 s the SCM gives a starting process, the SCM ended the process half way, and every later start refused the
half-migrated database. The way out was `DROP DATABASE` / `CREATE DATABASE` and `DW4Online.Server.exe migrate` in a
console. Later releases connect to the SCM first and migrate while they report "start pending" (section 9), and
every refusal names its way out; run `migrate` in a console before a start anyway (sections 5 and 8).

The scripts in this folder are meant to be read and then run by you in an elevated PowerShell on the server. (They
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
avoids the types 10.4 lacks, and every migration and integration test passes against a MariaDB 10.4.32 with XAMPP's
**unchanged** `my.ini` settings (non-strict `sql_mode`, `collation-server=utf8mb4_general_ci`, a 16 MB buffer pool;
migrations 1 to 7 took 0.25 s there). What it costs: MariaDB 10.4 reached its end of life in June 2024 and gets no
security fixes any more. That is acceptable only because it answers on 127.0.0.1 alone (step 3); move to a supported
MariaDB (section 1, then a backup and restore) when convenient.

1. Make XAMPP's MySQL a Windows service: start the **XAMPP Control Panel as administrator**, stop MySQL there if it
   runs, tick the **"Svc"** box in the MySQL row and confirm. The Control Panel installs the service **`mysql`**
   (`mysqld.exe --defaults-file=<XAMPP>\mysql\bin\my.ini mysql`, automatic start, LocalSystem). Start it once with the
   Control Panel's Start button or `Start-Service mysql`. From now on start and stop it as a service; with DW4Online
   installed, stop DW4Online first (it depends on `mysql`).
2. Add two lines at the **end of the `[mysqld]` section** of `<XAMPP>\mysql\bin\my.ini` (just above `[mysqldump]`;
   a later line wins over XAMPP's own `max_allowed_packet=1M` further up) and leave the rest as it is, since other
   databases on this MariaDB rely on it:

   ```ini
   bind-address=127.0.0.1
   max_allowed_packet=16M
   ```

3. `Restart-Service mysql`, then check that it listens on the loopback address only:

   ```powershell
   Get-NetTCPConnection -State Listen -LocalPort 3306 | Select-Object LocalAddress, OwningProcess   # only 127.0.0.1
   ```

4. XAMPP's root accounts (usually `root@localhost`, `root@127.0.0.1` and `root@::1`) have **no password** out of
   the box. Give every one of them the same strong password (HeidiSQL: Tools -> User manager, each root entry; or
   `ALTER USER 'root'@'localhost' IDENTIFIED BY '...'` and the same for the others; `SELECT user, host FROM mysql.user`
   lists them). XAMPP's phpMyAdmin then needs that password in `<XAMPP>\phpMyAdmin\config.inc.php` or stays unused;
   HeidiSQL asks for it.
5. Everywhere below: `-DatabaseService mysql` for `install-service.ps1`, `-MariaDbBin <XAMPP>\mysql\bin -DumpExe
   mysqldump.exe` for `backup-db.ps1`, `-MariaDbBin <XAMPP>\mysql\bin -ClientExe mysql.exe` for `restore-db.ps1`
   (XAMPP's 10.4 has `mysqldump.exe` and `mysql.exe`, no `mariadb-dump.exe` / `mariadb.exe`; both scripts also take the
   old names by themselves when the folder has no new ones). No firewall rule exists for XAMPP's MySQL by default; if
   one was added, disable it as in section 1 step 6.

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
   password (root and its password in the root-only variant, section 2.1; the database's port), a relay ticket key
   (`app\DW4Online.Server.exe gen-relay-key`), the IP address (the TLS settings of section 6.1), the HTTPS port
   (`Kestrel:Endpoints:Https:Url`, section 4.1 when 443 is taken), and the `Web` section: `RequireHttps` true,
   `DataProtectionKeysDirectory` = the `keys` folder, and your name, postal address and a contact in `OperatorName` /
   `OperatorAddress` / `OperatorContact` (the privacy page names the controller, GDPR Art. 13). The relay is on by
   default (`Relay:Enabled`); it needs `ListenAddress` 0.0.0.0 and the ticket key, and `Service:RelayHost` tells
   clients where it is. This file holds the secrets: never copy it into Git or the app folder.
3. Register the service, folder permissions and firewall rules (MSI MariaDB on 443; the XAMPP variant with HTTPS on
   8443 below it):

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

   5280 is the plain-HTTP endpoint on 127.0.0.1 that `appsettings.json` defines and the production settings keep
   (`Kestrel:Endpoints:Http`; `curl.exe http://127.0.0.1:5280/health` works on the server without the certificate).
   If another program uses 5280, give the endpoint another free loopback port in `appsettings.Production.json`; a
   port in use stops the service at its start.
6. Create the TLS certificate for the IP address and note its pins (section 6.1, `new-selfsigned-cert.ps1`).

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
   (`-HttpsPort 8443` does the same and also puts 8443 into the settings the script prints at creation);
   `curl.exe -k https://88.198.52.221:8443/health`.
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
.\app\DW4Online.Server.exe migrate --settings config\appsettings.Production.json   # ... "Migrating database 'dw4online' done in N s: schema 7"
Start-Service DW4Online
Get-ChildItem logs\dw4online-*.log | Sort-Object Name | Select-Object -Last 1 | Get-Content -Tail 30
curl.exe http://127.0.0.1:5280/health          # {"status":"ok","schema":7}, on the server itself
curl.exe -k https://88.198.52.221/health      # the same over HTTPS (:8443 in section 4.1); -k: the certificate is self-signed (section 6.1)
.\app\DW4Online.Server.exe tls-pin --host 88.198.52.221   # the certificate the service picks from the store, and its pin
powershell -ExecutionPolicy Bypass -File app\deploy\new-selfsigned-cert.ps1 -PinServedBy 88.198.52.221   # the pin of the certificate the HTTPS port really serves (-PinServedPort 8443 in section 4.1)
```

Both pins must equal the first pin `new-selfsigned-cert.ps1` printed when it made the certificate (section 6.1).
`Start-Service` may print `WARNING: Waiting for service 'DW4Online' to start...` a few times while the service waits
for the database or migrates: that is the service reporting progress, not an error. The log then says "Database schema
is current" (or "Applied migrations ..." if it migrated itself), "Now listening on: https://0.0.0.0:443" and the TLS
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

   (With HTTPS on 8443, section 4.1, add `-HttpsPort 8443`: the settings the script prints then name that port.)
   `E:` stands for a USB stick or any place that is not this server. On a rented server reached over Remote Desktop
   that is your own PC: with drive redirection on, write it there directly (`-SparePfx \\tsclient\C\Users\<you>\Desktop\dw4online-spare.pfx`),
   otherwise write it to the server's desktop, copy it to your PC through the Remote Desktop clipboard and delete it on
   the server (`Remove-Item`). [new-selfsigned-cert.ps1](new-selfsigned-cert.ps1)
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
2. In `appsettings.Production.json` (the example file already holds these values; the port is 8443 in section 4.1):

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
   picks from the store (the same selection, read from the store, no connection) and its pin;
   `new-selfsigned-cert.ps1 -PinServedBy 88.198.52.221` (with `-PinServedPort 8443` in section 4.1) connects to the
   HTTPS port as the game client does (no host name) and prints the pin of the certificate actually served; from
   another PC `curl.exe -k https://88.198.52.221/health` (`https://88.198.52.221:8443/health`) answers
   `{"status":"ok",...}`.

A certificate made again before any client pins it (for example after a failed first attempt) is fine: remove the
first one with its key (`Get-ChildItem Cert:\LocalMachine\My | Where-Object FriendlyName -eq 'DW4Online 88.198.52.221'
| ForEach-Object { Remove-Item -LiteralPath "Cert:\LocalMachine\My\$($_.Thumbprint)" -DeleteKey }`), run step 1 again
and use only the new pins.

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

**Case 2: another web server serves dekirai.crygod.de on this machine** (Traefik, IIS, nginx, Caddy). It forwards
`/dw4online/...` with the prefix kept to `http://127.0.0.1:5280` and sends `X-Forwarded-For` and `X-Forwarded-Proto`;
DW4Online then listens only there (remove its `Https` endpoint and `Tls:StoreHostName`):

```json
"Kestrel": { "Endpoints": { "Http": { "Url": "http://127.0.0.1:5280" } } },
"Service": { "PathBase": "/dw4online", "BehindProxy": true, "RelayHost": "88.198.52.221" }
```

`BehindProxy` believes those headers only from a proxy on this machine. Port 443 then belongs to that web server, so
it must also serve the pinned IP certificate to connections without a name (IIS: an https binding on 88.198.52.221
port 443 without host name, using the same certificate from `LocalMachine\My`, no export needed; Traefik: TLS
passthrough for connections without SNI) and forward them to DW4Online as well, or the clients that still pin the IP
certificate stop working. The relay keeps its own port 52041 in both cases.

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
Also keep a copy of `config\appsettings.Production.json` (it holds the database password and the relay key), stored
encrypted. The `keys` folder needs no backup: its keys are DPAPI-encrypted for this machine's service account and only
protect anti-forgery tokens; if it is lost, open website forms simply have to be reloaded.

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
   curl.exe -k https://88.198.52.221/health
   ```

   `migrate` runs the new release's migrations in the console, before the service starts (section 9): it prints each
   one with its time, and if it refuses or fails, nothing starts and the screen says why and what to do (usually: the
   rollback below). The new `app` folder inherits the root's permissions (service read and execute), and the firewall
   rules and the service name the same path, so nothing else changes.

Rollback: stop the service, swap `app` and `app.previous` back; if the update applied a migration (or one failed half
way), restore the backup from step 2 into `dw4online` first (the old build refuses a database that is newer than
itself; XAMPP: add `-MariaDbBin <XAMPP>\mysql\bin -ClientExe mysql.exe`):

```powershell
cd D:\Dekirai\Servers\DW4Online
Stop-Service DW4Online
Rename-Item app app.failed; Rename-Item app.previous app
powershell -ExecutionPolicy Bypass -File app\deploy\restore-db.ps1 -Backup backups\daily\dw4online-<date>.sql.gz -Database dw4online -Replace   # only after a migration
.\app\DW4Online.Server.exe migrate --settings config\appsettings.Production.json   # "The database schema is current."
Start-Service DW4Online
```

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
  (`Migrating database 'dw4online' done in 0.26 s: schema 7`), and exits with 1 and a `FAIL:` message if it refuses or
  fails;
- **at the service start** (`Database:AutoMigrate`, on by default), so a start after a forgotten `migrate` still
  works. The service connects to the Service Control Manager first and reports "start pending" with a new checkpoint
  every 5 s while it waits for the database (up to `Database:StartupWaitSeconds`, 60 s) and migrates, however long
  that takes; it reports "running" only once it listens. (Release 0.1.0 migrated before it connected, and the SCM
  ended it after 30 s, see the top of this document.)

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

A service start that the migrator refuses (or that wrong settings stop) ends with `Start-Service` failing ("Failed to
start service") and the System event log entry 7024 "The DW4Online service terminated with the following
service-specific error: The data is invalid." (code 13; Windows supplies that text, the log says what is wrong); the
service is not restarted (an operator has to act). A database that does not answer, or a port that is in use, ends the
start without that entry ("terminated unexpectedly"), and the restart actions (5 s, 30 s, 60 s) try again. The last
` FAIL ` line of the log always says which it was.

## 10. Operating notes

- Logs: `logs\dw4online-yyyyMMdd.log` (UTC, 30 days) when `Logging:File:Directory` is set; errors are ` FAIL ` lines
  (with the exception below them), crashes also appear in the Windows Application event log (source ".NET Runtime").
- Staff tools without the website: `DW4Online.Server.exe reset-password <user> --settings ...`, the `/api/v1/admin/...`
  endpoints with an admin token.
- Privacy (GDPR, the server is in Germany): the database holds user names, optional e-mail addresses, IP addresses in
  `sessions.created_ip` and `audit_log.ip` (audit rows are deleted after 180 days, old sessions after 30 days). The
  website has a privacy page (`/privacy`, fill in the `Web:Operator*` settings) and account deletion (by the player in
  the account settings, or by an admin on request on the admin page); deleting removes the personal records at once
  and leaves an anonymous `deleted_<id>` row. Backups keep deleted data until they roll over (14 daily + 8 weekly).
- Ports 80 / 443 must not collide with other software on the server (section 4.1); the Pangya web port 88 is not used.
