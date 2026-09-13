param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
$dolphinRoot = Join-Path $repoRoot 'work/moderngekko/vendor/dolphin'
$sourceRelative = 'GXRuntime/src/core/cpu.c'
$sourcePath = Join-Path $dolphinRoot $sourceRelative
$gxInclude = Join-Path $dolphinRoot 'GXRuntime/include'
$sourceRoot = Join-Path $repoRoot 'tools/recomp_quantizer_check'
$outputRoot = Join-Path $repoRoot 'build/recomp-quantizer-check'
$baselineCommit = '55c7b023fa0f4eba1cf3fdbbb25b1c5ec468d5ac'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'The quantizer equivalence checker requires Windows x64 and MSVC.'
}
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$encoding = New-Object System.Text.UTF8Encoding($false)
$baselineLines = & git -C $dolphinRoot show ($baselineCommit + ':' + $sourceRelative)
if ($LASTEXITCODE -ne 0) { throw 'Cannot read the pinned Dolphin quantizer source.' }
$baselineSource = ($baselineLines -join "`n") + "`n"
$baselineBlob = & git -C $dolphinRoot rev-parse ($baselineCommit + ':' + $sourceRelative)
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the baseline source blob.' }
$currentSource = [IO.File]::ReadAllText($sourcePath)

function Write-ExtractedQuantizer([string]$Text, [string]$Name) {
    # Deliberately accept the current simple helper only; fail closed on changed structure.
    $matches = [regex]::Matches($Text, '(?ms)^static s64 psq_quantize_int\(f64 value, s64 min_value, s64 max_value, s32 scale\) \{[^{}]*^\}')
    if ($matches.Count -ne 1) { throw "Expected one simple quantizer helper in $Name source." }
    $helper = $matches[0].Value
    $factor = [regex]::Match($helper, 'f32 conv = \(f32\)value \* (.+);')
    if (-not $factor.Success) { throw "Cannot extract the factor expression in $Name source." }
    [IO.File]::WriteAllText((Join-Path $outputRoot ($Name + '-helper.txt')), $helper, $encoding)
    $renamed = $helper.Replace('static s64 psq_quantize_int(', ('__declspec(noinline) s64 ' + $Name + '_quantize('))
    # Volatile arguments plus separate, non-LTCG translation units prevent constant folding.
    $factorExpression = $factor.Groups[1].Value.Replace('1.0f', 'one')
    $factorFunction = @"
__declspec(noinline) u32 ${Name}_factor(s32 requested_scale) {
    volatile s32 scale = requested_scale;
    volatile f32 one = 1.0f;
    (void)one;
    f32 value = $factorExpression;
    u32 bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}
"@
    $generated = "// Extracted from $Name GXRuntime cpu.c; do not edit.`n#include `"core/types.h`"`n#include <math.h>`n" + $renamed + "`n" + $factorFunction
    [IO.File]::WriteAllText((Join-Path $outputRoot ($Name + '.c')), $generated, $encoding)
}
Write-ExtractedQuantizer $baselineSource 'baseline'
Write-ExtractedQuantizer $currentSource 'current'

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
    if ($nativeExit -ne 0) { throw "Quantizer checker command failed ($nativeExit): $Executable. See $logPath" }
}
$objects = @()
foreach ($name in @('baseline', 'current', 'main')) {
    $object = Join-Path $outputRoot ($name + '.obj')
    $arguments = @('/nologo', '/c', '/O2', '/fp:strict', '/W4', '/WX', '/MD', '/utf-8', "/I$gxInclude")
    if ($name -eq 'main') {
        $arguments += @('/std:c++20', '/EHsc', (Join-Path $sourceRoot 'main.cpp'))
    } else {
        $arguments += @('/TC', '/std:c11', (Join-Path $outputRoot ($name + '.c')))
    }
    Invoke-CheckerCommand $compiler ($arguments + @("/Fo$object"))
    $objects += $object
}
$executable = Join-Path $outputRoot 'recomp-quantizer-check.exe'
Invoke-CheckerCommand $compiler (@('/nologo') + $objects + @("/Fe$executable", '/link', '/MACHINE:X64'))
$reportPath = Join-Path $outputRoot 'latest.json'
Invoke-CheckerCommand $executable @($reportPath)
$report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
if (-not $report.passed -or $report.original_mxcsr -ne $report.restored_mxcsr) { throw 'Quantizer equivalence or MXCSR restoration failed.' }
$record = [ordered]@{
    checked_at_utc = [DateTime]::UtcNow.ToString('o')
    baseline_commit = $baselineCommit
    baseline_cpu_blob = [string]$baselineBlob
    current_cpu_sha256 = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    baseline_helper_sha256 = (Get-FileHash -LiteralPath (Join-Path $outputRoot 'baseline-helper.txt') -Algorithm SHA256).Hash
    current_helper_sha256 = (Get-FileHash -LiteralPath (Join-Path $outputRoot 'current-helper.txt') -Algorithm SHA256).Hash
    gx_types_sha256 = (Get-FileHash -LiteralPath (Join-Path $gxInclude 'core/types.h') -Algorithm SHA256).Hash
    checker_source_sha256 = (Get-FileHash -LiteralPath (Join-Path $sourceRoot 'main.cpp') -Algorithm SHA256).Hash
    wrapper_sha256 = (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash
    checker_sha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
    compiler = $compiler
    compiler_version = [Diagnostics.FileVersionInfo]::GetVersionInfo($compiler).FileVersion
    compile_options = '/O2 /fp:strict /W4 /WX /MD; helpers /TC /std:c11; harness /std:c++20; no LTCG'
    report = $reportPath
    game_code_executed = $false
}
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputRoot 'invocation.json') -Encoding utf8
Write-Host "Quantizer equivalence evidence: $reportPath"
