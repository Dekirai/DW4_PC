param([string]$RuntimeSource = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'The audio runtime/status checker currently requires Windows and MSVC.'
}
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $RuntimeSource) { $RuntimeSource = Join-Path $repoRoot 'work/moderngekko' }
$RuntimeSource = (Resolve-Path -LiteralPath $RuntimeSource).Path
$outputRoot = Join-Path $repoRoot 'work/recomp-audio-runtime'
$toolsInclude = Join-Path $RuntimeSource 'tools'
$runtimeInclude = Join-Path $RuntimeSource 'src/runtime'
$coreInclude = Join-Path $RuntimeSource 'vendor/dolphin/Source/Core'
$testSource = Join-Path $repoRoot 'tests/recomp_audio_runtime_tests.cpp'
$protocolSource = Join-Path $toolsInclude 'automation_protocol.cpp'
$inputs = @($testSource,$protocolSource,(Join-Path $toolsInclude 'automation_protocol.hpp'),
    (Join-Path $runtimeInclude 'dolphin_runtime_internal.hpp'),
    (Join-Path $coreInclude 'AudioCommon/AudioTelemetry.h'))
foreach ($path in $inputs) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required audio test input is missing: $path" }
}
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null

# Compile the actual pure diagnostic policy and status serialization only.
# No runtime library, generated game module or audio backend is loaded.
if (-not (Get-Command cl.exe -CommandType Application -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio with the C++ workload is required.' }
    $installation = [string](& $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    if ($LASTEXITCODE -ne 0 -or -not $installation.Trim()) { throw 'Could not locate the MSVC x64 toolchain.' }
    $environmentScript = Join-Path $outputRoot 'compiler-env.cmd'
    $vcvars = Join-Path $installation.Trim() 'VC/Auxiliary/Build/vcvars64.bat'
    @('@echo off',('call "' + $vcvars + '" >nul'),'if errorlevel 1 exit /b %errorlevel%','set') |
        Set-Content -LiteralPath $environmentScript -Encoding ascii
    $compilerEnvironment = & $env:ComSpec /d /c $environmentScript
    if ($LASTEXITCODE -ne 0) { throw 'The MSVC x64 environment could not be initialized.' }
    foreach ($entry in $compilerEnvironment) {
        if ($entry -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process') }
    }
}
$compiler = (Get-Command cl.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$logPath = Join-Path $outputRoot 'build.log'
[IO.File]::WriteAllText($logPath,'')
function Invoke-AudioCheck([string]$Executable,[string[]]$Arguments) {
    $savedPreference = $ErrorActionPreference
    try {
        # Successful native stderr is not failure in Windows PowerShell 5.1.
        $ErrorActionPreference = 'Continue'
        & $Executable @Arguments 2>&1 | Tee-Object -FilePath $logPath -Append -ErrorAction Stop
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    if ($nativeExit -ne 0) { throw "Audio runtime/status check failed ($nativeExit): $Executable. See $logPath" }
}
$objects = @()
foreach ($source in @($testSource,$protocolSource)) {
    $object = Join-Path $outputRoot (([IO.Path]::GetFileNameWithoutExtension($source)) + '.obj')
    Invoke-AudioCheck $compiler @('/nologo','/c','/std:c++latest','/EHsc','/W4','/WX',
        '/permissive-','/utf-8','/MD','/O2',"/I$toolsInclude","/I$runtimeInclude","/I$coreInclude",
        $source,"/Fo$object")
    $objects += $object
}
$executable = Join-Path $outputRoot 'recomp-audio-runtime-check.exe'
Invoke-AudioCheck $compiler (@('/nologo') + $objects + @("/Fe$executable"))
Invoke-AudioCheck $executable @()
$hashes = [ordered]@{}
foreach ($path in $inputs) { $hashes[$path] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
$record = [ordered]@{
    passed=$true; checked_at_utc=[DateTime]::UtcNow.ToString('o'); runtime_source=$RuntimeSource;
    powershell_version=$PSVersionTable.PSVersion.ToString(); compiler=$compiler; source_sha256=$hashes;
    checker_sha256=(Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash;
    runtime_launched=$false; audio_device_opened=$false; original_assets_used=$false
}
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputRoot 'invocation.json') -Encoding UTF8
Write-Host "Audio runtime/status evidence: $logPath"
