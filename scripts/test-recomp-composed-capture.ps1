param([string]$RuntimeSource = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $RuntimeSource) { $RuntimeSource = Join-Path $repoRoot 'work/moderngekko' }
$RuntimeSource = (Resolve-Path -LiteralPath $RuntimeSource).Path
$outputRoot = Join-Path $repoRoot 'work/recomp-composed-capture'
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$coreInclude = Join-Path $RuntimeSource 'vendor/dolphin/Source/Core'
$header = Join-Path $coreInclude 'VideoCommon/ComposedFrameCapture.h'
if (-not (Test-Path -LiteralPath $header -PathType Leaf)) { throw 'The composed capture header is absent.' }

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
[IO.File]::WriteAllText($logPath, '')
function Invoke-CaptureCheck([string]$Executable, [string[]]$Arguments) {
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Executable @Arguments 2>&1 | Tee-Object -FilePath $logPath -Append -ErrorAction Stop
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    if ($nativeExit -ne 0) { throw "Composed capture check failed ($nativeExit): $Executable. See $logPath" }
}
$object = Join-Path $outputRoot 'main.obj'
$executable = Join-Path $outputRoot 'recomp-composed-capture-check.exe'
Invoke-CaptureCheck $compiler @('/nologo','/c','/std:c++20','/EHsc','/W4','/WX','/permissive-',
    '/utf-8','/MD','/O2',"/I$coreInclude",(Join-Path $repoRoot 'tools/recomp_composed_capture_check/main.cpp'),"/Fo$object")
Invoke-CaptureCheck $compiler @('/nologo',$object,"/Fe$executable")
Invoke-CaptureCheck $executable @()
$result = Select-String -LiteralPath $logPath -Pattern '^\{"passed":true' | Select-Object -Last 1
if (-not $result) { throw 'The checker did not publish a success report.' }
$reportPath = Join-Path $outputRoot 'latest.json'
[IO.File]::WriteAllText($reportPath, $result.Line, [Text.UTF8Encoding]::new($false))
[ordered]@{
    checked_at_utc = [DateTime]::UtcNow.ToString('o')
    runtime_source = $RuntimeSource
    header_sha256 = (Get-FileHash -LiteralPath $header -Algorithm SHA256).Hash
    checker_sha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
    compiler = $compiler
    report = $reportPath
    runtime_launched = $false
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot 'invocation.json')
Write-Host "Composed capture channel and SDR conversion evidence: $reportPath"
