param([string]$RuntimeSource = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $RuntimeSource) { $RuntimeSource = Join-Path $repoRoot 'work/moderngekko' }
$coreInclude = Join-Path $RuntimeSource 'vendor/dolphin/Source/Core'
$header = Join-Path $coreInclude 'AudioCommon/AudioTelemetry.h'
$source = Join-Path $repoRoot 'tools/recomp_audio_check/main.cpp'
$environmentScript = Join-Path $repoRoot 'work/recomp-compiler-env.cmd'
foreach ($required in @($header,$source,$environmentScript)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Prepare the local recompilation toolchain and patched sources first; missing: $required"
    }
}
$coreInclude = (Resolve-Path -LiteralPath $coreInclude).Path
$outputRoot = Join-Path $repoRoot 'work/recomp-audio-telemetry'
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$reportPath = Join-Path $outputRoot 'latest.json'
# A failed new compilation must not leave an earlier success as the latest result.
[ordered]@{passed=$false;status='started';device_started=$false;runtime_launched=$false} |
    ConvertTo-Json | Set-Content -LiteralPath $reportPath -Encoding UTF8
$compilerEnvironment = & $env:ComSpec /d /c $environmentScript
if ($LASTEXITCODE -ne 0) { throw 'The local MSVC x64 environment failed.' }
foreach ($entry in $compilerEnvironment) {
    if ($entry -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')
    }
}
$compiler = (Get-Command cl.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
function Invoke-TelemetryCommand([string]$Executable,[string[]]$Arguments,[string]$LogPath) {
    $savedPreference = $ErrorActionPreference
    try {
        # Native stderr is diagnostic output, not a terminating PowerShell 5.1 error.
        $ErrorActionPreference = 'Continue'
        $output = @(& $Executable @Arguments 2>&1)
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    $output | Set-Content -LiteralPath $LogPath -Encoding UTF8
    if ($nativeExit -ne 0) {
        throw "Audio telemetry check command failed ($nativeExit): $Executable. See $LogPath"
    }
    $output
}
$executable = Join-Path $outputRoot 'recomp-audio-check.exe'
$arguments = @('/nologo','/std:c++20','/EHsc','/O2','/W4','/WX','/permissive-','/utf-8','/MD',
    "/I$coreInclude",$source,"/Fe$executable",('/Fo' + (Join-Path $outputRoot 'main.obj')))
[ordered]@{
    checked_at_utc=[DateTime]::UtcNow.ToString('o');compiler=$compiler;arguments=$arguments;
    header=$header;header_sha256=(Get-FileHash -LiteralPath $header -Algorithm SHA256).Hash;
    source=$source;source_sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash;
    device_started=$false;runtime_launched=$false
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $outputRoot 'invocation.json') -Encoding UTF8
Invoke-TelemetryCommand $compiler $arguments (Join-Path $outputRoot 'build.log') | Out-Host
$resultText = @(Invoke-TelemetryCommand $executable @() (Join-Path $outputRoot 'test.log')) -join "`n"
$result = $resultText | ConvertFrom-Json
if ($result.passed -isnot [bool] -or -not $result.passed -or $result.checks -lt 1 -or
    $result.device_started -isnot [bool] -or $result.device_started -or
    $result.runtime_launched -isnot [bool] -or $result.runtime_launched) {
    throw 'The audio telemetry checker did not return its expected device-free success report.'
}
$result | ConvertTo-Json | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Host "Audio telemetry checks passed ($($result.checks)); no audio device or runtime was started. Evidence: $reportPath"
