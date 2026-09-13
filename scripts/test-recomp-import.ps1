param([switch]$IncludeOriginalRom, [string]$Launcher = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
$record = Get-Content -LiteralPath (Join-Path $repoRoot 'work/recomp-build-record.json') -Raw | ConvertFrom-Json
if (-not $Launcher) { $Launcher = Join-Path $repoRoot 'work/moderngekko-build/DW4_PC.exe' }
$Launcher = [IO.Path]::GetFullPath($Launcher)
$patch = Join-Path $repoRoot 'patches/moderngekko-local-runtime.patch'
if (-not $record.PSObject.Properties['launcher_binary_sha256'] -or
    (Get-FileHash -LiteralPath $launcher).Hash -ne $record.launcher_binary_sha256 -or
    (Get-FileHash -LiteralPath $patch).Hash -ne $record.runtime_patch_sha256) {
    throw 'Build the current verified launcher before checking ROM import.'
}
$runRoot = Join-Path $repoRoot ('work/recomp-import/' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$compilerEnvironment = & $env:ComSpec /d /c (Join-Path $repoRoot 'work/recomp-compiler-env.cmd')
if ($LASTEXITCODE -ne 0) { throw 'The local compiler environment failed.' }
foreach ($entry in $compilerEnvironment) {
    if ($entry -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process') }
}
$policySource = Join-Path $repoRoot 'tools/recomp_import_check/main.cpp'
$policyExe = Join-Path $runRoot 'import-policy.exe'
$policyInclude = '/I' + (Join-Path $repoRoot 'work/moderngekko/tools')
$policyOutput = '/Fe' + $policyExe
$policyObject = '/Fo' + (Join-Path $runRoot 'import-policy.obj')
& cl.exe /nologo /std:c++20 /EHsc /W4 /WX /O2 $policyInclude $policySource $policyOutput $policyObject
if ($LASTEXITCODE -ne 0) { throw 'Import policy compilation failed.' }
& $policyExe
if ($LASTEXITCODE -ne 0) { throw 'Import policy checks failed.' }
$results = [Collections.Generic.List[object]]::new()
function Invoke-LauncherCheck([string]$Name, [string[]]$Arguments, [int]$ExpectedExit) {
    # Every argument is a complete path/value. These Windows command-line
    # arguments have no literal quotes or trailing backslash; no shell is used.
    foreach ($value in $Arguments) {
        if ($value.Contains('"') -or $value.EndsWith('\')) { throw 'Unsupported test argument.' }
    }
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $launcher
    $info.Arguments = ($Arguments | ForEach-Object { '"' + $_ + '"' }) -join ' '
    $info.WorkingDirectory = $runRoot
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.EnvironmentVariables['SDL_VIDEODRIVER'] = 'dummy'
    $info.EnvironmentVariables['SDL_VIDEO_DRIVER'] = 'dummy'
    $info.EnvironmentVariables['SDL_RENDER_DRIVER'] = 'software'
    $info.EnvironmentVariables['PATH'] = "$env:SystemRoot\System32;$env:SystemRoot"
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    if (-not $process.Start()) { throw 'Could not start the launcher check.' }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(60000)) {
        $process.Kill()
        $process.WaitForExit()
        throw "Launcher check exceeded its bound: $Name"
    }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    $exitCode = $process.ExitCode
    $process.Dispose()
    $stdout | Set-Content -LiteralPath (Join-Path $runRoot "$Name.stdout.log")
    $stderr | Set-Content -LiteralPath (Join-Path $runRoot "$Name.stderr.log")
    $results.Add([pscustomobject]@{name=$Name;exit_code=$exitCode;expected_exit=$ExpectedExit})
    if ($exitCode -ne $ExpectedExit) { throw "Unexpected exit from $Name ($exitCode): $stderr" }
}
function Write-Be32([byte[]]$Buffer,[int]$Offset,[uint32]$Value) {
    for ($i=0; $i -lt 4; ++$i) { $Buffer[$Offset+$i] = [byte](($Value -shr (24-8*$i)) -band 255) }
}
function New-DiscFixture([string]$Path,[string]$Name,[string]$Id='GDJEB2') {
    $buffer = [byte[]]::new(65536)
    [Text.Encoding]::ASCII.GetBytes($Id).CopyTo($buffer,0)
    Write-Be32 $buffer 0x1c ([Convert]::ToUInt32('C2339F3D',16))
    Write-Be32 $buffer 0x420 0x2800
    Write-Be32 $buffer 0x424 0x5000
    Write-Be32 $buffer 0x428 (25+$Name.Length)
    Write-Be32 $buffer 0x42c (25+$Name.Length)
    Write-Be32 $buffer 0x5000 0x01000000
    Write-Be32 $buffer 0x5008 2
    Write-Be32 $buffer 0x5010 0x6000
    Write-Be32 $buffer 0x5014 1
    [Text.Encoding]::ASCII.GetBytes($Name).CopyTo($buffer,0x5018)
    [IO.File]::WriteAllBytes($Path,$buffer)
}
# Exercise real UTF-8/UTF-16 boundaries without depending on this script's
# source encoding under Windows PowerShell 5.1.
$profile = Join-Path $runRoot ('profile-' + [char]0x00fc + [char]0x65e5 + [char]0x672c)
$badNames = @('../escape','..\escape','C:stream','CON.txt','trailing.','bad|name')
for ($i=0; $i -lt $badNames.Count; ++$i) {
    $iso = Join-Path $runRoot "bad-name-$i.iso"
    New-DiscFixture $iso $badNames[$i]
    Invoke-LauncherCheck "bad-name-$i" @('--extract',$iso,'--user-dir',$profile) 1
    $errorText = Get-Content -LiteralPath (Join-Path $runRoot "bad-name-$i.stderr.log") -Raw
    if ($errorText -notmatch 'unsafe or duplicate host filename') { throw 'The malformed filename did not reach its intended check.' }
}
$deepDisc = Join-Path $runRoot 'deep-fst.iso'
New-DiscFixture $deepDisc 'd'
$deepBuffer = [IO.File]::ReadAllBytes($deepDisc)
[Array]::Clear($deepBuffer,0x5000,4096)
$entryCount = 81
$treeSize = 12*$entryCount+2
Write-Be32 $deepBuffer 0x428 $treeSize
Write-Be32 $deepBuffer 0x42c $treeSize
for ($i=0; $i -lt $entryCount; ++$i) {
    Write-Be32 $deepBuffer (0x5000+12*$i) 0x01000000
    Write-Be32 $deepBuffer (0x5004+12*$i) ([Math]::Max(0,$i-1))
    Write-Be32 $deepBuffer (0x5008+12*$i) $entryCount
}
$deepBuffer[0x5000+12*$entryCount] = [byte][char]'d'
[IO.File]::WriteAllBytes($deepDisc,$deepBuffer)
Invoke-LauncherCheck 'deep-fst' @('--extract',$deepDisc,'--user-dir',$profile) 1
foreach ($countCase in @(@('wrapped-count',[uint32]1073741826),@('zero-count',[uint32]0))) {
    $countDisc = Join-Path $runRoot ($countCase[0]+'.iso')
    New-DiscFixture $countDisc 'safe.bin'
    $countBuffer = [IO.File]::ReadAllBytes($countDisc)
    Write-Be32 $countBuffer 0x5008 $countCase[1]
    [IO.File]::WriteAllBytes($countDisc,$countBuffer)
    Invoke-LauncherCheck $countCase[0] @('--extract',$countDisc,'--user-dir',$profile) 1
}
foreach ($fstCase in @('deep-fst','wrapped-count','zero-count')) {
    $errorText = Get-Content -LiteralPath (Join-Path $runRoot "$fstCase.stderr.log") -Raw
    if ($errorText -notmatch 'could not read the game partition filesystem') {
        throw "The malformed FST did not reach its intended parser check: $fstCase"
    }
}
$wrongDisc = Join-Path $runRoot 'wrong-disc.iso'
New-DiscFixture $wrongDisc 'safe.bin' 'FAKE01'
Invoke-LauncherCheck 'wrong-disc' @('--extract',$wrongDisc,'--user-dir',$profile) 1
$badData = Join-Path $runRoot 'not-a-rom.iso'
[IO.File]::WriteAllBytes($badData,[byte[]]@(0,1,2,3))
Invoke-LauncherCheck 'truncated-disc' @('--extract',$badData,'--user-dir',$profile) 1
Invoke-LauncherCheck 'missing-game' @('--validate-game',(Join-Path $runRoot 'missing'),'--user-dir',$profile) 1
$png = Join-Path $runRoot 'launcher.png'
Invoke-LauncherCheck 'hidden-frontend' @('--smoke-test',$png,'--user-dir',$profile) 0
if (-not (Test-Path -LiteralPath $png -PathType Leaf)) { throw 'The hidden frontend did not produce its image.' }
if ($IncludeOriginalRom) {
    $iso = 'E:/Games/Emulator/Dolphin-x64/Games/Digimon World 4 (USA).iso'
    Invoke-LauncherCheck 'original-import' @('--extract',$iso,'--user-dir',$profile) 0
    $game = Join-Path $profile 'games/GDJEB2'
    Invoke-LauncherCheck 'original-validate' @('--validate-game',$game,'--user-dir',$profile) 0
    $sourceRoot = [IO.Path]::GetFullPath($record.extracted_root)
    $sourceFiles = @(Get-ChildItem -LiteralPath $sourceRoot -File -Recurse | Sort-Object FullName)
    $importedFiles = @(Get-ChildItem -LiteralPath $game -File -Recurse)
    if ($sourceFiles.Count -ne $importedFiles.Count) { throw 'The imported file count differs from the original extracted reference.' }
    $compared = 0
    foreach ($file in $sourceFiles) {
        $relative = $file.FullName.Substring($sourceRoot.TrimEnd('\').Length+1)
        $copy = Join-Path $game $relative
        if (-not (Test-Path -LiteralPath $copy -PathType Leaf) -or
            (Get-FileHash -LiteralPath $copy).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) {
            throw "Imported file differs from the original extracted reference: $relative"
        }
        ++$compared
    }
    $results.Add([pscustomobject]@{name='original-file-equivalence';files=$compared})
    Invoke-LauncherCheck 'original-reuse' @('--extract',$iso,'--user-dir',$profile) 0
    $importedPng = Join-Path $runRoot 'launcher-imported.png'
    Invoke-LauncherCheck 'hidden-imported-frontend' @('--smoke-test',$importedPng,'--user-dir',$profile) 0
    if (-not (Test-Path -LiteralPath $importedPng -PathType Leaf)) { throw 'The imported-game frontend did not produce its image.' }
    $preserveProfile = Join-Path $runRoot 'preserve-profile'
    $existing = Join-Path $preserveProfile 'games/GDJEB2'
    New-Item -ItemType Directory -Path $existing -Force | Out-Null
    $marker = Join-Path $existing 'keep.txt'
    'Existing user data' | Set-Content -LiteralPath $marker
    $markerHash = (Get-FileHash -LiteralPath $marker).Hash
    Invoke-LauncherCheck 'preserve-invalid-folder' @('--extract',$iso,'--user-dir',$preserveProfile) 1
    if ((Get-FileHash -LiteralPath $marker).Hash -ne $markerHash) { throw 'The importer changed an existing invalid folder.' }
}
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runRoot 'results.json')
Write-Host "ROM-import checks passed: $runRoot"
Write-Host 'No game was launched. Decode and inspect launcher.png for frontend visual acceptance.'
