param(
    [Parameter(Mandatory=$true)][string]$Module,
    [Parameter(Mandatory=$true)][string]$ExtractedRoot,
    [string]$Runner = '',
    [switch]$UnifiedExecutable,
    [switch]$UseBundledModule,
    [switch]$UsePortableProfile,
    [ValidateSet('Null','Vulkan')][string]$Graphics = 'Null',
    [ValidateSet('Enabled','Disabled')][string]$IdleMode = 'Enabled',
    [ValidateSet('Enabled','Disabled')][string]$CacheMode = 'Enabled',
    [ValidateSet('Null','Cubeb')][string]$Audio = 'Null',
    [ValidateSet('640x528','1280x720','1920x1080','2560x1440','3840x2160')][string]$Resolution = '640x528',
    [switch]$Screenshot,
    [int[]]$ScreenshotAtSeconds = @(),
    [ValidateRange(0,85)][int]$PressStartAtSeconds = 0,
    [ValidateRange(0,30)][int]$CpuProfileSeconds = 0,
    [ValidateRange(0,85)][int]$CpuProfileAtSeconds = 0,
    [ValidateRange(1,90)][int]$TimeoutSeconds = 30,
    [ValidateRange(2,10)][int]$ShutdownGraceSeconds = 5
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'recomp-startup-metrics.ps1')
. (Join-Path $PSScriptRoot 'recomp-audio-metrics.ps1')
if ($CpuProfileSeconds -gt 0) { . (Join-Path $PSScriptRoot 'profile-recomp.ps1') }
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'This startup harness currently requires Windows.'
}
if ($UsePortableProfile -and (-not $UnifiedExecutable -or -not $UseBundledModule -or -not $Runner)) {
    throw 'Portable-profile verification requires an explicit packaged unified EXE and its bundled module.'
}
if ($Screenshot -and $Graphics -ne 'Vulkan') {
    throw 'A meaningful screenshot requires the headless Vulkan backend, not Null.'
}
if ($ScreenshotAtSeconds.Count -gt 0 -and -not $Screenshot) { throw 'ScreenshotAtSeconds requires Screenshot.' }
if ($PressStartAtSeconds -gt 0 -and $PressStartAtSeconds -gt $TimeoutSeconds-5) {
    throw 'The controlled Start press must leave at least five seconds for release before shutdown.'
}
if ($CpuProfileSeconds -eq 0 -and $CpuProfileAtSeconds -ne 0) {
    throw 'CpuProfileAtSeconds requires a nonzero CpuProfileSeconds.'
}
if ($CpuProfileSeconds -gt 0 -and $CpuProfileAtSeconds + $CpuProfileSeconds -gt $TimeoutSeconds-5) {
    throw 'The CPU profile schedule must leave at least five seconds before the unchanged runtime deadline. Collector command latency also consumes this run budget.'
}
if ($ScreenshotAtSeconds.Count -gt 8 -or @($ScreenshotAtSeconds | Where-Object { $_ -lt 1 -or $_ -ge $TimeoutSeconds-1 }).Count) {
    throw 'Request at most eight screenshots, each at least one second before the test ends.'
}
$screenshotSchedule = @(if ($ScreenshotAtSeconds.Count) { $ScreenshotAtSeconds | Sort-Object -Unique } else { 0.9*$TimeoutSeconds })
$resolutionScales = @{ '640x528'=1; '1280x720'=2; '1920x1080'=3; '2560x1440'=4; '3840x2160'=6 }
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Runner) {
    $Runner = Join-Path $repoRoot $(if ($UnifiedExecutable) { 'work/moderngekko-build/DW4_PC.exe' } else { 'work/moderngekko-build/moderngekko-run.exe' })
}
foreach ($inputFile in @($Runner,$Module,(Join-Path $ExtractedRoot 'sys/main.dol'),(Join-Path $ExtractedRoot 'sys/boot.bin'))) {
    if (-not (Test-Path -LiteralPath $inputFile -PathType Leaf)) { throw "Required input is missing: $inputFile" }
}
$Runner = (Resolve-Path -LiteralPath $Runner).Path
$Module = (Resolve-Path -LiteralPath $Module).Path
$ExtractedRoot = (Resolve-Path -LiteralPath $ExtractedRoot).Path
$buildRecordPath = Join-Path $repoRoot 'work/recomp-build-record.json'
$runtimePatchPath = Join-Path $repoRoot 'patches/moderngekko-local-runtime.patch'
$chassisPatchPath = Join-Path $repoRoot 'patches/recompcore-headless-shader-progress.patch'
foreach ($required in @($buildRecordPath,$runtimePatchPath,$chassisPatchPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Rebuild the patched runtime before this hidden test; missing: $required" }
}
$buildRecord = Get-Content -LiteralPath $buildRecordPath -Raw | ConvertFrom-Json
$binaryHashField = if ($UnifiedExecutable) { 'launcher_binary_sha256' } else { 'runner_binary_sha256' }
foreach ($field in @('runner_binary_sha256','runtime_patch_sha256','chassis_patch_sha256','module_built','module_path',
    'module_sha256','abi_report_path','abi_report_sha256')) {
    if (-not $buildRecord.PSObject.Properties[$field]) { throw "The runtime build record is missing $field; rebuild before testing." }
}
if (-not $buildRecord.PSObject.Properties[$binaryHashField]) { throw "The runtime build record is missing $binaryHashField; rebuild before testing." }
$runnerHash = (Get-FileHash -LiteralPath $Runner -Algorithm SHA256).Hash
$moduleHash = (Get-FileHash -LiteralPath $Module -Algorithm SHA256).Hash
$patchHash = (Get-FileHash -LiteralPath $runtimePatchPath -Algorithm SHA256).Hash
$chassisPatchHash = (Get-FileHash -LiteralPath $chassisPatchPath -Algorithm SHA256).Hash
$patchText = [IO.File]::ReadAllText($runtimePatchPath)
if ($buildRecord.PSObject.Properties[$binaryHashField].Value -ne $runnerHash -or $buildRecord.runtime_patch_sha256 -ne $patchHash -or
    -not $patchText.Contains('Common::RegisterMsgAlertHandler') -or -not $patchText.Contains('[headless alert]')) {
    throw 'The runner/build record does not match the current headless-alert patch. Rebuild with scripts/build-recomp.ps1 before testing.'
}
if ($buildRecord.chassis_patch_sha256 -ne $chassisPatchHash) {
    throw 'The runner/build record does not match the headless shader-progress patch. Rebuild with scripts/build-recomp.ps1 before testing.'
}
if (-not [IO.File]::ReadAllText($chassisPatchPath).Contains('StaticRecompDw4Idle')) {
    throw 'This CPU comparison requires a rebuilt runner with the guarded DW4 idle option.'
}
if (-not [IO.File]::ReadAllText($chassisPatchPath).Contains('StaticRecompDw4CacheBatch')) {
    throw 'This startup comparison requires a rebuilt runner with the guarded DW4 cache-batch option.'
}
$expectedModulePath = if ($UseBundledModule) {
    Join-Path (Split-Path -Parent $Runner) 'gGDJEB2_recomp.dll'
} else { [IO.Path]::GetFullPath($buildRecord.module_path) }
if ($buildRecord.module_built -isnot [bool] -or -not $buildRecord.module_built -or
    -not $Module.Equals($expectedModulePath,[StringComparison]::OrdinalIgnoreCase) -or
    $buildRecord.module_sha256 -ne $moduleHash) {
    throw 'The requested module does not match the completed build record. Finish scripts/build-recomp.ps1 before testing.'
}
$abiReportPath = [IO.Path]::GetFullPath($buildRecord.abi_report_path)
if (-not (Test-Path -LiteralPath $abiReportPath -PathType Leaf) -or
    (Get-FileHash -LiteralPath $abiReportPath -Algorithm SHA256).Hash -ne $buildRecord.abi_report_sha256) {
    throw 'The ABI report is missing or differs from the completed build record. Rebuild before testing.'
}
$abiReport = Get-Content -LiteralPath $abiReportPath -Raw | ConvertFrom-Json
if (-not $abiReport.PSObject.Properties['passed'] -or $abiReport.passed -isnot [bool] -or -not $abiReport.passed) {
    throw 'The recorded CPU/module ABI comparison did not pass. No runtime was started.'
}
$dolPath = Join-Path $ExtractedRoot 'sys/main.dol'
$boot = [IO.File]::ReadAllBytes((Join-Path $ExtractedRoot 'sys/boot.bin'))
$dolHash = (Get-FileHash -LiteralPath $dolPath -Algorithm SHA256).Hash
if ($boot.Length -lt 6 -or [Text.Encoding]::ASCII.GetString($boot,0,6) -ne 'GDJEB2' -or
    $dolHash -ne 'EC61C010477C844781763A3041922E38719B3CED3B74BE5CE71D778CE458E471') {
    throw 'This startup harness requires the verified Digimon World 4 USA GameCube extraction.'
}

# Every run has its own configuration, save/cache directories and command queue.
# Nothing is removed from an earlier run or from the user's Dolphin profile.
$runName = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$runRoot = Join-Path $repoRoot "work/recomp-startup/$runName"
# Exercise the real runtime's Unicode user-directory boundary in both A/B modes.
$profileRoot = Join-Path $runRoot ('user-' + [char]0x00fc + [char]0x65e5 + [char]0x672c)
if ($UsePortableProfile) {
    $profileRoot = Join-Path (Split-Path -Parent $Runner) 'savedata'
    if (Test-Path -LiteralPath $profileRoot) {
        throw 'Portable-profile verification requires a fresh package without savedata; existing data was preserved.'
    }
}
$automationRoot = Join-Path $runRoot 'automation'
$commandRoot = Join-Path $automationRoot 'commands'
$tempRoot = Join-Path $runRoot 'temp'
foreach ($directory in @($runRoot,$profileRoot,$automationRoot,$commandRoot,$tempRoot,
    (Join-Path $profileRoot 'Config'),(Join-Path $profileRoot 'AppData/Roaming'),(Join-Path $profileRoot 'AppData/Local'))) {
    [IO.Directory]::CreateDirectory($directory) | Out-Null
}
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $profileRoot 'config.ini'),
    "[Video]`nresolution=$Resolution`nbackend=Vulkan`nfullscreen=false`nshow_fps_in_title=false`n",$utf8)
# The CLI explicitly selects the requested backend. Frontend config accepts only
# Vulkan/OpenGL labels. Keep alerts enabled for the patched headless log handler.
[IO.File]::WriteAllText((Join-Path $profileRoot 'Config/Dolphin.ini'),
    ("[Interface]`nUsePanicHandlers=True`n[Core]`nStaticRecompFallbackRanges=`nStaticRecompIdlePC=0`nStaticRecompDw4Idle=" +
        $(if ($IdleMode -eq 'Enabled') { 'True' } else { 'False' }) + "`nStaticRecompDw4CacheBatch=" +
        $(if ($CacheMode -eq 'Enabled') { 'True' } else { 'False' }) +
        "`nSIDevice0=6`nSIDevice1=0`nSIDevice2=0`nSIDevice3=0`n"),$utf8)
# A missing/empty pad file loads keyboard defaults (including RETURN=Start).
# Explicit sections instead load empty control expressions; keep emulated pad 0
# connected without reading the user's keyboard, gamepads or hardware adapter.
$padConfigPath = Join-Path $profileRoot 'Config/GCPadNew.ini'
[IO.File]::WriteAllText($padConfigPath,
    "[GCPad1]`nDevice =`nOptions/Always Connected = True`n[GCPad2]`nDevice =`n[GCPad3]`nDevice =`n[GCPad4]`nDevice =`n",$utf8)

function Quote-NativeArgument([string]$Value) {
    # Windows CRT quoting, without a shell or PowerShell command interpolation.
    $escaped = [regex]::Replace($Value,'(\\*)"','$1$1\"')
    return '"' + [regex]::Replace($escaped,'(\\+)$','$1$1') + '"'
}
function Publish-Stop([int]$Sequence) {
    # All files in commands/ are parsed, irrespective of extension. Construct
    # the complete file outside that directory before the same-volume rename.
    # A controlled-input probe must process any queued press/release before stop.
    $name = $(if ($script:PressStartAtSeconds -gt 0) { '099-stop-{0:D2}.txt' } else { '000-stop-{0:D2}.txt' }) -f $Sequence
    $pending = Join-Path $script:automationRoot ($name + '.pending')
    [IO.File]::WriteAllText($pending,"command=stop`n",$script:utf8)
    [IO.File]::Move($pending,(Join-Path $script:commandRoot $name))
}
function Publish-Screenshot([string]$Name,[string]$Path) {
    $pending = Join-Path $script:automationRoot ($Name + '.pending')
    [IO.File]::WriteAllText($pending,"command=screenshot`npath=$Path`n",$script:utf8)
    [IO.File]::Move($pending,(Join-Path $script:commandRoot $Name))
}
function Publish-StartInput([bool]$Pressed) {
    # Nonblocking pad commands keep status and shutdown responsive if frames stall.
    $name = if ($Pressed) { '020-start-press.txt' } else { '021-start-release.txt' }
    $body = if ($Pressed) { "command=pad`nport=0`nstart=1`n" } else { "command=clear_pad`nport=0`n" }
    $pending = Join-Path $script:automationRoot ($name + '.pending')
    [IO.File]::WriteAllText($pending,$body,$script:utf8)
    [IO.File]::Move($pending,(Join-Path $script:commandRoot $name))
}
function Read-Status {
    $path = Join-Path $script:automationRoot 'status.txt'
    if (-not [IO.File]::Exists($path)) { return }
    try { $raw = [IO.File]::ReadAllText($path) } catch { return }
    if ($raw -eq $script:lastStatusText) { return }
    $values = [ordered]@{}
    foreach ($line in $raw -split "`n") {
        $at = $line.IndexOf('=')
        if ($at -gt 0) { $values[$line.Substring(0,$at)] = $line.Substring($at+1).TrimEnd("`r") }
    }
    # Windows status publication can fall back from rename to copy. Ignore a
    # partial copy and read the next update instead of inventing missing values.
    foreach ($key in @('state','booted','frame_count','present_count','last_error')) {
        if (-not $values.Contains($key)) { return }
    }
    [UInt64]$frames = 0
    [UInt64]$presents = 0
    if (-not [UInt64]::TryParse($values['frame_count'],[ref]$frames) -or
        -not [UInt64]::TryParse($values['present_count'],[ref]$presents)) { return }
    $script:lastStatusText = $raw
    $script:lastStatus = $values
    if ($frames -gt $script:maxFrames) {
        $script:lastFrameProgressSeconds = $script:watch.Elapsed.TotalSeconds
        ++$script:frameProgressSamples
    }
    if ($presents -gt $script:maxPresents) { $script:lastPresentProgressSeconds = $script:watch.Elapsed.TotalSeconds }
    $script:maxFrames = [Math]::Max($script:maxFrames,$frames)
    $script:maxPresents = [Math]::Max($script:maxPresents,$presents)
    $script:sawRunning = $script:sawRunning -or ($values['booted'] -eq '1' -and $values['state'] -eq 'running')
    $processSample = if ($script:processStarted) { Get-RecompProcessSample -Process $script:taskProcess -Clock $script:watch } else { $null }
    $sample = [ordered]@{
        observed_at_utc=[DateTime]::UtcNow.ToString('o'); elapsed_seconds=$script:watch.Elapsed.TotalSeconds;
        status=$values; process=$processSample
    }
    [IO.File]::AppendAllText((Join-Path $script:runRoot 'status-history.jsonl'),
        ($sample | ConvertTo-Json -Depth 5 -Compress) + "`n",$script:utf8)
}
function Stop-OwnedProcessTree {
    if (-not $script:processStarted -or $script:taskProcess.HasExited) { return }
    # The attached Process handle remains live. Verify this same start time
    # before using Windows' tree terminator; never kill by image name.
    try { $current = [Diagnostics.Process]::GetProcessById($script:taskProcess.Id) }
    catch {
        if ($script:taskProcess.HasExited) { return }
        throw
    }
    try {
        if ($current.StartTime.ToUniversalTime().Ticks -ne $script:ownedStartTicks) {
            throw 'The watchdog refused a process identity different from its own child.'
        }
        $killInfo = [Diagnostics.ProcessStartInfo]::new()
        $killInfo.FileName = Join-Path $env:SystemRoot 'System32/taskkill.exe'
        $killInfo.Arguments = '/PID ' + [string]$script:taskProcess.Id + ' /T /F'
        $killInfo.UseShellExecute = $false
        $killInfo.CreateNoWindow = $true
        $killInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
        $killInfo.RedirectStandardOutput = $true
        $killInfo.RedirectStandardError = $true
        $killer = [Diagnostics.Process]::new()
        try {
            $killer.StartInfo = $killInfo
            if (-not $killer.Start()) { throw 'The child-process watchdog could not start.' }
            $out = $killer.StandardOutput.ReadToEndAsync()
            $err = $killer.StandardError.ReadToEndAsync()
            if (-not $killer.WaitForExit(3000)) { $killer.Kill(); throw 'The child-process watchdog timed out.' }
            [IO.File]::WriteAllText((Join-Path $script:runRoot 'watchdog.txt'),$out.Result + $err.Result,$script:utf8)
            if (-not $script:taskProcess.WaitForExit(2000)) { throw 'The started runtime survived its process-tree watchdog.' }
        } finally { $killer.Dispose() }
    } finally { $current.Dispose() }
}

function Complete-RecompStartupObservation {
    if ($script:observationCaptured) { return }
    $script:observationProcessSample = Get-RecompProcessSample -Process $script:taskProcess -Clock $script:watch
    $script:observationEndSeconds = $script:watch.Elapsed.TotalSeconds
    $script:observationFrameProgressSeconds = $script:lastFrameProgressSeconds
    $script:observationPresentProgressSeconds = $script:lastPresentProgressSeconds
    $script:observationFrameProgressSamples = $script:frameProgressSamples
    $script:observationCaptured = $true
}
function Invoke-RecompStartupHostPoll {
    # This function never invokes the profiler. The same scheduler services both
    # the host loop and collector command waits, using the original wall clock.
    # Explicit script scope prevents a collector's TimeoutSeconds parameter or
    # other dynamically scoped caller locals from replacing host configuration.
    Read-Status
    if ($script:taskProcess.HasExited) { Complete-RecompStartupObservation; return }
    if (-not $script:stopRequested -and $script:watch.Elapsed.TotalSeconds -ge $script:TimeoutSeconds) {
        Complete-RecompStartupObservation
        if ($null -ne $script:startPressQueuedSeconds -and $null -eq $script:startReleaseQueuedSeconds) {
            Publish-StartInput $false
            $script:startReleaseQueuedSeconds = $script:watch.Elapsed.TotalSeconds
        }
        Publish-Stop 1
        $script:stopRequested = $true
        $script:stopRequestedSeconds = $script:watch.Elapsed.TotalSeconds
    }
    if ($script:stopRequested) {
        if ($null -ne $script:startReleaseQueuedSeconds -and $null -eq $script:startReleaseAcknowledgedSeconds -and
            [IO.File]::Exists((Join-Path $script:automationRoot 'processed/021-start-release.txt'))) {
            $script:startReleaseAcknowledgedSeconds = $script:watch.Elapsed.TotalSeconds
        }
        # RequestShutdown first tries a guest power-button hook if present;
        # a second request takes the platform's ordinary shutdown path.
        if (-not $script:secondStopRequested -and $script:watch.Elapsed.TotalSeconds - $script:stopRequestedSeconds -ge 1) {
            Publish-Stop 2
            $script:secondStopRequested = $true
        }
        if ($script:watch.Elapsed.TotalSeconds - $script:stopRequestedSeconds -ge $script:ShutdownGraceSeconds) {
            $script:forcedTermination = $true
            Stop-OwnedProcessTree
        }
        return
    }
    if ($script:PressStartAtSeconds -gt 0 -and $null -eq $script:startPressQueuedSeconds -and
        $script:watch.Elapsed.TotalSeconds -ge $script:PressStartAtSeconds -and
        $script:watch.Elapsed.TotalSeconds -le $script:TimeoutSeconds-5 -and $script:sawRunning -and
        $script:lastStatus['state'] -eq 'running' -and $script:frameProgressSamples -ge 2 -and
        $script:watch.Elapsed.TotalSeconds - $script:lastFrameProgressSeconds -le 2) {
        Publish-StartInput $true
        $script:startPressQueuedSeconds = $script:watch.Elapsed.TotalSeconds
    }
    if ($null -ne $script:startPressQueuedSeconds) {
        if ($null -eq $script:startPressAcknowledgedSeconds -and
            [IO.File]::Exists((Join-Path $script:automationRoot 'processed/020-start-press.txt'))) {
            $script:startPressAcknowledgedSeconds = $script:watch.Elapsed.TotalSeconds
        }
        if ($null -eq $script:startReleaseQueuedSeconds -and
            (($null -ne $script:startPressAcknowledgedSeconds -and $script:watch.Elapsed.TotalSeconds - $script:startPressAcknowledgedSeconds -ge 0.5) -or
             $script:watch.Elapsed.TotalSeconds - $script:startPressQueuedSeconds -ge 2)) {
            Publish-StartInput $false
            $script:startReleaseQueuedSeconds = $script:watch.Elapsed.TotalSeconds
        }
        if ($null -ne $script:startReleaseQueuedSeconds -and $null -eq $script:startReleaseAcknowledgedSeconds -and
            [IO.File]::Exists((Join-Path $script:automationRoot 'processed/021-start-release.txt'))) {
            $script:startReleaseAcknowledgedSeconds = $script:watch.Elapsed.TotalSeconds
        }
    }
    if ($script:Screenshot -and $script:screenshotIndex -lt $script:screenshotSchedule.Count -and $script:sawRunning -and
        $script:lastStatus['state'] -eq 'running' -and $script:frameProgressSamples -ge 2 -and
        $script:watch.Elapsed.TotalSeconds -ge $script:screenshotSchedule[$script:screenshotIndex] -and
        $script:watch.Elapsed.TotalSeconds - $script:lastFrameProgressSeconds -le 2) {
        # The command only queues an asynchronous renderer capture. Its
        # processed file alone is not evidence that a PNG was written.
        $isLast = $script:screenshotIndex -eq $script:screenshotSchedule.Count-1
        $captureName = if ($isLast) { '050-screenshot.txt' } else { '040-screenshot-{0:D2}.txt' -f $script:screenshotIndex }
        $capturePath = if ($isLast) { $script:screenshotPath } else { Join-Path $script:runRoot ('screenshot-{0:D2}.png' -f $script:screenshotIndex) }
        Publish-Screenshot $captureName $capturePath
        $script:screenshotCaptures.Add([pscustomobject]@{
            requested_seconds=$script:screenshotSchedule[$script:screenshotIndex]; queued_seconds=$script:watch.Elapsed.TotalSeconds;
            command_name=$captureName; path=$capturePath
        })
        if ($isLast) {
            $script:screenshotCommandQueued = $true
            $script:screenshotQueuedSeconds = $script:watch.Elapsed.TotalSeconds
        }
        ++$script:screenshotIndex
    }
}
function Invoke-RecompStartupScheduledCpuProfile {
    if ($script:CpuProfileSeconds -le 0 -or $script:cpuProfileOutcome.classification -ne 'waiting' -or
        $script:stopRequested -or $script:taskProcess.HasExited -or $script:watch.Elapsed.TotalSeconds -lt $script:CpuProfileAtSeconds) { return }
    if ($script:watch.Elapsed.TotalSeconds + $script:CpuProfileSeconds -gt $script:TimeoutSeconds-5) {
        $script:cpuProfileOutcome.classification = 'schedule_missed'
        $script:cpuProfileOutcome.error = 'No fresh running sample left the requested observation plus five seconds before the runtime deadline.'
        return
    }
    if (-not $script:sawRunning -or $script:lastStatus['state'] -ne 'running' -or
        $script:frameProgressSamples -lt 2 -or $script:watch.Elapsed.TotalSeconds - $script:lastFrameProgressSeconds -gt 2) { return }
    # Mark the attempt before invoking a blocking helper. Its callback cannot
    # recursively enter profiling, and collector latency cannot extend runtime.
    $script:cpuProfileOutcome.classification = 'running'
    $script:cpuProfileOutcome.attempted = $true
    $script:cpuProfileOutcome.begin_seconds = $script:watch.Elapsed.TotalSeconds
    try {
        $script:cpuProfileOutcome.result = Invoke-RecompCpuProfile -OwnedProcess $script:taskProcess `
            -ExpectedStartTimeUtcTicks $script:ownedStartTicks -ExpectedExecutablePath $script:Runner `
            -ExpectedExecutableSha256 $script:buildRecord.PSObject.Properties[$script:binaryHashField].Value `
            -OutputDirectory (Join-Path $script:runRoot 'cpu-profile') -DurationSeconds $script:CpuProfileSeconds `
            -OnPoll { Invoke-RecompStartupHostPoll } -ModulePath $script:Module
        $script:cpuProfileOutcome.classification = $script:cpuProfileOutcome.result.classification
    } catch {
        $script:cpuProfileOutcome.classification = 'failed'
        $script:cpuProfileOutcome.error = $_.Exception.Message
        if (Test-Path -LiteralPath $script:cpuProfileOutcome.summary_path -PathType Leaf) {
            try { $script:cpuProfileOutcome.result = Get-Content -LiteralPath $script:cpuProfileOutcome.summary_path -Raw | ConvertFrom-Json }
            catch { $script:cpuProfileOutcome.error += ' Profile summary: ' + $_.Exception.Message }
        }
    } finally {
        $script:cpuProfileOutcome.end_seconds = $script:watch.Elapsed.TotalSeconds
        $script:cpuProfileOutcome.elapsed_seconds = $script:cpuProfileOutcome.end_seconds - $script:cpuProfileOutcome.begin_seconds
    }
}

$arguments = @('--game',$ExtractedRoot,
    '--automation-dir',$automationRoot,'--headless','--graphics',$Graphics,'--no-mods','--mute')
if (-not $UsePortableProfile) { $arguments += @('--user-dir',$profileRoot) }
if (-not $UseBundledModule) { $arguments += @('--module',$Module) }
if ($Audio -eq 'Cubeb') { $arguments += @('--headless-audio','--audio','Cubeb') }
if ($UnifiedExecutable) { $arguments = @('--run') + $arguments }
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $Runner
$start.Arguments = ($arguments | ForEach-Object { Quote-NativeArgument $_ }) -join ' '
$start.WorkingDirectory = Split-Path -Parent $Runner
if ($UsePortableProfile) { $start.WorkingDirectory = $runRoot }
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
# Start with the OS essentials, not the calling shell's emulator/compiler/mod
# settings. These values belong only to this child, never the current process.
$start.EnvironmentVariables.Clear()
foreach ($name in @('SystemRoot','SystemDrive','WINDIR','ComSpec','NUMBER_OF_PROCESSORS',
    'PROCESSOR_ARCHITECTURE','PROCESSOR_IDENTIFIER','PROCESSOR_LEVEL','PROCESSOR_REVISION')) {
    $value = [Environment]::GetEnvironmentVariable($name)
    if ($value) { $start.EnvironmentVariables[$name] = $value }
}
$start.EnvironmentVariables['PATH'] = (Split-Path -Parent $Runner) + ';' + (Join-Path $env:SystemRoot 'System32') + ';' + $env:SystemRoot
$start.EnvironmentVariables['USERPROFILE'] = $profileRoot
$start.EnvironmentVariables['APPDATA'] = Join-Path $profileRoot 'AppData/Roaming'
$start.EnvironmentVariables['LOCALAPPDATA'] = Join-Path $profileRoot 'AppData/Local'
$start.EnvironmentVariables['TEMP'] = $tempRoot
$start.EnvironmentVariables['TMP'] = $tempRoot
$start.EnvironmentVariables['MODERNGEKKO_STATICRECOMP'] = '1'
$start.EnvironmentVariables['STATICRECOMP_FALLBACK_RANGES'] = ''
$start.EnvironmentVariables['STATICRECOMP_LOCKSTEP'] = '0'
$start.EnvironmentVariables['STATICRECOMP_DISPATCH_SAMPLES'] = '1'
$start.EnvironmentVariables['STATICRECOMP_TRACE_FILE'] = Join-Path $runRoot 'dispatch.csv'

$cpuProfileOutcome = [ordered]@{
    enabled=($CpuProfileSeconds -gt 0); requested_at_seconds=$CpuProfileAtSeconds;
    requested_duration_seconds=$CpuProfileSeconds; attempted=$false;
    classification=$(if ($CpuProfileSeconds -gt 0) { 'waiting' } else { 'disabled' });
    executable_hash_field=$binaryHashField; expected_executable_sha256=$runnerHash;
    begin_seconds=$null; end_seconds=$null; elapsed_seconds=$null; error=$null; result=$null;
    summary_path=$(if ($CpuProfileSeconds -gt 0) { Join-Path $runRoot 'cpu-profile/summary.json' } else { $null });
    timing_scope='Requested time is relative to runtime launch; metadata and collector command latency precede sampling and count toward the unchanged runtime deadline. Exact sampling bounds and command latency remain in the helper summary.'
}
$record = [ordered]@{
    schema_version=2; scope='Bounded hidden startup with optional Vulkan images and silent real-device audio diagnostics; not audible quality or complete gameplay validation';
    started_at_utc=[DateTime]::UtcNow.ToString('o'); runner=$Runner; module=$Module; extracted_root=$ExtractedRoot;
    bundled_module_discovery=[bool]$UseBundledModule;
    portable_profile_default=[bool]$UsePortableProfile;
    unified_executable=[bool]$UnifiedExecutable; executable_hash_field=$binaryHashField;
    runner_sha256=$runnerHash; runtime_patch_sha256=$patchHash; chassis_patch_sha256=$chassisPatchHash;
    runtime_build_record=$buildRecordPath;
    module_sha256=$moduleHash; abi_report_path=$abiReportPath; abi_report_sha256=$buildRecord.abi_report_sha256;
    main_dol_sha256=$dolHash; arguments=$arguments; working_directory=$start.WorkingDirectory;
    user_directory=$profileRoot; automation_directory=$automationRoot;
    timeout_seconds=$TimeoutSeconds; shutdown_grace_seconds=$ShutdownGraceSeconds; run_directory=$runRoot;
    graphics_backend=$Graphics; screenshot_requested=[bool]$Screenshot; dw4_idle_mode=$IdleMode;
    dw4_cache_mode=$CacheMode;
    resolution_label=$Resolution; efb_scale=$resolutionScales[$Resolution];
    resolution_scope='Frontend label selects an integer EFB scale, not an exact offscreen PNG size or verified widescreen output.';
    audio_backend_requested=$Audio; audio_volume_percent=0;
    audio_scope='Cubeb explicitly opts into real device callbacks with volume zero; Null uses no output device. Source and callback counters do not prove audible quality.';
    input_scope='Emulated GameCube port 0 always connected with explicit empty bindings; other ports disconnected. Physical keyboard/gamepad/adapter input is excluded.';
    start_press_requested_seconds=$PressStartAtSeconds;
    pad_config_path=$padConfigPath; initial_pad_config_sha256=(Get-FileHash -LiteralPath $padConfigPath -Algorithm SHA256).Hash;
    screenshot_schedule_seconds=$(if ($Screenshot) { $screenshotSchedule } else { @() });
    dw4_idle_config=[ordered]@{section='Core'; key='StaticRecompDw4Idle'; value=($IdleMode -eq 'Enabled')};
    dw4_cache_config=[ordered]@{section='Core'; key='StaticRecompDw4CacheBatch'; value=($CacheMode -eq 'Enabled')};
    process_sampling_scope='Retained owned runtime Process only; user+kernel CPU across all threads, plus working/private/peak working-set bytes. No machine-wide utilization or child-process totals.'
    cpu_profile=$cpuProfileOutcome;
}
[IO.File]::WriteAllText((Join-Path $runRoot 'invocation.json'),($record | ConvertTo-Json -Depth 5),$utf8)
Write-Host "Hidden recompilation startup check: $runRoot"
$taskProcess = [Diagnostics.Process]::new()
$taskProcess.StartInfo = $start
$processStarted = $false
$ownedStartTicks = 0L
$watch = [Diagnostics.Stopwatch]::StartNew()
$script:lastStatusText = ''
$script:lastStatus = $null
$script:maxFrames = [UInt64]0
$script:maxPresents = [UInt64]0
$script:lastFrameProgressSeconds = -1.0
$script:lastPresentProgressSeconds = -1.0
$script:frameProgressSamples = 0
$script:sawRunning = $false
$observationEndSeconds = 0.0
$observationCaptured = $false
$observationProcessSample = $null
$observationFrameProgressSeconds = -1.0
$observationPresentProgressSeconds = -1.0
$observationFrameProgressSamples = 0
$stallWindowSeconds = [Math]::Min(5.0,[double]$TimeoutSeconds)
$stopRequested = $false
$stopRequestedSeconds = $null
$secondStopRequested = $false
$forcedTermination = $false
$harnessError = ''
$exitCode = $null
$stdoutFile = $null
$stderrFile = $null
$stdoutCopy = $null
$stderrCopy = $null
$logsComplete = $false
$screenshotPath = Join-Path $runRoot 'screenshot.png'
$screenshotCommandQueued = $false
$screenshotQueuedSeconds = $null
$screenshotCaptures = [Collections.Generic.List[object]]::new()
$screenshotIndex = 0
$startPressQueuedSeconds = $null
$startPressAcknowledgedSeconds = $null
$startReleaseQueuedSeconds = $null
$startReleaseAcknowledgedSeconds = $null
try {
    $stdoutFile = [IO.File]::Open((Join-Path $runRoot 'stdout.log'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $stderrFile = [IO.File]::Open((Join-Path $runRoot 'stderr.log'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $processStarted = $taskProcess.Start()
    if (-not $processStarted) { throw 'The runtime process did not start.' }
    $ownedStartTicks = $taskProcess.StartTime.ToUniversalTime().Ticks
    $record['process_id'] = $taskProcess.Id
    $record['process_start_time_utc_ticks'] = $ownedStartTicks
    [IO.File]::WriteAllText((Join-Path $runRoot 'invocation.json'),($record | ConvertTo-Json -Depth 5),$utf8)
    $stdoutCopy = $taskProcess.StandardOutput.BaseStream.CopyToAsync($stdoutFile)
    $stderrCopy = $taskProcess.StandardError.BaseStream.CopyToAsync($stderrFile)
    while (-not $taskProcess.HasExited) {
        Invoke-RecompStartupHostPoll
        Invoke-RecompStartupScheduledCpuProfile
        if (-not $taskProcess.HasExited) { Start-Sleep -Milliseconds 100 }
    }
    Read-Status
    Complete-RecompStartupObservation
} catch {
    $harnessError = $_.Exception.Message
} finally {
    if ($processStarted -and -not $taskProcess.HasExited) {
        $forcedTermination = $true
        try { Stop-OwnedProcessTree } catch { $harnessError += ' Watchdog: ' + $_.Exception.Message }
    }
    if ($processStarted -and $taskProcess.HasExited) { $exitCode = $taskProcess.ExitCode }
    try {
        if ($null -ne $stdoutCopy -and $null -ne $stderrCopy) {
            $logsComplete = $stdoutCopy.Wait(2000) -and $stderrCopy.Wait(2000)
        }
    } catch { $harnessError += ' Log capture: ' + $_.Exception.Message }
    if ($null -ne $stdoutFile) { $stdoutFile.Dispose() }
    if ($null -ne $stderrFile) { $stderrFile.Dispose() }
    Read-Status
    $watch.Stop()
    $taskProcess.Dispose()
}

$stderr = if ([IO.File]::Exists((Join-Path $runRoot 'stderr.log'))) { [IO.File]::ReadAllText((Join-Path $runRoot 'stderr.log')) } else { '' }
$runtimeErrors = @([regex]::Matches($stderr,'(?m)^(?:initialization failed|runtime failed|fatal error): [^\r\n]*') |
    ForEach-Object { $_.Value })
$headlessAlerts = @([regex]::Matches($stderr,'(?m)^\[headless alert\] ([^:\r\n]*): ([^\r\n]*)') |
    ForEach-Object { [ordered]@{caption=$_.Groups[1].Value; first_line=$_.Groups[2].Value} })
$alertsNeedReview = @($headlessAlerts | Where-Object { $_.caption -ne 'Information' }).Count -gt 0
$moduleMatches = [regex]::Matches($stderr,'(?m)^\[staticrecomp\] module loaded: (.+) entry=0x([0-9A-Fa-f]+)\r?$')
$loadedModules = @($moduleMatches | ForEach-Object { [ordered]@{path=$_.Groups[1].Value; entry_pc=('0x' + $_.Groups[2].Value)} })
$shutdowns = @()
foreach ($match in [regex]::Matches($stderr,'(?m)^\[staticrecomp\] shutdown: ([^\r\n]+)')) {
    $metrics = [ordered]@{}
    foreach ($metric in [regex]::Matches($match.Groups[1].Value,'([a-z_]+)=(\d+)')) {
        $metrics[$metric.Groups[1].Value] = [UInt64]::Parse($metric.Groups[2].Value)
    }
    $shutdowns += $metrics
}
[UInt64]$nativeTotal = 0
[UInt64]$fallbackTotal = 0
foreach ($metrics in $shutdowns) {
    if ($metrics.Contains('native')) { $nativeTotal += $metrics['native'] }
    if ($metrics.Contains('fallback')) { $fallbackTotal += $metrics['fallback'] }
}
$dispatchSites = @([regex]::Matches($stderr,'(?m)^\[staticrecomp\] dispatch-site pc=([0-9a-fA-F]+) samples=(\d+)') |
    ForEach-Object { [ordered]@{pc=('0x' + $_.Groups[1].Value); samples=[UInt64]::Parse($_.Groups[2].Value)} })
$traceRows = @()
$tracePath = Join-Path $runRoot 'dispatch.csv'
if ([IO.File]::Exists($tracePath)) { $traceRows = @(Import-Csv -LiteralPath $tracePath) }
$matchedModule = @($loadedModules | Where-Object { $_.path -eq $Module }).Count -gt 0
$inputUnchanged = (Get-FileHash -LiteralPath $dolPath -Algorithm SHA256).Hash -eq $dolHash -and
    (Get-FileHash -LiteralPath $Module -Algorithm SHA256).Hash -eq $record.module_sha256 -and
    (Get-FileHash -LiteralPath $Runner -Algorithm SHA256).Hash -eq $record.runner_sha256
$screenshotProcessed = [IO.File]::Exists((Join-Path $automationRoot 'processed/050-screenshot.txt'))
$screenshotValidHeader = $false
$screenshotHash = $null
$screenshotWidth = $null
$screenshotHeight = $null
if ($Screenshot -and [IO.File]::Exists($screenshotPath)) {
    $png = [IO.File]::OpenRead($screenshotPath)
    try {
        $header = [byte[]]::new(24)
        if ($png.Read($header,0,$header.Length) -eq $header.Length -and
            [BitConverter]::ToString($header,0,8) -eq '89-50-4E-47-0D-0A-1A-0A' -and
            [Text.Encoding]::ASCII.GetString($header,12,4) -eq 'IHDR') {
            $screenshotWidth = [uint32]($header[16]*16777216 + $header[17]*65536 + $header[18]*256 + $header[19])
            $screenshotHeight = [uint32]($header[20]*16777216 + $header[21]*65536 + $header[22]*256 + $header[23])
            $screenshotValidHeader = $screenshotWidth -gt 0 -and $screenshotHeight -gt 0
        }
    } finally { $png.Dispose() }
    $screenshotHash = (Get-FileHash -LiteralPath $screenshotPath -Algorithm SHA256).Hash
}
$classification = 'passed_startup'
if ($harnessError -or -not $logsComplete -or -not $inputUnchanged) { $classification = 'harness_error' }
elseif ($forcedTermination) { $classification = 'watchdog_timeout' }
elseif ($null -eq $exitCode -or $exitCode -ne 0) { $classification = 'runtime_error' }
elseif (-not $stopRequested) { $classification = 'unexpected_exit' }
elseif (-not $matchedModule -or $nativeTotal -eq 0 -or $shutdowns.Count -eq 0) { $classification = 'native_execution_unverified' }
elseif (-not $script:sawRunning -or $script:maxFrames -eq 0 -or $script:maxPresents -eq 0) { $classification = 'no_frames' }
elseif ($observationFrameProgressSamples -lt 2 -or
    $observationEndSeconds - $observationFrameProgressSeconds -gt $stallWindowSeconds -or
    $observationEndSeconds - $observationPresentProgressSeconds -gt $stallWindowSeconds) { $classification = 'stalled_after_frames' }
elseif ($null -ne $script:lastStatus -and $script:lastStatus['last_error']) { $classification = 'automation_error' }
elseif ($alertsNeedReview) { $classification = 'headless_alert_needs_review' }
elseif ($Screenshot -and (-not $screenshotCommandQueued -or -not $screenshotProcessed -or -not $screenshotValidHeader)) {
    $classification = 'screenshot_missing'
}
$record['classification'] = $classification
$record['process_exit_code'] = $exitCode
$record['elapsed_seconds'] = $watch.Elapsed.TotalSeconds
$record['stop_requested'] = $stopRequested
$record['stop_requested_seconds'] = $stopRequestedSeconds
$record['forced_termination'] = $forcedTermination
$record['harness_error'] = $harnessError
$record['runtime_error_lines'] = $runtimeErrors
$record['log_capture_complete'] = $logsComplete
$record['inputs_unchanged'] = $inputUnchanged
$record['final_pad_config_sha256'] = if (Test-Path -LiteralPath $padConfigPath -PathType Leaf) { (Get-FileHash -LiteralPath $padConfigPath -Algorithm SHA256).Hash } else { $null }
$record['start_probe'] = [ordered]@{
    press_queued_seconds=$startPressQueuedSeconds; press_acknowledged_seconds=$startPressAcknowledgedSeconds;
    release_queued_seconds=$startReleaseQueuedSeconds; release_acknowledged_seconds=$startReleaseAcknowledgedSeconds;
    scope='One nonblocking Start press on port 0, cleared after 0.5 seconds from observed acknowledgment (two-second fallback). Acknowledgment proves publication, not that the game consumed Start or reached a menu.'
}
if ($PressStartAtSeconds -gt 0 -and $classification -eq 'passed_startup' -and
    ($null -eq $startPressAcknowledgedSeconds -or $null -eq $startReleaseAcknowledgedSeconds -or
     $null -eq $startReleaseQueuedSeconds -or
     $startReleaseQueuedSeconds - $startPressAcknowledgedSeconds -lt 0.5 -or
     $startReleaseAcknowledgedSeconds -le $startReleaseQueuedSeconds)) {
    $classification = 'start_input_unverified'
    $record['classification'] = $classification
}
$record['runtime_graphics_config_written'] = Test-Path -LiteralPath (Join-Path $profileRoot 'Config/GFX.ini') -PathType Leaf
if ($UsePortableProfile -and -not $record['runtime_graphics_config_written']) {
    if ($classification -eq 'passed_startup') { $classification = 'portable_profile_unverified' }
    $record['classification'] = $classification
}
$record['saw_running_status'] = $script:sawRunning
$record['maximum_frame_count'] = $script:maxFrames
$record['maximum_present_count'] = $script:maxPresents
$record['observation_end_seconds'] = $observationEndSeconds
$record['process_at_observation_end'] = $observationProcessSample
$record['last_observation_frame_progress_seconds'] = $observationFrameProgressSeconds
$record['last_observation_present_progress_seconds'] = $observationPresentProgressSeconds
$record['observation_frame_progress_samples'] = $observationFrameProgressSamples
$record['maximum_allowed_frame_stall_seconds'] = $stallWindowSeconds
$record['last_status'] = $script:lastStatus
$record['loaded_modules'] = $loadedModules
$record['headless_alerts'] = $headlessAlerts
$record['headless_alerts_need_review'] = $alertsNeedReview
$record['initialized_vulkan_adapters'] = @([regex]::Matches($stderr,'(?m)^\[headless graphics\] Vulkan adapter=(\d+) name=([^\r\n]+)') |
    ForEach-Object { [pscustomobject]@{ index=[int]$_.Groups[1].Value; name=$_.Groups[2].Value } })
$record['shutdown_counters'] = $shutdowns
$record['shutdown_counters_available'] = $shutdowns.Count -gt 0
$record['cache_batch_counters'] = @([regex]::Matches($stderr,'(?m)^\[staticrecomp\] dw4_cache: ([^\r\n]+)') |
    ForEach-Object {
        $counters = [ordered]@{}
        foreach ($metric in [regex]::Matches($_.Groups[1].Value,'([a-z_]+)=(\d+)')) {
            $counters[$metric.Groups[1].Value] = [UInt64]::Parse($metric.Groups[2].Value)
        }
        [pscustomobject]$counters
    })
$record['reported_native_dispatches_total'] = $nativeTotal
$record['reported_interpreter_fallback_steps_total'] = $fallbackTotal
$record['fallback_counter_scope'] = 'The runtime counter excludes fallback-JIT execution; zero does not prove wholly native execution.'
$record['dispatch_sites'] = $dispatchSites
$record['dispatch_site_sampling_interval'] = 4096
$record['dispatch_trace_rows'] = $traceRows.Count
$record['dispatch_trace_sampling_interval'] = 1048576
$record['dispatch_trace_scope'] = 'The runtime reopens this sampled trace on core Run reentry, including a screenshot CPUThreadGuard; earlier rows may be absent. Shutdown counters retain the total.'
$record['dispatch_trace_first'] = if ($traceRows.Count) { $traceRows[0] } else { $null }
$record['dispatch_trace_last'] = if ($traceRows.Count) { $traceRows[-1] } else { $null }
$record['screenshot_command_queued'] = $screenshotCommandQueued
$record['screenshot_queued_seconds'] = $screenshotQueuedSeconds
$record['screenshot_command_processed'] = $screenshotProcessed
$record['screenshot_path'] = if ($Screenshot) { $screenshotPath } else { $null }
$record['screenshot_valid_png_header'] = $screenshotValidHeader
$record['screenshot_sha256'] = $screenshotHash
$record['screenshot_width'] = $screenshotWidth
$record['screenshot_height'] = $screenshotHeight
$record['screenshot_scope'] = 'The PNG must also be decoded and visually inspected; file/header checks alone do not validate graphics or gameplay.'
$record['screenshot_captures'] = @($screenshotCaptures.ToArray() | ForEach-Object {
    [pscustomobject]@{
        requested_seconds=$_.requested_seconds; queued_seconds=$_.queued_seconds; path=$_.path;
        processed=[IO.File]::Exists((Join-Path $automationRoot ('processed/' + $_.command_name)));
        file_exists=[IO.File]::Exists($_.path);
        sha256=$(if ([IO.File]::Exists($_.path)) { (Get-FileHash -LiteralPath $_.path -Algorithm SHA256).Hash } else { $null })
    }
})
$timingSamples = [Collections.Generic.List[object]]::new()
$statusHistoryPath = Join-Path $runRoot 'status-history.jsonl'
if ([IO.File]::Exists($statusHistoryPath)) {
    foreach ($line in [IO.File]::ReadLines($statusHistoryPath)) {
        try { $timingSamples.Add(($line | ConvertFrom-Json)) }
        catch { $timingSamples.Add($null) }
    }
}
$record['frame_timing'] = Get-RecompStartupFrameMetrics -Samples ($timingSamples.ToArray()) -ObservationEndSeconds $observationEndSeconds
$record['audio_timing'] = Get-RecompAudioMetrics -Samples ($timingSamples.ToArray()) -ObservationEndSeconds $observationEndSeconds
$audioDeviceExercised = $false
$audioWindow = $record['audio_timing'].windows[0]
if ($audioWindow.audio_available -and $audioWindow.device_output_observed) {
    # Use validated context, not unchecked properties from partially written JSON.
    $audioContext = $audioWindow.audio_context_last
    $audioDeviceExercised = $record['audio_timing'].latest_active_sample_age_seconds -le 2 -and
        $audioContext.audio_device_requested_volume_percent -eq 0 -and
        $audioContext.audio_device_volume_apply_complete -eq '1' -and
        $audioContext.audio_device_volume_apply_succeeded -eq '1' -and
        $audioWindow.device_error_totals.operation_errors -eq 0 -and
        $audioWindow.device_error_totals.state_errors -eq 0
}
$record['muted_real_device_exercised'] = $audioDeviceExercised
if ($Audio -eq 'Cubeb' -and -not $audioDeviceExercised -and $classification -eq 'passed_startup') {
    $classification = 'audio_device_unverified'
    $record['classification'] = $classification
}
$allCapturesPresent = $screenshotCaptures.Count -eq $screenshotSchedule.Count -and
    @($record['screenshot_captures'] | Where-Object { -not $_.processed -or -not $_.file_exists }).Count -eq 0
if ($Screenshot -and -not $allCapturesPresent -and $classification -eq 'passed_startup') {
    $classification = 'screenshot_missing'
    $record['classification'] = $classification
}
if ($cpuProfileOutcome.classification -eq 'waiting') { $cpuProfileOutcome.classification = 'not_reached' }
if ($CpuProfileSeconds -gt 0 -and $cpuProfileOutcome.classification -ne 'completed' -and $classification -eq 'passed_startup') {
    $classification = 'cpu_profile_unverified'
    $record['classification'] = $classification
}
$record['finished_at_utc'] = [DateTime]::UtcNow.ToString('o')
$summaryPath = Join-Path $runRoot 'summary.json'
[IO.File]::WriteAllText($summaryPath,($record | ConvertTo-Json -Depth 12),$utf8)
Write-Host "Result: $classification; native dispatches=$nativeTotal; reported interpreter fallback steps=$fallbackTotal; frame count=$script:maxFrames"
Write-Host "Evidence: $summaryPath"
if ($classification -ne 'passed_startup') { throw "The bounded recompilation startup check did not pass ($classification). Evidence: $summaryPath" }
