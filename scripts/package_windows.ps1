<#
.SYNOPSIS
Builds the Windows release zip DW4_PC-<Version>-windows-x64.zip and its SHA-256.

.DESCRIPTION
Configures and builds the release game in -BuildDirectory (Release, DW4_ONLINE_DEV=OFF, no tests, the Windows program
without a console window), installs it with "cmake --install" into a fresh staging folder, checks the content rules and
writes the zip and <zip>.sha256 into -OutputDirectory (docs/DEVELOPMENT.md "Release packages").

The content rules: exactly the release layout of CMakeLists.txt's install rules (DW4_PC.exe, dw4_catalog.exe, README.md,
LICENSE, THIRD_PARTY_NOTICES.md, licenses/*, docs/PLAYER_GUIDE.md, tools/verify_game.py, lang/<code>/language.ini and
textures.tsv of every pack committed in data/lang); no game.tsv, no PDB; no file names the build machine's profile folder
("Users/" or "Users\", ASCII or UTF-16) or a PDB path; DW4_PC.exe is a Windows (GUI) program and needs no C++ runtime DLL.

-Version names the release (release numbers follow the server, for example 0.2.1); the game's own version, CMake's
project version, is not changed. Player packages need the server's certificate pins (DW4_ONLINE_REQUIRE_PINS=ON:
local/online_pins.txt, -PinsFile or -CMakeArgs "-DDW4_ONLINE_PINS=..."); -NoPins allows a test package without them,
whose game plays offline and over LAN only.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\package_windows.ps1 -Version 0.2.1
#>
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [string]$BuildDirectory = 'build\Package',
    [string]$OutputDirectory = 'dist',
    [string]$PinsFile = '',
    [switch]$NoPins,
    [string[]]$CMakeArgs = @(),
    [ValidateRange(1, 64)][int]$Jobs = 4,
    [switch]$AllowDirty,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
function Resolve-FromRoot([string]$path) {
    if ([System.IO.Path]::IsPathRooted($path)) { return [System.IO.Path]::GetFullPath($path) }
    return [System.IO.Path]::GetFullPath((Join-Path $projectRoot $path))
}
$buildDirectory = Resolve-FromRoot $BuildDirectory
$outputDirectory = Resolve-FromRoot $OutputDirectory
$name = "DW4_PC-$Version-windows-x64"
$zipPath = Join-Path $outputDirectory "$name.zip"
$shaPath = "$zipPath.sha256"
if ((Test-Path -LiteralPath $zipPath) -and -not $Force) { throw "$zipPath exists already (-Force replaces it)." }

# The source: a clean checkout of a commit (tracked files unchanged), unless -AllowDirty.
$git = (Get-Command git -ErrorAction Stop).Source
$commit = (& $git -C $projectRoot rev-parse --short=8 HEAD)
if ($LASTEXITCODE -ne 0) { throw 'git rev-parse failed: the packaging needs a git checkout.' }
$changes = @(& $git -C $projectRoot status --porcelain --untracked-files=no)
if ($changes.Count -gt 0) {
    if (-not $AllowDirty) { throw "The checkout has changes to tracked files (-AllowDirty packages them anyway):`n$($changes -join "`n")" }
    Write-Warning "Packaging a checkout with changes to tracked files ($($changes.Count))."
}
Write-Host "DW4_PC $Version from commit $commit"

# The compiler environment of Visual Studio, imported into this process only (as build.ps1).
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswherePath)) { throw 'Install Visual Studio with Desktop development with C++.' }
$vsRoot = & $vswherePath -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ tools were not found.' }
New-Item -ItemType Directory -Force $buildDirectory | Out-Null
$environmentScript = Join-Path $buildDirectory 'compiler-env.cmd'
@('@echo off', "call `"$(Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat')`" >nul 2>nul", 'if errorlevel 1 exit /b %errorlevel%', 'set') |
    Set-Content -LiteralPath $environmentScript -Encoding ascii
$compilerEnvironment = & $env:ComSpec /d /c $environmentScript
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the Visual Studio compiler environment.' }
foreach ($line in $compilerEnvironment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
$cmakeExe = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmakeExe)) { $cmakeExe = (Get-Command cmake -ErrorAction Stop).Source }

# Configure and build the release game (and dw4_catalog).
$require = if ($NoPins) { 'OFF' } else { 'ON' }
$configure = @('-S', $projectRoot, '-B', $buildDirectory, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DDW4_BUILD_APP=ON',
    '-DBUILD_TESTING=OFF', '-DDW4_ONLINE_DEV=OFF', '-DDW4_GAME_CONSOLE=OFF', '-DDW4_GHOST_PROBE=OFF', "-DDW4_ONLINE_REQUIRE_PINS=$require")
if ($PinsFile) { $configure += "-DDW4_ONLINE_PINS_FILE=$((Resolve-FromRoot $PinsFile) -replace '\\', '/')" }
$configure += $CMakeArgs
$configureOutput = & $cmakeExe @configure
$configureExit = $LASTEXITCODE
$configureOutput | ForEach-Object { Write-Host $_ }
if ($configureExit -ne 0) { throw 'CMake configuration failed.' }
# (The summary names the pins' count only; the configure output above names the pins file.)
$pins = @($configureOutput | ForEach-Object { if ($_ -match 'DW4Online server pins: (\S+)') { "server pins: $($Matches[1])" } })
& $cmakeExe --build $buildDirectory --parallel $Jobs --target dw4_game dw4_catalog
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

# Install into a fresh staging folder (the script's own output inside the build folder).
$stage = Join-Path $buildDirectory "package\$name"
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
& $cmakeExe --install $buildDirectory --prefix $stage
if ($LASTEXITCODE -ne 0) { throw 'cmake --install failed.' }

# The content rules.
$problems = New-Object System.Collections.Generic.List[string]
$expected = New-Object System.Collections.Generic.List[string]
foreach ($file in @('DW4_PC.exe', 'dw4_catalog.exe', 'README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'licenses/SDL.txt',
        'licenses/imgui.txt', 'licenses/stb.txt', 'licenses/zstd.txt', 'licenses/enbrip-MIT.txt', 'licenses/pl_mpeg-MIT.txt',
        'docs/PLAYER_GUIDE.md', 'tools/verify_game.py')) { $expected.Add($file) }
# (Every pack committed in data/lang: its language.ini and textures.tsv.)
$tracked = @(& $git -C $projectRoot ls-files -- data/lang)
if ($LASTEXITCODE -ne 0) { throw 'git ls-files failed.' }
foreach ($file in ($tracked | Sort-Object)) {
    if ($file -match '^data/lang/([^/]+)/(language\.ini|textures\.tsv)$') { $expected.Add("lang/$($Matches[1])/$($Matches[2])") }
}
$staged = @(Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
        $_.FullName.Substring($stage.Length + 1).Replace('\', '/')
    })
foreach ($file in $staged) {
    if ($file -match '(^|/)game\.tsv$') { $problems.Add("$file`: a game.tsv (game-derived text) must never ship") }
    elseif ($file -match '\.(pdb|ilk|exp|lib)$') { $problems.Add("$file`: a build file (PDB, ILK, EXP, LIB) must not ship") }
    elseif (-not $expected.Contains($file)) { $problems.Add("$file`: not part of the release layout") }
}
foreach ($file in $expected) { if ($staged -notcontains $file) { $problems.Add("$file`: missing") } }
$latin1 = [System.Text.Encoding]::GetEncoding(28591)
foreach ($file in $staged) {
    $bytes = [System.IO.File]::ReadAllBytes((Join-Path $stage $file))
    $texts = @($latin1.GetString($bytes), [System.Text.Encoding]::Unicode.GetString($bytes))
    if ($bytes.Length -gt 1) { $texts += [System.Text.Encoding]::Unicode.GetString($bytes, 1, $bytes.Length - 1) }
    foreach ($text in $texts) {
        $found = [regex]::Match($text, '(?i)users[/\\][ -~]{0,60}')
        if ($found.Success) { $problems.Add("$file`: names a profile folder: $($found.Value)"); break }
        $found = [regex]::Match($text, '(?i)[a-z]:[/\\][ -~]{0,200}\.pdb')
        if ($found.Success) { $problems.Add("$file`: names a PDB: $($found.Value)"); break }
    }
}
# DW4_PC.exe: a Windows program (PE subsystem 2: no console window), linked without a C++ runtime DLL.
$gamePath = Join-Path $stage 'DW4_PC.exe'
if (Test-Path -LiteralPath $gamePath) {
    $bytes = [System.IO.File]::ReadAllBytes($gamePath)
    $pe = [BitConverter]::ToInt32($bytes, 0x3C)
    $subsystem = [BitConverter]::ToUInt16($bytes, $pe + 24 + 68)
    if ($subsystem -ne 2) { $problems.Add("DW4_PC.exe: PE subsystem $subsystem, not 2 (a Windows program without a console window)") }
    $dumpbin = Get-Command dumpbin -ErrorAction SilentlyContinue
    if ($dumpbin) {
        foreach ($exe in @('DW4_PC.exe', 'dw4_catalog.exe')) {
            $dlls = @(& $dumpbin.Source /nologo /dependents (Join-Path $stage $exe) | Where-Object { $_ -match '^\s+\S+\.dll\s*$' } | ForEach-Object { $_.Trim() })
            Write-Host "$exe needs: $($dlls -join ', ')"
            foreach ($dll in $dlls) {
                if ($dll -match '^(msvcp|vcruntime|ucrtbase|api-ms-win-crt|concrt|sdl|zstd|imgui)') { $problems.Add("$exe`: needs $dll (not a system DLL)") }
            }
        }
    }
}
if ($problems.Count -gt 0) {
    $problems | ForEach-Object { Write-Host "PROBLEM: $_" }
    throw "The package breaks $($problems.Count) content rule(s); nothing was written to $outputDirectory."
}

# The zip (entries in the release layout's order, '/' separators) and its SHA-256.
New-Item -ItemType Directory -Force $outputDirectory | Out-Null
if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open($zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in $expected) {
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, (Join-Path $stage $file), $file,
            [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally {
    $zip.Dispose()
}
$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText($shaPath, "$hash  $name.zip`n", [System.Text.Encoding]::ASCII) # (sha256sum -c reads it)

Write-Host ''
Write-Host "Package $name (commit $commit; $($pins -join '; '))"
$total = 0
foreach ($file in $expected) {
    $size = (Get-Item -LiteralPath (Join-Path $stage $file)).Length
    $total += $size
    Write-Host ('{0,10}  {1}' -f $size, $file)
}
Write-Host ('{0,10}  {1} files' -f $total, $expected.Count)
Write-Host "$zipPath ($((Get-Item -LiteralPath $zipPath).Length) bytes)"
Write-Host "SHA-256 $hash ($shaPath)"
