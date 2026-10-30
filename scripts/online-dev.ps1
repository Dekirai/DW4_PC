<#
.SYNOPSIS
  Online co-op preview on this PC: a private local server and two game windows in one room.

.DESCRIPTION
  Starts the local online stack of tools/online_test/run.py (a private MariaDB from XAMPP's binaries with its own data
  directory and port under tools/online_test/.work, the DW4Online server and relay on 127.0.0.1 with a throwaway HTTPS
  certificate that the clients pin), registers two throwaway accounts, and starts two game windows: the first creates
  a room, the second joins it. Both start at the title with a new game (nothing is saved online yet). Closing both
  windows stops the local server again. Nothing leaves this PC (loopback only), nothing touches the real server.

  The game windows need a development build (DW4_ONLINE_DEV, the loopback server override); without -GameExe the
  script builds one in build\OnlineDev the first time.

  -Hidden runs both clients without windows for -Frames frames with the campaign bot (the check automated runs use).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\online-dev.ps1
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\online-dev.ps1 -Data "D:\Games\Digimon World 4 (USA).iso"
#>
param(
    [string]$GameExe = '',
    [string]$Data = '',
    [string]$Card = '',
    [switch]$Hidden,
    [int]$Frames = 12000,
    [int]$MySqlPort = 3431,
    [string]$Ports = '53100-53199',
    [switch]$NoBuild
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

# The game data: the -Data argument, else the [data] path of the game's settings.ini (read only).
if (-not $Data) {
    foreach ($ini in @((Join-Path $projectRoot 'build\Game\settings.ini'), (Join-Path $projectRoot 'settings.ini'))) {
        if (Test-Path -LiteralPath $ini) {
            $section = ''
            foreach ($line in Get-Content -LiteralPath $ini) {
                if ($line -match '^\s*\[(.+)\]\s*$') { $section = $Matches[1]; continue }
                if ($section -eq 'data' -and $line -match '^\s*path\s*=\s*(.+?)\s*$') { $Data = $Matches[1] }
            }
            if ($Data) { break }
        }
    }
}
if (-not $Data) { throw 'Pass -Data with the game disc image or its extracted folder.' }

# A development build of the game (DW4_ONLINE_DEV).
if (-not $GameExe) {
    $GameExe = Join-Path $projectRoot 'build\OnlineDev\dw4_game.exe'
    if (-not (Test-Path -LiteralPath $GameExe)) {
        if ($NoBuild) { throw "No development build at $GameExe (run without -NoBuild once, or pass -GameExe)." }
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio with Desktop development with C++.' }
        $vsRoot = & $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not $vsRoot) { throw 'Visual Studio C++ tools were not found.' }
        $envScript = Join-Path $projectRoot 'build\compiler-env.cmd'
        New-Item -ItemType Directory -Force (Split-Path $envScript) | Out-Null
        @('@echo off', "call `"$(Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat')`" >nul", 'if errorlevel 1 exit /b %errorlevel%', 'set') |
            Set-Content -LiteralPath $envScript -Encoding ascii
        foreach ($line in (& $env:ComSpec /d /c $envScript)) {
            if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
        }
        $cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (-not (Test-Path -LiteralPath $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
        $buildDir = Join-Path $projectRoot 'build\OnlineDev'
        & $cmake -S $projectRoot -B $buildDir -G Ninja -DCMAKE_BUILD_TYPE=Release -DDW4_BUILD_APP=ON -DBUILD_TESTING=OFF -DDW4_ONLINE_DEV=ON
        if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
        & $cmake --build $buildDir --parallel 4 --target dw4_game
        if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    }
}
if (-not (Test-Path -LiteralPath $GameExe)) { throw "No game at $GameExe." }

# Python 3 for the stack (tools/online_test/run.py); dotnet builds the server on the way.
$python = $null
foreach ($candidate in @(@('py', '-3'), @('python'))) {
    if (Get-Command $candidate[0] -ErrorAction SilentlyContinue) { $python = $candidate; break }
}
if (-not $python) { throw 'Python 3 was not found (py or python on PATH).' }
if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) { throw 'The .NET SDK (dotnet) was not found.' }

$runArgs = @((Join-Path $projectRoot 'tools\online_test\run.py'), 'dev', '--game-exe', $GameExe, '--data', $Data,
             '--mysql-port', $MySqlPort, '--ports', $Ports)
if ($Hidden) { $runArgs += @('--hidden', '--frames', $Frames) }
if ($Card) { $runArgs += @('--dev-card', $Card) }
$pyExe = $python[0]
$pyArgs = @($python | Select-Object -Skip 1) + $runArgs
& $pyExe @pyArgs
exit $LASTEXITCODE
