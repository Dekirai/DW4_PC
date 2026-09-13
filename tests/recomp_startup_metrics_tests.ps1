$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts/recomp-startup-metrics.ps1')
$script:checks = 0
function Check($Condition,[string]$Message) { ++$script:checks; if (-not $Condition) { throw $Message } }
function Sample($Time,$Frames,$Presents,$Speed='1',$State='running') {
    [pscustomobject]@{ elapsed_seconds=$Time; status=[pscustomobject]@{ booted='1'; state=$State; frame_count=$Frames; present_count=$Presents; speed=$Speed } }
}
$samples = @((Sample 1 10 20 0.5),(Sample 3.2 32 64 0.8),(Sample 5 50 100 1.1),(Sample 8.6 86 172 0.9),(Sample 11.4 114 228 1.0),(Sample 14 140 280 1.2),(Sample 15.3 153 306 1.0),(Sample 16 9999 9999 9))
$result = Get-RecompStartupFrameMetrics $samples 15.5
Check ($result.active_samples -eq 7) 'Post-stop samples entered the active selection.'
Check ($result.windows[0].start_seconds -eq 8.6) 'Five-second boundary must use the last observation at or before the threshold.'
Check ([math]::Abs($result.windows[0].elapsed_seconds-6.7) -lt 0.000001) 'Actual irregular timestamp duration was lost.'
Check ($result.windows[0].frame_advances -eq 67) 'Frame counter delta differs.'
Check ([math]::Abs($result.windows[0].frame_advances_per_second-10) -lt 0.000001) 'Frame rate used nominal duration.'
Check ([math]::Abs($result.windows[0].present_advances_per_second-20) -lt 0.000001) 'Present counter rate differs.'
Check ($result.windows[0].reported_speed_min -eq 0.9 -and $result.windows[0].reported_speed_max -eq 1.2) 'Reported speed range differs.'
Check ($result.windows[1].start_seconds -eq 5 -and $result.windows[1].full_window_available) 'Ten-second boundary differs.'
$short = Get-RecompStartupFrameMetrics @((Sample 1 5 10),(Sample 2.5 5 10)) 3
Check (-not $short.windows[0].full_window_available -and $short.windows[0].elapsed_seconds -eq 1.5) 'A short run was represented as a full window.'
Check ($short.windows[0].frame_advances_per_second -eq 0 -and $short.windows[0].present_advances_per_second -eq 0) 'Zero progress must remain zero.'
$single = Get-RecompStartupFrameMetrics @((Sample 4 9 18 'NaN')) 5
Check ($null -eq $single.windows[0].frame_advances_per_second) 'One sample fabricated a rate.'
Check ($null -eq $single.windows[0].reported_speed_last) 'Invalid speed fabricated a value.'
$invalid = Get-RecompStartupFrameMetrics @((Sample 'NaN' 0 0),(Sample 1 -1 0),(Sample 2 'bad' 0),(Sample 3 3 6),(Sample 3 333 666),(Sample 2.9 222 444),(Sample 4 4 8)) 5
Check ($invalid.invalid_samples -eq 5 -and $invalid.active_samples -eq 2) 'Malformed/non-increasing samples were accepted.'
Check ($invalid.windows[0].frame_advances_per_second -eq 1) 'Invalid samples changed the surviving rate.'
$pause = Get-RecompStartupFrameMetrics @((Sample 1 1 2),(Sample 2 2 4 1 'paused'),(Sample 10 10 20),(Sample 11 11 22)) 12
Check ($pause.active_samples -eq 2 -and $pause.windows[0].start_seconds -eq 10) 'The timing window crossed an inactive segment.'
$reset = Get-RecompStartupFrameMetrics @((Sample 1 100 200),(Sample 2 1 2),(Sample 3 2 4)) 4
Check ($reset.counter_resets -eq 1 -and $reset.active_samples -eq 2 -and $reset.windows[0].frame_advances -eq 1) 'A counter reset was subtracted across segments.'
$empty = Get-RecompStartupFrameMetrics @() 5
Check ($empty.active_samples -eq 0 -and $null -eq $empty.windows[0].elapsed_seconds) 'Missing samples fabricated timing.'
$rejected = $false
try { Get-RecompStartupFrameMetrics @() ([double]::NaN) | Out-Null } catch { $rejected = $true }
Check $rejected 'Invalid observation end was accepted.'
function CpuSample($Time,$Frames,$Cpu,$CpuTime=$Time,$State='running') {
    $sample = Sample $Time $Frames ($Frames*2) 1 $State
    $sample | Add-Member -NotePropertyName process -NotePropertyValue ([pscustomobject]@{
        total_cpu_seconds=$Cpu; cpu_observed_seconds=$CpuTime
    })
    return $sample
}
$cpu = Get-RecompStartupFrameMetrics @((CpuSample 1 0 2 0.9),(CpuSample 2.2 60 4.4 2.1),(CpuSample 4.2 180 8.2 4.0)) 5
Check $cpu.windows[0].process_cpu_available 'Valid CPU observations were rejected.'
Check ([math]::Abs($cpu.windows[0].process_cpu_seconds-6.2) -lt 0.000001) 'Cumulative CPU values were not subtracted.'
Check ([math]::Abs($cpu.windows[0].process_cpu_observation_seconds-3.1) -lt 0.000001) 'CPU measurement did not use its own observed timestamps.'
Check ([math]::Abs($cpu.windows[0].process_average_busy_cores-2) -lt 0.000001) 'Two busy cores were clamped or divided by machine cores.'
Check ([math]::Abs($cpu.windows[0].process_cpu_milliseconds_per_frame-(6200.0/180)) -lt 0.000001) 'CPU time per frame differs.'
Check ([math]::Abs($cpu.windows[0].frame_advances_per_second-56.25) -lt 0.000001) 'CPU sampling changed existing frame timing.'
$cpuZero = Get-RecompStartupFrameMetrics @((CpuSample 1 1 5),(CpuSample 3 1 5)) 4
Check ($cpuZero.windows[0].process_cpu_available -and $cpuZero.windows[0].process_average_busy_cores -eq 0) 'Zero CPU delta was treated as missing.'
Check ($cpuZero.latest_active_sample_age_seconds -eq 1) 'The active-window age was not retained.'
Check ($null -eq $cpuZero.windows[0].process_cpu_milliseconds_per_frame) 'A zero-frame window divided CPU time by zero.'
Check (-not $result.windows[0].process_cpu_available -and $null -eq $result.windows[0].process_cpu_seconds) 'Legacy status history fabricated CPU metrics.'
$cpuMissing = Get-RecompStartupFrameMetrics @((CpuSample 1 1 1),(Sample 2 2 4),(CpuSample 3 3 3)) 4
Check (-not $cpuMissing.windows[0].process_cpu_available -and $cpuMissing.windows[0].frame_advances -eq 2) 'Missing intermediate CPU data changed or fabricated a full-window measurement.'
$cpuInvalid = Get-RecompStartupFrameMetrics @((CpuSample 1 1 'NaN'),(CpuSample 2 2 2)) 3
Check (-not $cpuInvalid.windows[0].process_cpu_available) 'Nonfinite CPU data was accepted.'
$cpuReset = Get-RecompStartupFrameMetrics @((CpuSample 1 1 5),(CpuSample 2 2 4),(CpuSample 3 3 6)) 4
Check (-not $cpuReset.windows[0].process_cpu_available -and $cpuReset.windows[0].frame_advances -eq 2) 'A CPU counter reversal was hidden by positive endpoint subtraction.'
$cpuTimeReset = Get-RecompStartupFrameMetrics @((CpuSample 1 1 1 1),(CpuSample 2 2 2 1),(CpuSample 3 3 3 3)) 4
Check (-not $cpuTimeReset.windows[0].process_cpu_available) 'Duplicate CPU observation timestamps were accepted.'
$cpuAfterStop = Get-RecompStartupFrameMetrics @((CpuSample 1 1 1),(CpuSample 2 2 2 4)) 3
Check (-not $cpuAfterStop.windows[0].process_cpu_available) 'A CPU read beyond the captured observation endpoint entered the window.'
$cpuPause = Get-RecompStartupFrameMetrics @((CpuSample 1 1 1),(CpuSample 2 2 99 2 'paused'),(CpuSample 3 3 100),(CpuSample 4 4 101)) 5
Check ($cpuPause.windows[0].process_cpu_available -and $cpuPause.windows[0].process_cpu_seconds -eq 1) 'CPU measurement crossed a paused segment.'
$selfClock = [Diagnostics.Stopwatch]::StartNew()
$selfProcess = [Diagnostics.Process]::GetCurrentProcess()
try {
    $selfSample = Get-RecompProcessSample -Process $selfProcess -Clock $selfClock
    Check ($null -eq $selfSample.error -and $null -ne $selfSample.total_cpu_seconds -and $selfSample.total_cpu_seconds -ge 0) 'Owned Process CPU sampling failed.'
    Check ($selfSample.working_set_bytes -gt 0 -and $selfSample.peak_working_set_bytes -ge $selfSample.working_set_bytes) 'Owned Process memory counters are invalid.'
    Check ($selfSample.measurement_begin_seconds -le $selfSample.cpu_observed_seconds -and $selfSample.cpu_observed_seconds -le $selfSample.measurement_end_seconds) 'CPU sampling timestamps are not bracketed.'
} finally { $selfProcess.Dispose() }
$unstartedProcess = [Diagnostics.Process]::new()
try {
    $missingSample = Get-RecompProcessSample -Process $unstartedProcess -Clock $selfClock
    Check ($null -ne $missingSample.error -and $null -eq $missingSample.total_cpu_seconds) 'Unavailable Process CPU time was replaced with zero.'
} finally { $unstartedProcess.Dispose() }
Write-Output "$script:checks startup frame-metric checks passed. No runtime was launched."
