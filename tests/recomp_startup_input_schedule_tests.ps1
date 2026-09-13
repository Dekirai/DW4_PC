# Execute the actual extracted input scheduler with fake time, a fake owned
# process and filesystem command acknowledgments. No native process is started.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$inputTokens = $null
$inputErrors = $null
$inputAst = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot '../scripts/test-recomp-startup.ps1'),[ref]$inputTokens,[ref]$inputErrors)
$script:inputChecks = 0
function Check-Input([bool]$Condition,[string]$Message) {
    ++$script:inputChecks
    if (-not $Condition) { throw $Message }
}
Check-Input ($inputErrors.Count -eq 0) 'The startup harness must parse.'
foreach ($inputFunction in @('Publish-StartInput','Publish-Stop','Invoke-RecompStartupSecondStartPoll',
    'Get-RecompStartupStartOutcome','Complete-RecompStartupObservation','Invoke-RecompStartupHostPoll')) {
    $inputNode = $inputAst.Find({ param($Node)
        $Node -is [Management.Automation.Language.FunctionDefinitionAst] -and $Node.Name -eq $inputFunction
    },$false)
    Check-Input ($null -ne $inputNode) ('Missing input scheduler function: ' + $inputFunction)
    . ([scriptblock]::Create($inputNode.Extent.Text))
}
$inputGuards = @($inputAst.EndBlock.Statements | Where-Object {
    $_ -is [Management.Automation.Language.IfStatementAst] -and
    ($_.Extent.Text.StartsWith('if ($PressStartAtSeconds') -or $_.Extent.Text.StartsWith('if ($SecondStartPressAtSeconds')) -and
    $_.Extent.Text.Contains('throw ')
})
Check-Input ($inputGuards.Count -eq 2) 'Both controlled input schedule guards must exist.'
$validateInput = [scriptblock]::Create($inputAst.ParamBlock.Extent.Text + "`n" +
    (($inputGuards | ForEach-Object { $_.Extent.Text }) -join "`n"))
foreach ($inputParameter in @('PressStartAtSeconds','SecondStartPressAtSeconds')) {
    $inputParam = @($inputAst.ParamBlock.Parameters | Where-Object { $_.Name.VariablePath.UserPath -eq $inputParameter })
    Check-Input ($inputParam.Count -eq 1 -and $inputParam[0].DefaultValue.Value -eq 0) 'Controlled input must default to disabled.'
}
foreach ($inputCase in @(@(0,0,30),@(5,0,30),@(5,10,30),@(80,85,90),@(25,0,30))) {
    & $validateInput -Module synthetic -ExtractedRoot synthetic -PressStartAtSeconds $inputCase[0] `
        -SecondStartPressAtSeconds $inputCase[1] -TimeoutSeconds $inputCase[2]
    Check-Input $true 'A valid input schedule was rejected.'
}
foreach ($inputCase in @(@(0,10,30),@(5,9,30),@(10,5,30),@(5,26,30),@(26,0,30),@(80,85,89),@(5,-1,30),@(5,86,90))) {
    $inputRejected = $false
    try {
        & $validateInput -Module synthetic -ExtractedRoot synthetic -PressStartAtSeconds $inputCase[0] `
            -SecondStartPressAtSeconds $inputCase[1] -TimeoutSeconds $inputCase[2]
    } catch { $inputRejected = $true }
    Check-Input $inputRejected 'An orphaned, unordered, too-close, late or out-of-range input schedule was accepted.'
}
$inputFinallyNode = @($inputAst.EndBlock.Statements | Where-Object {
    $_ -is [Management.Automation.Language.TryStatementAst] -and $_.Extent.Text.Contains('$processStarted = $taskProcess.Start()')
})
Check-Input ($inputFinallyNode.Count -eq 1) 'The owned-process cleanup must remain identifiable.'
$inputCleanup = [scriptblock]::Create($inputFinallyNode[0].Finally.Statements[0].Extent.Text)
$inputRoot = Join-Path (Split-Path -Parent $PSScriptRoot) ('work/recomp-startup-input-tests/' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($inputRoot) | Out-Null
# Verify that real script parameter binding survives the same helper imports.
# This fixture contains only parameter declarations/imports and an observation.
$inputHelperRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../scripts'))
$inputBindingFixture = Join-Path $inputRoot 'binding.ps1'
$inputBindingLines = @($inputAst.ParamBlock.Extent.Text)
foreach ($inputHelper in @('recomp-startup-metrics.ps1','recomp-audio-metrics.ps1','recomp-inprocess-probe.ps1','profile-recomp.ps1')) {
    $inputBindingLines += ". '" + (Join-Path $inputHelperRoot $inputHelper).Replace("'","''") + "'"
}
$inputBindingLines += 'function Read-SyntheticInputBinding { $script:PSBoundParameters.ContainsKey(''SecondStartPressAtSeconds'') -and $script:SecondStartPressAtSeconds -gt 0 }; Read-SyntheticInputBinding'
[IO.File]::WriteAllText($inputBindingFixture,($inputBindingLines -join "`n"))
Check-Input (& $inputBindingFixture -Module synthetic -ExtractedRoot synthetic -SecondStartPressAtSeconds 12) 'Helper imports lost the explicit second-press binding.'
Check-Input (-not (& $inputBindingFixture -Module synthetic -ExtractedRoot synthetic)) 'The default binding enabled a second press.'
$script:inputCaseNumber = 0
function Reset-InputState([int]$First = 5,[int]$Second = 12) {
    ++$script:inputCaseNumber
    $script:runRoot = Join-Path $inputRoot ([string]$script:inputCaseNumber)
    $script:automationRoot = Join-Path $script:runRoot 'automation'
    $script:commandRoot = Join-Path $script:automationRoot 'commands'
    [IO.Directory]::CreateDirectory($script:commandRoot) | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $script:automationRoot 'processed')) | Out-Null
    $script:utf8 = [Text.UTF8Encoding]::new($false)
    $script:PressStartAtSeconds = $First
    $script:SecondStartPressAtSeconds = $Second
    $script:PSBoundParameters['SecondStartPressAtSeconds'] = $Second
    $script:TimeoutSeconds = 30
    $script:ShutdownGraceSeconds = 5
    $script:watch = [pscustomobject]@{Elapsed=[pscustomobject]@{TotalSeconds=0.0}}
    $script:taskProcess = [pscustomobject]@{HasExited=$false}
    $script:processStarted = $true
    $script:stopRequested = $false
    $script:stopRequestedSeconds = $null
    $script:secondStopRequested = $false
    $script:forcedTermination = $false
    $script:observationCaptured = $false
    $script:sawRunning = $true
    $script:lastStatus = @{state='running'}
    $script:frameProgressSamples = 3
    $script:lastFrameProgressSeconds = 0.0
    $script:lastPresentProgressSeconds = 0.0
    $script:Screenshot = $false
    $script:startPressQueuedSeconds = $null
    $script:startPressAcknowledgedSeconds = $null
    $script:startReleaseQueuedSeconds = $null
    $script:startReleaseAcknowledgedSeconds = $null
    $script:secondStartProbe = [ordered]@{
        requested_seconds=$Second; press_queued_seconds=$null; press_acknowledged_seconds=$null;
        release_queued_seconds=$null; release_acknowledged_seconds=$null
    }
    $script:inputKillCount = 0
    $script:harnessError = ''
}
function Read-Status { }
function Get-RecompProcessSample { param($Process,$Clock) return [pscustomobject]@{elapsed_seconds=$Clock.Elapsed.TotalSeconds} }
function Stop-OwnedProcessTree { ++$script:inputKillCount; $script:taskProcess.HasExited = $true }
function Poll-Input([double]$Time,[switch]$Stale) {
    $script:watch.Elapsed.TotalSeconds = $Time
    if (-not $Stale) {
        $script:lastFrameProgressSeconds = $Time
        $script:lastPresentProgressSeconds = $Time
    }
    Invoke-RecompStartupHostPoll
}
function Acknowledge-Input([string]$Name) {
    [IO.File]::Move((Join-Path $script:commandRoot $Name),(Join-Path $script:automationRoot ('processed/' + $Name)))
}
function Complete-FirstInput([double]$At = 5) {
    Poll-Input $At
    Acknowledge-Input '020-start-press.txt'
    Poll-Input ($At+0.1)
    Poll-Input ($At+0.61)
    Acknowledge-Input '021-start-release.txt'
    Poll-Input ($At+0.7)
}

Reset-InputState
Poll-Input 4.9
Check-Input (@([IO.Directory]::GetFiles($commandRoot)).Count -eq 0) 'Input was queued before the first schedule.'
Complete-FirstInput
Check-Input ($startPressQueuedSeconds -eq 5 -and $startPressAcknowledgedSeconds -eq 5.1 -and
    $startReleaseQueuedSeconds -eq 5.61 -and $startReleaseAcknowledgedSeconds -eq 5.7) 'The original first-press timing changed.'
Poll-Input 11.9
Check-Input ($null -eq $secondStartProbe.press_queued_seconds) 'The second press fired early.'
Poll-Input 12
Poll-Input 12
Check-Input ($secondStartProbe.press_queued_seconds -eq 12) 'The second press was not scheduled.'
Check-Input ([IO.File]::ReadAllText((Join-Path $commandRoot '022-start-press-02.txt')) -ceq "command=pad`nport=0`nstart=1`n") 'The second press did not target emulated port 0.'
Acknowledge-Input '022-start-press-02.txt'
Poll-Input 12.1
Poll-Input 12.59
Check-Input ($null -eq $secondStartProbe.release_queued_seconds) 'The second press released before a half-second acknowledged hold.'
Poll-Input 12.61
Poll-Input 12.61
Check-Input ([IO.File]::ReadAllText((Join-Path $commandRoot '023-start-release-02.txt')) -ceq "command=clear_pad`nport=0`n") 'The second release did not clear emulated port 0.'
Acknowledge-Input '023-start-release-02.txt'
Poll-Input 12.7
Poll-Input 20
Check-Input ($secondStartProbe.press_acknowledged_seconds -eq 12.1 -and
    $secondStartProbe.release_queued_seconds -eq 12.61 -and $secondStartProbe.release_acknowledged_seconds -eq 12.7) 'Per-press timestamps were lost or replaced by subsequent polls.'
Check-Input ((Get-RecompStartupStartOutcome $secondStartProbe) -eq 'verified_command_cycle') 'Acknowledged command cycle was not verified.'
Check-Input (@([IO.Directory]::GetFiles($commandRoot)).Count -eq 0 -and
    @([IO.Directory]::GetFiles((Join-Path $automationRoot 'processed'))).Count -eq 4) 'Repeated polls duplicated an input command.'
Check-Input (@([IO.Directory]::GetFiles($automationRoot,'*.pending')).Count -eq 0) 'A partial input command remained in the publish directory.'
# Build the real final per-press evidence and apply both input verification gates.
$record = [ordered]@{}
$classification = 'passed_startup'
$inputRecordNodes = @($inputAst.EndBlock.Statements | Where-Object {
    ($_.Extent.Text.StartsWith('$record[''start_probe'']') -or
     $_.Extent.Text.StartsWith('$secondStartProbe[''classification'']') -or
     $_.Extent.Text.StartsWith('$record[''start_probes'']') -or
     $_.Extent.Text.StartsWith('$record[''start_schedule_scope'']')) -or
    ($_ -is [Management.Automation.Language.IfStatementAst] -and
     $_.Extent.Text.Contains('$classification = ''start_input_unverified'''))
})
$inputRecordWriter = [scriptblock]::Create(($inputRecordNodes | ForEach-Object { $_.Extent.Text }) -join "`n")
. $inputRecordWriter
Check-Input ($record.start_probes.Count -eq 2 -and $record.start_probe.press_queued_seconds -eq 5 -and
    $record.start_probes[1].press_queued_seconds -eq 12 -and $classification -eq 'passed_startup') 'Final evidence lost the first/second press distinction or rejected verified command cycles.'
Check-Input ($record.start_probes[0].classification -eq 'verified_command_cycle' -and
    $record.start_probes[1].classification -eq 'verified_command_cycle' -and
    $record.start_schedule_scope.Contains('screenshots require visual inspection')) 'Per-press outcomes incorrectly imply a verified original menu.'
$secondStartProbe.release_acknowledged_seconds = $secondStartProbe.release_queued_seconds
. $inputRecordWriter
Check-Input ($classification -eq 'start_input_unverified' -and
    $record.start_probes[1].classification -eq 'release_unacknowledged') 'A same-poll release acknowledgment bypassed the strict input verification gate.'
$secondStartProbe.release_queued_seconds = 12.2
Check-Input ((Get-RecompStartupStartOutcome $secondStartProbe) -eq 'hold_unverified') 'A shortened second hold was reported as verified.'

Reset-InputState
Complete-FirstInput 9
Poll-Input 12
Check-Input ($null -eq $secondStartProbe.press_queued_seconds) 'A delayed first press allowed less than five seconds of actual separation.'
Poll-Input 14
Check-Input ($secondStartProbe.press_queued_seconds -eq 14) 'Second press did not wait for actual first-press separation.'

Reset-InputState
Poll-Input 5
Poll-Input 7
Check-Input ($startReleaseQueuedSeconds -eq 7) 'The original two-second fallback release changed.'
Poll-Input 12
Check-Input ($null -eq $secondStartProbe.press_queued_seconds) 'Second press ran before the first release acknowledgment.'

Reset-InputState
Complete-FirstInput
Poll-Input 12
Poll-Input 13.9
Check-Input ($null -eq $secondStartProbe.release_queued_seconds) 'Second fallback released before two seconds.'
Poll-Input 14
Check-Input ($secondStartProbe.release_queued_seconds -eq 14 -and
    (Get-RecompStartupStartOutcome $secondStartProbe) -eq 'press_unacknowledged') 'Fallback release invented a verified input cycle.'

Reset-InputState
Complete-FirstInput
Poll-Input 12 -Stale
Check-Input ($null -eq $secondStartProbe.press_queued_seconds) 'Second press accepted stale frame progress.'
$sawRunning = $false
Poll-Input 20
Check-Input ($null -eq $secondStartProbe.press_queued_seconds) 'Second press ran without running status.'
$sawRunning = $true
Poll-Input 25.01
Check-Input ($null -eq $secondStartProbe.press_queued_seconds -and
    (Get-RecompStartupStartOutcome $secondStartProbe) -eq 'not_reached') 'A missed schedule consumed the five-second shutdown margin.'

Reset-InputState
Complete-FirstInput
Poll-Input 12
Acknowledge-Input '022-start-press-02.txt'
Poll-Input 12.1
Poll-Input 30
Check-Input ($secondStartProbe.release_queued_seconds -eq 30 -and $stopRequestedSeconds -eq 30 -and
    $observationEndSeconds -eq 30) 'Deadline did not clear active second input and retain the original observation boundary.'
Check-Input ([IO.File]::Exists((Join-Path $commandRoot '023-start-release-02.txt')) -and
    [IO.File]::Exists((Join-Path $commandRoot '099-stop-01.txt'))) 'Shutdown did not publish release before the ordered stop command.'
Acknowledge-Input '023-start-release-02.txt'
Poll-Input 30.1
Poll-Input 31
Poll-Input 31
Check-Input ($secondStartProbe.release_acknowledged_seconds -eq 30.1 -and $secondStopRequested) 'Shutdown stopped servicing release acknowledgments or its second stop.'
Poll-Input 35
Check-Input ($inputKillCount -eq 1 -and $forcedTermination -and $taskProcess.HasExited) 'Input scheduling moved the owned-process shutdown grace boundary.'
Poll-Input 36
Check-Input ($inputKillCount -eq 1) 'Exited process received another watchdog action.'

Reset-InputState
Complete-FirstInput
Poll-Input 12
$watch.Elapsed.TotalSeconds = 12.1
. $inputCleanup
Check-Input ($secondStartProbe.release_queued_seconds -eq 12.1 -and $inputKillCount -eq 1 -and
    [IO.File]::Exists((Join-Path $commandRoot '023-start-release-02.txt'))) 'Exceptional shutdown left the second press active before owned-process cleanup.'
Check-Input ((Get-RecompStartupStartOutcome $secondStartProbe) -ne 'verified_command_cycle') 'An unacknowledged shutdown release was misreported as verified.'

Reset-InputState -Second 0
Poll-Input 5
$watch.Elapsed.TotalSeconds = 5.1
. $inputCleanup
Check-Input ($startReleaseQueuedSeconds -eq 5.1 -and $inputKillCount -eq 1 -and
    [IO.File]::Exists((Join-Path $commandRoot '021-start-release.txt'))) 'Exceptional shutdown left the first press active.'

Reset-InputState -First 0 -Second 0
$script:PSBoundParameters.Remove('SecondStartPressAtSeconds') | Out-Null
Poll-Input 12
Check-Input (@([IO.Directory]::GetFiles($commandRoot)).Count -eq 0 -and
    (Get-RecompStartupStartOutcome $secondStartProbe) -eq 'disabled') 'Default neutral scheduling published input.'
Poll-Input 30
Check-Input ([IO.File]::Exists((Join-Path $commandRoot '000-stop-01.txt'))) 'Neutral shutdown command ordering changed.'

Write-Host "Startup input scheduling: $script:inputChecks checks passed; no native process, device input or collector launched."
