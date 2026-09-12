param(
    [Parameter(Mandatory=$true)][string]$Module,
    [string]$RuntimeSource = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'The recompilation ABI checker currently requires Windows and MSVC.'
}
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $RuntimeSource) { $RuntimeSource = Join-Path $repoRoot 'work/moderngekko' }
$RuntimeSource = (Resolve-Path -LiteralPath $RuntimeSource).Path
$Module = (Resolve-Path -LiteralPath $Module).Path
$sourceRoot = Join-Path $repoRoot 'tools/recomp_abi_check'
$outputRoot = Join-Path $repoRoot 'work/recomp-abi'
$publicInclude = Join-Path $RuntimeSource 'include'
$gxInclude = Join-Path $RuntimeSource 'vendor/dolphin/GXRuntime/include'
$chassisInclude = Join-Path $RuntimeSource 'vendor/dolphin/Source/Core/Core/PowerPC/StaticRecomp'
$publicCpu = Join-Path $publicInclude 'moderngekko/cpu_state.h'
$gxCpu = Join-Path $gxInclude 'core/cpu.h'
foreach ($path in @($Module,$publicCpu,$gxCpu,(Join-Path $chassisInclude 'StaticRecompABI.h'))) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required ABI input is missing: $path" }
}
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null

# Fail if a header gains an unchecked field, even if it fits existing padding.
# This is deliberately a checked parser for the pinned, simple declarations.
function Get-CpuFieldNames([string]$Path) {
    $text = [IO.File]::ReadAllText($Path)
    $body = [regex]::Match($text,'(?s)struct\s+CPUState\s*\{(.*?)\n\};')
    if (-not $body.Success) { throw "Could not find the CPUState declaration: $Path" }
    $fields = [regex]::Matches($body.Groups[1].Value,'(?m)^\s*[A-Za-z_0-9*]+\s+([A-Za-z_0-9]+)(?:\[[^\]]*\])?\s*;')
    return @($fields | ForEach-Object { $_.Groups[1].Value })
}
$fieldText = [IO.File]::ReadAllText((Join-Path $sourceRoot 'fields.inc'))
$expectedFields = @([regex]::Matches($fieldText,'DW4_CPU_FIELD\(([A-Za-z_0-9]+)\)') |
    ForEach-Object { $_.Groups[1].Value })
foreach ($header in @($publicCpu,$gxCpu)) {
    $actualFields = @(Get-CpuFieldNames $header)
    if (($actualFields -join ',') -cne ($expectedFields -join ',')) {
        throw "CPUState field inventory changed in $header. Update/review the ABI checker before building a module."
    }
}

# build-recomp.ps1 already establishes this environment. Allow a standalone
# check to discover the same installed x64 compiler without building anything.
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
function Invoke-AbiCommand([string]$Executable,[string[]]$Arguments) {
    $savedPreference = $ErrorActionPreference
    try {
        # Native diagnostics are not native failure in Windows PowerShell 5.1.
        $ErrorActionPreference = 'Continue'
        & $Executable @Arguments 2>&1 | Tee-Object -FilePath $logPath -Append -ErrorAction Stop
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    if ($nativeExit -ne 0) { throw "ABI check command failed ($nativeExit): $Executable. See $logPath" }
}
$objects = @()
foreach ($name in @('host','chassis','main')) {
    $object = Join-Path $outputRoot ($name + '.obj')
    $arguments = @('/nologo','/c','/std:c++20','/EHsc','/W4','/WX','/permissive-','/utf-8','/MD','/O2','/GR',
        '/DWIN32_LEAN_AND_MEAN','/DNOMINMAX',"/I$publicInclude","/I$gxInclude","/I$chassisInclude",
        (Join-Path $sourceRoot ($name + '.cpp')),"/Fo$object")
    Invoke-AbiCommand $compiler $arguments
    $objects += $object
}
$executable = Join-Path $outputRoot 'recomp-abi-check.exe'
Invoke-AbiCommand $compiler (@('/nologo') + $objects + @("/Fe$executable"))
$reportPath = Join-Path $outputRoot 'latest.json'
Invoke-AbiCommand $executable @($Module,$reportPath)
$report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
if (-not $report.passed) { throw "The ABI checker rejected the module. See $reportPath" }
$record = [ordered]@{
    checked_at_utc=[DateTime]::UtcNow.ToString('o'); runtime_source=$RuntimeSource; module=$Module;
    module_sha256=(Get-FileHash -LiteralPath $Module -Algorithm SHA256).Hash;
    checker_sha256=(Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash;
    public_cpu_header_sha256=(Get-FileHash -LiteralPath $publicCpu -Algorithm SHA256).Hash;
    gx_cpu_header_sha256=(Get-FileHash -LiteralPath $gxCpu -Algorithm SHA256).Hash;
    report=$reportPath; compiler=$compiler; game_code_executed=$false
}
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputRoot 'invocation.json')
Write-Host "Recompilation ABI evidence: $reportPath"
