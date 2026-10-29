<#
.SYNOPSIS
  Runs the DW4Online end-to-end tests on loopback.

.DESCRIPTION
  1. Builds the WinHTTP test client (tests\winhttp_client\build.cmd, MSVC).
  2. Starts the private MariaDB of scripts\local-db.ps1 (its own data folder and port, 127.0.0.1 only).
  3. Runs the EndToEnd tests of DW4Online.Integration.Tests: each starts the real service on Kestrel (127.0.0.1, a free
     port) with the relay on a free loopback port and a throw-away database, then
     - registers on the website, runs the C++ client (login, cloud save up / down, online character, room, stage and
       relay handshake, achievement) and checks the profile page;
     - runs the groundwork spike's Python test (tests\e2e\relay_spike_test.py) against rooms and the relay.
  Nothing listens on other interfaces, nothing is deployed, no window opens.

.PARAMETER Python
  A python.exe for the spike test (or set DW4ONLINE_PYTHON). Without one that test is skipped.

.PARAMETER StopDatabase
  Stop the private MariaDB afterwards.

.PARAMETER Port
  The private MariaDB's port (local-db.ps1 -Port, default 3427).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\e2e.ps1 -Python C:\Python313\python.exe
#>
param(
    [string]$Python = $env:DW4ONLINE_PYTHON,
    [switch]$StopDatabase,
    [int]$Port = 3427
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

& cmd /c (Join-Path $root 'tests\winhttp_client\build.cmd')
if ($LASTEXITCODE -ne 0) { throw 'Building the WinHTTP test client failed.' }

$db = Join-Path $PSScriptRoot 'local-db.ps1'
& powershell -NoProfile -ExecutionPolicy Bypass -File $db start -Port $Port
$line = & powershell -NoProfile -ExecutionPolicy Bypass -File $db env -Port $Port
if ($line -notmatch "'(.+)'") { throw "Could not read the test connection string from local-db.ps1 env." }
$env:DW4ONLINE_TEST_MYSQL = $Matches[1]
if ($Python) {
    if (-not (Test-Path $Python)) { throw "Python not found: $Python" }
    $env:DW4ONLINE_PYTHON = $Python
    $env:PYTHONIOENCODING = 'utf-8'
} else {
    Write-Output 'No Python given (-Python or DW4ONLINE_PYTHON): the spike test is skipped.'
}

try {
    & dotnet test (Join-Path $root 'tests\DW4Online.Integration.Tests') --nologo --filter 'FullyQualifiedName~EndToEnd' --logger 'console;verbosity=detailed'
    $code = $LASTEXITCODE
} finally {
    if ($StopDatabase) { & powershell -NoProfile -ExecutionPolicy Bypass -File $db stop -Port $Port }
}
exit $code
