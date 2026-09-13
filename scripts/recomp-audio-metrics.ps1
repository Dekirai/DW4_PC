# Analyze preserved status observations only. This file never starts a process.
function Get-RecompAudioMetrics {
    param([object[]]$Samples, [double]$ObservationEndSeconds)
    if ([double]::IsNaN($ObservationEndSeconds) -or [double]::IsInfinity($ObservationEndSeconds) -or $ObservationEndSeconds -lt 0) {
        throw 'The observation end must be a finite nonnegative elapsed time.'
    }
    $culture = [Globalization.CultureInfo]::InvariantCulture
    function Field($Object, [string]$Name) {
        if ($null -eq $Object) { return $null }
        if ($Object -is [Collections.IDictionary]) { return $Object[$Name] }
        if ($Object.PSObject.Properties[$Name]) { return $Object.$Name }
        return $null
    }
    function Unsigned($Value) {
        [uint64]$parsed = 0
        if ($null -ne $Value -and [uint64]::TryParse([string]$Value,[Globalization.NumberStyles]::Integer,$culture,[ref]$parsed)) { return $parsed }
        return $null
    }
    function Finite($Value) {
        $parsed = 0.0
        if ($null -ne $Value -and [double]::TryParse([string]$Value,[Globalization.NumberStyles]::Float,$culture,[ref]$parsed) -and
            -not [double]::IsNaN($parsed) -and -not [double]::IsInfinity($parsed) -and $parsed -ge 0) { return $parsed }
        return $null
    }
    $deviceCounters = @('callbacks','callback_frames','nonzero_output_frames','callback_interval_count',
        'callback_interval_sum_ns','callback_work_sum_ns','callback_gaps_over_two_periods',
        'callback_work_over_period','state_started','state_stopped','state_drained','state_errors','operation_errors')
    $sourceCounters = @('push_calls','pushed_frames','pushed_nonzero_frames','accepted_frames','enqueued_granules',
        'queue_full_dropped_granules','queue_trimmed_granules','active_empty_events','active_replay_events',
        'active_fade_events','active_mixed_output_frames','active_faded_output_frames')
    $counterFields = @($deviceCounters | ForEach-Object { 'audio_device_' + $_ })
    foreach ($source in @('dma','dtk')) { $counterFields += @($sourceCounters | ForEach-Object { "audio_${source}_" + $_ }) }
    $unsignedGauges = @('audio_mixer_output_sample_rate','audio_activity_window_ns','audio_device_sample_rate',
        'audio_device_channels','audio_device_sample_bits','audio_device_callback_interval_max_ns','audio_device_callback_work_max_ns')
    $boolFields = @('audio_headless_device','audio_muted','audio_device_init_complete','audio_device_init_succeeded',
        'audio_device_start_complete','audio_device_start_succeeded','audio_device_running_requested',
        'audio_device_volume_apply_complete','audio_device_volume_apply_succeeded')
    foreach ($source in @('dma','dtk')) {
        $unsignedGauges += @("audio_${source}_last_push_age_ns","audio_${source}_sample_rate_dividend","audio_${source}_sample_rate_divisor")
        $boolFields += "audio_${source}_recently_active"
    }
    $active = [Collections.Generic.List[object]]::new()
    $lastTime = -1.0
    $invalid = 0
    $resets = 0
    $generationChanges = 0
    $formatChanges = 0
    $audioUnavailable = 0
    foreach ($sample in $Samples) {
        $time = Finite (Field $sample 'elapsed_seconds')
        if ($null -eq $time) { ++$invalid; $active.Clear(); continue }
        if ($time -gt $ObservationEndSeconds) { continue }
        if ($time -le $lastTime) { ++$invalid; $active.Clear(); continue }
        $lastTime = $time
        $status = Field $sample 'status'
        if ($null -eq $status) { ++$invalid; $active.Clear(); continue }
        if ((Field $status 'booted') -ne '1' -or (Field $status 'state') -ne 'running') { $active.Clear(); continue }
        $entry = [pscustomobject]@{
            time=$time; max_speed=(Finite (Field $status 'max_speed')); generation=$null;
            counters=[ordered]@{}; context=[ordered]@{}; audio_error=$null;
            frame=(Unsigned (Field $status 'frame_count')); present=(Unsigned (Field $status 'present_count'));
            status_error=(Field $status 'last_error')
        }
        if ((Field $status 'audio_available') -ne '1') { $entry.audio_error = 'Audio telemetry is missing or unavailable.' }
        elseif ((Field $status 'audio_stream_alive') -ne '1') { $entry.audio_error = 'The audio stream is retired or its lifetime is unavailable.' }
        else {
            $entry.generation = Unsigned (Field $status 'audio_generation')
            if ($null -eq $entry.generation -or $entry.generation -eq 0) { $entry.audio_error = 'Missing or invalid audio generation.' }
            foreach ($name in $counterFields) {
                $value = Unsigned (Field $status $name)
                $entry.counters[$name] = $value
                if ($null -eq $value) { $entry.audio_error = "Missing or invalid audio counter: $name." }
            }
            foreach ($name in $unsignedGauges) {
                $value = Unsigned (Field $status $name)
                $entry.context[$name] = $value
                if ($null -eq $value) { $entry.audio_error = "Missing or invalid audio context: $name." }
            }
            foreach ($name in $boolFields) {
                $value = Field $status $name
                $entry.context[$name] = $value
                if ([string]$value -cnotin @('0','1')) { $entry.audio_error = "Missing or invalid audio flag: $name." }
            }
            $backend = Field $status 'audio_device_backend'
            $entry.context['audio_device_backend'] = $backend
            if ($backend -cnotin @('Cubeb','Null','Unknown')) { $entry.audio_error = 'Missing or unknown device telemetry backend.' }
            $entry.context['audio_requested_backend'] = Field $status 'audio_requested_backend'
            foreach ($name in @('audio_device_requested_volume_percent','audio_device_last_state')) {
                [int]$signedContext = 0
                if (-not [int]::TryParse([string](Field $status $name),[Globalization.NumberStyles]::Integer,$culture,[ref]$signedContext)) {
                    $entry.audio_error = "Missing or invalid audio context: $name."
                }
                $entry.context[$name] = $signedContext
            }
        }
        if ($entry.audio_error) { ++$audioUnavailable }
        if ($active.Count) {
            $previous = $active[$active.Count-1]
            $reset = ($null -ne $entry.frame -and $null -ne $previous.frame -and $entry.frame -lt $previous.frame) -or
                ($null -ne $entry.present -and $null -ne $previous.present -and $entry.present -lt $previous.present)
            if ($null -ne $entry.generation -and $null -ne $previous.generation -and $entry.generation -ne $previous.generation) {
                ++$generationChanges
                $active.Clear()
            } elseif (-not $entry.audio_error -and -not $previous.audio_error) {
                foreach ($name in $counterFields) { if ($entry.counters[$name] -lt $previous.counters[$name]) { $reset = $true } }
                foreach ($name in @('audio_device_backend','audio_device_sample_rate','audio_device_channels','audio_device_sample_bits','audio_mixer_output_sample_rate')) {
                    if ($entry.context[$name] -ne $previous.context[$name]) { ++$formatChanges; $active.Clear(); break }
                }
            }
            if ($reset) { ++$resets; $active.Clear() }
        }
        $active.Add($entry)
    }
    $windows = @()
    foreach ($requested in @(5,10)) {
        $window = [ordered]@{
            requested_seconds=$requested; full_window_available=$false; sample_count=0;
            start_seconds=$null; end_seconds=$null; elapsed_seconds=$null;
            max_speed_available=$false; max_speed_valid_samples=0; max_speed_first=$null; max_speed_last=$null;
            max_speed_min=$null; max_speed_max=$null;
            audio_available=$false; audio_unavailable_reason='Insufficient active observations.';
            audio_generation=$null; audio_context_first=$null; audio_context_last=$null;
            counter_deltas=$null; counter_rates_per_second=$null;
            device_output_observed=$false; device_output_unavailable_reason='No complete audio window.';
            device_error_totals=$null; device_errors_in_window=$null;
            callback_interval_mean_ms=$null; callback_work_mean_ms=$null;
            nonzero_source_measurement_available=$false;
            dma_nonzero_source_activity=$null; dtk_nonzero_source_activity=$null;
            status_errors=@()
        }
        if ($active.Count) {
            $end = $active[$active.Count-1]
            $beginIndex = 0
            for ($i=0; $i -lt $active.Count; ++$i) {
                if ($active[$i].time -le $end.time-$requested) { $beginIndex=$i; $window.full_window_available=$true }
            }
            $selected = $active.GetRange($beginIndex,$active.Count-$beginIndex)
            $begin = $selected[0]
            $window.sample_count = $selected.Count
            $window.start_seconds = $begin.time
            $window.end_seconds = $end.time
            $window.elapsed_seconds = $end.time-$begin.time
            $speeds = @($selected | ForEach-Object { if ($null -ne $_.max_speed) { $_.max_speed } })
            $window.max_speed_valid_samples = $speeds.Count
            if ($speeds.Count -eq $selected.Count) {
                $window.max_speed_available = $true
                $window.max_speed_first = $begin.max_speed
                $window.max_speed_last = $end.max_speed
                $stats = $speeds | Measure-Object -Minimum -Maximum
                $window.max_speed_min = $stats.Minimum
                $window.max_speed_max = $stats.Maximum
            }
            $window.status_errors = @($selected | ForEach-Object { if ($_.status_error) { [string]$_.status_error } } | Select-Object -Unique)
            $errors = @($selected | ForEach-Object { if ($_.audio_error) { $_.audio_error } } | Select-Object -Unique)
            if ($errors.Count) { $window.audio_unavailable_reason = $errors -join ' ' }
            elseif ($selected.Count -ge 2 -and $window.elapsed_seconds -gt 0) {
                $window.audio_available = $true
                $window.audio_unavailable_reason = $null
                $window.audio_generation = $end.generation
                $window.audio_context_first = [pscustomobject]$begin.context
                $window.audio_context_last = [pscustomobject]$end.context
                $deltas = [ordered]@{}
                $rates = [ordered]@{}
                foreach ($name in $counterFields) {
                    # Decimal subtraction retains integer precision beyond double's 2^53 boundary.
                    $delta = [uint64]([decimal]$end.counters[$name]-[decimal]$begin.counters[$name])
                    $deltas[$name] = $delta
                    $rates[$name] = [double]$delta/$window.elapsed_seconds
                }
                $window.counter_deltas = [pscustomobject]$deltas
                $window.counter_rates_per_second = [pscustomobject]$rates
                # Null output deliberately does not inspect the supplied PCM.
                # Its zero nonzero-frame counter does not establish source silence.
                $window.nonzero_source_measurement_available = @($selected | Where-Object {
                    $_.context['audio_mixer_output_sample_rate'] -eq 0
                }).Count -eq 0
                if ($window.nonzero_source_measurement_available) {
                    $window.dma_nonzero_source_activity = $deltas['audio_dma_pushed_nonzero_frames'] -gt 0
                    $window.dtk_nonzero_source_activity = $deltas['audio_dtk_pushed_nonzero_frames'] -gt 0
                }
                $window.device_error_totals = [pscustomobject]@{
                    state_errors=$end.counters['audio_device_state_errors']; operation_errors=$end.counters['audio_device_operation_errors']
                }
                $window.device_errors_in_window = [pscustomobject]@{
                    state_errors=$deltas['audio_device_state_errors']; operation_errors=$deltas['audio_device_operation_errors']
                }
                if ($deltas['audio_device_callback_interval_count'] -gt 0) {
                    $window.callback_interval_mean_ms = [double]$deltas['audio_device_callback_interval_sum_ns']/$deltas['audio_device_callback_interval_count']/1000000
                }
                if ($deltas['audio_device_callbacks'] -gt 0) {
                    $window.callback_work_mean_ms = [double]$deltas['audio_device_callback_work_sum_ns']/$deltas['audio_device_callbacks']/1000000
                }
                $outputErrors = [Collections.Generic.List[string]]::new()
                foreach ($entry in $selected) {
                    if ($entry.context['audio_device_backend'] -ne 'Cubeb') { $outputErrors.Add('The active telemetry backend is not Cubeb.'); break }
                    if ($entry.context['audio_mixer_output_sample_rate'] -eq 0 -or $entry.context['audio_device_sample_rate'] -eq 0) {
                        $outputErrors.Add('The output sample rate is zero.'); break
                    }
                    if ($entry.context['audio_device_init_complete'] -ne '1' -or $entry.context['audio_device_init_succeeded'] -ne '1' -or
                        $entry.context['audio_device_start_complete'] -ne '1' -or $entry.context['audio_device_start_succeeded'] -ne '1' -or
                        $entry.context['audio_device_running_requested'] -ne '1') {
                        $outputErrors.Add('Successful device initialization and running state were not observed throughout the window.'); break
                    }
                }
                if ($deltas['audio_device_callbacks'] -eq 0 -or $deltas['audio_device_callback_frames'] -eq 0) { $outputErrors.Add('No device callback progress in this window.') }
                $window.device_output_observed = $outputErrors.Count -eq 0
                $window.device_output_unavailable_reason = if ($outputErrors.Count) { $outputErrors -join ' ' } else { $null }
            }
        }
        $windows += [pscustomobject]$window
    }
    [pscustomobject]@{
        schema_version=1;
        scope='Nearby source/mixer/device telemetry observations, not hardware-underrun counts or proof of audible quality. Queue events are gated by recent accepted nonzero source signal; source activity is reported separately.';
        max_speed_scope='Estimated emulation-speed headroom excluding throttle sleep, not actual FPS, presentation support or guaranteed performance.';
        selection='Final contiguous booted/running segment before stop. Invalid timestamps/status, frame/counter resets, audio generation changes and output format changes break the segment. Missing audio observations invalidate an affected window.';
        cumulative_max_scope='callback_interval_max_ns and callback_work_max_ns in audio context are generation-to-date high-water marks, not window maxima or subtractable counters.';
        observation_end_seconds=$ObservationEndSeconds; active_samples=$active.Count;
        latest_active_sample_age_seconds=$(if ($active.Count) { $ObservationEndSeconds-$active[$active.Count-1].time } else { $null });
        invalid_samples=$invalid; audio_unavailable_samples=$audioUnavailable; counter_resets=$resets;
        audio_generation_changes=$generationChanges; output_format_changes=$formatChanges; windows=$windows
    }
}
