# Owned-process sampling and counter timing only; no launches or pass criteria.
function Get-RecompProcessSample {
    param([Diagnostics.Process]$Process, [Diagnostics.Stopwatch]$Clock)
    $begin = $Clock.Elapsed.TotalSeconds
    $cpu = $null
    $observed = $null
    $working = $null
    $peak = $null
    $private = $null
    $errorText = $null
    try {
        $Process.Refresh()
        $cpu = $Process.TotalProcessorTime.TotalSeconds
        $observed = $Clock.Elapsed.TotalSeconds
        $working = $Process.WorkingSet64
        $peak = $Process.PeakWorkingSet64
        $private = $Process.PrivateMemorySize64
    } catch { $errorText = $_.Exception.Message }
    [pscustomobject]@{
        cpu_observed_seconds=$observed; total_cpu_seconds=$cpu;
        working_set_bytes=$working; peak_working_set_bytes=$peak; private_bytes=$private;
        measurement_begin_seconds=$begin; measurement_end_seconds=$Clock.Elapsed.TotalSeconds;
        error=$errorText
    }
}

function Get-RecompStartupFrameMetrics {
    param([object[]]$Samples, [double]$ObservationEndSeconds)
    if ([double]::IsNaN($ObservationEndSeconds) -or [double]::IsInfinity($ObservationEndSeconds) -or $ObservationEndSeconds -lt 0) {
        throw 'The observation end must be a finite nonnegative elapsed time.'
    }
    $culture = [Globalization.CultureInfo]::InvariantCulture
    $valid = [Collections.Generic.List[object]]::new()
    $invalid = 0
    $lastTime = -1.0
    $resets = 0
    foreach ($sample in $Samples) {
        $time = 0.0
        if ($null -eq $sample -or -not $sample.PSObject.Properties['elapsed_seconds'] -or
            -not [double]::TryParse([string]$sample.elapsed_seconds,[Globalization.NumberStyles]::Float,$culture,[ref]$time) -or
            [double]::IsNaN($time) -or [double]::IsInfinity($time) -or $time -lt 0) { ++$invalid; continue }
        if ($time -gt $ObservationEndSeconds) { continue }
        if ($time -le $lastTime) { ++$invalid; continue }
        $lastTime = $time
        if (-not $sample.PSObject.Properties['status'] -or $null -eq $sample.status) { ++$invalid; continue }
        $status = $sample.status
        if (-not $status.PSObject.Properties['booted'] -or -not $status.PSObject.Properties['state']) { ++$invalid; continue }
        if ($status.booted -ne '1' -or $status.state -ne 'running') { $valid.Clear(); continue }
        [uint64]$frames = 0
        [uint64]$presents = 0
        if (-not $status.PSObject.Properties['frame_count'] -or -not $status.PSObject.Properties['present_count'] -or
            -not [uint64]::TryParse([string]$status.frame_count,[Globalization.NumberStyles]::Integer,$culture,[ref]$frames) -or
            -not [uint64]::TryParse([string]$status.present_count,[Globalization.NumberStyles]::Integer,$culture,[ref]$presents)) { ++$invalid; continue }
        $speed = 0.0
        $hasSpeed = $status.PSObject.Properties['speed'] -and
            [double]::TryParse([string]$status.speed,[Globalization.NumberStyles]::Float,$culture,[ref]$speed) -and
            -not [double]::IsNaN($speed) -and -not [double]::IsInfinity($speed) -and $speed -ge 0
        if ($valid.Count -and ($frames -lt $valid[$valid.Count-1].frames -or $presents -lt $valid[$valid.Count-1].presents)) {
            $valid.Clear()
            ++$resets
        }
        $cpu = $null
        $cpuTime = $null
        if ($sample.PSObject.Properties['process'] -and $null -ne $sample.process) {
            $process = $sample.process
            $parsedCpu = 0.0
            $parsedTime = 0.0
            if ($process.PSObject.Properties['total_cpu_seconds'] -and $process.PSObject.Properties['cpu_observed_seconds'] -and
                [double]::TryParse([string]$process.total_cpu_seconds,[Globalization.NumberStyles]::Float,$culture,[ref]$parsedCpu) -and
                [double]::TryParse([string]$process.cpu_observed_seconds,[Globalization.NumberStyles]::Float,$culture,[ref]$parsedTime) -and
                -not [double]::IsNaN($parsedCpu) -and -not [double]::IsInfinity($parsedCpu) -and $parsedCpu -ge 0 -and
                -not [double]::IsNaN($parsedTime) -and -not [double]::IsInfinity($parsedTime) -and $parsedTime -ge 0 -and
                $parsedTime -le $ObservationEndSeconds) {
                $cpu = $parsedCpu
                $cpuTime = $parsedTime
            }
        }
        $valid.Add([pscustomobject]@{
            time=$time; frames=$frames; presents=$presents;
            speed=$(if ($hasSpeed) { $speed } else { $null }); cpu=$cpu; cpu_time=$cpuTime
        })
    }
    $windows = @()
    foreach ($requested in @(5,10)) {
        $window = [ordered]@{
            requested_seconds=$requested; full_window_available=$false; sample_count=0;
            start_seconds=$null; end_seconds=$null; elapsed_seconds=$null;
            frame_advances=$null; present_advances=$null;
            frame_advances_per_second=$null; present_advances_per_second=$null;
            reported_speed_first=$null; reported_speed_last=$null;
            reported_speed_min=$null; reported_speed_max=$null;
            process_cpu_available=$false; process_cpu_unavailable_reason='Insufficient active observations.';
            process_cpu_start_seconds=$null; process_cpu_end_seconds=$null;
            process_cpu_observation_seconds=$null; process_cpu_seconds=$null;
            process_average_busy_cores=$null; process_cpu_milliseconds_per_frame=$null
        }
        if ($valid.Count) {
            $end = $valid[$valid.Count-1]
            $beginIndex = 0
            for ($index=0; $index -lt $valid.Count; ++$index) {
                if ($valid[$index].time -le $end.time-$requested) { $beginIndex = $index; $window.full_window_available = $true }
            }
            $begin = $valid[$beginIndex]
            $elapsed = $end.time-$begin.time
            $window.start_seconds = $begin.time
            $window.end_seconds = $end.time
            $window.elapsed_seconds = $elapsed
            $window.sample_count = $valid.Count-$beginIndex
            $window.frame_advances = $end.frames-$begin.frames
            $window.present_advances = $end.presents-$begin.presents
            if ($elapsed -gt 0) {
                $window.frame_advances_per_second = $window.frame_advances/$elapsed
                $window.present_advances_per_second = $window.present_advances/$elapsed
            }
            $window.reported_speed_first = $begin.speed
            $window.reported_speed_last = $end.speed
            $speeds = @($valid.GetRange($beginIndex,$valid.Count-$beginIndex) | ForEach-Object { if ($null -ne $_.speed) { $_.speed } })
            if ($speeds.Count) {
                $measured = $speeds | Measure-Object -Minimum -Maximum
                $window.reported_speed_min = $measured.Minimum
                $window.reported_speed_max = $measured.Maximum
            }
            $cpuValid = $true
            $priorCpu = -1.0
            $priorCpuTime = -1.0
            foreach ($entry in $valid.GetRange($beginIndex,$valid.Count-$beginIndex)) {
                if ($null -eq $entry.cpu -or $null -eq $entry.cpu_time) {
                    $cpuValid = $false
                    $window.process_cpu_unavailable_reason = 'Missing or invalid process CPU observation in this window.'
                    break
                }
                if ($entry.cpu -lt $priorCpu -or $entry.cpu_time -le $priorCpuTime) {
                    $cpuValid = $false
                    $window.process_cpu_unavailable_reason = 'Nonmonotonic process CPU counter or observation timestamp.'
                    break
                }
                $priorCpu = $entry.cpu
                $priorCpuTime = $entry.cpu_time
            }
            if ($cpuValid -and $window.sample_count -ge 2 -and $end.cpu_time -gt $begin.cpu_time) {
                $window.process_cpu_available = $true
                $window.process_cpu_unavailable_reason = $null
                $window.process_cpu_start_seconds = $begin.cpu_time
                $window.process_cpu_end_seconds = $end.cpu_time
                $window.process_cpu_observation_seconds = $end.cpu_time-$begin.cpu_time
                $window.process_cpu_seconds = $end.cpu-$begin.cpu
                $window.process_average_busy_cores = $window.process_cpu_seconds/$window.process_cpu_observation_seconds
                if ($window.frame_advances -gt 0) {
                    $window.process_cpu_milliseconds_per_frame = 1000*$window.process_cpu_seconds/$window.frame_advances
                }
            }
        }
        $windows += [pscustomobject]$window
    }
    [pscustomobject]@{
        scope='Observed headless frame/present counter deltas over actual time; not monitor FPS or audio-output proof.';
        process_cpu_scope='Owned runtime process user+kernel CPU time across all its threads; average busy cores=CPU seconds/actual observation seconds, not machine-wide percent. Frame counters and CPU reads are nearby observations, not an atomic renderer sample.';
        selection='Final contiguous booted/running segment before stop; invalid and non-increasing timestamp samples excluded; counter reset starts a new segment.';
        observation_end_seconds=$ObservationEndSeconds; active_samples=$valid.Count;
        latest_active_sample_age_seconds=$(if ($valid.Count) { $ObservationEndSeconds-$valid[$valid.Count-1].time } else { $null });
        invalid_samples=$invalid; counter_resets=$resets; windows=$windows
    }
}
