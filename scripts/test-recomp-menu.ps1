param([string]$RuntimeSource = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = Join-Path $repoRoot 'work/recomp-menu'
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
$reportPath = Join-Path $outputRoot 'latest.json'
# Invalidate previous success before discovery or compilation can fail.
[IO.File]::WriteAllText($reportPath, (@{
    passed=$false; status='started'; runtime_launched=$false; window_created=$false; device_started=$false
} | ConvertTo-Json), $utf8)

if (-not $RuntimeSource) { $RuntimeSource = Join-Path $repoRoot 'work/moderngekko' }
$RuntimeSource = (Resolve-Path -LiteralPath $RuntimeSource).Path
$runtimeInclude = Join-Path $RuntimeSource 'src'
$coreInclude = Join-Path $RuntimeSource 'vendor/dolphin/Source/Core'
$source = Join-Path $repoRoot 'tools/recomp_menu_check/main.cpp'
$inputs = @($source, (Join-Path $runtimeInclude 'runtime/ingame_menu_input.hpp'),
    (Join-Path $coreInclude 'DolphinNoGUI/UIInput.h'))
foreach ($inputPath in $inputs) {
    if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) {
        throw "Prepare the patched local runtime sources first; missing: $inputPath"
    }
}
$inputHashes = @($inputs | ForEach-Object {
    [ordered]@{path=$_; sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}
})

if (-not (Get-Command cl.exe -CommandType Application -ErrorAction SilentlyContinue)) {
    $environmentScript = Join-Path $repoRoot 'work/recomp-compiler-env.cmd'
    if (-not (Test-Path -LiteralPath $environmentScript -PathType Leaf)) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
            throw 'Visual Studio with the C++ workload is required.'
        }
        $installation = [string](& $vswhere -latest -prerelease -products '*' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
        if ($LASTEXITCODE -ne 0 -or -not $installation.Trim()) {
            throw 'Could not locate the MSVC x64 toolchain.'
        }
        $environmentScript = Join-Path $outputRoot 'compiler-env.cmd'
        $vcvars = Join-Path $installation.Trim() 'VC/Auxiliary/Build/vcvars64.bat'
        @('@echo off', ('call "' + $vcvars + '" >nul'), 'if errorlevel 1 exit /b %errorlevel%', 'set') |
            Set-Content -LiteralPath $environmentScript -Encoding ascii
    }
    $compilerEnvironment = & $env:ComSpec /d /c $environmentScript
    if ($LASTEXITCODE -ne 0) { throw 'The MSVC x64 environment could not be initialized.' }
    foreach ($entry in $compilerEnvironment) {
        if ($entry -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')
        }
    }
}
$compiler = (Get-Command cl.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$executable = Join-Path $outputRoot 'recomp-menu-check.exe'
$arguments = @('/nologo','/std:c++20','/EHsc','/O2','/W4','/WX','/permissive-','/utf-8','/MD',
    "/I$runtimeInclude", "/I$coreInclude", $source, "/Fe$executable",
    ('/Fo' + (Join-Path $outputRoot 'main.obj')))
$invocation = [ordered]@{
    checked_at_utc=[DateTime]::UtcNow.ToString('o'); runtime_source=$RuntimeSource
    compiler=$compiler; arguments=$arguments; inputs=$inputHashes
    runtime_launched=$false; window_created=$false; device_started=$false
}
[IO.File]::WriteAllText((Join-Path $outputRoot 'invocation.json'),
    ($invocation | ConvertTo-Json -Depth 5), $utf8)

function Invoke-MenuCheck([string]$Command, [string[]]$CommandArguments, [string]$LogPath) {
    $savedPreference = $ErrorActionPreference
    try {
        # Native stderr is diagnostic text, not a terminating PS 5.1 error.
        $ErrorActionPreference = 'Continue'
        $commandOutput = @(& $Command @CommandArguments 2>&1)
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    [IO.File]::WriteAllText($LogPath, ($commandOutput -join "`n"), $utf8)
    if ($nativeExit -ne 0) { throw "Menu policy check failed ($nativeExit): $Command. See $LogPath" }
    $commandOutput
}

Invoke-MenuCheck $compiler $arguments (Join-Path $outputRoot 'build.log') | Out-Host
$resultText = @(Invoke-MenuCheck $executable @() (Join-Path $outputRoot 'test.log')) -join "`n"
$result = [regex]::Match($resultText.Trim(),
    '^PASS: ([0-9]+) menu input policy checks; no core, window or audio device initialized\.$')
# Keep this aligned with the actual checker: 26 policy/lifecycle assertions plus
# 2,000 bounded concurrent drains. A different report must be reviewed explicitly.
if (-not $result.Success -or [int]$result.Groups[1].Value -ne 2026) {
    throw 'The menu checker did not publish its expected 2,026-check success report.'
}
foreach ($inputHash in $inputHashes) {
    if ((Get-FileHash -LiteralPath $inputHash.path -Algorithm SHA256).Hash -ne $inputHash.sha256) {
        throw "A checked source changed during compilation: $($inputHash.path)"
    }
}
$report = [ordered]@{
    passed=$true; checks=[int]$result.Groups[1].Value
    checked_at_utc=[DateTime]::UtcNow.ToString('o'); inputs=$inputHashes
    checker_sha256=(Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
    runtime_launched=$false; window_created=$false; device_started=$false
}
[IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 5), $utf8)
Write-Host "Menu input policy checks passed ($($report.checks)); no core, window or device initialized. Evidence: $reportPath"
