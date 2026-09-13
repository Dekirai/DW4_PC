param(
    [Parameter(Mandatory=$true)][string]$Module,
    [Parameter(Mandatory=$true)][string]$ExtractedRoot,
    [string]$Runner = '',
    [switch]$UseBundledModule,
    [ValidateSet('Null','Vulkan')][string]$Graphics = 'Null',
    [ValidateSet('Enabled','Disabled')][string]$IdleMode = 'Enabled',
    [switch]$Screenshot,
    [ValidateRange(1,30)][int]$TimeoutSeconds = 30,
    [ValidateRange(2,10)][int]$ShutdownGraceSeconds = 5
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'recomp-startup-metrics.ps1')
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'This startup harness currently requires Windows.'
}
if ($Screenshot -and $Graphics -ne 'Vulkan') {
    throw 'A meaningful screenshot requires the headless Vulkan backend, not Null.'
}
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Runner) { $Runner = Join-Path $repoRoot 'work/moderngekko-build/moderngekko-run.exe' }
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
foreach ($field in @('runner_binary_sha256','runtime_patch_sha256','chassis_patch_sha256','module_built','module_path',
    'module_sha256','abi_report_path','abi_report_sha256')) {
    if (-not $buildRecord.PSObject.Properties[$field]) { throw "The runtime build record is missing $field; rebuild before testing." }
}
$runnerHash = (Get-FileHash -LiteralPath $Runner -Algorithm SHA256).Hash
$moduleHash = (Get-FileHash -LiteralPath $Module -Algorithm SHA256).Hash
$patchHash = (Get-FileHash -LiteralPath $runtimePatchPath -Algorithm SHA256).Hash
$chassisPatchHash = (Get-FileHash -LiteralPath $chassisPatchPath -Algorithm SHA256).Hash
$patchText = [IO.File]::ReadAllText($runtimePatchPath)
if ($buildRecord.runner_binary_sha256 -ne $runnerHash -or $buildRecord.runtime_patch_sha256 -ne $patchHash -or
    -not $patchText.Contains('Common::RegisterMsgAlertHandler') -or -not $patchText.Contains('[headless alert]')) {
    throw 'The runner/build record does not match the current headless-alert patch. Rebuild with scripts/build-recomp.ps1 before testing.'
}
if ($buildRecord.chassis_patch_sha256 -ne $chassisPatchHash) {
    throw 'The runner/build record does not match the headless shader-progress patch. Rebuild with scripts/build-recomp.ps1 before testing.'
}
if (-not [IO.File]::ReadAllText($chassisPatchPath).Contains('StaticRecompDw4Idle')) {
    throw 'This CPU comparison requires a rebuilt runner with the guarded DW4 idle option.'
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
$automationRoot = Join-Path $runRoot 'automation'
$commandRoot = Join-Path $automationRoot 'commands'
$tempRoot = Join-Path $runRoot 'temp'
foreach ($directory in @($runRoot,$profileRoot,$automationRoot,$commandRoot,$tempRoot,
    (Join-Path $profileRoot 'Config'),(Join-Path $profileRoot 'AppData/Roaming'),(Join-Path $profileRoot 'AppData/Local'))) {
    [IO.Directory]::CreateDirectory($directory) | Out-Null
}
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $profileRoot 'config.ini'),
    "[Video]`nresolution=640x528`nbackend=Vulkan`nfullscreen=false`nshow_fps_in_title=false`n",$utf8)
# The CLI explicitly selects the requested backend. Frontend config accepts only
# Vulkan/OpenGL labels. Keep alerts enabled for the patched headless log handler.
[IO.File]::WriteAllText((Join-Path $profileRoot 'Config/Dolphin.ini'),
    ("[Interface]`nUsePanicHandlers=True`n[Core]`nStaticRecompFallbackRanges=`nStaticRecompIdlePC=0`nStaticRecompDw4Idle=" +
        $(if ($IdleMode -eq 'Enabled') { 'True' } else { 'False' }) + "`n"),$utf8)

function Quote-NativeArgument([string]$Value) {
    # Windows CRT quoting, without a shell or PowerShell command interpolation.
    $escaped = [regex]::Replace($Value,'(\\*)"','$1$1\"')
    return '"' + [regex]::Replace($escaped,'(\\+)$','$1$1') + '"'
}
function Publish-Stop([int]$Sequence) {
    # All files in commands/ are parsed, irrespective of extension. Construct
    # the complete file outside that directory before the same-volume rename.
    $name = '000-stop-{0:D2}.txt' -f $Sequence
    $pending = Join-Path $automationRoot ($name + '.pending')
    [IO.File]::WriteAllText($pending,"command=stop`n",$utf8)
    [IO.File]::Move($pending,(Join-Path $commandRoot $name))
}
function Publish-Screenshot {
    $pending = Join-Path $automationRoot '050-screenshot.txt.pending'
    [IO.File]::WriteAllText($pending,"command=screenshot`npath=$screenshotPath`n",$utf8)
    [IO.File]::Move($pending,(Join-Path $commandRoot '050-screenshot.txt'))
}
function Read-Status {
    $path = Join-Path $automationRoot 'status.txt'
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
        $script:lastFrameProgressSeconds = $watch.Elapsed.TotalSeconds
        ++$script:frameProgressSamples
    }
    if ($presents -gt $script:maxPresents) { $script:lastPresentProgressSeconds = $watch.Elapsed.TotalSeconds }
    $script:maxFrames = [Math]::Max($script:maxFrames,$frames)
    $script:maxPresents = [Math]::Max($script:maxPresents,$presents)
    $script:sawRunning = $script:sawRunning -or ($values['booted'] -eq '1' -and $values['state'] -eq 'running')
    $processSample = if ($processStarted) { Get-RecompProcessSample -Process $taskProcess -Clock $watch } else { $null }
    $sample = [ordered]@{
        observed_at_utc=[DateTime]::UtcNow.ToString('o'); elapsed_seconds=$watch.Elapsed.TotalSeconds;
        status=$values; process=$processSample
    }
    [IO.File]::AppendAllText((Join-Path $runRoot 'status-history.jsonl'),
        ($sample | ConvertTo-Json -Depth 5 -Compress) + "`n",$utf8)
}
function Stop-OwnedProcessTree {
    if (-not $processStarted -or $taskProcess.HasExited) { return }
    # The attached Process handle remains live. Verify this same start time
    # before using Windows' tree terminator; never kill by image name.
    try { $current = [Diagnostics.Process]::GetProcessById($taskProcess.Id) }
    catch {
        if ($taskProcess.HasExited) { return }
        throw
    }
    try {
        if ($current.StartTime.ToUniversalTime().Ticks -ne $ownedStartTicks) {
            throw 'The watchdog refused a process identity different from its own child.'
        }
        $killInfo = [Diagnostics.ProcessStartInfo]::new()
        $killInfo.FileName = Join-Path $env:SystemRoot 'System32/taskkill.exe'
        $killInfo.Arguments = '/PID ' + [string]$taskProcess.Id + ' /T /F'
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
            [IO.File]::WriteAllText((Join-Path $runRoot 'watchdog.txt'),$out.Result + $err.Result,$utf8)
            if (-not $taskProcess.WaitForExit(2000)) { throw 'The started runtime survived its process-tree watchdog.' }
        } finally { $killer.Dispose() }
    } finally { $current.Dispose() }
}

$arguments = @('--game',$ExtractedRoot,'--user-dir',$profileRoot,
    '--automation-dir',$automationRoot,'--headless','--graphics',$Graphics,'--no-mods','--mute')
if (-not $UseBundledModule) { $arguments += @('--module',$Module) }
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $Runner
$start.Arguments = ($arguments | ForEach-Object { Quote-NativeArgument $_ }) -join ' '
$start.WorkingDirectory = Split-Path -Parent $Runner
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

$record = [ordered]@{
    schema_version=1; scope='Bounded hidden startup; an optional Vulkan screenshot verifies one output image, not audio or complete gameplay';
    started_at_utc=[DateTime]::UtcNow.ToString('o'); runner=$Runner; module=$Module; extracted_root=$ExtractedRoot;
    bundled_module_discovery=[bool]$UseBundledModule;
    runner_sha256=$runnerHash; runtime_patch_sha256=$patchHash; chassis_patch_sha256=$chassisPatchHash;
    runtime_build_record=$buildRecordPath;
    module_sha256=$moduleHash; abi_report_path=$abiReportPath; abi_report_sha256=$buildRecord.abi_report_sha256;
    main_dol_sha256=$dolHash; arguments=$arguments; working_directory=$start.WorkingDirectory;
    user_directory=$profileRoot; automation_directory=$automationRoot;
    timeout_seconds=$TimeoutSeconds; shutdown_grace_seconds=$ShutdownGraceSeconds; run_directory=$runRoot;
    graphics_backend=$Graphics; screenshot_requested=[bool]$Screenshot; dw4_idle_mode=$IdleMode;
    dw4_idle_config=[ordered]@{section='Core'; key='StaticRecompDw4Idle'; value=($IdleMode -eq 'Enabled')};
    process_sampling_scope='Retained owned runtime Process only; user+kernel CPU across all threads, plus working/private/peak working-set bytes. No machine-wide utilization or child-process totals.'
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
$observationProcessSample = $null
$observationFrameProgressSeconds = -1.0
$observationPresentProgressSeconds = -1.0
$observationFrameProgressSamples = 0
$stallWindowSeconds = [Math]::Min(5.0,[double]$TimeoutSeconds)
$stopRequested = $false
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
try {
    $stdoutFile = [IO.File]::Open((Join-Path $runRoot 'stdout.log'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $stderrFile = [IO.File]::Open((Join-Path $runRoot 'stderr.log'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $processStarted = $taskProcess.Start()
    if (-not $processStarted) { throw 'The runtime process did not start.' }
    $ownedStartTicks = $taskProcess.StartTime.ToUniversalTime().Ticks
    $record['process_id'] = $taskProcess.Id
    [IO.File]::WriteAllText((Join-Path $runRoot 'invocation.json'),($record | ConvertTo-Json -Depth 5),$utf8)
    $stdoutCopy = $taskProcess.StandardOutput.BaseStream.CopyToAsync($stdoutFile)
    $stderrCopy = $taskProcess.StandardError.BaseStream.CopyToAsync($stderrFile)
    while (-not $taskProcess.HasExited -and $watch.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        Read-Status
        if ($Screenshot -and -not $screenshotCommandQueued -and $script:sawRunning -and
            $script:lastStatus['state'] -eq 'running' -and $script:frameProgressSamples -ge 2 -and
            $watch.Elapsed.TotalSeconds -ge 0.9*$TimeoutSeconds -and
            $watch.Elapsed.TotalSeconds - $script:lastFrameProgressSeconds -le 2) {
            # The command only queues an asynchronous renderer capture. Its
            # processed file alone is not evidence that a PNG was written.
            Publish-Screenshot
            $screenshotCommandQueued = $true
            $screenshotQueuedSeconds = $watch.Elapsed.TotalSeconds
        }
        Start-Sleep -Milliseconds 100
    }
    Read-Status
    $observationProcessSample = Get-RecompProcessSample -Process $taskProcess -Clock $watch
    $observationEndSeconds = $watch.Elapsed.TotalSeconds
    $observationFrameProgressSeconds = $script:lastFrameProgressSeconds
    $observationPresentProgressSeconds = $script:lastPresentProgressSeconds
    $observationFrameProgressSamples = $script:frameProgressSamples
    if (-not $taskProcess.HasExited) {
        Publish-Stop 1
        $stopRequested = $true
        $grace = [Diagnostics.Stopwatch]::StartNew()
        $secondStop = $false
        while (-not $taskProcess.HasExited -and $grace.Elapsed.TotalSeconds -lt $ShutdownGraceSeconds) {
            Read-Status
            # RequestShutdown first tries a guest power-button hook if present;
            # a second request takes the platform's ordinary shutdown path.
            if (-not $secondStop -and $grace.Elapsed.TotalSeconds -ge 1) {
                Publish-Stop 2
                $secondStop = $true
            }
            Start-Sleep -Milliseconds 100
        }
        if (-not $taskProcess.HasExited) {
            $forcedTermination = $true
            Stop-OwnedProcessTree
        }
    }
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
$record['forced_termination'] = $forcedTermination
$record['harness_error'] = $harnessError
$record['runtime_error_lines'] = $runtimeErrors
$record['log_capture_complete'] = $logsComplete
$record['inputs_unchanged'] = $inputUnchanged
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
$record['shutdown_counters'] = $shutdowns
$record['shutdown_counters_available'] = $shutdowns.Count -gt 0
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
$timingSamples = [Collections.Generic.List[object]]::new()
$statusHistoryPath = Join-Path $runRoot 'status-history.jsonl'
if ([IO.File]::Exists($statusHistoryPath)) {
    foreach ($line in [IO.File]::ReadLines($statusHistoryPath)) {
        try { $timingSamples.Add(($line | ConvertFrom-Json)) }
        catch { $timingSamples.Add($null) }
    }
}
$record['frame_timing'] = Get-RecompStartupFrameMetrics -Samples ($timingSamples.ToArray()) -ObservationEndSeconds $observationEndSeconds
$record['finished_at_utc'] = [DateTime]::UtcNow.ToString('o')
$summaryPath = Join-Path $runRoot 'summary.json'
[IO.File]::WriteAllText($summaryPath,($record | ConvertTo-Json -Depth 8),$utf8)
Write-Host "Result: $classification; native dispatches=$nativeTotal; reported interpreter fallback steps=$fallbackTotal; frame count=$script:maxFrames"
Write-Host "Evidence: $summaryPath"
if ($classification -ne 'passed_startup') { throw "The bounded recompilation startup check did not pass ($classification). Evidence: $summaryPath" }
