param(
    [string]$DolRecompSource = (Join-Path $env:USERPROFILE 'Downloads/DolRecomp-main'),
    [string]$TemplateSource = (Join-Path $env:USERPROFILE 'Downloads/ModernGekko-Template-main'),
    [Parameter(Mandatory=$true)][string]$ExtractedRoot,
    [ValidateRange(1,64)][int]$Jobs = 4,
    [ValidateRange(0,3)][int]$OptLevel = 2,
    [switch]$RuntimeOnly
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
$workRoot = Join-Path $repoRoot 'work'
$runtimeSource = Join-Path $workRoot 'moderngekko'
$runtimeBuild = Join-Path $workRoot 'moderngekko-build'
$moduleOutput = Join-Path $workRoot 'recomp-modules'
$runtimeRevision = '5417826c31187d4dadf8588c7aa25bf107782936'
$chassisRevision = '55c7b023fa0f4eba1cf3fdbbb25b1c5ec468d5ac'
$expectedDolSha256 = 'EC61C010477C844781763A3041922E38719B3CED3B74BE5CE71D778CE458E471'
$runtimeUrl = 'https://github.com/ExpansionPak/ModernGekko.git'
$patchPath = Join-Path $repoRoot 'patches/moderngekko-local-runtime.patch'
$chassisPatchPath = Join-Path $repoRoot 'patches/recompcore-headless-shader-progress.patch'
$chassisSource = Join-Path $runtimeSource 'vendor/dolphin'

function Resolve-RequiredDirectory([string]$Value, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Value -PathType Container)) { throw "$Description directory is missing: $Value" }
    return (Resolve-Path -LiteralPath $Value).Path
}
function Invoke-Checked([string]$Executable, [string[]]$Arguments, [string]$Log) {
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw "Native executable is missing: $Executable" }
    $previousPreference = $ErrorActionPreference
    try {
        # Windows PowerShell 5.1 turns redirected native stderr into error
        # records even for exit zero. Keep those diagnostics and use the real
        # process exit code, while log-write errors remain terminating.
        $ErrorActionPreference = 'Continue'
        if ($Log) { & $Executable @Arguments 2>&1 | Tee-Object -FilePath $Log -ErrorAction Stop }
        else { & $Executable @Arguments }
        $nativeExitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if ($nativeExitCode -ne 0) { throw "Command failed ($nativeExitCode): $Executable $($Arguments -join ' ')" }
}
function Normalize-Patch([string]$Text) { return $Text.Replace("`r`n", "`n").TrimEnd() }
function Assert-SourcePatch([string]$Source, [string]$ExpectedPatch, [bool]$AllowClean) {
    $staged = (& $git -C $Source diff --cached --name-only) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $staged) { throw "The source has staged or unreadable changes: $Source" }
    $untracked = (& $git -C $Source ls-files --others --exclude-standard) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $untracked) { throw "The source has untracked or unreadable files: $Source" }
    # Nested repositories are checked separately; their known patches must not
    # hide an unrelated change in either parent or descendant source.
    $sourceDiff = (& $git -C $Source diff --binary --ignore-submodules=all) -join "`n"
    if ($LASTEXITCODE -ne 0) { throw "Could not inspect source changes: $Source" }
    if ((Normalize-Patch $sourceDiff) -ne (Normalize-Patch $ExpectedPatch) -and
        -not ($AllowClean -and -not $sourceDiff)) {
        throw "The source has changes beyond its supported patch; this build does not reset them: $Source"
    }
    return [bool]$sourceDiff
}
function Assert-DependencySources([string[]]$Pins, [bool]$AllowMissing) {
    foreach ($pin in $Pins) {
        if ($pin -notmatch '^([ +U-])[0-9a-f]{40} (.+?)(?: \(.+\))?$') { throw "Unexpected dependency status: $pin" }
        $state = $Matches[1]
        $relative = $Matches[2]
        if ($state -eq '-' -and $AllowMissing) { continue }
        if ($state -ne ' ') { throw "A dependency differs from its pinned revision; preserve/reconcile it manually: $relative" }
        $source = Join-Path $runtimeSource $relative
        $expected = if ($relative -eq 'vendor/dolphin') { $chassisPatch } else { '' }
        Assert-SourcePatch $source $expected $AllowMissing | Out-Null
    }
}
function Find-Python {
    $candidates = [System.Collections.Generic.List[string]]::new()
    foreach ($name in @('python.exe','python3.exe','python','python3')) {
        $found = Get-Command $name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($found -and $found.Source -notmatch '[\\/]WindowsApps[\\/]') { $candidates.Add($found.Source) }
    }
    $launcher = Get-Command py.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($launcher) {
        $located = & $launcher.Source -3 -c 'import sys; print(sys.executable)' 2>$null
        if ($LASTEXITCODE -eq 0 -and $located) { $candidates.Add([string]$located) }
    }
    foreach ($registry in @('HKCU:\SOFTWARE\Python\PythonCore','HKLM:\SOFTWARE\Python\PythonCore')) {
        if (Test-Path $registry) {
            foreach ($version in Get-ChildItem $registry) {
                $install = Join-Path $version.PSPath 'InstallPath'
                if (Test-Path $install) {
                    $directory = (Get-Item $install).GetValue('')
                    if ($directory) { $candidates.Add((Join-Path $directory 'python.exe')) }
                }
            }
        }
    }
    $runtimeCache = Join-Path $env:USERPROFILE '.cache/codex-runtimes'
    if (Test-Path -LiteralPath $runtimeCache) {
        foreach ($runtime in Get-ChildItem -LiteralPath $runtimeCache -Directory) {
            $candidate = Join-Path $runtime.FullName 'dependencies/python/python.exe'
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { $candidates.Add($candidate) }
        }
    }
    foreach ($candidate in $candidates | Select-Object -Unique) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        $located = & $candidate -c 'import sys; assert sys.version_info >= (3, 8); print(sys.executable)' 2>$null
        if ($LASTEXITCODE -eq 0 -and $located) { return [string]$located }
    }
    throw 'Python 3.8 or newer is required. Install it and place python.exe on PATH.'
}

$DolRecompSource = Resolve-RequiredDirectory $DolRecompSource 'DolRecomp source'
$TemplateSource = Resolve-RequiredDirectory $TemplateSource 'Template source'
$ExtractedRoot = Resolve-RequiredDirectory $ExtractedRoot 'Extracted game'
foreach ($file in @((Join-Path $DolRecompSource 'CMakeLists.txt'), (Join-Path $TemplateSource 'README.md'),
                    (Join-Path $TemplateSource 'Makefile'), (Join-Path $ExtractedRoot 'sys/main.dol'),
                    (Join-Path $ExtractedRoot 'sys/boot.bin'))) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required input is missing: $file" }
}
$boot = [System.IO.File]::ReadAllBytes((Join-Path $ExtractedRoot 'sys/boot.bin'))
if ($boot.Length -lt 6 -or [System.Text.Encoding]::ASCII.GetString($boot,0,6) -ne 'GDJEB2') {
    throw 'This wrapper requires the verified USA GameCube disc ID GDJEB2.'
}
$inputDolSha256 = (Get-FileHash -LiteralPath (Join-Path $ExtractedRoot 'sys/main.dol') -Algorithm SHA256).Hash
if ($inputDolSha256 -ne $expectedDolSha256) { throw 'The input DOL differs from the verified Digimon World 4 USA executable.' }
$git = (Get-Command git -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
New-Item -ItemType Directory -Path $workRoot -Force | Out-Null
if (-not (Test-Path -LiteralPath $runtimeSource)) {
    Invoke-Checked $git @('clone','--no-checkout',$runtimeUrl,$runtimeSource) ''
    Invoke-Checked $git @('-C',$runtimeSource,'checkout','--detach',$runtimeRevision) ''
} elseif (-not (Test-Path -LiteralPath (Join-Path $runtimeSource '.git'))) {
    throw "The runtime destination exists but is not a Git checkout: $runtimeSource"
}
$actualRevision = [string](& $git -C $runtimeSource rev-parse HEAD)
if ($LASTEXITCODE -ne 0 -or $actualRevision.Trim() -ne $runtimeRevision) {
    throw "Runtime revision differs from the pinned source. Preserve/reconcile the existing checkout manually: $runtimeSource"
}
$patch = Get-Content -LiteralPath $patchPath -Raw
$chassisPatch = Get-Content -LiteralPath $chassisPatchPath -Raw
if (-not (Assert-SourcePatch $runtimeSource $patch $true)) {
    Invoke-Checked $git @('-C',$runtimeSource,'apply','--check',$patchPath) ''
    Invoke-Checked $git @('-C',$runtimeSource,'apply',$patchPath) ''
}
$existingPins = @(& $git -C $runtimeSource submodule status --recursive)
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect existing dependency revisions.' }
Assert-DependencySources $existingPins $true
Invoke-Checked $git @('-C',$runtimeSource,'submodule','update','--init','--recursive') (Join-Path $workRoot 'recomp-submodules.log')
$dependencyPins = @(& $git -C $runtimeSource submodule status --recursive)
if ($LASTEXITCODE -ne 0) { throw 'Could not record recursive dependency revisions.' }
if (@($dependencyPins | Where-Object { $_ -notmatch '^ ' }).Count -ne 0) {
    throw 'A recursive dependency is missing, conflicted or differs from its pinned revision.'
}
$actualChassisRevision = [string](& $git -C $chassisSource rev-parse HEAD)
if ($LASTEXITCODE -ne 0 -or $actualChassisRevision.Trim() -ne $chassisRevision) {
    throw 'The execution chassis differs from the verified dependency revision.'
}
if (-not (Assert-SourcePatch $chassisSource $chassisPatch $true)) {
    Invoke-Checked $git @('-C',$chassisSource,'apply','--check',$chassisPatchPath) ''
    Invoke-Checked $git @('-C',$chassisSource,'apply',$chassisPatchPath) ''
}
Assert-SourcePatch $runtimeSource $patch $false | Out-Null
Assert-DependencySources $dependencyPins $false
$dependencyPins | Set-Content -LiteralPath (Join-Path $workRoot 'recomp-source-pins.txt')

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer/vswhere.exe is required to discover the C++ toolchain.' }
$vsRoot = [string](& $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
if ($LASTEXITCODE -ne 0 -or -not $vsRoot.Trim()) { throw 'No Visual Studio installation with the C++ workload was found.' }
$vsRoot = $vsRoot.Trim()
$vcvars = Join-Path $vsRoot 'VC/Auxiliary/Build/vcvars64.bat'
$compilerScript = Join-Path $workRoot 'recomp-compiler-env.cmd'
@('@echo off', ('call "' + $vcvars + '" >nul'), 'if errorlevel 1 exit /b %errorlevel%', 'set') |
    Set-Content -LiteralPath $compilerScript -Encoding ascii
$compilerEnvironment = & $env:ComSpec /d /c $compilerScript
if ($LASTEXITCODE -ne 0) { throw 'Visual Studio compiler environment failed.' }
foreach ($entry in $compilerEnvironment) {
    if ($entry -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
$cmakeDirectory = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$ninjaDirectory = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
if (-not (Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue)) { $env:PATH = "$cmakeDirectory;$env:PATH" }
if (-not (Get-Command ninja -CommandType Application -ErrorAction SilentlyContinue)) { $env:PATH = "$ninjaDirectory;$env:PATH" }
$cmake = (Get-Command cmake -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$ninja = (Get-Command ninja -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$python = Find-Python
$env:PATH = (Split-Path -Parent $python) + ';' + $env:PATH
$env:MODERNGEKKO_BUILD_JOBS = [string]$Jobs
$configure = @('-S',$runtimeSource,'-B',$runtimeBuild,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
    '-DBUILD_TESTING=OFF','-DDOLRECOMP_ENABLE_LLVM=OFF','-DMODERNGEKKO_GAMECUBE_CONTROLLERS=ON',
    "-DMODERNGEKKO_DOLRECOMP_SOURCE_DIR=$DolRecompSource","-DPython3_EXECUTABLE=$python",
    '-DCMAKE_C_COMPILER=cl','-DCMAKE_CXX_COMPILER=cl',"-DCMAKE_MAKE_PROGRAM=$ninja")
Invoke-Checked $cmake $configure (Join-Path $workRoot 'recomp-configure.log')
Invoke-Checked $cmake @('--build',$runtimeBuild,'--target','moderngekko-port','--parallel',[string]$Jobs) (Join-Path $workRoot 'recomp-runtime-build.log')
$port = Join-Path $runtimeBuild 'moderngekko-port.exe'
if (-not (Test-Path -LiteralPath $port -PathType Leaf)) { throw 'The runtime build did not produce moderngekko-port.exe.' }
$record = [ordered]@{
    runtime_revision=$runtimeRevision; dolrecomp_source=$DolRecompSource; template_source=$TemplateSource;
    chassis_revision=$chassisRevision;
    extracted_root=$ExtractedRoot; jobs=$Jobs; module_opt_level=$OptLevel; compiler_installation=$vsRoot;
    cmake=$cmake; ninja=$ninja; python=$python; generated_root=$workRoot;
    runtime_patch_sha256=(Get-FileHash -LiteralPath $patchPath -Algorithm SHA256).Hash;
    chassis_patch_sha256=(Get-FileHash -LiteralPath $chassisPatchPath -Algorithm SHA256).Hash;
    template_readme_sha256=(Get-FileHash -LiteralPath (Join-Path $TemplateSource 'README.md') -Algorithm SHA256).Hash;
    template_makefile_sha256=(Get-FileHash -LiteralPath (Join-Path $TemplateSource 'Makefile') -Algorithm SHA256).Hash;
    main_dol_sha256=$inputDolSha256;
    dolrecomp_binary_sha256=(Get-FileHash -LiteralPath (Join-Path $runtimeBuild 'dolrecomp.exe') -Algorithm SHA256).Hash;
    port_binary_sha256=(Get-FileHash -LiteralPath $port -Algorithm SHA256).Hash;
    runner_binary_sha256=(Get-FileHash -LiteralPath (Join-Path $runtimeBuild 'moderngekko-run.exe') -Algorithm SHA256).Hash;
    dependency_pins=@($dependencyPins); built_at_utc=[DateTime]::UtcNow.ToString('o'); module_built=$false
}
$recordPath = Join-Path $workRoot 'recomp-build-record.json'
$record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $recordPath
if (-not $RuntimeOnly) {
    Invoke-Checked $port @('inspect',$ExtractedRoot,'--output',$moduleOutput) (Join-Path $workRoot 'recomp-inspect.log')
    Invoke-Checked $port @('build',$ExtractedRoot,'--backend','c','--toolchain','msvc','--opt-level',[string]$OptLevel,'--output',$moduleOutput) (Join-Path $workRoot 'recomp-module-build.log')
    $activeFile = Join-Path $moduleOutput 'GDJEB2/active-module.txt'
    $activeModule = [System.IO.Path]::GetFullPath((Get-Content -LiteralPath $activeFile -Raw).Trim())
    $outputPrefix = [System.IO.Path]::GetFullPath($moduleOutput).TrimEnd([char[]]@('\','/')) + [System.IO.Path]::DirectorySeparatorChar
    if (-not $activeModule.StartsWith($outputPrefix,[System.StringComparison]::OrdinalIgnoreCase) -or
        [System.IO.Path]::GetFileName($activeModule) -ne 'gGDJEB2_recomp.dll' -or
        -not (Test-Path -LiteralPath $activeModule -PathType Leaf)) {
        throw 'The active module does not identify the expected DLL inside the owned build output.'
    }
    $manifestPath = Join-Path (Split-Path -Parent $activeModule) 'manifest.txt'
    $manifest = @{}
    foreach ($line in Get-Content -LiteralPath $manifestPath) {
        if ($line -match '^([^=]+)=(.*)$') { $manifest[$Matches[1]]=$Matches[2] }
    }
    if ($manifest['disc_id'] -ne 'GDJEB2' -or $manifest['dol_sha256'] -ne $inputDolSha256 -or
        $manifest['dolrecomp_binary_sha256'] -ne $record.dolrecomp_binary_sha256 -or $manifest['backend'] -ne 'c') {
        throw 'The published module manifest does not match the verified input and compiler binary.'
    }
    # Compare the real public/chassis layouts and the loaded descriptor before
    # publishing this module as ready. The check never dispatches guest code.
    & (Join-Path $PSScriptRoot 'test-recomp-abi.ps1') -RuntimeSource $runtimeSource -Module $activeModule
    $abiReportPath = Join-Path $workRoot 'recomp-abi/latest.json'
    $record['abi_report_path']=$abiReportPath
    $record['abi_report_sha256']=(Get-FileHash -LiteralPath $abiReportPath -Algorithm SHA256).Hash
    $record['module_path']=$activeModule
    $record['module_sha256']=(Get-FileHash -LiteralPath $activeModule -Algorithm SHA256).Hash
    $record['module_manifest_path']=$manifestPath
    $record['module_build_finished_at_utc']=[DateTime]::UtcNow.ToString('o')
    $record.module_built=$true
    $record | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $recordPath
}
Write-Host "Build record: $recordPath"
Write-Host 'Runtime/module build finished. No game or preview was launched.'
