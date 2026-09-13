$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts/recomp-audio-metrics.ps1')
$script:checks = 0
function Check($Condition,[string]$Message) { ++$script:checks; if (-not $Condition) { throw $Message } }
function Sample([double]$Time,[uint64]$Counter=0,[string]$Generation='1') {
    $status = [ordered]@{
        state='running'; booted='1'; frame_count=([string][int]($Time*60)); present_count=([string][int]($Time*120)); max_speed='1.5'; last_error='';
        audio_available='1'; audio_stream_alive='1'; audio_generation=$Generation; audio_requested_backend='Cubeb';
        audio_headless_device='1'; audio_muted='1'; audio_activity_window_ns='250000000'; audio_mixer_output_sample_rate='48000';
        audio_device_backend='Cubeb'; audio_device_init_complete='1'; audio_device_init_succeeded='1';
        audio_device_start_complete='1'; audio_device_start_succeeded='1'; audio_device_running_requested='1';
        audio_device_volume_apply_complete='1'; audio_device_volume_apply_succeeded='1';
        audio_device_sample_rate='48000'; audio_device_channels='2'; audio_device_sample_bits='16';
        audio_device_requested_volume_percent='0'; audio_device_last_state='0';
        audio_device_callback_interval_max_ns='50000000'; audio_device_callback_work_max_ns='3000000'
    }
    foreach ($name in @('callbacks','callback_frames','nonzero_output_frames','callback_interval_count','callback_interval_sum_ns',
        'callback_work_sum_ns','callback_gaps_over_two_periods','callback_work_over_period','state_started','state_stopped','state_drained',
        'state_errors','operation_errors')) { $status['audio_device_'+$name] = [string]$Counter }
    foreach ($source in @('dma','dtk')) {
        foreach ($name in @('push_calls','pushed_frames','pushed_nonzero_frames','accepted_frames','enqueued_granules',
            'queue_full_dropped_granules','queue_trimmed_granules','active_empty_events','active_replay_events','active_fade_events',
            'active_mixed_output_frames','active_faded_output_frames')) { $status["audio_${source}_$name"] = [string]$Counter }
        $status["audio_${source}_last_push_age_ns"] = '1000000'
        $status["audio_${source}_sample_rate_dividend"] = '108000000'
        $status["audio_${source}_sample_rate_divisor"] = '2250'
        $status["audio_${source}_recently_active"] = '1'
    }
    [pscustomobject]@{ elapsed_seconds=$Time; status=$status }
}
$samples = @((Sample 1 10),(Sample 3.2 32),(Sample 5 50),(Sample 8.6 86),(Sample 11.4 114),(Sample 14 140),(Sample 15.3 153),(Sample 16 9999))
$before = $samples | ConvertTo-Json -Depth 8
$result = Get-RecompAudioMetrics $samples 15.5
$five = $result.windows[0]
Check ($result.active_samples -eq 7 -and $result.latest_active_sample_age_seconds -gt 0.19) 'Post-stop observations entered the active segment.'
Check ($five.full_window_available -and $five.start_seconds -eq 8.6 -and $five.end_seconds -eq 15.3) 'The five-second window used the wrong endpoints.'
Check ([math]::Abs($five.elapsed_seconds-6.7) -lt 0.000001) 'Irregular timestamp duration was replaced by nominal duration.'
Check ($five.audio_available -and $five.counter_deltas.audio_dma_pushed_frames -eq 67) 'Source counter delta differs.'
Check ([math]::Abs($five.counter_rates_per_second.audio_device_callbacks-10) -lt 0.000001) 'Callback rate did not use actual elapsed time.'
Check ($five.device_output_observed -and $five.dma_nonzero_source_activity -and $five.dtk_nonzero_source_activity) 'Device/source activity was lost.'
Check ($five.max_speed_available -and $five.max_speed_first -eq 1.5 -and $five.max_speed_max -eq 1.5) 'Valid headroom observations were lost.'
Check ($five.audio_context_last.audio_device_callback_interval_max_ns -eq 50000000 -and
    -not $five.counter_deltas.PSObject.Properties['audio_device_callback_interval_max_ns']) 'A lifetime high-water mark became a window delta.'
Check ($result.windows[1].start_seconds -eq 5 -and $result.windows[1].full_window_available) 'Ten-second window differs.'
Check (($samples | ConvertTo-Json -Depth 8) -ceq $before) 'Analysis mutated the original status history.'
$jsonSamples = $before | ConvertFrom-Json
$jsonResult = Get-RecompAudioMetrics $jsonSamples 15.5
Check ($jsonResult.windows[0].counter_deltas.audio_dma_pushed_frames -eq 67) 'Deserialized PSObject status differs from dictionary status.'
$quiet = @((Sample 1),(Sample 7))
$quietResult = Get-RecompAudioMetrics $quiet 7
Check ($quietResult.windows[0].audio_available -and $quietResult.windows[0].counter_deltas.audio_dma_pushed_frames -eq 0) 'Valid zero counters were treated as unavailable.'
Check (-not $quietResult.windows[0].dma_nonzero_source_activity -and -not $quietResult.windows[0].device_output_observed) 'Silent or absent callback progress fabricated output.'
Check ($null -eq $quietResult.windows[0].callback_interval_mean_ms) 'Zero interval count fabricated an average.'
$queueOnly = @((Sample 1 0),(Sample 7 20))
$queueOnly[1].status.audio_dma_pushed_nonzero_frames='0'
$queueOnly[1].status.audio_dtk_pushed_nonzero_frames='0'
$queueResult = Get-RecompAudioMetrics $queueOnly 7
Check (-not $queueResult.windows[0].dma_nonzero_source_activity -and $queueResult.windows[0].counter_deltas.audio_dma_active_empty_events -eq 20) 'Queue observations were relabeled as new nonzero source activity.'
$timing = @((Sample 1 0),(Sample 7 0))
$timing[1].status.audio_device_callbacks='3'
$timing[1].status.audio_device_callback_work_sum_ns='6000000'
$timing[1].status.audio_device_callback_interval_count='2'
$timing[1].status.audio_device_callback_interval_sum_ns='20000000'
$timingResult = Get-RecompAudioMetrics $timing 7
Check ($timingResult.windows[0].callback_work_mean_ms -eq 2 -and $timingResult.windows[0].callback_interval_mean_ms -eq 10) 'Callback timing used the wrong units or denominators.'
$nullBackend = @((Sample 1 1),(Sample 7 100))
foreach ($s in $nullBackend) {
    $s.status.audio_device_backend='Null'; $s.status.audio_mixer_output_sample_rate='0'; $s.status.audio_device_sample_rate='0'
    $s.status.audio_dma_pushed_nonzero_frames='0'; $s.status.audio_dtk_pushed_nonzero_frames='0'
    $s.status.audio_dma_accepted_frames='0'; $s.status.audio_dtk_accepted_frames='0'
}
$nullResult = Get-RecompAudioMetrics $nullBackend 7
Check ($nullResult.windows[0].audio_available -and $null -eq $nullResult.windows[0].dma_nonzero_source_activity -and
    -not $nullResult.windows[0].nonzero_source_measurement_available -and -not $nullResult.windows[0].device_output_observed) 'Null backend counters fabricated source-silence knowledge or real device output.'
$missing = @((Sample 1 1),(Sample 4 4),(Sample 7 7))
$missing[1].status.Remove('audio_dma_active_empty_events')
$missingResult = Get-RecompAudioMetrics $missing 7
Check (-not $missingResult.windows[0].audio_available -and $null -eq $missingResult.windows[0].counter_deltas -and $missingResult.windows[0].max_speed_available) 'Missing intermediate audio observation was bridged or destroyed independent headroom.'
$legacy = @([pscustomobject]@{elapsed_seconds=1; status=@{state='running';booted='1';max_speed='2'}},[pscustomobject]@{elapsed_seconds=7;status=@{state='running';booted='1';max_speed='3'}})
$legacyResult = Get-RecompAudioMetrics $legacy 7
Check (-not $legacyResult.windows[0].audio_available -and $legacyResult.windows[0].max_speed_max -eq 3) 'Legacy status fabricated audio or lost available headroom.'
$retired = @((Sample 1 1),(Sample 7 7)); $retired[1].status.audio_stream_alive='0'
Check (-not (Get-RecompAudioMetrics $retired 7).windows[0].audio_available) 'Retired snapshots became an active audio window.'
$unavailable = @((Sample 1 1),(Sample 7 7)); $unavailable[1].status.audio_available='0'
Check (-not (Get-RecompAudioMetrics $unavailable 7).windows[0].audio_available) 'Explicitly unavailable telemetry became zero or stale metrics.'
$generation = Get-RecompAudioMetrics @((Sample 1 1),(Sample 4 4 '2'),(Sample 7 7 '2')) 7
Check ($generation.audio_generation_changes -eq 1 -and $generation.windows[0].start_seconds -eq 4 -and $generation.windows[0].counter_deltas.audio_dma_pushed_frames -eq 3) 'Counter subtraction crossed a stream generation.'
$reset = Get-RecompAudioMetrics @((Sample 1 50),(Sample 4 10),(Sample 7 20)) 7
Check ($reset.counter_resets -eq 1 -and $reset.windows[0].start_seconds -eq 4 -and $reset.windows[0].counter_deltas.audio_device_callbacks -eq 10) 'Counter rollback was hidden by endpoint subtraction.'
$intermediateReset = Get-RecompAudioMetrics @((Sample 1 50),(Sample 4 10),(Sample 7 70)) 7
Check ($intermediateReset.counter_resets -eq 1 -and $intermediateReset.windows[0].counter_deltas.audio_device_callbacks -eq 60) 'Positive endpoints concealed an intermediate reset.'
$format = @((Sample 1 1),(Sample 4 4),(Sample 7 7)); $format[1].status.audio_device_sample_rate='44100'; $format[2].status.audio_device_sample_rate='44100'
Check ((Get-RecompAudioMetrics $format 7).windows[0].start_seconds -eq 4) 'Output format change was bridged.'
$pause = @((Sample 1 1),(Sample 3 3),(Sample 4 4),(Sample 7 7)); $pause[1].status.state='paused'
Check ((Get-RecompAudioMetrics $pause 7).windows[0].start_seconds -eq 4) 'Window crossed a paused segment.'
$timestamp = @((Sample 1 1),(Sample 4 4),(Sample 4 5),(Sample 7 7))
$timestampResult = Get-RecompAudioMetrics $timestamp 7
Check ($timestampResult.invalid_samples -eq 1 -and $timestampResult.active_samples -eq 1 -and -not $timestampResult.windows[0].audio_available) 'Duplicate timestamp was bridged into a fabricated complete window.'
$badTimestamp = @((Sample 1 1),(Sample 4 4),(Sample 7 7)); $badTimestamp[1].elapsed_seconds=[double]::NaN
Check ((Get-RecompAudioMetrics $badTimestamp 7).active_samples -eq 1) 'Nonfinite timestamp did not break the segment.'
$badCounter = @((Sample 1 1),(Sample 7 7)); $badCounter[1].status.audio_device_callbacks='-1'
Check (-not (Get-RecompAudioMetrics $badCounter 7).windows[0].audio_available) 'Negative cumulative counter was accepted.'
$badSpeed = @((Sample 1 1),(Sample 4 4),(Sample 7 7)); $badSpeed[1].status.max_speed='NaN'
$badSpeedResult = Get-RecompAudioMetrics $badSpeed 7
Check ($badSpeedResult.windows[0].audio_available -and -not $badSpeedResult.windows[0].max_speed_available -and $null -eq $badSpeedResult.windows[0].max_speed_min) 'Invalid intermediate headroom fabricated a full-window range or suppressed audio.'
$errors = @((Sample 1 1),(Sample 7 5)); $errors[1].status.last_error='Synthetic command failure'; $errors[1].status.audio_device_start_succeeded='0'
$errorResult = Get-RecompAudioMetrics $errors 7
Check ($errorResult.windows[0].device_error_totals.state_errors -eq 5 -and $errorResult.windows[0].device_errors_in_window.state_errors -eq 4) 'Device errors were hidden or confused with deltas.'
Check (-not $errorResult.windows[0].device_output_observed -and $errorResult.windows[0].status_errors[0] -eq 'Synthetic command failure') 'Failed device startup or status error was lost.'
$large = @((Sample 1 18446744073709551500),(Sample 7 18446744073709551501))
Check ((Get-RecompAudioMetrics $large 7).windows[0].counter_deltas.audio_dma_pushed_frames -eq 1) 'Large uint64 counters lost precision.'
$single = Get-RecompAudioMetrics @((Sample 1 1)) 2
Check (-not $single.windows[0].audio_available -and $null -eq $single.windows[0].counter_rates_per_second -and $single.windows[0].max_speed_available) 'One observation fabricated counter rates or lost its gauge.'
$empty = Get-RecompAudioMetrics @() 2
Check ($empty.active_samples -eq 0 -and $null -eq $empty.windows[0].elapsed_seconds -and -not $empty.windows[0].max_speed_available) 'Empty input fabricated observations.'
$rejected = $false
try { Get-RecompAudioMetrics @() ([double]::PositiveInfinity) | Out-Null } catch { $rejected=$true }
Check $rejected 'Nonfinite observation endpoint was accepted.'
Write-Output "$script:checks audio/headroom metric checks passed. No runtime was launched."
