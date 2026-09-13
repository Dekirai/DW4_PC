param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
$includeRoot = Join-Path $repoRoot 'work/moderngekko/tools'
$headerPath = Join-Path $includeRoot 'portable_profile.hpp'
$sourcePath = Join-Path $repoRoot 'tools/recomp_portable_profile_check/main.cpp'
$outputRoot = Join-Path $repoRoot 'build/recomp-portable-profile-check'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'The portable-profile migration checker requires Windows x64 and MSVC.'
}
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$headerHash = (Get-FileHash -LiteralPath $headerPath -Algorithm SHA256).Hash
if (-not (Get-Command cl.exe -CommandType Application -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio with the C++ workload is required.' }
    $installation = [string](& $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    if ($LASTEXITCODE -ne 0 -or -not $installation.Trim()) { throw 'Could not locate the MSVC x64 toolchain.' }
    $environmentScript = Join-Path $outputRoot 'compiler-env.cmd'
    $vcvars = Join-Path $installation.Trim() 'VC/Auxiliary/Build/vcvars64.bat'
    @('@echo off', ('call "' + $vcvars + '" >nul'), 'if errorlevel 1 exit /b %errorlevel%', 'set') |
        Set-Content -LiteralPath $environmentScript -Encoding ascii
    $compilerEnvironment = & $env:ComSpec /d /c $environmentScript
    if ($LASTEXITCODE -ne 0) { throw 'The MSVC x64 environment could not be initialized.' }
    foreach ($entry in $compilerEnvironment) {
        if ($entry -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
    }
}
$compiler = (Get-Command cl.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$logPath = Join-Path $outputRoot 'build.log'
[IO.File]::WriteAllText($logPath, '')
function Invoke-CheckerCommand([string]$Executable, [string[]]$Arguments) {
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Executable @Arguments 2>&1 | Tee-Object -FilePath $logPath -Append -ErrorAction Stop
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    if ($nativeExit -ne 0) { throw "Portable-profile checker command failed ($nativeExit): $Executable. See $logPath" }
}
$executable = Join-Path $outputRoot 'recomp-portable-profile-check.exe'
$object = Join-Path $outputRoot 'main.obj'
$arguments = @('/nologo', '/O2', '/std:c++20', '/EHsc', '/W4', '/WX', '/MD', '/utf-8', "/I$includeRoot", $sourcePath, "/Fo$object", "/Fe$executable", '/link', '/MACHINE:X64')
Invoke-CheckerCommand $compiler $arguments
$fixtures = Join-Path $outputRoot ('fixtures-' + [Guid]::NewGuid().ToString('N'))
$reportPath = Join-Path $outputRoot 'latest.json'
Invoke-CheckerCommand $executable @($fixtures, $reportPath)
$report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
if (-not $report.passed -or $report.failures -ne 0) { throw 'Portable-profile migration validation failed.' }
if ($headerHash -ne (Get-FileHash -LiteralPath $headerPath -Algorithm SHA256).Hash) { throw 'Migration header changed during validation.' }
$record = [ordered]@{
    checked_at_utc = [DateTime]::UtcNow.ToString('o')
    header_sha256 = $headerHash
    checker_source_sha256 = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    wrapper_sha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
    checker_sha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
    compiler = $compiler
    compiler_version = [Diagnostics.FileVersionInfo]::GetVersionInfo($compiler).FileVersion
    compile_options = '/O2 /std:c++20 /EHsc /W4 /WX /MD; actual portable_profile.hpp'
    fixtures = $fixtures
    report = $reportPath
    game_executed = $false
}
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputRoot 'invocation.json') -Encoding utf8
Write-Host "Portable-profile migration evidence: $reportPath"
