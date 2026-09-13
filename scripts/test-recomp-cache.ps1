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
$body = [regex]::Match($generatedText,'(?s)(?<=\n)label_8025D3A0:.*?(?=\r?\nlabel_8025D3B0:)')
if (-not $body.Success) { throw 'The emitted A0/A4 cache loop and terminal return are absent.' }
# The local wrapper only selects A0 or A4. The extracted instructions and
# fallback call remain verbatim. Synthetic LR targets are outside this chunk;
# its full return-dispatch switch would therefore take the default return.
$returnSwitch = [regex]::Match($generatedText,'(?s)return_dispatch_8025C160:\s*.*?switch \(ctx->pc\) \{(?<cases>.*?)\r?\n\s*\}')
if (-not $returnSwitch.Success -or $returnSwitch.Groups['cases'].Value -notmatch 'default:\s*return;') {
    throw 'The generated return-dispatch default is unsupported.'
}
$wrapper = @(
    'static void GeneratedCache(CPUState* ctx) {',
    '    if (ctx->pc == 0x8025D3A4u) goto label_8025D3A4;',
    '    Check(ctx->pc == 0x8025D3A0u, "Unsupported extracted entry PC.");',
    '    goto label_8025D3A0;',
    $body.Value,
    'return_dispatch_8025C160:',
    '    Check(ctx->pc < 0x8025C160u || ctx->pc >= 0x80260160u, "Return must leave the extracted chunk.");',
    '    return;',
    '}'
) -join "`n"
[IO.File]::WriteAllText((Join-Path $outputRoot 'generated_cache.inc'),$wrapper,[Text.UTF8Encoding]::new($false))
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
foreach ($instruction in @(@(0x1240,0x7c00186cL),@(0x1244,0x38630020L),@(0x1248,0x4200fff8L),@(0x124c,0x4e800020L))) {
    if ((Read-BE32 ($chunkOffset + $instruction[0])) -ne $instruction[1]) { throw 'Original loop instruction mismatch.' }
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
    module_tables_sha256=(Get-FileHash -LiteralPath $tables -Algorithm SHA256).Hash;
    original_dol_sha256=(Get-FileHash -LiteralPath $DolPath -Algorithm SHA256).Hash;
    checker_sha256=(Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash;
    report=$reportPath; compiler=$compiler; runtime_launched=$false
}
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputRoot 'invocation.json')
Write-Host "DW4 cache policy evidence: $reportPath"
