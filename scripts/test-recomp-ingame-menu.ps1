param(
    [Parameter(Mandatory=$true)][string]$ExtractedRoot,
    [string]$Executable = '',
    [ValidateRange(70,120)][int]$TimeoutSeconds = 100
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'The hidden native-surface diagnostic requires Windows.'
}
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Executable) { $Executable = Join-Path $repoRoot 'work/moderngekko-build/DW4_PC.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$ExtractedRoot = (Resolve-Path -LiteralPath $ExtractedRoot).Path
$recordPath = Join-Path $repoRoot 'work/recomp-build-record.json'
$record = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
$runtimePatch = Join-Path $repoRoot 'patches/moderngekko-local-runtime.patch'
$chassisPatch = Join-Path $repoRoot 'patches/recompcore-headless-shader-progress.patch'
$module = (Resolve-Path -LiteralPath $record.module_path).Path
$executableHash = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash
if ($record.launcher_binary_sha256 -ne $executableHash -or
    $record.runtime_patch_sha256 -ne (Get-FileHash -LiteralPath $runtimePatch -Algorithm SHA256).Hash -or
    $record.chassis_patch_sha256 -ne (Get-FileHash -LiteralPath $chassisPatch -Algorithm SHA256).Hash -or
    $record.module_built -isnot [bool] -or -not $record.module_built -or
    $record.module_sha256 -ne (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash) {
    throw 'The executable, module and source patches must match the completed build record.'
}
if (-not [IO.File]::ReadAllText($runtimePatch).Contains('--ui-smoke-test') -or
    -not [IO.File]::ReadAllText($chassisPatch).Contains('Diagnostic windows never become visible')) {
    throw 'Rebuild the hidden native-surface diagnostic before testing. No process was started.'
}
if ((Get-FileHash -LiteralPath $record.abi_report_path -Algorithm SHA256).Hash -ne $record.abi_report_sha256 -or
    -not (Get-Content -LiteralPath $record.abi_report_path -Raw | ConvertFrom-Json).passed) {
    throw 'The matching CPU/module ABI report is required.'
}
$dolHash = (Get-FileHash -LiteralPath (Join-Path $ExtractedRoot 'sys/main.dol') -Algorithm SHA256).Hash
$boot = [IO.File]::ReadAllBytes((Join-Path $ExtractedRoot 'sys/boot.bin'))
if ($dolHash -ne 'EC61C010477C844781763A3041922E38719B3CED3B74BE5CE71D778CE458E471' -or
    $boot.Length -lt 6 -or [Text.Encoding]::ASCII.GetString($boot,0,6) -ne 'GDJEB2') {
    throw 'This diagnostic requires the verified Digimon World 4 USA extraction.'
}

# Fresh test-only storage; never reuse or remove the user's savedata.
$runName = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
$runRoot = Join-Path $repoRoot "work/recomp-ingame-menu/$runName"
$profile = Join-Path $runRoot ('user-' + [char]0x00fc + [char]0x65e5 + [char]0x672c)
$output = Join-Path $runRoot 'capture'
$temp = Join-Path $runRoot 'temp'
foreach ($directory in @($profile,$output,$temp,(Join-Path $profile 'Config'),
    (Join-Path $profile 'AppData/Roaming'),(Join-Path $profile 'AppData/Local'))) {
    [IO.Directory]::CreateDirectory($directory) | Out-Null
}
$utf8 = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $profile 'config.ini'),
    "[Video]`nresolution=640x528`nbackend=Vulkan`nfullscreen=false`nshow_fps_in_title=false`n",$utf8)
[IO.File]::WriteAllText((Join-Path $profile 'Config/Dolphin.ini'),
    "[Interface]`nUsePanicHandlers=True`n[Core]`nSIDevice0=6`nSIDevice1=0`nSIDevice2=0`nSIDevice3=0`n",$utf8)
[IO.File]::WriteAllText((Join-Path $profile 'Config/GCPadNew.ini'),
    "[GCPad1]`nDevice =`nOptions/Always Connected = True`n[GCPad2]`nDevice =`n[GCPad3]`nDevice =`n[GCPad4]`nDevice =`n",$utf8)

function Quote-NativeArgument([string]$Value) {
    $escaped = [regex]::Replace($Value,'(\\*)"','$1$1\"')
    return '"' + [regex]::Replace($escaped,'(\\+)$','$1$1') + '"'
}
$arguments = @('--run','--game',$ExtractedRoot,'--module',$module,'--user-dir',$profile,
    '--ui-smoke-test',$output,'--no-mods','--mute')
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $Executable
$start.Arguments = ($arguments | ForEach-Object { Quote-NativeArgument $_ }) -join ' '
$start.WorkingDirectory = $runRoot
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.EnvironmentVariables.Clear()
foreach ($name in @('SystemRoot','SystemDrive','WINDIR','ComSpec','NUMBER_OF_PROCESSORS',
    'PROCESSOR_ARCHITECTURE','PROCESSOR_IDENTIFIER','PROCESSOR_LEVEL','PROCESSOR_REVISION')) {
    $value = [Environment]::GetEnvironmentVariable($name)
    if ($value) { $start.EnvironmentVariables[$name] = $value }
}
$start.EnvironmentVariables['PATH'] = (Split-Path -Parent $Executable) + ';' + (Join-Path $env:SystemRoot 'System32') + ';' + $env:SystemRoot
$start.EnvironmentVariables['USERPROFILE'] = $profile
$start.EnvironmentVariables['MODERNGEKKO_STATICRECOMP'] = '1'
$start.EnvironmentVariables['APPDATA'] = Join-Path $profile 'AppData/Roaming'
$start.EnvironmentVariables['LOCALAPPDATA'] = Join-Path $profile 'AppData/Local'
$start.EnvironmentVariables['TEMP'] = $temp
$start.EnvironmentVariables['TMP'] = $temp
$taskProcess = [Diagnostics.Process]::new()
$taskProcess.StartInfo = $start
$started = $false
$timedOut = $false
$ownedStartTicks = 0L
$exitCode = $null
$failure = $null
$stdoutTask = $null
$stderrTask = $null
$watch = [Diagnostics.Stopwatch]::StartNew()
function Stop-OwnedDiagnostic {
    $taskProcess.Refresh()
    if (-not $taskProcess.HasExited) {
        try {
            if ($taskProcess.StartTime.ToUniversalTime().Ticks -ne $ownedStartTicks -or
                -not $taskProcess.MainModule.FileName.Equals($Executable,[StringComparison]::OrdinalIgnoreCase)) {
                throw 'The diagnostic process identity changed; it was not stopped.'
            }
            $taskProcess.Kill()
            if (-not $taskProcess.WaitForExit(5000)) { throw 'The owned diagnostic did not exit after termination.' }
        } catch {
            # Normal exit can race the identity query or termination call.
            $taskProcess.Refresh()
            if (-not $taskProcess.HasExited) { throw }
        }
    }
}
try {
    if (-not $taskProcess.Start()) { throw 'The hidden diagnostic process did not start.' }
    $started = $true
    $ownedStartTicks = $taskProcess.StartTime.ToUniversalTime().Ticks
    $stdoutTask = $taskProcess.StandardOutput.ReadToEndAsync()
    $stderrTask = $taskProcess.StandardError.ReadToEndAsync()
    while (-not $taskProcess.HasExited) {
        if ($watch.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
            $timedOut = $true
            Stop-OwnedDiagnostic
            break
        }
        Start-Sleep -Milliseconds 100
        $taskProcess.Refresh()
    }
    $exitCode = $taskProcess.ExitCode
} catch {
    $failure = $_.Exception.Message
} finally {
    if ($started) {
        try { Stop-OwnedDiagnostic } catch { $failure += ' Cleanup: ' + $_.Exception.Message }
    }
    $watch.Stop()
    if ($stdoutTask -and $stdoutTask.Wait(5000)) {
        [IO.File]::WriteAllText((Join-Path $runRoot 'stdout.log'),$stdoutTask.Result,$utf8)
    }
    if ($stderrTask -and $stderrTask.Wait(5000)) {
        [IO.File]::WriteAllText((Join-Path $runRoot 'stderr.log'),$stderrTask.Result,$utf8)
    }
}
$reportPath = Join-Path $output 'ingame-menu-smoke.json'
$imagePath = Join-Path $output 'composed-menu.png'
$report = if (Test-Path -LiteralPath $reportPath -PathType Leaf) {
    Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
} else { $null }
$observedSilentSurface = $false
if ($null -ne $report -and $report.PSObject.Properties['audio'] -and $report.PSObject.Properties['window_checks']) {
    $observedSilentSurface = $report.audio.verified_null -is [bool] -and $report.audio.verified_null -and
        $report.audio.available -and $report.audio.stream_alive -and $report.audio.generation -gt 0 -and
        $report.audio.backend -eq 'Null' -and $report.audio.output_sample_rate -eq 0 -and
        -not $report.audio.device_started -and $report.audio.device_callbacks -eq 0 -and $report.window_checks -gt 0
}
$passed = -not $failure -and -not $timedOut -and $exitCode -eq 0 -and $null -ne $report -and
    $report.passed -is [bool] -and $report.passed -and $observedSilentSurface -and
    (Test-Path -LiteralPath $imagePath -PathType Leaf)
$summary = [ordered]@{
    passed = [bool]$passed; executable = $Executable; executable_sha256 = $executableHash
    module = $module; module_sha256 = $record.module_sha256; main_dol_sha256 = $dolHash
    runtime_patch_sha256 = $record.runtime_patch_sha256; chassis_patch_sha256 = $record.chassis_patch_sha256
    process_id = $(if ($started) { $taskProcess.Id } else { $null }); process_start_utc_ticks = $ownedStartTicks
    duration_seconds = $watch.Elapsed.TotalSeconds; exit_code = $exitCode; timed_out = $timedOut
    user_directory = $profile; report = $reportPath; composed_image = $imagePath; error = $failure
    audio_policy = 'NullSound; no audio device; unmute disabled'
    visibility_policy = 'Real Win32 Vulkan surface, never shown or activated'
    observed_silent_surface = [bool]$observedSilentSurface
}
[IO.File]::WriteAllText((Join-Path $runRoot 'summary.json'),($summary | ConvertTo-Json -Depth 6),$utf8)
$taskProcess.Dispose()
Write-Output "Hidden in-game menu evidence: $runRoot"
if (-not $passed) { throw "Hidden in-game menu diagnostic failed. Inspect $reportPath and stderr.log." }
