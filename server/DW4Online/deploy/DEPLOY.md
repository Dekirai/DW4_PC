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
| TLS certificate (now) | self-signed for `IP 88.198.52.221` in `LocalMachine\My` on a key made once and kept offline, pinned by the game client (sections 6.1, 6.4) |
| Kept once, never regenerated | the two TLS keys (in use, spare; offline), `Relay:TicketKey` (in the settings file and its encrypted copy) |
| Ports | 443 TCP (HTTPS: API and website), 52041 TCP (relay), 80 TCP only for a later domain's certificate validation; MariaDB 3306 on 127.0.0.1 only; plain HTTP on 127.0.0.1:5280 for checks on the server itself (`appsettings.json`'s endpoint, never reachable from outside) |

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
and run in `mariadb.exe -u root -p` (if PowerShell does not find `mariadb.exe`, add the client folder for this
session: `$env:PATH += ';C:\Program Files\MariaDB 11.4\bin'`):

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
   password, the relay ticket key, the IP address (the TLS settings of
   section 6.1), and the `Web` section: `RequireHttps` true, `DataProtectionKeysDirectory` = the `keys` folder, and your
   name, postal address and a contact in `OperatorName` / `OperatorAddress` / `OperatorContact` (the privacy page names
   the controller, GDPR Art. 13). The relay is on by default (`Relay:Enabled`); it needs `ListenAddress` 0.0.0.0 and
   the ticket key, and `Service:RelayHost` tells clients where it is. This file holds the secrets: never copy it into
   Git or the app folder.

   **The relay ticket key is made once and kept.** Make it with `app\DW4Online.Server.exe gen-relay-key` (32 random
   bytes, base64) the first time only, put it into `Relay:TicketKey` and keep it with the encrypted copy of this file
   (section 7). Updates, restores and a moved server keep the same key: it signs the one-time relay tickets (a new key
   only breaks the tickets of the last 60 seconds) and, through HKDF, keys the security log's tags of unknown user names
   (`audit-tag`): with a new key the tags written before can no longer be matched, and account deletions leave them
   behind. Without the key a production start fails on purpose (a temporary key only in Development).
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
5. Check that nothing else listens on the service's ports (a XAMPP Apache often holds 443 and 80, IIS or another
   http.sys user shows up as `System`, PID 4):

   ```powershell
   Get-NetTCPConnection -State Listen -LocalPort 443,52041,5280 -ErrorAction SilentlyContinue   # no output = free
   ```

   5280 is the plain-HTTP endpoint on 127.0.0.1 that `appsettings.json` defines and the production settings keep
   (`Kestrel:Endpoints:Http`; `curl.exe http://127.0.0.1:5280/health` works on the server without the certificate).
   If another program uses 5280, give the endpoint another free loopback port in `appsettings.Production.json`; a
   port in use stops the service at its start.
6. Install the TLS certificate made on the kept key and write the client's pins (section 6.1, `tls-key.ps1`,
   `new-selfsigned-cert.ps1 -ImportPfx`, `print-pins.ps1`).

Kestrel binds the ports itself; on Windows a non-admin account may bind ports below 1024 with sockets (no URL ACL is
needed because http.sys is not used).

## 5. First start

```powershell
Start-Service DW4Online
Get-Content D:\Dekirai\Servers\DW4Online\logs\dw4online-*.log -Tail 30
curl.exe -k https://88.198.52.221/health      # {"status":"ok","schema":9}; -k: the certificate is self-signed (section 6.1)
D:\Dekirai\Servers\DW4Online\app\DW4Online.Server.exe tls-pin --host 88.198.52.221   # the certificate the service picks from the store, and its pin
powershell -ExecutionPolicy Bypass -File D:\Dekirai\Servers\DW4Online\app\deploy\new-selfsigned-cert.ps1 -PinServedBy 88.198.52.221   # the pin of the certificate port 443 really serves
```

Both pins must equal the first pin `new-selfsigned-cert.ps1` printed when it made the certificate (section 6.1).

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

   `E:` is wherever the PFX of step 2 is. On a rented server reached over Remote Desktop that is your own PC: with
   drive redirection on, read it there directly (`-ImportPfx \\tsclient\E\dw4online\server.pfx`), otherwise copy it to
   the server's desktop through the Remote Desktop clipboard. [new-selfsigned-cert.ps1](new-selfsigned-cert.ps1) asks
   for the PFX password, installs the certificate in `LocalMachine\My` without export permission, gives
   `NT SERVICE\DW4Online` read access to its key (the service runs the TLS handshakes) and prints its pin, checked
   against the service's own `DW4Online.Server.exe tls-pin`. Delete the PFX from the server afterwards
   (`Remove-Item`): the kept PEM key is the master copy. `-DryRun` shows what it would do.
4. In `appsettings.Production.json` (the example file already holds these values):

   ```json
   "Kestrel": { "Endpoints": { "Https": { "Url": "https://0.0.0.0:443" } } },
   "Tls": { "StoreHostName": "88.198.52.221" },
   "Service": { "RelayHost": "88.198.52.221" },
   "Web": { "RequireHttps": true, "Hsts": false }
   ```

   then `Restart-Service DW4Online` (or wait: the service re-reads the store every 10 minutes). Its log names the
   certificate and its pin (`TLS certificate for 88.198.52.221: <thumbprint>, valid until ..., SPKI pin ...`).
5. **The pins for the game client**, on the development PC (the pin script):

   ```powershell
   powershell -ExecutionPolicy Bypass -File print-pins.ps1 -KeyFile E:\dw4online\server-key.pem,E:\dw4online\spare-key.pem -Out ..\..\..\local\online_pins.txt
   ```

   [print-pins.ps1](print-pins.ps1) prints both pins (in use first, then the spare) and writes them to
   `local/online_pins.txt` of the DW4_PC checkout, which the client build reads (section 6.4). It also reads
   certificates (`-CertFile`) and PFX files (`-Pfx`).
6. **Check**: `print-pins.ps1 -Server 88.198.52.221` (from any PC; a TLS handshake, nothing is sent) prints the key the
   server presents; together with the `-KeyFile` list of step 5 it checks that this is the key in use. On the server,
   `.\DW4Online.Server.exe tls-pin --host 88.198.52.221` (in the app folder) prints the certificate the service picks
   from the store (the same selection, read from the store, no connection) and its pin, and
   `new-selfsigned-cert.ps1 -PinServedBy 88.198.52.221` connects to port 443 as the game client does (no host name)
   and prints the pin of the certificate actually served; from another PC `curl.exe -k https://88.198.52.221/health`
   answers `{"status":"ok",...}`.

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
the build the same way, but a renewal makes a new key and a new pin.

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

Schedule it nightly as SYSTEM (elevated PowerShell):

```powershell
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument '-NoProfile -ExecutionPolicy Bypass -File D:\Dekirai\Servers\DW4Online\app\deploy\backup-db.ps1'
$trigger = New-ScheduledTaskTrigger -Daily -At 03:30
Register-ScheduledTask -TaskName 'DW4Online DB backup' -Action $action -Trigger $trigger -User 'SYSTEM' -RunLevel Highest
```

Copy the backups off the server as well (for example to your own FTPS server with WinSCP scripting, or a storage box).
Also keep a copy of `config\appsettings.Production.json` (it holds the database password and the relay ticket key, which
stays the same for good, section 4), stored encrypted. The TLS keys are kept offline anyway (section 6.1); the
certificate in the store needs no backup, as a new one is made on the kept key. The `keys` folder needs no backup: its keys are DPAPI-encrypted for this machine's service account and only
protect anti-forgery tokens; if it is lost, open website forms simply have to be reloaded.

Run the backup once by hand and check the file (elevated PowerShell; `Start-ScheduledTask 'DW4Online DB backup'` runs
it as SYSTEM the same way):

```powershell
powershell -ExecutionPolicy Bypass -File D:\Dekirai\Servers\DW4Online\app\deploy\backup-db.ps1   # "Backup written: ...sql.gz (.. KB)"
```

Restore drill (once, and after schema changes) with [restore-db.ps1](restore-db.ps1), which asks for the MariaDB root
password once and prints the restored database's account count and schema version:

```powershell
cd D:\Dekirai\Servers\DW4Online
$latest = (Get-ChildItem backups\daily\*.sql.gz | Sort-Object Name | Select-Object -Last 1).FullName
powershell -ExecutionPolicy Bypass -File app\deploy\restore-db.ps1 -Backup $latest     # into dw4online_restore
mariadb.exe -u root -p -e "DROP DATABASE dw4online_restore"
```

Do not pipe a dump into the client in Windows PowerShell 5.1 (`7z e -so x.sql.gz | mariadb.exe ...`): the pipe
re-encodes the text and silently turns every non-ASCII character into `?` (the rehearsal restored "PC von Jürgen" as
"PC von J??rgen"). `restore-db.ps1` decompresses to a file and lets cmd.exe feed it unchanged; by hand that is
`cmd /c "mariadb.exe -u root -p dw4online_restore < dump.sql"`.

## 8. Updates and rollback

1. Announce the update and pick a quiet time: stopping the service ends running co-op sessions and rooms (they close
   with `server_restart` at the next start). The service reads its settings only when it starts, so `Service:Motd` or
   `Service:Maintenance` (no new rooms) take effect only after a restart: announce elsewhere, or set them for a
   restart some time before the update.
2. In an elevated PowerShell, with the new zip on the server (for example `C:\Users\Administrator\Desktop\DW4Online-<version>.zip`):

   ```powershell
   cd D:\Dekirai\Servers\DW4Online
   Stop-Service DW4Online
   powershell -ExecutionPolicy Bypass -File app\deploy\backup-db.ps1      # migrations commit DDL implicitly: this backup is the way back
   if (Test-Path app.previous) { Remove-Item -Recurse -Force app.previous }
   Rename-Item app app.previous
   Expand-Archive C:\Users\Administrator\Desktop\DW4Online-<version>.zip -DestinationPath app
   Start-Service DW4Online
   Get-Content (Get-ChildItem logs\dw4online-*.log | Sort-Object Name | Select-Object -Last 1).FullName -Tail 20   # "Database schema is current" or "Applied migrations ..."
   curl.exe -k https://88.198.52.221/health
   ```

   The new `app` folder inherits the root's permissions (service read and execute), and the firewall rules and the
   service name the same path, so nothing else changes.

Rollback: stop the service, swap `app` and `app.previous` back; if the update applied a migration, restore the backup
from step 2 into `dw4online` first (the old build refuses a database that is newer than itself):

```powershell
cd D:\Dekirai\Servers\DW4Online
Stop-Service DW4Online
Rename-Item app app.failed; Rename-Item app.previous app
powershell -ExecutionPolicy Bypass -File app\deploy\restore-db.ps1 -Backup backups\daily\dw4online-<date>.sql.gz -Database dw4online -Replace   # only after a migration
Start-Service DW4Online
```

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
