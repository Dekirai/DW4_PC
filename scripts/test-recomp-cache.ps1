param(
    [Parameter(Mandatory=$true)][string]$Module,
    [string]$RuntimeSource = '',
    [Parameter(Mandatory=$true)][string]$DolPath
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $RuntimeSource) { $RuntimeSource = Join-Path $repoRoot 'work/moderngekko' }
$RuntimeSource = (Resolve-Path -LiteralPath $RuntimeSource).Path
$Module = (Resolve-Path -LiteralPath $Module).Path
$DolPath = (Resolve-Path -LiteralPath $DolPath).Path
if ((Get-FileHash -LiteralPath $DolPath -Algorithm SHA256).Hash -ne 'EC61C010477C844781763A3041922E38719B3CED3B74BE5CE71D778CE458E471') {
    throw 'The cache equivalence check requires the verified USA GDJEB2 main.dol.'
}
$outputRoot = Join-Path $repoRoot 'work/recomp-cache'
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$gxInclude = Join-Path $RuntimeSource 'vendor/dolphin/GXRuntime/include'
$coreInclude = Join-Path $RuntimeSource 'vendor/dolphin/Source/Core'
$policy = Join-Path $coreInclude 'Core/PowerPC/StaticRecomp/StaticRecompDw4Cache.h'
$moduleRoot = Split-Path -Parent $Module
$generated = Join-Path $moduleRoot 'dolrecomp-output/generated/chunks/chunk_0144_text1_8025C160.c'
$generatedText = [IO.File]::ReadAllText($generated)
$loops = @(
    @{ start='8025D3A0'; resume='8025D3A4'; end='8025D3B0'; raw=0x7c00186cL; terminal=0x4e800020L },
    @{ start='8025D314'; resume='8025D318'; end='8025D324'; raw=0x7c0018acL; terminal=0x44000002L },
    @{ start='8025D2E8'; resume='8025D2EC'; end='8025D2F8'; raw=0x7c001bacL; terminal=0x4e800020L }
)
# The local wrapper selects only each real cache instruction or addi reentry.
# Extracted instructions, fallback calls and the dcbf system call stay verbatim.
# Synthetic LR targets are outside this chunk;
# its full return-dispatch switch would therefore take the default return.
$returnSwitch = [regex]::Match($generatedText,'(?s)return_dispatch_8025C160:\s*.*?switch \(ctx->pc\) \{(?<cases>.*?)\r?\n\s*\}')
if (-not $returnSwitch.Success -or $returnSwitch.Groups['cases'].Value -notmatch 'default:\s*return;') {
    throw 'The generated return-dispatch default is unsupported.'
}
$wrapper = @('static void GeneratedCache(CPUState* ctx) {', '    switch (ctx->pc) {')
foreach ($loop in $loops) {
    foreach ($pc in @($loop.start, $loop.resume)) {
        $wrapper += "    case 0x${pc}u: goto label_${pc};"
    }
}
$wrapper += @('    default: Check(false, "Unsupported extracted entry PC."); return;', '    }')
foreach ($loop in $loops) {
    $pattern = '(?s)(?<=\n)label_' + $loop.start + ':.*?(?=\r?\nlabel_' + $loop.end + ':)'
    $body = [regex]::Match($generatedText, $pattern)
    if (-not $body.Success) { throw "The emitted cache loop at $($loop.start) is absent." }
    $wrapper += $body.Value
}
$wrapper += @(
    'return_dispatch_8025C160:',
    '    Check(ctx->pc < 0x8025C160u || ctx->pc >= 0x80260160u, "Return must leave the extracted chunk.");',
    '    return;',
    '}'
)
[IO.File]::WriteAllText((Join-Path $outputRoot 'generated_cache.inc'),($wrapper -join "`n"),[Text.UTF8Encoding]::new($false))

function Extract-Function([string]$Text, [string]$Name) {
    $clean = [regex]::Replace($Text, '(?s)/\*.*?\*/|//[^\r\n]*|"(?:\\.|[^"\\])*"',
        [Text.RegularExpressions.MatchEvaluator]{ param($m) ' ' * $m.Length })
    $matches = [regex]::Matches($clean, '(?m)^void\s+' + [regex]::Escape($Name) + '\s*\(')
    if ($matches.Count -ne 1) { throw "Expected exactly one definition of $Name." }
    $start = $matches[0].Index
    $brace = $clean.IndexOf('{', $start)
    if ($brace -lt 0 -or $clean.Substring($start, $brace - $start).Contains(';')) {
        throw "Cannot find function body: $Name"
    }
    $depth = 1
    $end = $brace + 1
    while ($end -lt $clean.Length -and $depth -gt 0) {
        if ($clean[$end] -eq '{') { $depth++ }
        if ($clean[$end] -eq '}') { $depth-- }
        $end++
    }
    if ($depth -ne 0) { throw "Unbalanced function: $Name" }
    return $Text.Substring($start, $end - $start)
}
$interpreterPath = Join-Path $coreInclude 'Core/PowerPC/Interpreter/Interpreter_LoadStore.cpp'
$interpreterText = [IO.File]::ReadAllText($interpreterPath)
$interpreterParts = foreach ($name in @('dcbf', 'dcbi', 'dcbst')) {
    Extract-Function $interpreterText ('Interpreter::' + $name)
}
[IO.File]::WriteAllText((Join-Path $outputRoot 'generated_interpreter.inc'),
    ($interpreterParts -join "`n`n"), [Text.UTF8Encoding]::new($false))
$cpuPath = Join-Path $RuntimeSource 'vendor/dolphin/GXRuntime/src/core/cpu.c'
$exceptionPath = Join-Path $RuntimeSource 'vendor/dolphin/GXRuntime/src/core/cpu_exception.c'
$exceptions = [IO.File]::ReadAllText($exceptionPath) + "`n" +
    (Extract-Function ([IO.File]::ReadAllText($cpuPath)) 'ppc_system_call_exception')
[IO.File]::WriteAllText((Join-Path $outputRoot 'generated_exceptions.inc'),
    $exceptions, [Text.UTF8Encoding]::new($false))
# The fixture retains the chassis split cost, verified against actual tables.
$tablesPath = Join-Path $coreInclude 'Core/PowerPC/PPCTables.cpp'
$tableText = [IO.File]::ReadAllText($tablesPath)
foreach ($op in @(@(54,'dcbst'), @(86,'dcbf'), @(470,'dcbi'))) {
    if ($tableText -notmatch ('\{' + $op[0] + ', "' + $op[1] + '", OpType::DataCache, 5,')) {
        throw 'Cache instruction cost differs from the five-cycle chassis fixture.'
    }
}
$tables = Join-Path $moduleRoot 'module-build/module_tables.inc'
$ranges = [regex]::Match([IO.File]::ReadAllText($tables),'(?s)static const StaticRecompRange s_code_ranges\[\] = \{.*?\r?\n\};')
if (-not $ranges.Success) { throw 'The actual generated module code-range table is absent.' }
[IO.File]::WriteAllText((Join-Path $outputRoot 'generated_code_ranges.inc'),$ranges.Value,[Text.UTF8Encoding]::new($false))
# Assert the actual original bytes and full cache chunk against the same
# constants as the policy; game contents remain local and are not redistributed.
$bytes = [IO.File]::ReadAllBytes($DolPath)
function Read-BE32([int]$Offset) {
    return [uint32](([uint64]$bytes[$Offset] -shl 24) -bor ([uint64]$bytes[$Offset+1] -shl 16) -bor
        ([uint64]$bytes[$Offset+2] -shl 8) -bor [uint64]$bytes[$Offset+3])
}
$chunkOffset = -1
for ($i=0; $i -lt 18; $i++) {
    $base = Read-BE32 (0x48 + 4*$i)
    $length = Read-BE32 (0x90 + 4*$i)
    if ([uint64]$base -le 0x8025C160L -and [uint64]$base + $length -ge 0x80260160L) {
        $chunkOffset = [int]((Read-BE32 (4*$i)) + 0x8025C160L - $base)
        break
    }
}
if ($chunkOffset -lt 0) { throw 'The original cache chunk is outside the DOL sections.' }
$chunkPath = Join-Path $outputRoot 'original-cache.bin'
$chunkBytes = [byte[]]::new(0x4000)
[Array]::Copy($bytes,$chunkOffset,$chunkBytes,0,$chunkBytes.Length)
[IO.File]::WriteAllBytes($chunkPath,$chunkBytes)
foreach ($loop in $loops) {
    $offset = [int]([Convert]::ToUInt32($loop.start,16) - 0x8025C160L)
    foreach ($instruction in @(@(0,$loop.raw),@(4,0x38630020L),@(8,0x4200fff8L),@(12,$loop.terminal))) {
        if ((Read-BE32 ($chunkOffset + $offset + $instruction[0])) -ne $instruction[1]) {
            throw "Original loop instruction mismatch at $($loop.start)."
        }
    }
}
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
function Invoke-CacheCommand([string]$Executable,[string[]]$Arguments) {
    $savedPreference = $ErrorActionPreference
    try {
        # Native diagnostics are not native failure in Windows PowerShell 5.1.
        $ErrorActionPreference = 'Continue'
        & $Executable @Arguments 2>&1 | Tee-Object -FilePath $logPath -Append -ErrorAction Stop
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    if ($nativeExit -ne 0) { throw "Cache policy check command failed ($nativeExit): $Executable. See $logPath" }
}
$object = Join-Path $outputRoot 'main.obj'
$executable = Join-Path $outputRoot 'recomp-cache-check.exe'
Invoke-CacheCommand $compiler @('/nologo','/c','/std:c++20','/EHsc','/W4','/WX','/permissive-',
    '/utf-8','/MD','/O2',"/I$gxInclude","/I$coreInclude","/I$outputRoot",
    (Join-Path $repoRoot 'tools/recomp_cache_check/main.cpp'),"/Fo$object")
Invoke-CacheCommand $compiler @('/nologo',$object,"/Fe$executable")
$reportPath = Join-Path $outputRoot 'latest.json'
Invoke-CacheCommand $executable @($reportPath,$chunkPath)
$record = [ordered]@{
    checked_at_utc=[DateTime]::UtcNow.ToString('o'); runtime_source=$RuntimeSource;
    module=$Module; module_sha256=(Get-FileHash -LiteralPath $Module -Algorithm SHA256).Hash;
    policy_sha256=(Get-FileHash -LiteralPath $policy -Algorithm SHA256).Hash;
    generated_source_sha256=(Get-FileHash -LiteralPath $generated -Algorithm SHA256).Hash;
    generated_block_sha256=(Get-FileHash -LiteralPath (Join-Path $outputRoot 'generated_cache.inc') -Algorithm SHA256).Hash;
    interpreter_source_sha256=(Get-FileHash -LiteralPath $interpreterPath -Algorithm SHA256).Hash;
    interpreter_functions_sha256=(Get-FileHash -LiteralPath (Join-Path $outputRoot 'generated_interpreter.inc') -Algorithm SHA256).Hash;
    gxruntime_cpu_sha256=(Get-FileHash -LiteralPath $cpuPath -Algorithm SHA256).Hash;
    gxruntime_exceptions_sha256=(Get-FileHash -LiteralPath $exceptionPath -Algorithm SHA256).Hash;
    exception_functions_sha256=(Get-FileHash -LiteralPath (Join-Path $outputRoot 'generated_exceptions.inc') -Algorithm SHA256).Hash;
    instruction_costs_sha256=(Get-FileHash -LiteralPath $tablesPath -Algorithm SHA256).Hash;
    module_tables_sha256=(Get-FileHash -LiteralPath $tables -Algorithm SHA256).Hash;
    original_dol_sha256=(Get-FileHash -LiteralPath $DolPath -Algorithm SHA256).Hash;
    checker_sha256=(Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash;
    report=$reportPath; compiler=$compiler; runtime_launched=$false
}
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputRoot 'invocation.json')
Write-Host "DW4 cache policy evidence: $reportPath"
