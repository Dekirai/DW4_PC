<#
.SYNOPSIS
  Registers DW4Online as a Windows service under its virtual account, sets folder permissions and firewall rules.

.DESCRIPTION
  Run elevated on the server after extracting the release to <Root>\app and writing <Root>\config\appsettings.Production.json
  (see DEPLOY.md). Needs no restart. -DryRun prints the commands without running them. Does nothing to other services,
  SQL Server or the Pangya servers.

  The service depends on the MariaDB service (sc.exe config depend=): after a reboot Windows starts MariaDB first, and
  stopping MariaDB stops DW4Online first. Before anything changes the script checks that MariaDB listens on
  -DatabasePort and only on the loopback address (bind-address=127.0.0.1, DEPLOY.md section 1).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File install-service.ps1 -DryRun
  powershell -ExecutionPolicy Bypass -File install-service.ps1 -OpenPort80
#>
param(
    [string]$Root = 'D:\Dekirai\Servers\DW4Online',
    [string]$ServiceName = 'DW4Online',
    [int]$HttpsPort = 443,
    [int]$RelayPort = 52041,
    [string]$DatabaseService = 'MariaDB',
    [int]$DatabasePort = 3306,
    [switch]$OpenPort80,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$app = Join-Path $Root 'app'
$exe = Join-Path $app 'DW4Online.Server.exe'
$config = Join-Path $Root 'config'
$settings = Join-Path $config 'appsettings.Production.json'
$account = "NT SERVICE\$ServiceName"

function Step([string]$text, [scriptblock]$action) {
    Write-Output "> $text"
    if (-not $DryRun) { & $action }
}

if (-not $DryRun) {
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run this script elevated.' }
    if (-not (Test-Path $exe)) { throw "$exe not found: extract the release to $app first." }
    if (-not (Test-Path $settings)) { throw "$settings not found: create it from appsettings.Production.example.json first." }
    if (Get-Service -Name $ServiceName -ErrorAction SilentlyContinue) { throw "Service $ServiceName exists already (updates: see DEPLOY.md, section 8)." }
    if (-not (Get-Service -Name $DatabaseService -ErrorAction SilentlyContinue)) {
        throw "Service $DatabaseService not found: install MariaDB first (DEPLOY.md section 1) or pass -DatabaseService <its service name>."
    }
    # MariaDB must answer locally and nowhere else (the database port stays closed to the network).
    $listen = @(Get-NetTCPConnection -State Listen -LocalPort $DatabasePort -ErrorAction SilentlyContinue)
    if ($listen.Count -eq 0) { throw "Nothing listens on port ${DatabasePort}: start $DatabaseService (or pass -DatabasePort)." }
    $open = @($listen | Where-Object { $_.LocalAddress -notin @('127.0.0.1', '::1') })
    if ($open.Count -gt 0) {
        $where = ($open | ForEach-Object { $_.LocalAddress } | Sort-Object -Unique) -join ', '
        throw "Port $DatabasePort listens on ${where}: set bind-address=127.0.0.1 in MariaDB's my.ini and restart it (DEPLOY.md section 1)."
    }
    # ... and it is the service DW4Online will depend on, not another mysqld (a XAMPP one on 3306 while MariaDB has 3307).
    $dbPid = (Get-CimInstance Win32_Service -Filter "Name='$DatabaseService'").ProcessId
    $owners = @($listen | ForEach-Object { $_.OwningProcess } | Sort-Object -Unique)
    if (-not $dbPid -or $owners -notcontains $dbPid) {
        $who = ($owners | ForEach-Object { $p = Get-Process -Id $_ -ErrorAction SilentlyContinue; if ($p) { "$($p.ProcessName) (PID $_)" } else { "PID $_" } }) -join ', '
        throw "Port $DatabasePort belongs to $who, not to the service $DatabaseService (PID $dbPid): pass the -DatabasePort of $DatabaseService (or -DatabaseService)."
    }
}

foreach ($dir in @('config', 'logs', 'backups', 'acme', 'keys')) {
    $path = Join-Path $Root $dir
    Step "create $path" { New-Item -ItemType Directory -Force $path | Out-Null }
}

$binPath = "`"$exe`" --service --settings `"$settings`""
# New-Service hands the path to CreateService verbatim (sc.exe binPath= would lose the inner quotes in Windows PowerShell 5.1).
Step "New-Service $ServiceName -BinaryPathName $binPath" {
    New-Service -Name $ServiceName -BinaryPathName $binPath -DisplayName 'DW4Online' -StartupType Automatic `
        -Description 'Digimon World 4 PC online services: API, website and relay.' | Out-Null
}
Step "sc.exe config $ServiceName start= delayed-auto obj= `"$account`" (virtual account, no password)" {
    & sc.exe config $ServiceName start= delayed-auto obj= $account | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "sc.exe config failed ($LASTEXITCODE)" }
}
Step "sc.exe failure ${ServiceName}: restart after 5 s, 30 s, 60 s" {
    & sc.exe failure $ServiceName reset= 86400 actions= restart/5000/restart/30000/restart/60000 | Out-Null
}
Step "sc.exe config $ServiceName depend= $DatabaseService (Windows starts MariaDB first)" {
    & sc.exe config $ServiceName depend= $DatabaseService | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "sc.exe config depend= failed ($LASTEXITCODE)" }
}

# The virtual account exists once the service does; grant it only what it needs. Every folder drops the permissions
# inherited from the drive (on a data volume Users may read and Authenticated Users may even change files) and keeps
# only Administrators and SYSTEM (full control, by SID so localized Windows works too) plus the service's rights. The
# root goes first: a folder created in it later (the next release's app, see DEPLOY.md section 8) inherits its rights.
function Set-FolderAcl([string]$path, [string]$serviceRights, [string]$what) {
    $grants = @('*S-1-5-32-544:(OI)(CI)F', '*S-1-5-18:(OI)(CI)F')
    if ($serviceRights) { $grants += "${account}:(OI)(CI)$serviceRights" }
    Step "icacls $path /inheritance:r ($what)" {
        & icacls $path /inheritance:r /grant:r @grants | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "icacls $path failed ($LASTEXITCODE)" }
    }
}
Set-FolderAcl $Root 'RX' 'Administrators, SYSTEM full; service read and execute'
Set-FolderAcl $app 'RX' 'the executable: Administrators, SYSTEM full; service read and execute'
Set-FolderAcl $config 'R' 'secrets: Administrators, SYSTEM full; service read'
foreach ($dir in @('logs', 'acme', 'keys')) {
    Set-FolderAcl (Join-Path $Root $dir) 'M' 'Administrators, SYSTEM full; service modify'
}
# Full dumps with password hashes, e-mail addresses and cloud saves: the nightly backup runs as SYSTEM, nobody else reads them.
Set-FolderAcl (Join-Path $Root 'backups') '' 'Administrators and SYSTEM only'

$ports = @($HttpsPort, $RelayPort)
if ($OpenPort80) { $ports += 80 }
foreach ($port in $ports) {
    Step "firewall: allow inbound TCP $port for $exe" {
        New-NetFirewallRule -DisplayName "DW4Online TCP $port" -Direction Inbound -Protocol TCP -LocalPort $port `
            -Program $exe -Action Allow -Profile Any | Out-Null
    }
}

Write-Output ''
Write-Output "Done. Start with: Start-Service $ServiceName   (log: $Root\logs)"
Write-Output "Remove again: Stop-Service $ServiceName; sc.exe delete $ServiceName; Get-NetFirewallRule -DisplayName 'DW4Online TCP *' | Remove-NetFirewallRule"
