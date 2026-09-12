# Counter timing only. This helper never launches a runtime or changes pass criteria.
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
        $valid.Add([pscustomobject]@{ time=$time; frames=$frames; presents=$presents; speed=$(if ($hasSpeed) { $speed } else { $null }) })
    }
    $windows = @()
    foreach ($requested in @(5,10)) {
        $window = [ordered]@{
            requested_seconds=$requested; full_window_available=$false; sample_count=0;
            start_seconds=$null; end_seconds=$null; elapsed_seconds=$null;
            frame_advances=$null; present_advances=$null;
            frame_advances_per_second=$null; present_advances_per_second=$null;
            reported_speed_first=$null; reported_speed_last=$null;
            reported_speed_min=$null; reported_speed_max=$null
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
        }
        $windows += [pscustomobject]$window
    }
    [pscustomobject]@{
        scope='Observed headless frame/present counter deltas over actual time; not monitor FPS or audio-output proof.';
        selection='Final contiguous booted/running segment before stop; invalid and non-increasing timestamp samples excluded; counter reset starts a new segment.';
        observation_end_seconds=$ObservationEndSeconds; active_samples=$valid.Count;
        invalid_samples=$invalid; counter_resets=$resets; windows=$windows
    }
}
