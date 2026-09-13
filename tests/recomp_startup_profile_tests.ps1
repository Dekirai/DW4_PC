# Exercise extracted scheduler functions with synthetic state only. The startup
# script, runtime, profiler, ETW recorder and services are never invoked.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$startupProfileSource = Join-Path $PSScriptRoot '../scripts/test-recomp-startup.ps1'
$startupProfileTokens = $null
$startupProfileErrors = $null
$startupProfileAst = [Management.Automation.Language.Parser]::ParseFile(
    $startupProfileSource,[ref]$startupProfileTokens,[ref]$startupProfileErrors)
$script:startupProfileChecks = 0
function Check-StartupProfile([bool]$Condition,[string]$Message) {
    ++$script:startupProfileChecks
    if (-not $Condition) { throw $Message }
}
Check-StartupProfile ($startupProfileErrors.Count -eq 0) 'Startup harness has parser errors.'
foreach ($startupProfileFunction in @('Complete-RecompStartupObservation','Invoke-RecompStartupHostPoll','Invoke-RecompStartupScheduledCpuProfile')) {
    $startupProfileNode = $startupProfileAst.Find({
        param($Node)
        $Node -is [Management.Automation.Language.FunctionDefinitionAst] -and $Node.Name -eq $startupProfileFunction
    },$false)
    Check-StartupProfile ($null -ne $startupProfileNode) ('Missing scheduler function: ' + $startupProfileFunction)
    . ([scriptblock]::Create($startupProfileNode.Extent.Text))
}
foreach ($startupProfileParameter in @('CpuProfileSeconds','CpuProfileAtSeconds')) {
    $startupProfileParam = @($startupProfileAst.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq $startupProfileParameter })[0]
    Check-StartupProfile ($startupProfileParam.DefaultValue.Value -eq 0) 'Profiling must default to disabled/zero.'
}
$startupProfileValidation = @($startupProfileAst.EndBlock.Statements | Where-Object {
    $_ -is [Management.Automation.Language.IfStatementAst] -and
    $_.Extent.Text.StartsWith('if ($CpuProfileSeconds') -and $_.Extent.Text.Contains('throw ')
})
Check-StartupProfile ($startupProfileValidation.Count -eq 2) 'CPU schedule guards are missing.'
$startupProfileValidate = [scriptblock]::Create('param($CpuProfileSeconds,$CpuProfileAtSeconds,$TimeoutSeconds)' + "`n" +
    (($startupProfileValidation | ForEach-Object { $_.Extent.Text }) -join "`n"))
& $startupProfileValidate 0 0 30
& $startupProfileValidate 15 60 90
$startupProfileRejected = $false
try { & $startupProfileValidate 0 10 90 } catch { $startupProfileRejected = $true }
Check-StartupProfile $startupProfileRejected 'An enabled schedule without a duration was accepted.'
$startupProfileRejected = $false
try { & $startupProfileValidate 15 75 90 } catch { $startupProfileRejected = $true }
Check-StartupProfile $startupProfileRejected 'A profile that consumes the shutdown margin was accepted.'
$startupProfileHashAssignment = $startupProfileAst.EndBlock.Statements | Where-Object {
    $_ -is [Management.Automation.Language.AssignmentStatementAst] -and $_.Left.Extent.Text -eq '$binaryHashField'
}
Check-StartupProfile ($null -ne $startupProfileHashAssignment) 'Executable build-record selection is missing.'
$startupProfileHashSelector = [scriptblock]::Create($startupProfileHashAssignment.Extent.Text)
$startupProfileRoot = Join-Path (Split-Path -Parent $PSScriptRoot) ('work/recomp-startup-profile-tests/' + [Guid]::NewGuid().ToString('N'))
$script:startupProfileCase = 0
function Reset-StartupProfileState {
    ++$script:startupProfileCase
    $script:runRoot = Join-Path $startupProfileRoot ([string]$script:startupProfileCase)
    $script:automationRoot = Join-Path $runRoot 'automation'
    [IO.Directory]::CreateDirectory((Join-Path $automationRoot 'processed')) | Out-Null
    $script:CpuProfileSeconds = 15
    $script:CpuProfileAtSeconds = 10
    $script:TimeoutSeconds = 60
    $script:ShutdownGraceSeconds = 5
    $script:PressStartAtSeconds = 12
    $script:Screenshot = $true
    $script:screenshotSchedule = @(15,40)
    $script:screenshotPath = Join-Path $runRoot 'screenshot.png'
    $script:screenshotIndex = 0
    $script:screenshotCommandQueued = $false
    $script:screenshotQueuedSeconds = $null
    $script:screenshotCaptures = [Collections.Generic.List[object]]::new()
    $script:watch = [pscustomobject]@{Elapsed=[pscustomobject]@{TotalSeconds=0.0}}
    $script:taskProcess = [pscustomobject]@{HasExited=$false}
    $script:ownedStartTicks = 123456789L
    $script:Runner = Join-Path $runRoot 'DW4_PC.exe'
    $script:Module = Join-Path $runRoot 'gGDJEB2_recomp.dll'
    $script:buildRecord = [pscustomobject]@{launcher_binary_sha256=('A'*64);runner_binary_sha256=('B'*64)}
    $script:binaryHashField = 'launcher_binary_sha256'
    $script:cpuProfileOutcome = [ordered]@{
        classification='waiting';attempted=$false;begin_seconds=$null;end_seconds=$null;elapsed_seconds=$null
        error=$null;result=$null;summary_path=(Join-Path $runRoot 'cpu-profile/summary.json')
    }
    $script:stopRequested = $false
    $script:stopRequestedSeconds = $null
    $script:secondStopRequested = $false
    $script:forcedTermination = $false
    $script:observationCaptured = $false
    $script:observationEndSeconds = 0.0
    $script:observationProcessSample = $null
    $script:observationFrameProgressSeconds = -1.0
    $script:observationPresentProgressSeconds = -1.0
    $script:observationFrameProgressSamples = 0
    $script:sawRunning = $true
    $script:lastStatus = @{state='running'}
    $script:frameProgressSamples = 4
    $script:lastFrameProgressSeconds = 0.0
    $script:lastPresentProgressSeconds = 0.0
    $script:startPressQueuedSeconds = $null
    $script:startPressAcknowledgedSeconds = $null
    $script:startReleaseQueuedSeconds = $null
    $script:startReleaseAcknowledgedSeconds = $null
    $script:startupProfileCalls = 0
    $script:startupProfileCall = $null
    $script:startupProfileReadCount = 0
    $script:startupProfileKillCount = 0
    $script:startupProfileActions = [Collections.Generic.List[object]]::new()
    $script:startupProfileCallbackTimes = @(10.1,12.0,12.2,12.69,12.71,12.8,15.0,25.0)
    $script:startupProfileFail = $false
}
function Read-Status {
    ++$script:startupProfileReadCount
    $script:lastFrameProgressSeconds = $watch.Elapsed.TotalSeconds
    $script:lastPresentProgressSeconds = $watch.Elapsed.TotalSeconds
    if ($null -ne $script:startPressQueuedSeconds -and $watch.Elapsed.TotalSeconds -ge $script:startPressQueuedSeconds+0.1) {
        [IO.File]::WriteAllText((Join-Path $automationRoot 'processed/020-start-press.txt'),'synthetic acknowledgment')
    }
    if ($null -ne $script:startReleaseQueuedSeconds -and $watch.Elapsed.TotalSeconds -ge $script:startReleaseQueuedSeconds+0.05) {
        [IO.File]::WriteAllText((Join-Path $automationRoot 'processed/021-start-release.txt'),'synthetic acknowledgment')
    }
}
function Get-RecompProcessSample { param($Process,$Clock) return [pscustomobject]@{elapsed_seconds=$Clock.Elapsed.TotalSeconds} }
function Publish-StartInput([bool]$Pressed) { $script:startupProfileActions.Add([pscustomobject]@{action='input';value=$Pressed;time=$watch.Elapsed.TotalSeconds}) }
function Publish-Screenshot([string]$Name,[string]$Path) { $script:startupProfileActions.Add([pscustomobject]@{action='screenshot';value=$Name;time=$watch.Elapsed.TotalSeconds}) }
function Publish-Stop([int]$Sequence) { $script:startupProfileActions.Add([pscustomobject]@{action='stop';value=$Sequence;time=$watch.Elapsed.TotalSeconds}) }
function Stop-OwnedProcessTree { ++$script:startupProfileKillCount; $script:taskProcess.HasExited=$true }
function Invoke-RecompCpuProfile {
    param($OwnedProcess,$ExpectedStartTimeUtcTicks,$ExpectedExecutablePath,$ExpectedExecutableSha256,
        $OutputDirectory,$DurationSeconds,[scriptblock]$OnPoll,$ModulePath)
    ++$script:startupProfileCalls
    $script:startupProfileCall = [pscustomobject]@{
        same_process=[object]::ReferenceEquals($OwnedProcess,$taskProcess);ticks=$ExpectedStartTimeUtcTicks
        exe=$ExpectedExecutablePath;hash=$ExpectedExecutableSha256;output=$OutputDirectory
        duration=$DurationSeconds;module=$ModulePath
    }
    foreach ($startupProfileTime in $script:startupProfileCallbackTimes) {
        $script:watch.Elapsed.TotalSeconds = $startupProfileTime
        & $OnPoll
    }
    if ($script:startupProfileFail) {
        [IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
        [IO.File]::WriteAllText((Join-Path $OutputDirectory 'summary.json'),'{"classification":"failed","own_session_stop_confirmed":true}')
        throw 'Synthetic collector failure after its own cleanup.'
    }
    [pscustomobject]@{classification='completed';own_session_stop_confirmed=$true}
}

Reset-StartupProfileState
$CpuProfileSeconds = 0
$watch.Elapsed.TotalSeconds = 20
Invoke-RecompStartupScheduledCpuProfile
Check-StartupProfile ($startupProfileCalls -eq 0) 'Disabled profiling invoked the helper.'

Reset-StartupProfileState
$watch.Elapsed.TotalSeconds = 9
Invoke-RecompStartupHostPoll
Invoke-RecompStartupScheduledCpuProfile
Check-StartupProfile ($startupProfileCalls -eq 0) 'Profiling started before its requested time.'
$watch.Elapsed.TotalSeconds = 10
Invoke-RecompStartupHostPoll
$UnifiedExecutable = $true
. $startupProfileHashSelector
Invoke-RecompStartupScheduledCpuProfile
Check-StartupProfile ($startupProfileCalls -eq 1 -and $cpuProfileOutcome.classification -eq 'completed') 'One scheduled profile did not complete.'
Check-StartupProfile ($startupProfileCall.same_process -and $startupProfileCall.ticks -eq $ownedStartTicks) 'Profiling lost the retained process/start time.'
Check-StartupProfile ($startupProfileCall.exe -eq $Runner -and $startupProfileCall.hash -eq ('A'*64)) 'Unified executable did not use its validated launcher hash.'
Check-StartupProfile ($startupProfileCall.output -eq (Join-Path $runRoot 'cpu-profile') -and $startupProfileCall.module -eq $Module) 'Profile evidence or module metadata used another path.'
Check-StartupProfile ($startupProfileCall.duration -eq 15) 'Profile duration was not forwarded.'
Check-StartupProfile ($cpuProfileOutcome.begin_seconds -eq 10 -and $cpuProfileOutcome.end_seconds -eq 25 -and $cpuProfileOutcome.elapsed_seconds -eq 15) 'Host profile timing was not retained.'
Check-StartupProfile ($startupProfileReadCount -ge 10) 'Status was not serviced inside helper callbacks.'
Check-StartupProfile ($startPressQueuedSeconds -eq 12 -and $startPressAcknowledgedSeconds -eq 12.2) 'Start publication/acknowledgment was delayed until after profiling.'
Check-StartupProfile ([Math]::Abs(($startReleaseQueuedSeconds-$startPressAcknowledgedSeconds)-0.51) -lt 0.00001) 'Start did not release at the acknowledgment-based half-second boundary.'
Check-StartupProfile ($startReleaseAcknowledgedSeconds -eq 12.8) 'Release acknowledgment was not polled during profiling.'
Check-StartupProfile ($screenshotCaptures.Count -eq 1 -and $screenshotCaptures[0].queued_seconds -eq 15) 'The scheduled screenshot did not run during profiling.'
Invoke-RecompStartupScheduledCpuProfile
Check-StartupProfile ($startupProfileCalls -eq 1) 'The completed profile was retried.'

Reset-StartupProfileState
$UnifiedExecutable = $false
. $startupProfileHashSelector
$watch.Elapsed.TotalSeconds = 10
Invoke-RecompStartupHostPoll
Invoke-RecompStartupScheduledCpuProfile
Check-StartupProfile ($startupProfileCall.hash -eq ('B'*64)) 'Standalone runner did not use its validated runner hash.'

Reset-StartupProfileState
$watch.Elapsed.TotalSeconds = 10
$sawRunning = $false
Invoke-RecompStartupScheduledCpuProfile
Check-StartupProfile ($startupProfileCalls -eq 0 -and $cpuProfileOutcome.classification -eq 'waiting') 'Profiling attached before running status.'
$watch.Elapsed.TotalSeconds = 41
Invoke-RecompStartupScheduledCpuProfile
Check-StartupProfile ($startupProfileCalls -eq 0 -and $cpuProfileOutcome.classification -eq 'schedule_missed') 'Late boot ignored the profile time budget.'

Reset-StartupProfileState
$watch.Elapsed.TotalSeconds = 10
$lastFrameProgressSeconds = 7
Invoke-RecompStartupScheduledCpuProfile
Check-StartupProfile ($startupProfileCalls -eq 0) 'Profiling accepted stale frame progress.'

Reset-StartupProfileState
$startupProfileFail = $true
$watch.Elapsed.TotalSeconds = 10
Invoke-RecompStartupHostPoll
Invoke-RecompStartupScheduledCpuProfile
Check-StartupProfile ($cpuProfileOutcome.classification -eq 'failed' -and $cpuProfileOutcome.result.own_session_stop_confirmed) 'Failure lost the helper cleanup evidence.'
Check-StartupProfile ($cpuProfileOutcome.error.Contains('Synthetic collector failure') -and -not $stopRequested) 'Collector failure bypassed the normal remaining runtime schedule.'
Invoke-RecompStartupScheduledCpuProfile
Check-StartupProfile ($startupProfileCalls -eq 1) 'Failed profiling was automatically retried.'

Reset-StartupProfileState
# Model slow collector startup/stop crossing both the game and grace deadlines.
$startupProfileCallbackTimes = @(59.9,60.0,60.5,61.0,65.0,66.0)
$watch.Elapsed.TotalSeconds = 10
Invoke-RecompStartupHostPoll
Invoke-RecompStartupScheduledCpuProfile
$startupProfileStops = @($startupProfileActions | Where-Object { $_.action -eq 'stop' })
Check-StartupProfile ($startupProfileStops.Count -eq 2 -and $startupProfileStops[0].time -eq 60 -and $startupProfileStops[1].time -eq 61) 'Collector latency postponed or duplicated normal shutdown commands.'
Check-StartupProfile ($observationEndSeconds -eq 60 -and $observationProcessSample.elapsed_seconds -eq 60) 'The observation boundary moved past shutdown.'
Check-StartupProfile ($startupProfileKillCount -eq 1 -and $forcedTermination -and $taskProcess.HasExited) 'The owned-process watchdog was not serviced during profiling.'
Check-StartupProfile ($startupProfileCalls -eq 1 -and $cpuProfileOutcome.elapsed_seconds -eq 56) 'Collector overhead or nonrecursive invocation was lost.'

Reset-StartupProfileState
$watch.Elapsed.TotalSeconds = 5
$taskProcess.HasExited = $true
Invoke-RecompStartupHostPoll
Invoke-RecompStartupScheduledCpuProfile
Check-StartupProfile ($observationCaptured -and $observationEndSeconds -eq 5 -and $startupProfileCalls -eq 0) 'Exited process was profiled or lost its observation boundary.'
Check-StartupProfile ($startupProfileActions.Count -eq 0) 'Exited process received commands.'

# Regression for the actual helper -> native command -> OnPoll call chain.
# The native command's TimeoutSeconds=5 must not shadow the runtime's 70-second
# budget. Load the real helper; only native commands/owner reads/time are mocked.
. (Join-Path $PSScriptRoot '../scripts/profile-recomp.ps1')
$script:startupProfileActualHelper = (Get-Command Invoke-RecompCpuProfile).ScriptBlock
Reset-StartupProfileState
$script:TimeoutSeconds = 70
$script:CpuProfileAtSeconds = 40
$script:PressStartAtSeconds = 0
$script:screenshotSchedule = @(45,65)
$script:watch.Elapsed.TotalSeconds = 40
$script:startupProfileFixtureTools = Join-Path $runRoot 'synthetic-collector'
foreach ($startupProfileFolder in @('Agents','AgentConfigs')) {
    [IO.Directory]::CreateDirectory((Join-Path $script:startupProfileFixtureTools $startupProfileFolder)) | Out-Null
}
foreach ($startupProfileFile in @($Runner,$Module,(Join-Path $script:startupProfileFixtureTools 'VSDiagnostics.exe'),
    (Join-Path $script:startupProfileFixtureTools 'Agents/DiagnosticsHub.CpuAgent.dll'))) {
    [IO.File]::WriteAllText($startupProfileFile,'Synthetic fixture; not an executable.')
}
[IO.File]::WriteAllText((Join-Path $script:startupProfileFixtureTools 'AgentConfigs/CpuUsageLow.json'),
    '{"Agents":[{"CLSID":"4EA90761-2248-496C-B854-3C0399A591A4","Name":"DiagnosticsHub.CpuAgent.dll","Config":{"cpuSampleRate":100}}]}')
$script:buildRecord.launcher_binary_sha256 = (Get-FileHash -LiteralPath $Runner -Algorithm SHA256).Hash
$script:taskProcess = [Diagnostics.Process]::new() # Never started.
$script:taskProcess | Add-Member -MemberType NoteProperty -Name HasExited -Value $false -Force
function Get-RecompCpuProfileOwnerObservation {
    param($OwnedProcess)
    [pscustomobject]@{process_id=4242;start_time_utc_ticks=$script:ownedStartTicks;
        launched_executable=$script:Runner;loaded_executable=$script:Runner}
}
function Get-RecompCpuProfileElapsed { param($Clock) return $script:watch.Elapsed.TotalSeconds - 40 }
function Wait-RecompCpuProfilePoll { param([int]$Milliseconds) $script:watch.Elapsed.TotalSeconds += $Milliseconds/1000.0 }
function Invoke-RecompCpuProfilerCommand {
    param([string]$Executable,[string[]]$Arguments,[string]$OutputStem,
        [int]$TimeoutSeconds,[scriptblock]$OnPoll,[switch]$Cleanup)
    Check-StartupProfile ($TimeoutSeconds -eq 5 -and $script:TimeoutSeconds -eq 70) 'The regression did not reproduce nested timeout shadowing.'
    # Also shadow common host variables to exercise the callback's scope boundary.
    $watch = [pscustomobject]@{Elapsed=[pscustomobject]@{TotalSeconds=999.0}}
    $taskProcess = [pscustomobject]@{HasExited=$true}
    $Screenshot = $false
    $automationRoot = 'not-the-owned-command-directory'
    $script:watch.Elapsed.TotalSeconds += 0.2
    & $OnPoll
    Check-StartupProfile (-not $script:stopRequested -and -not $script:observationCaptured) 'Collector-local values caused premature runtime shutdown.'
    [IO.File]::WriteAllText($OutputStem+'.stdout.txt','Synthetic collector output.')
    [IO.File]::WriteAllText($OutputStem+'.stderr.txt','')
    if ($Arguments[0] -eq 'stop') {
        $startupProfileArchive = @($Arguments | Where-Object { $_.StartsWith('/output:') })[0].Substring(8)
        [IO.File]::WriteAllText($startupProfileArchive,'Synthetic archive, not a CPU trace.')
    }
    [pscustomobject]@{exit_code=0;timed_out=$false;error=$null;poll_error=$null;output_complete=$true;
        elapsed_seconds=0.2;stdout_path=($OutputStem+'.stdout.txt');stderr_path=($OutputStem+'.stderr.txt')}
}
function Invoke-RecompCpuProfile {
    param($OwnedProcess,$ExpectedStartTimeUtcTicks,$ExpectedExecutablePath,$ExpectedExecutableSha256,
        $OutputDirectory,$DurationSeconds,[scriptblock]$OnPoll,$ModulePath)
    & $script:startupProfileActualHelper @PSBoundParameters -ProfilerRoot $script:startupProfileFixtureTools
}
try {
    Invoke-RecompStartupHostPoll
    Invoke-RecompStartupScheduledCpuProfile
    Check-StartupProfile ($cpuProfileOutcome.classification -eq 'completed' -and $cpuProfileOutcome.result.commands.Count -eq 4) 'Real helper lifecycle did not finish with mocked native commands.'
    Check-StartupProfile ($watch.Elapsed.TotalSeconds -gt 55 -and $watch.Elapsed.TotalSeconds -lt 57 -and -not $stopRequested) 'Collector command latency changed the runtime deadline.'
    Check-StartupProfile ($screenshotCaptures.Count -eq 1 -and $screenshotCaptures[0].queued_seconds -lt 46) 'The real nested helper did not service the scheduled screenshot.'
    $script:watch.Elapsed.TotalSeconds = 70
    Invoke-RecompStartupHostPoll
    Check-StartupProfile ($stopRequested -and $observationEndSeconds -eq 70 -and -not $forcedTermination) 'Normal runtime shutdown did not retain its original 70-second deadline.'
} finally { $script:taskProcess.Dispose() }

Write-Host "Startup CPU profile integration: $script:startupProfileChecks checks passed; no native process or recorder launched."
