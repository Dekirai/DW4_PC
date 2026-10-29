<#
.SYNOPSIS
  Registers DW4Online as a Windows service under its virtual account, sets folder permissions and firewall rules.

.DESCRIPTION
  Run elevated on the server after extracting the release to <Root>\app and writing <Root>\config\appsettings.Production.json
  (see DEPLOY.md). Needs no restart. -DryRun prints the commands without running them. Does nothing to other services,
  SQL Server or the Pangya servers.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File install-service.ps1 -DryRun
  powershell -ExecutionPolicy Bypass -File install-service.ps1 -OpenPort80
#>
param(
    [string]$Root = 'D:\Dekirai\Servers\DW4Online',
    [string]$ServiceName = 'DW4Online',
    [int]$HttpsPort = 443,
    [int]$RelayPort = 52041,
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
    if (Get-Service -Name $ServiceName -ErrorAction SilentlyContinue) { throw "Service $ServiceName exists already (updates: see DEPLOY.md, step 8)." }
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

# The virtual account exists once the service does; grant it only what it needs.
Step "icacls $config (Administrators, SYSTEM full; service read)" {
    & icacls $config /inheritance:r /grant:r 'Administrators:(OI)(CI)F' 'SYSTEM:(OI)(CI)F' "${account}:(OI)(CI)R" | Out-Null
}
foreach ($dir in @('logs', 'acme', 'keys')) {
    $path = Join-Path $Root $dir
    Step "icacls $path (service modify)" { & icacls $path /grant "${account}:(OI)(CI)M" | Out-Null }
}
Step "icacls $app (service read and execute)" { & icacls $app /grant "${account}:(OI)(CI)RX" | Out-Null }

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
