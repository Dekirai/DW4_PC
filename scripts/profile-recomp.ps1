# Dot-source this file from the startup harness. It never starts a game process.
function Get-RecompCpuProfileOwnerObservation {
    param([Diagnostics.Process]$OwnedProcess)
    $OwnedProcess.Refresh()
    if ($OwnedProcess.HasExited) { throw 'The retained game process has exited.' }
    [pscustomobject]@{
        process_id=$OwnedProcess.Id
        start_time_utc_ticks=$OwnedProcess.StartTime.ToUniversalTime().Ticks
        launched_executable=$OwnedProcess.StartInfo.FileName
        loaded_executable=$OwnedProcess.MainModule.FileName
    }
}

function Assert-RecompCpuProfileOwner {
    param([Diagnostics.Process]$OwnedProcess, [long]$ExpectedStartTimeUtcTicks,
          [string]$ExpectedExecutablePath, [int]$ExpectedProcessId = 0)
    $cpuProfileOwner = Get-RecompCpuProfileOwnerObservation $OwnedProcess
    if ($cpuProfileOwner.process_id -le 0 -or
        ($ExpectedProcessId -ne 0 -and $cpuProfileOwner.process_id -ne $ExpectedProcessId) -or
        $cpuProfileOwner.start_time_utc_ticks -ne $ExpectedStartTimeUtcTicks) {
        throw 'The retained game process does not match the expected PID/start time.'
    }
    foreach ($cpuProfileObservedPath in @($cpuProfileOwner.launched_executable,$cpuProfileOwner.loaded_executable)) {
        if ([string]::IsNullOrWhiteSpace($cpuProfileObservedPath) -or
            -not [string]::Equals([IO.Path]::GetFullPath($cpuProfileObservedPath),
                $ExpectedExecutablePath,[StringComparison]::OrdinalIgnoreCase)) {
            throw 'The retained process launch path or loaded executable differs from the expected binary.'
        }
    }
    return $cpuProfileOwner
}

function Get-RecompCpuProfileElapsed {
    param([Diagnostics.Stopwatch]$Clock)
    return $Clock.Elapsed.TotalSeconds
}

function Wait-RecompCpuProfilePoll {
    param([int]$Milliseconds)
    [Threading.Thread]::Sleep($Milliseconds)
}

function ConvertTo-RecompCpuProfileNativeArgument {
    param([string]$Value)
    # Windows CRT quoting, with no shell interpolation.
    $cpuProfileEscaped = [regex]::Replace($Value,'(\\*)"','$1$1\"')
    $cpuProfileEscaped = [regex]::Replace($cpuProfileEscaped,'(\\+)$','$1$1')
    return '"' + $cpuProfileEscaped + '"'
}

function Get-RecompCpuProfilerDiagnosticError {
    param([string]$StandardError)
    # VSDiagnostics can return exit 0 after a COM exception (including a
    # localized E_ACCESSDENIED). Treat any stderr diagnostic as unverified;
    # retain its original text instead of relying on language-specific parsing.
    if (-not [string]::IsNullOrWhiteSpace($StandardError)) {
        return 'The profiler wrote a diagnostic to stderr; inspect the retained raw output even if its exit code is zero.'
    }
    return $null
}

function Invoke-RecompCpuProfilerCommand {
    param([string]$Executable, [string[]]$Arguments, [string]$OutputStem,
          [int]$TimeoutSeconds, [scriptblock]$OnPoll, [switch]$Cleanup)
    $cpuProfileCommandInfo = [Diagnostics.ProcessStartInfo]::new()
    $cpuProfileCommandInfo.FileName = $Executable
    $cpuProfileCommandInfo.Arguments = ($Arguments | ForEach-Object { ConvertTo-RecompCpuProfileNativeArgument $_ }) -join ' '
    $cpuProfileCommandInfo.WorkingDirectory = Split-Path -Parent $Executable
    $cpuProfileCommandInfo.UseShellExecute = $false
    $cpuProfileCommandInfo.CreateNoWindow = $true
    $cpuProfileCommandInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $cpuProfileCommandInfo.RedirectStandardOutput = $true
    $cpuProfileCommandInfo.RedirectStandardError = $true
    $cpuProfileCommandProcess = [Diagnostics.Process]::new()
    $cpuProfileCommandProcess.StartInfo = $cpuProfileCommandInfo
    $cpuProfileCommandClock = [Diagnostics.Stopwatch]::StartNew()
    $cpuProfileCommandStarted = $false
    $cpuProfileCommandTimeout = $false
    $cpuProfileCommandError = $null
    $cpuProfilePollError = $null
    $cpuProfileCommandExit = $null
    $cpuProfileOutTask = $null
    $cpuProfileErrTask = $null
    try {
        $cpuProfileCommandStarted = $cpuProfileCommandProcess.Start()
        if (-not $cpuProfileCommandStarted) { throw 'Could not start the owned profiler command.' }
        $cpuProfileOutTask = $cpuProfileCommandProcess.StandardOutput.ReadToEndAsync()
        $cpuProfileErrTask = $cpuProfileCommandProcess.StandardError.ReadToEndAsync()
        while (-not $cpuProfileCommandProcess.WaitForExit(0)) {
            if ($OnPoll) {
                try { & $OnPoll | Out-Null }
                catch {
                    if (-not $cpuProfilePollError) { $cpuProfilePollError = $_.Exception.Message }
                    if (-not $Cleanup) { throw }
                }
            }
            if ($cpuProfileCommandClock.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
                $cpuProfileCommandTimeout = $true
                throw 'The owned profiler command exceeded its deadline.'
            }
            Wait-RecompCpuProfilePoll 50
        }
        $cpuProfileCommandExit = $cpuProfileCommandProcess.ExitCode
    } catch { $cpuProfileCommandError = $_.Exception.Message }
    finally {
        if ($cpuProfileCommandStarted -and -not $cpuProfileCommandProcess.HasExited) {
            # Kill only this retained command process, never the game, a service,
            # another collector session, or processes selected by image name.
            try {
                $cpuProfileCommandProcess.Kill()
                if (-not $cpuProfileCommandProcess.WaitForExit(1000)) {
                    $cpuProfileCommandError = 'The owned profiler command did not exit after termination.'
                }
            } catch { $cpuProfileCommandError = $_.Exception.Message }
        }
        $cpuProfileDrainClock = [Diagnostics.Stopwatch]::StartNew()
        while ($cpuProfileOutTask -and $cpuProfileErrTask -and
               (-not $cpuProfileOutTask.IsCompleted -or -not $cpuProfileErrTask.IsCompleted) -and
               $cpuProfileDrainClock.Elapsed.TotalSeconds -lt 1) {
            if ($OnPoll) {
                try { & $OnPoll | Out-Null }
                catch { if (-not $cpuProfilePollError) { $cpuProfilePollError = $_.Exception.Message } }
            }
            Wait-RecompCpuProfilePoll 50
        }
        $cpuProfileOutputComplete = $cpuProfileOutTask -and $cpuProfileErrTask -and
            $cpuProfileOutTask.Status -eq [Threading.Tasks.TaskStatus]::RanToCompletion -and
            $cpuProfileErrTask.Status -eq [Threading.Tasks.TaskStatus]::RanToCompletion
        if ($cpuProfileCommandStarted -and -not $cpuProfileOutputComplete -and -not $cpuProfileCommandError) {
            $cpuProfileCommandError = 'Profiler command output did not finish draining within its deadline.'
        }
        $cpuProfileStdout = if ($cpuProfileOutTask -and $cpuProfileOutTask.Status -eq [Threading.Tasks.TaskStatus]::RanToCompletion) { $cpuProfileOutTask.Result } else { '' }
        $cpuProfileStderr = if ($cpuProfileErrTask -and $cpuProfileErrTask.Status -eq [Threading.Tasks.TaskStatus]::RanToCompletion) { $cpuProfileErrTask.Result } else { '' }
        $cpuProfileDiagnosticError = Get-RecompCpuProfilerDiagnosticError $cpuProfileStderr
        if ($cpuProfileDiagnosticError) {
            $cpuProfileCommandError = (($cpuProfileCommandError,$cpuProfileDiagnosticError) | Where-Object { $_ }) -join ' '
        }
        [IO.File]::WriteAllText($OutputStem + '.stdout.txt',$cpuProfileStdout,[Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($OutputStem + '.stderr.txt',$cpuProfileStderr,[Text.UTF8Encoding]::new($false))
        $cpuProfileCommandProcess.Dispose()
    }
    [pscustomobject]@{
        exit_code=$cpuProfileCommandExit; timed_out=$cpuProfileCommandTimeout
        error=$cpuProfileCommandError; poll_error=$cpuProfilePollError
        output_complete=[bool]$cpuProfileOutputComplete
        elapsed_seconds=$cpuProfileCommandClock.Elapsed.TotalSeconds
        stdout_path=($OutputStem + '.stdout.txt'); stderr_path=($OutputStem + '.stderr.txt')
    }
}

function Invoke-RecompCpuProfile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][Diagnostics.Process]$OwnedProcess,
        [Parameter(Mandatory=$true)][ValidateRange(1,[long]::MaxValue)][long]$ExpectedStartTimeUtcTicks,
        [Parameter(Mandatory=$true)][string]$ExpectedExecutablePath,
        [Parameter(Mandatory=$true)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedExecutableSha256,
        [Parameter(Mandatory=$true)][string]$OutputDirectory,
        [ValidateRange(1,30)][int]$DurationSeconds = 15,
        [ValidateRange(1,10)][int]$CommandTimeoutSeconds = 5,
        [scriptblock]$OnPoll,
        [string]$ModulePath = '',
        [string]$ProfilerRoot = 'C:/Program Files/Microsoft Visual Studio/18/Insiders/Team Tools/DiagnosticsHub/Collector'
    )
    $cpuProfileRepoRoot = Split-Path -Parent $PSScriptRoot
    $cpuProfileWorkRoot = [IO.Path]::GetFullPath((Join-Path $cpuProfileRepoRoot 'work')) + [IO.Path]::DirectorySeparatorChar
    $cpuProfileOutput = [IO.Path]::GetFullPath($OutputDirectory)
    if (-not $cpuProfileOutput.StartsWith($cpuProfileWorkRoot,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'CPU profile evidence must use a fresh directory below this repository work directory.'
    }
    if (Test-Path -LiteralPath $cpuProfileOutput) { throw 'CPU profile output already exists; existing evidence will not be overwritten.' }
    $cpuProfileExpectedExe = (Resolve-Path -LiteralPath $ExpectedExecutablePath -ErrorAction Stop).Path
    if (-not (Test-Path -LiteralPath $cpuProfileExpectedExe -PathType Leaf)) { throw 'The expected game executable is not a file.' }
    [IO.Directory]::CreateDirectory($cpuProfileOutput) | Out-Null
    $cpuProfileClock = [Diagnostics.Stopwatch]::StartNew()
    $cpuProfileErrors = [Collections.Generic.List[string]]::new()
    $cpuProfileCommands = [Collections.Generic.List[object]]::new()
    $cpuProfileSession = [Guid]::NewGuid().ToString('D')
    $cpuProfileExecutable = Join-Path $ProfilerRoot 'VSDiagnostics.exe'
    $cpuProfileConfig = Join-Path $ProfilerRoot 'AgentConfigs/CpuUsageLow.json'
    $cpuProfileAgent = Join-Path $ProfilerRoot 'Agents/DiagnosticsHub.CpuAgent.dll'
    $cpuProfileTrace = Join-Path $cpuProfileOutput 'cpu.diagsession'
    $cpuProfileStartAttempted = $false
    $cpuProfileStartCompleted = $null
    $cpuProfileStopRequested = $null
    $cpuProfileStopCompleted = $null
    $cpuProfileObservedDuration = $false
    $cpuProfileStopped = $false
    $cpuProfileRecord = [ordered]@{
        schema_version=1; started_at_utc=[DateTime]::UtcNow.ToString('o')
        scope='CPU sampling targets only the caller-retained game PID. Collector/ETW artifacts may include broader machine metadata; process-exclusive raw capture is not claimed.'
        session_id=$cpuProfileSession; requested_duration_seconds=$DurationSeconds
        command_timeout_seconds=$CommandTimeoutSeconds; output_directory=$cpuProfileOutput
        expected_executable=$cpuProfileExpectedExe; expected_executable_sha256=$ExpectedExecutableSha256.ToUpperInvariant()
        expected_start_time_utc_ticks=$ExpectedStartTimeUtcTicks
        runtime_launched=$false; collector_start_attempted=$false; classification='failed'
        sampling_duration_scope='Sampling may begin/end within the collector start/stop command intervals. Host-observed bounds are recorded separately; exact event-derived sampling duration is not asserted.'
        symbol_scope='Only adjacent EXE/DLL PDB candidates are inspected. Existence does not prove matching PDB GUID/age. Current optimized generated code needs matching symbols/source for precise native attribution.'
        limitations=@('CPU samples are not dispatch counts or off-CPU wait measurements.',
            'Keep neutral input, muted Cubeb, scene evidence and process deadlines in the owning startup harness.',
            'Capture and collector startup can perturb timing; no FPS improvement follows from a successful profile.')
    }
    function Invoke-RecompCpuProfileTrackedCommand([string]$Phase,[string[]]$Arguments,[switch]$Cleanup) {
        $cpuProfileCommandBegin = Get-RecompCpuProfileElapsed $cpuProfileClock
        $cpuProfileCommandStem = Join-Path $cpuProfileOutput ('{0:D2}-{1}' -f $cpuProfileCommands.Count,$Phase)
        $cpuProfileCommandResult = Invoke-RecompCpuProfilerCommand -Executable $cpuProfileExecutable -Arguments $Arguments `
            -OutputStem $cpuProfileCommandStem -TimeoutSeconds $CommandTimeoutSeconds -OnPoll $OnPoll -Cleanup:$Cleanup
        $cpuProfileCommandEnd = Get-RecompCpuProfileElapsed $cpuProfileClock
        $cpuProfileCommandEntry = [ordered]@{
            phase=$Phase; arguments=$Arguments; begin_seconds=$cpuProfileCommandBegin
            end_seconds=$cpuProfileCommandEnd; result=$cpuProfileCommandResult
        }
        $cpuProfileCommands.Add($cpuProfileCommandEntry)
        if ($cpuProfileCommandResult.error -or $cpuProfileCommandResult.timed_out -or $cpuProfileCommandResult.exit_code -ne 0) {
            throw ('Profiler {0} failed: exit={1}, timeout={2}, error={3}' -f $Phase,
                $cpuProfileCommandResult.exit_code,$cpuProfileCommandResult.timed_out,$cpuProfileCommandResult.error)
        }
        if ($cpuProfileCommandResult.poll_error) { $cpuProfileErrors.Add('Polling callback: ' + $cpuProfileCommandResult.poll_error) }
        return $cpuProfileCommandEntry
    }
    try {
        $cpuProfileOwner = Assert-RecompCpuProfileOwner $OwnedProcess $ExpectedStartTimeUtcTicks $cpuProfileExpectedExe
        $cpuProfileRecord['owner_before_attach'] = $cpuProfileOwner
        $cpuProfileActualHash = (Get-FileHash -LiteralPath $cpuProfileExpectedExe -Algorithm SHA256 -ErrorAction Stop).Hash
        $cpuProfileRecord['actual_executable_sha256'] = $cpuProfileActualHash
        if ($cpuProfileActualHash -ne $ExpectedExecutableSha256) { throw 'The live executable file hash does not match the expected immutable binary.' }
        foreach ($cpuProfileToolPath in @($cpuProfileExecutable,$cpuProfileConfig,$cpuProfileAgent)) {
            if (-not (Test-Path -LiteralPath $cpuProfileToolPath -PathType Leaf)) { throw ('Required profiler file is absent: ' + $cpuProfileToolPath) }
        }
        $cpuProfileParsedConfig = Get-Content -LiteralPath $cpuProfileConfig -Raw | ConvertFrom-Json
        if (@($cpuProfileParsedConfig.Agents).Count -ne 1 -or
            $cpuProfileParsedConfig.Agents[0].CLSID -ne '4EA90761-2248-496C-B854-3C0399A591A4' -or
            $cpuProfileParsedConfig.Agents[0].Name -ne 'DiagnosticsHub.CpuAgent.dll' -or
            @($cpuProfileParsedConfig.Agents[0].Config.PSObject.Properties).Count -ne 1 -or
            $cpuProfileParsedConfig.Agents[0].Config.cpuSampleRate -ne 100) {
            throw 'The installed CPU configuration is not the expected low-rate CPU-only agent without child-process attachment.'
        }
        $cpuProfileRecord['tools'] = @($cpuProfileExecutable,$cpuProfileConfig,$cpuProfileAgent) | ForEach-Object {
            [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash;file_version=(Get-Item -LiteralPath $_).VersionInfo.FileVersion}
        }
        [IO.File]::Copy($cpuProfileConfig,(Join-Path $cpuProfileOutput 'CpuUsageLow.json'))
        $cpuProfileSymbolFiles = @([IO.Path]::ChangeExtension($cpuProfileExpectedExe,'.pdb'))
        if ($ModulePath) {
            $cpuProfileModule = (Resolve-Path -LiteralPath $ModulePath -ErrorAction Stop).Path
            $cpuProfileRecord['module'] = [ordered]@{path=$cpuProfileModule;sha256=(Get-FileHash -LiteralPath $cpuProfileModule -Algorithm SHA256).Hash}
            $cpuProfileSymbolFiles += [IO.Path]::ChangeExtension($cpuProfileModule,'.pdb')
        }
        $cpuProfileRecord['adjacent_pdb_candidates'] = @($cpuProfileSymbolFiles | ForEach-Object { [ordered]@{path=$_;exists=(Test-Path -LiteralPath $_ -PathType Leaf)} })
        Invoke-RecompCpuProfileTrackedCommand 'help' @('help') | Out-Null
        # Recheck after metadata/help latency before the first attach attempt.
        Assert-RecompCpuProfileOwner $OwnedProcess $ExpectedStartTimeUtcTicks $cpuProfileExpectedExe $cpuProfileOwner.process_id | Out-Null
        if ((Get-FileHash -LiteralPath $cpuProfileExpectedExe -Algorithm SHA256).Hash -ne $ExpectedExecutableSha256) { throw 'Executable changed before profiler attachment.' }
        $cpuProfileStartAttempted = $true
        $cpuProfileRecord.collector_start_attempted = $true
        $cpuProfileStart = Invoke-RecompCpuProfileTrackedCommand 'start' @('start',$cpuProfileSession,
            ('/attach:' + $cpuProfileOwner.process_id),('/loadConfig:' + $cpuProfileConfig),
            ('/scratchLocation:' + $cpuProfileOutput),('/symbolCachePath:' + (Join-Path $cpuProfileOutput 'symbols')))
        $cpuProfileStartCompleted = [double]$cpuProfileStart.end_seconds
        Assert-RecompCpuProfileOwner $OwnedProcess $ExpectedStartTimeUtcTicks $cpuProfileExpectedExe $cpuProfileOwner.process_id | Out-Null
        Invoke-RecompCpuProfileTrackedCommand 'lifetime' @('update',$cpuProfileSession,('/lifetimeProcess:' + $cpuProfileOwner.process_id)) | Out-Null
        while ((Get-RecompCpuProfileElapsed $cpuProfileClock) - $cpuProfileStartCompleted -lt $DurationSeconds) {
            Assert-RecompCpuProfileOwner $OwnedProcess $ExpectedStartTimeUtcTicks $cpuProfileExpectedExe $cpuProfileOwner.process_id | Out-Null
            if ($OnPoll) { & $OnPoll | Out-Null }
            Wait-RecompCpuProfilePoll 50
        }
        $cpuProfileRecord['owner_after_observation'] = Assert-RecompCpuProfileOwner $OwnedProcess $ExpectedStartTimeUtcTicks $cpuProfileExpectedExe $cpuProfileOwner.process_id
        $cpuProfileRecord['executable_sha256_after_observation'] = (Get-FileHash -LiteralPath $cpuProfileExpectedExe -Algorithm SHA256).Hash
        if ($cpuProfileRecord.executable_sha256_after_observation -ne $ExpectedExecutableSha256) { throw 'Executable file changed during the profiling observation.' }
        $cpuProfileObservedDuration = $true
    } catch { $cpuProfileErrors.Add($_.Exception.Message) }
    finally {
        if ($cpuProfileStartAttempted) {
            # Even an unsuccessful/timed-out start may have created this GUID.
            # Never cancel or stop another profiler's global session.
            $cpuProfileStopRequested = Get-RecompCpuProfileElapsed $cpuProfileClock
            try {
                $cpuProfileStop = Invoke-RecompCpuProfileTrackedCommand 'stop' @('stop',$cpuProfileSession,('/output:' + $cpuProfileTrace)) -Cleanup
                $cpuProfileStopCompleted = [double]$cpuProfileStop.end_seconds
                $cpuProfileStopped = $true
            } catch { $cpuProfileErrors.Add('Owned session cleanup: ' + $_.Exception.Message) }
        }
        $cpuProfileTraceExists = Test-Path -LiteralPath $cpuProfileTrace -PathType Leaf
        $cpuProfileTraceSize = if ($cpuProfileTraceExists) { (Get-Item -LiteralPath $cpuProfileTrace).Length } else { 0 }
        if ($cpuProfileStartAttempted -and (-not $cpuProfileStopped -or $cpuProfileTraceSize -le 0)) {
            $cpuProfileErrors.Add('A clean stop with a nonempty diagnostic archive was not confirmed; inspect raw command outputs.')
        }
        $cpuProfileRecord['observation_completed'] = $cpuProfileObservedDuration
        $cpuProfileRecord['own_session_stop_confirmed'] = $cpuProfileStopped
        $cpuProfileRecord['start_command_completed_seconds'] = $cpuProfileStartCompleted
        $cpuProfileRecord['stop_command_requested_seconds'] = $cpuProfileStopRequested
        $cpuProfileRecord['stop_command_completed_seconds'] = $cpuProfileStopCompleted
        $cpuProfileRecord['host_observed_attached_seconds'] = if ($null -ne $cpuProfileStartCompleted -and $null -ne $cpuProfileStopRequested) { $cpuProfileStopRequested - $cpuProfileStartCompleted } else { $null }
        $cpuProfileRecord['commands'] = @($cpuProfileCommands.ToArray())
        $cpuProfileRecord['trace'] = [ordered]@{path=$cpuProfileTrace;exists=$cpuProfileTraceExists;bytes=$cpuProfileTraceSize}
        $cpuProfileRecord['errors'] = @($cpuProfileErrors.ToArray())
        $cpuProfileRecord['finished_at_utc'] = [DateTime]::UtcNow.ToString('o')
        $cpuProfileRecord['elapsed_seconds'] = Get-RecompCpuProfileElapsed $cpuProfileClock
        if ($cpuProfileObservedDuration -and $cpuProfileStopped -and $cpuProfileTraceSize -gt 0 -and $cpuProfileErrors.Count -eq 0) { $cpuProfileRecord.classification = 'completed' }
        $cpuProfileRecord | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $cpuProfileOutput 'summary.json') -Encoding utf8
    }
    if ($cpuProfileRecord.classification -ne 'completed') {
        throw ('CPU profile failed; evidence: ' + (Join-Path $cpuProfileOutput 'summary.json') + '. ' + ($cpuProfileErrors -join ' '))
    }
    return [pscustomobject]$cpuProfileRecord
}
