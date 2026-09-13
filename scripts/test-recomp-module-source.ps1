param(
    [string]$RuntimeSource = '',
    [switch]$InspectRuntimeSource
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $RuntimeSource) { $RuntimeSource = Join-Path $repoRoot 'work/moderngekko' }
$RuntimeSource = (Resolve-Path -LiteralPath $RuntimeSource).Path
$includeRoot = Join-Path $RuntimeSource 'include'
$toolsRoot = Join-Path $RuntimeSource 'tools'
$source = Join-Path $repoRoot 'tools/recomp_module_source_check/main.cpp'
$header = Join-Path $toolsRoot 'module_source_identity.hpp'
$shaHeader = Join-Path $includeRoot 'moderngekko/sha256.hpp'
$outputRoot = Join-Path $repoRoot 'build/recomp-module-source-check'
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)
$reportPath = Join-Path $outputRoot 'latest.json'
[IO.File]::WriteAllText($reportPath,('{"passed":false,"status":"started","runtime_launched":false}'),$utf8)
$inputHashes = @($source,$header,$shaHeader) | ForEach-Object {
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
$executable = Join-Path $outputRoot 'recomp-module-source-check.exe'
$arguments = @('/nologo','/std:c++20','/EHsc','/O2','/W4','/WX','/permissive-','/utf-8','/MD',
    "/I$includeRoot","/I$toolsRoot",$source,"/Fe$executable",('/Fo'+(Join-Path $outputRoot 'main.obj')))
function Invoke-SourceCheck([string]$Command,[string[]]$CommandArguments,[string]$LogPath) {
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $commandOutput = @(& $Command @CommandArguments 2>&1)
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    [IO.File]::WriteAllText($LogPath,($commandOutput -join "`n"),$utf8)
    if ($nativeExit -ne 0) { throw "Module source checker failed ($nativeExit). See $LogPath" }
    $commandOutput
}
$checkerArguments = @()
if ($InspectRuntimeSource) { $checkerArguments += $RuntimeSource }
$invocation = [ordered]@{
    checked_at_utc=[DateTime]::UtcNow.ToString('o'); compiler=$compiler; arguments=$arguments
    checker_arguments=$checkerArguments; inputs=$inputHashes
    runtime_launched=$false; audio_started=$false; export_started=$false; full_build_started=$false
}
[IO.File]::WriteAllText((Join-Path $outputRoot 'invocation.json'),($invocation | ConvertTo-Json -Depth 5),$utf8)
Push-Location -LiteralPath $outputRoot
try {
    Invoke-SourceCheck $compiler $arguments (Join-Path $outputRoot 'build.log') | Out-Host
    $resultText = @(Invoke-SourceCheck $executable $checkerArguments (Join-Path $outputRoot 'test.log')) -join "`n"
} finally { Pop-Location }
$result = [regex]::Match($resultText,'(?m)^PASS: ([0-9]+) module source checks; ([0-9]+) skipped; no game, export or full build launched\.$')
if (-not $result.Success -or [int]$result.Groups[1].Value -lt 100) { throw 'Expected synthetic source identity result was not published.' }
foreach ($inputHash in $inputHashes) {
    if ((Get-FileHash -LiteralPath $inputHash.path -Algorithm SHA256).Hash -ne $inputHash.sha256) { throw 'Checker input source changed during testing.' }
}
$report = [ordered]@{
    passed=$true;checks=[int]$result.Groups[1].Value;skipped=[int]$result.Groups[2].Value
    runtime_launched=$false;audio_started=$false;export_started=$false;full_build_started=$false
    checker_sha256=(Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
    scope='Synthetic module source identity checks; optional source-tree collection is read-only. No game, export or full build.'
}
if ($InspectRuntimeSource) {
    $identity = [regex]::Match($resultText,'(?m)^SOURCE: ([0-9a-f]{64}); ([0-9]+) files$')
    if (-not $identity.Success) { throw 'Expected read-only source identity was not published.' }
    $report.source_sha256 = $identity.Groups[1].Value
    $report.source_file_count = [int]$identity.Groups[2].Value
}
[IO.File]::WriteAllText($reportPath,($report | ConvertTo-Json -Depth 5),$utf8)
Write-Host $resultText
Write-Host "Module source evidence: $reportPath"
