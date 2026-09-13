param([string]$RuntimeSource = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $RuntimeSource) { $RuntimeSource = Join-Path $repoRoot 'work/moderngekko' }
$RuntimeSource = (Resolve-Path -LiteralPath $RuntimeSource).Path
$coreInclude = Join-Path $RuntimeSource 'vendor/dolphin/Source/Core'
$header = Join-Path $coreInclude 'Core/PowerPC/StaticRecomp/StaticRecompDispatchProbe.h'
$source = Join-Path $repoRoot 'tools/recomp_dispatch_probe_check/main.cpp'
$outputRoot = Join-Path $repoRoot 'work/recomp-dispatch-probe'
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
$reportPath = Join-Path $outputRoot 'latest.json'
[IO.File]::WriteAllText($reportPath,('{"passed":false,"status":"started","runtime_launched":false}'),$utf8)
$inputHashes = @($source,$header) | ForEach-Object {
    [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256 -ErrorAction Stop).Hash}
}
if (-not (Get-Command cl.exe -CommandType Application -ErrorAction SilentlyContinue)) {
    $environmentScript = Join-Path $repoRoot 'work/recomp-compiler-env.cmd'
    if (-not (Test-Path -LiteralPath $environmentScript -PathType Leaf)) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        $installation = [string](& $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
        if ($LASTEXITCODE -ne 0 -or -not $installation.Trim()) { throw 'MSVC x64 could not be located.' }
        $environmentScript = Join-Path $outputRoot 'compiler-env.cmd'
        $vcvars = Join-Path $installation.Trim() 'VC/Auxiliary/Build/vcvars64.bat'
        @('@echo off',('call "' + $vcvars + '" >nul'),'if errorlevel 1 exit /b %errorlevel%','set') |
            Set-Content -LiteralPath $environmentScript -Encoding ascii
    }
    $compilerEnvironment = & $env:ComSpec /d /c $environmentScript
    if ($LASTEXITCODE -ne 0) { throw 'MSVC environment initialization failed.' }
    foreach ($entry in $compilerEnvironment) {
        if ($entry -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process') }
    }
}
$compiler = (Get-Command cl.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$executable = Join-Path $outputRoot 'recomp-dispatch-probe-check.exe'
$arguments = @('/nologo','/std:c++20','/EHsc','/O2','/W4','/WX','/permissive-','/utf-8','/MD',
    "/I$coreInclude",$source,"/Fe$executable",('/Fo'+(Join-Path $outputRoot 'main.obj')))
function Invoke-ProbeCheck([string]$Command,[string[]]$CommandArguments,[string]$LogPath) {
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $commandOutput = @(& $Command @CommandArguments 2>&1)
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    [IO.File]::WriteAllText($LogPath,($commandOutput -join "`n"),$utf8)
    if ($nativeExit -ne 0) { throw "Dispatch probe checker failed ($nativeExit). See $LogPath" }
    $commandOutput
}
$invocation = [ordered]@{
    checked_at_utc=[DateTime]::UtcNow.ToString('o'); compiler=$compiler; arguments=$arguments
    inputs=$inputHashes; runtime_launched=$false; audio_started=$false; external_profiler_started=$false
}
[IO.File]::WriteAllText((Join-Path $outputRoot 'invocation.json'),($invocation | ConvertTo-Json -Depth 5),$utf8)
Invoke-ProbeCheck $compiler $arguments (Join-Path $outputRoot 'build.log') | Out-Host
$resultText = @(Invoke-ProbeCheck $executable @() (Join-Path $outputRoot 'test.log')) -join "`n"
$result = [regex]::Match($resultText.Trim(),'^PASS: ([0-9]+) dispatch probe checks; no game, audio, ETW or service launched\.$')
if (-not $result.Success -or [int]$result.Groups[1].Value -lt 300) { throw 'Expected synthetic probe result was not published.' }
foreach ($inputHash in $inputHashes) {
    if ((Get-FileHash -LiteralPath $inputHash.path -Algorithm SHA256).Hash -ne $inputHash.sha256) { throw 'Probe source changed during testing.' }
}
[IO.File]::WriteAllText($reportPath,([ordered]@{
    passed=$true;checks=[int]$result.Groups[1].Value;runtime_launched=$false;audio_started=$false
    external_profiler_started=$false;checker_sha256=(Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
    scope='Injected counter/state tests plus a few own-checker-thread Windows API reads; no CPU busy workload or gameplay validation.'
} | ConvertTo-Json -Depth 5),$utf8)
Write-Host $resultText
Write-Host "Dispatch probe evidence: $reportPath"
