param([string]$RuntimeSource = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $RuntimeSource) { $RuntimeSource = Join-Path $repoRoot 'work/moderngekko' }
$RuntimeSource = (Resolve-Path -LiteralPath $RuntimeSource).Path
$chassisRoot = Join-Path $RuntimeSource 'vendor/dolphin'
$upstreamRevision = '55c7b023fa0f4eba1cf3fdbbb25b1c5ec468d5ac'
$relativeSource = 'Source/Core/Core/PowerPC/JitCommon/JitCache.cpp'
$source = Join-Path $chassisRoot $relativeSource
$outputRoot = Join-Path $repoRoot 'work/recomp-jit-cache'
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$utf8 = [Text.UTF8Encoding]::new($false)

# Capture UTF-8 directly from Git, avoiding Windows PowerShell's native console
# decoding. This reads the pinned source; it does not change the dependency.
function Read-PinnedGitText([string]$Object) {
    $git = (Get-Command git.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $git
    $start.WorkingDirectory = $chassisRoot
    $start.Arguments = 'cat-file blob ' + $Object
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = $utf8
    $start.StandardErrorEncoding = $utf8
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw 'Could not start the read-only Git source query.' }
        $errorTask = $process.StandardError.ReadToEndAsync()
        $result = $process.StandardOutput.ReadToEnd()
        $process.WaitForExit()
        $diagnostic = $errorTask.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { throw "Pinned source query failed: $diagnostic" }
        return $result
    } finally { $process.Dispose() }
}

function Get-InvalidationMethods([string]$Text) {
    $normalized = $Text.Replace("`r`n", "`n")
    $match = [regex]::Match($normalized,
        '(?s)void JitBaseBlockCache::InvalidateICache\(.*?(?=\nvoid JitBaseBlockCache::ErasePhysicalRange\()')
    if (-not $match.Success -or
        ([regex]::Matches($match.Value, 'void JitBaseBlockCache::')).Count -ne 2) {
        throw 'Could not extract the exact InvalidateICache and InvalidateICacheInternal methods.'
    }
    return $match.Value
}

$originalText = Read-PinnedGitText ($upstreamRevision + ':' + $relativeSource)
$currentText = [IO.File]::ReadAllText($source, $utf8)
$originalMethods = Get-InvalidationMethods $originalText
$patchedMethods = Get-InvalidationMethods $currentText
$originalCondition = 'if (!forced)'
$patchedCondition = @'
if (!forced && (!m_jit.js.fifoWriteAddresses.empty() ||
                    !m_jit.js.pairedQuantizeAddresses.empty() ||
                    !m_jit.js.noSpeculativeConstantsAddresses.empty()))
'@
$patchedCondition = $patchedCondition.Replace("`r`n", "`n")
if ([regex]::Matches($originalMethods, [regex]::Escape($originalCondition)).Count -ne 1 -or
    $originalMethods.Replace($originalCondition, $patchedCondition) -cne $patchedMethods) {
    throw 'The actual method change is not exclusively the reviewed empty-metadata-set guard.'
}
[IO.File]::WriteAllText((Join-Path $outputRoot 'original_methods.inc'),
    $originalMethods.Replace('JitBaseBlockCache::', 'OriginalCache::'), $utf8)
[IO.File]::WriteAllText((Join-Path $outputRoot 'patched_methods.inc'),
    $patchedMethods.Replace('JitBaseBlockCache::', 'PatchedCache::'), $utf8)

$mmuHeader = Join-Path $chassisRoot 'Source/Core/Core/PowerPC/MMU.h'
$mmuText = [IO.File]::ReadAllText($mmuHeader, $utf8)
$constants = @('namespace PowerPC {')
foreach ($name in @('BAT_INDEX_SHIFT', 'HW_PAGE_INDEX_SHIFT')) {
    $matches = [regex]::Matches($mmuText, ('constexpr (?:int|u32) ' + $name + ' = \d+;'))
    if ($matches.Count -ne 1) { throw "The actual MMU declaration is unsupported: $name" }
    $constants += $matches[0].Value
}
$constants += '}'
[IO.File]::WriteAllText((Join-Path $outputRoot 'mmu_constants.inc'), ($constants -join "`n"), $utf8)

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
        if ($entry -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
}
$compiler = (Get-Command cl.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$logPath = Join-Path $outputRoot 'build.log'
[IO.File]::WriteAllText($logPath, '', $utf8)
function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Executable @Arguments 2>&1 | Tee-Object -FilePath $logPath -Append -ErrorAction Stop
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    if ($nativeExit -ne 0) { throw "JIT cache checker failed ($nativeExit): $Executable. See $logPath" }
}
$object = Join-Path $outputRoot 'main.obj'
$executable = Join-Path $outputRoot 'recomp-jit-cache-check.exe'
$reportPath = Join-Path $outputRoot 'latest.json'
if (Test-Path -LiteralPath $reportPath) { Remove-Item -LiteralPath $reportPath }
Invoke-Checked $compiler @('/nologo', '/c', '/std:c++20', '/EHsc', '/W4', '/WX', '/permissive-',
    '/utf-8', '/MD', '/O2', "/I$outputRoot", (Join-Path $repoRoot 'tools/recomp_jit_cache_check/main.cpp'), "/Fo$object")
Invoke-Checked $compiler @('/nologo', $object, "/Fe$executable")
Invoke-Checked $executable @($reportPath)
$report = [IO.File]::ReadAllText($reportPath, $utf8) | ConvertFrom-Json
if ($report.passed -isnot [bool] -or -not $report.passed -or $report.cases -lt 1) {
    throw 'The checker did not publish a valid successful JSON report.'
}
$record = [ordered]@{
    checked_at_utc = [DateTime]::UtcNow.ToString('o'); runtime_source = $RuntimeSource;
    upstream_revision = $upstreamRevision;
    source_sha256 = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash;
    original_methods_sha256 = (Get-FileHash -LiteralPath (Join-Path $outputRoot 'original_methods.inc') -Algorithm SHA256).Hash;
    patched_methods_sha256 = (Get-FileHash -LiteralPath (Join-Path $outputRoot 'patched_methods.inc') -Algorithm SHA256).Hash;
    mmu_header_sha256 = (Get-FileHash -LiteralPath $mmuHeader -Algorithm SHA256).Hash;
    checker_sha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash;
    powershell_version = $PSVersionTable.PSVersion.ToString();
    report = $reportPath; compiler = $compiler; game_data_used = $false; runtime_launched = $false
}
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputRoot 'invocation.json')
Write-Host "JIT metadata invalidation evidence: $reportPath"
