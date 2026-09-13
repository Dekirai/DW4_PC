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
    throw 'The idle equivalence check requires the verified USA GDJEB2 main.dol.'
}
$outputRoot = Join-Path $repoRoot 'work/recomp-idle'
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$gxInclude = Join-Path $RuntimeSource 'vendor/dolphin/GXRuntime/include'
$coreInclude = Join-Path $RuntimeSource 'vendor/dolphin/Source/Core'
$policy = Join-Path $coreInclude 'Core/PowerPC/StaticRecomp/StaticRecompDw4Idle.h'
$generated = Join-Path (Split-Path -Parent $Module) 'dolrecomp-output/generated/chunks/chunk_0145_text1_80260160.c'
$generatedText = [IO.File]::ReadAllText($generated)
$body = [regex]::Match($generatedText,'(?s)static void loop_80261558\(CPUState\* ctx\) \{.*?\r?\n\}')
if (-not $body.Success) { throw 'The generated original idle loop is absent.' }
[IO.File]::WriteAllText((Join-Path $outputRoot 'generated_idle.inc'),$body.Value,[Text.UTF8Encoding]::new($false))
# Assert the actual original bytes and full scheduler chunk against the same
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
    if ([uint64]$base -le 0x80260160L -and [uint64]$base + $length -ge 0x80264160L) {
        $chunkOffset = [int]((Read-BE32 (4*$i)) + 0x80260160L - $base)
        break
    }
}
if ($chunkOffset -lt 0) { throw 'The original scheduler chunk is outside the DOL sections.' }
$chunkPath = Join-Path $outputRoot 'original-scheduler.bin'
$chunkBytes = [byte[]]::new(0x4000)
[Array]::Copy($bytes,$chunkOffset,$chunkBytes,0,$chunkBytes.Length)
[IO.File]::WriteAllBytes($chunkPath,$chunkBytes)
foreach ($instruction in @(@(0x13f8,0x800d1010L),@(0x13fc,0x28000000L),@(0x1400,0x4182fff8L))) {
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
function Invoke-IdleCommand([string]$Executable,[string[]]$Arguments) {
    $savedPreference = $ErrorActionPreference
    try {
        # Native diagnostics are not native failure in Windows PowerShell 5.1.
        $ErrorActionPreference = 'Continue'
        & $Executable @Arguments 2>&1 | Tee-Object -FilePath $logPath -Append -ErrorAction Stop
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    if ($nativeExit -ne 0) { throw "Idle policy check command failed ($nativeExit): $Executable. See $logPath" }
}
$object = Join-Path $outputRoot 'main.obj'
$executable = Join-Path $outputRoot 'recomp-idle-check.exe'
Invoke-IdleCommand $compiler @('/nologo','/c','/std:c++20','/EHsc','/W4','/WX','/permissive-',
    '/utf-8','/MD','/O2',"/I$gxInclude","/I$coreInclude","/I$outputRoot",
    (Join-Path $repoRoot 'tools/recomp_idle_check/main.cpp'),"/Fo$object")
Invoke-IdleCommand $compiler @('/nologo',$object,"/Fe$executable")
$reportPath = Join-Path $outputRoot 'latest.json'
Invoke-IdleCommand $executable @($reportPath,$chunkPath)
$record = [ordered]@{
    checked_at_utc=[DateTime]::UtcNow.ToString('o'); runtime_source=$RuntimeSource;
    module=$Module; module_sha256=(Get-FileHash -LiteralPath $Module -Algorithm SHA256).Hash;
    policy_sha256=(Get-FileHash -LiteralPath $policy -Algorithm SHA256).Hash;
    generated_source_sha256=(Get-FileHash -LiteralPath $generated -Algorithm SHA256).Hash;
    generated_loop_sha256=(Get-FileHash -LiteralPath (Join-Path $outputRoot 'generated_idle.inc') -Algorithm SHA256).Hash;
    original_dol_sha256=(Get-FileHash -LiteralPath $DolPath -Algorithm SHA256).Hash;
    checker_sha256=(Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash;
    report=$reportPath; compiler=$compiler; runtime_launched=$false
}
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputRoot 'invocation.json')
Write-Host "DW4 idle policy evidence: $reportPath"
