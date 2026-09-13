param([string]$Python = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
$runtimeSource = Join-Path $repoRoot 'work/moderngekko'
$core = Join-Path $runtimeSource 'vendor/dolphin/Source/Core'
$fmt = Join-Path $runtimeSource 'vendor/dolphin/Externals/fmt/fmt/include'
$sysRoot = Join-Path $runtimeSource 'vendor/dolphin/Data/Sys'
$sourcePath = Join-Path $repoRoot 'tools/recomp_system_resource_check/main.cpp'
$outputRoot = Join-Path $repoRoot 'build/recomp-system-resource-check'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'The system-resource checker requires Windows x64 and MSVC.'
}
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
if (-not $Python) {
    $cache = Join-Path $repoRoot 'work/moderngekko-build/CMakeCache.txt'
    if (Test-Path -LiteralPath $cache) {
        $match = Select-String -LiteralPath $cache -Pattern '^Python3_EXECUTABLE:[^=]+=(.+)$' | Select-Object -First 1
        if ($match) { $Python = $match.Matches[0].Groups[1].Value }
    }
    if (-not $Python) {
        $found = Get-Command python.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found -and $found.Source -notmatch '[\\/]WindowsApps[\\/]') { $Python = $found.Source }
    }
}
if (-not $Python -or -not (Test-Path -LiteralPath $Python -PathType Leaf)) {
    throw 'Pass -Python with the path to Python 3.8 or newer, or configure the runtime first.'
}
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
    if ($nativeExit -ne 0) { throw "System-resource checker command failed ($nativeExit): $Executable. See $logPath" }
}
$sourceFiles = @(
    $sourcePath, $PSCommandPath,
    (Join-Path $runtimeSource 'tools/embed_system_resources.py'),
    (Join-Path $runtimeSource 'tools/test_embed_system_resources.py'),
    (Join-Path $core 'Common/SystemResource.cpp'), (Join-Path $core 'Common/SystemResource.h'),
    (Join-Path $core 'Common/IniFile.cpp'), (Join-Path $core 'Common/IniFile.h'),
    (Join-Path $core 'Common/StringUtil.cpp'), (Join-Path $core 'Common/StringUtil.h'))
$sourceHashes = @{}
foreach ($path in $sourceFiles) { $sourceHashes[$path] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
Invoke-CheckerCommand $Python @('-B', (Join-Path $runtimeSource 'tools/test_embed_system_resources.py'))
$generated = Join-Path $outputRoot 'generated'
Invoke-CheckerCommand $Python @('-B', (Join-Path $runtimeSource 'tools/embed_system_resources.py'),
    '--sys-directory', $sysRoot,
    '--output', (Join-Path $generated 'moderngekko_system_resources.inc'),
    '--manifest', (Join-Path $generated 'system-resources.json'))
$results = @()
foreach ($mode in @('embedded', 'generic')) {
    $output = Join-Path $outputRoot $mode
    [IO.Directory]::CreateDirectory($output) | Out-Null
    $executable = Join-Path $output 'resource-test.exe'
    # Upstream Common/Swap.h has an intentionally unused parameter in its one-byte specialization.
    $arguments = @('/nologo', '/utf-8', '/std:c++latest', '/EHsc', '/Zc:preprocessor', '/permissive-',
        '/W4', '/WX', '/wd4100', '/MD', '/O2', '/Gy', '/DNDEBUG', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS',
        '/D_M_X86_64=1', '/D_ARCH_64=1', '/DFMT_HEADER_ONLY=1',
        "/I$core", "/external:I$fmt", '/external:W0', "/I$generated",
        "/Fo$output/", "/Fe$executable", $sourcePath,
        (Join-Path $core 'Common/SystemResource.cpp'),
        (Join-Path $core 'Common/IniFile.cpp'),
        (Join-Path $core 'Common/StringUtil.cpp'))
    if ($mode -eq 'embedded') { $arguments += @('/DMODERNGEKKO_EMBED_SYSTEM_RESOURCES=1', '/DTEST_EMBEDDED=1') }
    $arguments += @('/link', '/OPT:REF', '/MACHINE:X64', 'shell32.lib')
    Invoke-CheckerCommand $compiler $arguments
    $fixtures = Join-Path $output ('fixtures-' + [Guid]::NewGuid().ToString('N'))
    $result = Invoke-CheckerCommand $executable @($fixtures, $sysRoot)
    $result | Write-Output
    $results += [ordered]@{ mode = $mode; passed = $true; result = ($result -join "`n"); fixtures = $fixtures;
        executable_sha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash }
}
foreach ($path in $sourceFiles) {
    if ($sourceHashes[$path] -ne (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash) {
        throw "Source changed during validation: $path"
    }
}
$record = [ordered]@{
    checked_at_utc = [DateTime]::UtcNow.ToString('o')
    passed = $true
    generator_tests = 8
    source_sha256 = $sourceHashes
    compiler = $compiler
    compiler_version = [Diagnostics.FileVersionInfo]::GetVersionInfo($compiler).FileVersion
    compile_options = '/O2 /std:c++latest /EHsc /W4 /WX /wd4100 /MD; actual SystemResource, IniFile, StringUtil sources'
    generated_include_sha256 = (Get-FileHash -LiteralPath (Join-Path $generated 'moderngekko_system_resources.inc') -Algorithm SHA256).Hash
    resources = (Get-Content -LiteralPath (Join-Path $generated 'system-resources.json') -Raw | ConvertFrom-Json)
    modes = $results
    game_executed = $false
}
$reportPath = Join-Path $outputRoot 'latest.json'
$record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8
Write-Host "System-resource and actual INI parser evidence: $reportPath"
