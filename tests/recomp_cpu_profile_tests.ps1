# Synthetic lifecycle checks only. No game, profiler, ETW session or service starts.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '../scripts/profile-recomp.ps1')
$script:cpuTestChecks = 0
function Check-CpuProfile([bool]$Condition,[string]$Message) {
    ++$script:cpuTestChecks
    if (-not $Condition) { throw $Message }
}
$cpuTestRepo = Split-Path -Parent $PSScriptRoot
$cpuTestRoot = Join-Path $cpuTestRepo ('work/recomp-profile-tests/' + [Guid]::NewGuid().ToString('N'))
$cpuTestTools = Join-Path $cpuTestRoot 'collector'
[IO.Directory]::CreateDirectory((Join-Path $cpuTestTools 'Agents')) | Out-Null
[IO.Directory]::CreateDirectory((Join-Path $cpuTestTools 'AgentConfigs')) | Out-Null
$script:cpuTestExe = Join-Path $cpuTestRoot 'owned.exe'
$cpuTestModule = Join-Path $cpuTestRoot 'module.dll'
foreach ($cpuTestFile in @($script:cpuTestExe,$cpuTestModule,(Join-Path $cpuTestTools 'VSDiagnostics.exe'),(Join-Path $cpuTestTools 'Agents/DiagnosticsHub.CpuAgent.dll'))) {
    [IO.File]::WriteAllText($cpuTestFile,'Synthetic non-executable fixture.')
}
$cpuTestConfig = Join-Path $cpuTestTools 'AgentConfigs/CpuUsageLow.json'
$cpuTestGoodConfig = '{"Agents":[{"CLSID":"4EA90761-2248-496C-B854-3C0399A591A4","Name":"DiagnosticsHub.CpuAgent.dll","Config":{"cpuSampleRate":100}}]}'
[IO.File]::WriteAllText($cpuTestConfig,$cpuTestGoodConfig)
$cpuTestHash = (Get-FileHash -LiteralPath $script:cpuTestExe -Algorithm SHA256).Hash
$cpuTestProcess = [Diagnostics.Process]::new() # Never started; observations are substituted below.
$script:cpuTestTime = 0.0
$script:cpuTestCommands = [Collections.Generic.List[object]]::new()
$script:cpuTestOwnerCalls = 0
$script:cpuTestScenario = ''
$script:cpuTestPhase = ''
function Get-RecompCpuProfileElapsed { param($Clock) return $script:cpuTestTime }
function Wait-RecompCpuProfilePoll {
    param([int]$Milliseconds)
    $script:cpuTestTime += $Milliseconds / 1000.0
    $script:cpuTestPhase = 'observing'
}
function Get-RecompCpuProfileOwnerObservation {
    param($OwnedProcess)
    ++$script:cpuTestOwnerCalls
    if ($script:cpuTestScenario -eq 'exited') { throw 'Synthetic retained process exited.' }
    $cpuTestObservedPid = if ($script:cpuTestScenario -eq 'pid_changes' -and $script:cpuTestOwnerCalls -ge 3) { 4343 } else { 4242 }
    $cpuTestObservedTicks = if ($script:cpuTestScenario -eq 'wrong_start') { 1001 } else { 1000 }
    $cpuTestLaunch = if ($script:cpuTestScenario -eq 'no_launch_ownership') { '' } else { $script:cpuTestExe }
    $cpuTestLoaded = if ($script:cpuTestScenario -eq 'wrong_path') { $script:cpuTestExe + '.other' } else { $script:cpuTestExe }
    [pscustomobject]@{process_id=$cpuTestObservedPid;start_time_utc_ticks=$cpuTestObservedTicks;launched_executable=$cpuTestLaunch;loaded_executable=$cpuTestLoaded}
}
function Invoke-RecompCpuProfilerCommand {
    param([string]$Executable,[string[]]$Arguments,[string]$OutputStem,[int]$TimeoutSeconds,[scriptblock]$OnPoll,[switch]$Cleanup)
    $script:cpuTestPhase = $Arguments[0]
    $script:cpuTestCommands.Add([pscustomobject]@{arguments=$Arguments;cleanup=[bool]$Cleanup;timeout=$TimeoutSeconds})
    $cpuTestPollError = $null
    if ($OnPoll) {
        try { & $OnPoll | Out-Null }
        catch { if (-not $Cleanup) { throw }; $cpuTestPollError = $_.Exception.Message }
    }
    $script:cpuTestTime += 0.2
    $cpuTestFailure = ($script:cpuTestScenario -eq 'start_failure' -and $Arguments[0] -eq 'start') -or
        ($script:cpuTestScenario -eq 'stop_failure' -and $Arguments[0] -eq 'stop') -or
        ($script:cpuTestScenario -eq 'lifetime_failure' -and $Arguments[0] -eq 'update')
    $cpuTestTimeout = $script:cpuTestScenario -eq 'start_timeout' -and $Arguments[0] -eq 'start'
    $cpuTestStderr = if (($script:cpuTestScenario -eq 'zero_exit_start_stderr' -and $Arguments[0] -eq 'start') -or
        ($script:cpuTestScenario -eq 'zero_exit_stop_stderr' -and $Arguments[0] -eq 'stop')) {
        'Zugriff verweigert (HRESULT: 0x80070005 (E_ACCESSDENIED))'
    } else { '' }
    [IO.File]::WriteAllText($OutputStem + '.stdout.txt',('Synthetic raw output: ' + ($Arguments -join ' ')))
    [IO.File]::WriteAllText($OutputStem + '.stderr.txt',$cpuTestStderr)
    if ($Arguments[0] -eq 'stop' -and -not $cpuTestFailure -and $script:cpuTestScenario -ne 'missing_archive') {
        $cpuTestTrace = @($Arguments | Where-Object { $_.StartsWith('/output:') })[0].Substring(8)
        [IO.File]::WriteAllText($cpuTestTrace,'Synthetic archive fixture, not a CPU trace.')
    }
    [pscustomobject]@{exit_code=$(if($cpuTestFailure){5}else{0});timed_out=$cpuTestTimeout;error=(Get-RecompCpuProfilerDiagnosticError $cpuTestStderr);poll_error=$cpuTestPollError;elapsed_seconds=0.2;output_complete=$true;stdout_path=($OutputStem+'.stdout.txt');stderr_path=($OutputStem+'.stderr.txt')}
}
function Run-CpuProfileCase([string]$Scenario,[bool]$ShouldPass,[hashtable]$Overrides = @{}) {
    $script:cpuTestScenario = $Scenario
    $script:cpuTestTime = 0.0
    $script:cpuTestOwnerCalls = 0
    $script:cpuTestCommands.Clear()
    $cpuTestOutput = Join-Path $cpuTestRoot $Scenario
    $cpuTestOptions = @{
        OwnedProcess=$cpuTestProcess;ExpectedStartTimeUtcTicks=1000
        ExpectedExecutablePath=$script:cpuTestExe;ExpectedExecutableSha256=$cpuTestHash
        OutputDirectory=$cpuTestOutput;ProfilerRoot=$cpuTestTools;ModulePath=$cpuTestModule
        DurationSeconds=15;CommandTimeoutSeconds=5
        OnPoll={
            if (($script:cpuTestScenario -eq 'poll_failure' -and $script:cpuTestPhase -eq 'observing') -or
                ($script:cpuTestScenario -eq 'stop_poll_failure' -and $script:cpuTestPhase -eq 'stop')) {
                throw 'Synthetic polling callback failure.'
            }
        }
    }
    foreach($cpuTestKey in $Overrides.Keys){$cpuTestOptions[$cpuTestKey]=$Overrides[$cpuTestKey]}
    $cpuTestThrew = $false
    try { Invoke-RecompCpuProfile @cpuTestOptions | Out-Null } catch { $cpuTestThrew = $true }
    Check-CpuProfile ($cpuTestThrew -ne $ShouldPass) ('Unexpected outcome: ' + $Scenario)
    $cpuTestSummaryPath = Join-Path $cpuTestOutput 'summary.json'
    if (Test-Path -LiteralPath $cpuTestSummaryPath) { return Get-Content -LiteralPath $cpuTestSummaryPath -Raw | ConvertFrom-Json }
    return $null
}
try {
    Check-CpuProfile ((ConvertTo-RecompCpuProfileNativeArgument 'C:\with space\') -ceq '"C:\with space\\"') 'Trailing CRT slash quoting failed.'
    Check-CpuProfile ((ConvertTo-RecompCpuProfileNativeArgument 'a"b') -ceq '"a\"b"') 'Embedded CRT quote escaping failed.'
    Check-CpuProfile ((ConvertTo-RecompCpuProfileNativeArgument '$(not-a-command)') -ceq '"$(not-a-command)"') 'Argument text was interpreted.'
    Check-CpuProfile ($null -eq (Get-RecompCpuProfilerDiagnosticError " `r`n`t")) 'Whitespace stderr was treated as a failure.'
    Check-CpuProfile ($null -ne (Get-RecompCpuProfilerDiagnosticError 'Zugriff verweigert (HRESULT: 0x80070005 (E_ACCESSDENIED))')) 'Localized zero-exit COM failure was accepted.'
    $cpuTestSuccess = Run-CpuProfileCase 'success' $true
    Check-CpuProfile ($cpuTestSuccess.classification -eq 'completed' -and $cpuTestSuccess.observation_completed) 'Successful capture was not completed.'
    Check-CpuProfile ($cpuTestSuccess.host_observed_attached_seconds -ge 15 -and $cpuTestSuccess.host_observed_attached_seconds -lt 15.1) 'The requested observation window was not bounded.'
    Check-CpuProfile ($cpuTestSuccess.commands.Count -eq 4 -and $cpuTestSuccess.commands[0].phase -eq 'help' -and $cpuTestSuccess.commands[3].phase -eq 'stop') 'Unexpected collector command sequence.'
    Check-CpuProfile ($cpuTestSuccess.commands[1].arguments -contains '/attach:4242') 'Collector did not target the retained PID.'
    Check-CpuProfile ($cpuTestSuccess.commands[2].arguments -contains '/lifetimeProcess:4242') 'Collector lifetime was not bound to the retained PID.'
    Check-CpuProfile ($cpuTestSuccess.commands[1].arguments[1] -eq $cpuTestSuccess.session_id -and $cpuTestSuccess.commands[3].arguments[1] -eq $cpuTestSuccess.session_id) 'Start and stop used different sessions.'
    Check-CpuProfile ($script:cpuTestCommands[3].cleanup) 'Stop did not enter cleanup polling mode.'
    Check-CpuProfile ($cpuTestSuccess.tools.Count -eq 3 -and $cpuTestSuccess.tools[0].sha256.Length -eq 64) 'Profiler tool/config identities were not retained.'
    Check-CpuProfile (Test-Path -LiteralPath $cpuTestSuccess.commands[0].result.stdout_path) 'Raw help was not retained.'
    Check-CpuProfile (@($cpuTestSuccess.adjacent_pdb_candidates | Where-Object {$_.exists}).Count -eq 0) 'Absent PDBs were falsely reported present.'
    Check-CpuProfile (-not $cpuTestSuccess.runtime_launched) 'The helper claimed to launch a game.'
    $cpuTestOther = Run-CpuProfileCase 'second_success' $true
    Check-CpuProfile ($cpuTestOther.session_id -ne $cpuTestSuccess.session_id) 'Session GUID was reused.'
    foreach($cpuTestName in @('wrong_start','wrong_path','no_launch_ownership','exited')) {
        $cpuTestFailureRecord = Run-CpuProfileCase $cpuTestName $false
        Check-CpuProfile (-not $cpuTestFailureRecord.collector_start_attempted -and $script:cpuTestCommands.Count -eq 0) ('Rejected ownership launched a collector: ' + $cpuTestName)
    }
    $cpuTestBadHash = Run-CpuProfileCase 'wrong_hash' $false @{ExpectedExecutableSha256=('0'*64)}
    Check-CpuProfile (-not $cpuTestBadHash.collector_start_attempted -and $script:cpuTestCommands.Count -eq 0) 'Wrong executable hash launched a collector.'
    [IO.File]::WriteAllText($cpuTestConfig,$cpuTestGoodConfig.Replace('"cpuSampleRate":100','"cpuSampleRate":100,"attachToChildProcesses":true'))
    $cpuTestBadConfig = Run-CpuProfileCase 'children_config' $false
    Check-CpuProfile (-not $cpuTestBadConfig.collector_start_attempted -and $script:cpuTestCommands.Count -eq 0) 'A broader agent configuration was accepted.'
    [IO.File]::WriteAllText($cpuTestConfig,$cpuTestGoodConfig)
    foreach($cpuTestName in @('start_failure','start_timeout','lifetime_failure','pid_changes','poll_failure','zero_exit_start_stderr')) {
        $cpuTestFailureRecord = Run-CpuProfileCase $cpuTestName $false
        $cpuTestStops = @($script:cpuTestCommands | Where-Object {$_.arguments[0] -eq 'stop'})
        Check-CpuProfile ($cpuTestFailureRecord.collector_start_attempted -and $cpuTestStops.Count -eq 1) ('Failed attachment/observation did not stop its own session: ' + $cpuTestName)
        Check-CpuProfile ($cpuTestStops[0].arguments[1] -eq $cpuTestFailureRecord.session_id -and $cpuTestStops[0].cleanup) 'Failure cleanup targeted the wrong session.'
        Check-CpuProfile ($cpuTestFailureRecord.own_session_stop_confirmed -and -not $cpuTestFailureRecord.observation_completed) 'A failed observation was misclassified.'
    }
    $cpuTestStopFailure = Run-CpuProfileCase 'stop_failure' $false
    Check-CpuProfile (-not $cpuTestStopFailure.own_session_stop_confirmed -and $cpuTestStopFailure.errors.Count -gt 0) 'Stop failure was hidden.'
    $cpuTestStopDiagnostic = Run-CpuProfileCase 'zero_exit_stop_stderr' $false
    Check-CpuProfile (-not $cpuTestStopDiagnostic.own_session_stop_confirmed -and $cpuTestStopDiagnostic.commands[-1].result.exit_code -eq 0) 'Zero-exit stderr failure was treated as confirmed cleanup.'
    Check-CpuProfile ((Get-Content -LiteralPath $cpuTestStopDiagnostic.commands[-1].result.stderr_path -Raw).Contains('E_ACCESSDENIED')) 'The raw collector failure diagnostic was lost.'
    $cpuTestNoArchive = Run-CpuProfileCase 'missing_archive' $false
    Check-CpuProfile ($cpuTestNoArchive.trace.bytes -eq 0 -and $cpuTestNoArchive.classification -eq 'failed') 'Missing archive was treated as a capture.'
    $cpuTestStopPoll = Run-CpuProfileCase 'stop_poll_failure' $false
    Check-CpuProfile ($cpuTestStopPoll.own_session_stop_confirmed -and $cpuTestStopPoll.trace.bytes -gt 0) 'Polling failure prevented session cleanup.'
    Run-CpuProfileCase 'bad_duration' $false @{DurationSeconds=31} | Out-Null
    Check-CpuProfile ($script:cpuTestCommands.Count -eq 0) 'Invalid duration launched a collector.'
    Run-CpuProfileCase 'outside_work' $false @{OutputDirectory=(Join-Path $cpuTestRepo 'profile-test-outside-work')} | Out-Null
    Check-CpuProfile ($script:cpuTestCommands.Count -eq 0) 'Evidence outside work was accepted.'
    Run-CpuProfileCase 'existing_output' $false @{OutputDirectory=$cpuTestRoot} | Out-Null
    Check-CpuProfile ($script:cpuTestCommands.Count -eq 0) 'Existing evidence was overwritten.'
    Write-Host ('CPU profiler lifecycle: {0} checks passed; no native collector or game launched.' -f $script:cpuTestChecks)
} finally { $cpuTestProcess.Dispose() }
