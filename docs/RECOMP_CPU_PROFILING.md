# Bounded CPU sampling

`scripts/test-recomp-startup.ps1` can opt into Visual Studio CPU sampling of the
runtime process that the harness launched and retained. Profiling is disabled by
default: both `CpuProfileSeconds` and `CpuProfileAtSeconds` default to zero.

The current local collector session described below is paused but not removed.
Resolve that session before another recording. The first live attempt produced
no usable CPU profile, so this work identifies no CPU hotspot and demonstrates
no performance or audio improvement.

## Usage

After resolving the residual session, an example from the repository root is:

```powershell
$package = (Get-Content 'work/recomp-unified-package-path.txt' -Raw).Trim()
./scripts/test-recomp-startup.ps1 `
  -Runner (Join-Path $package 'DW4_PC.exe') `
  -UnifiedExecutable -UseBundledModule `
  -Module (Join-Path $package 'gGDJEB2_recomp.dll') `
  -ExtractedRoot 'E:/Games/Emulator/Dolphin-x64/Games/Digimon World 4 - Extracted' `
  -Graphics Vulkan -Resolution 1920x1080 -Audio Cubeb `
  -IdleMode Enabled -CacheMode Enabled `
  -Screenshot -ScreenshotAtSeconds 35,65 `
  -CpuProfileSeconds 15 -CpuProfileAtSeconds 40 -TimeoutSeconds 70
```

This uses the packaged unified executable and a fresh profile under ignored
`work/`. The existing build-record, patch, executable, module, original-DOL and
ABI gates still apply. The selected package is usable only while those identities
match the recorded build; the script does not bypass a mismatch. Empty physical
controller bindings, emulated port 0 and Cubeb volume zero remain in effect.
No controlled Start press is requested by this example.

`CpuProfileSeconds` accepts 1–30 seconds when enabled. The requested start time
is measured on the harness wall clock; attachment also requires fresh running
status and frame progress. The nominal observation must leave five seconds
before `TimeoutSeconds`. Metadata reads and collector start/update/stop latency
consume the same run budget. A delayed collector can therefore fail to complete
the requested observation; it cannot extend the runtime deadline. Status reads,
scheduled screenshots, controlled-input release if requested, normal shutdown
and the owned-process watchdog remain serviced during collector command waits.

## Ownership and evidence

The reusable `scripts/profile-recomp.ps1` helper does not launch a game. It checks
the retained process's PID, start time, original launch path, loaded executable
path and executable SHA256. A unified executable uses the validated launcher
hash; a standalone runner uses the runner hash. Each attempt creates one fresh
collector GUID, attaches only that PID and requests `CpuUsageLow.json` with its
single CPU agent. It does not request child-process attachment. The intended
sequence is `help`, `start`, `update /lifetimeProcess`, bounded observation and
an own-GUID `stop` in `finally`.

Raw stdout/stderr, installed tool/config hashes, copied configuration, ownership
observations, command timing intervals and archive availability stay under the
run's `cpu-profile/` directory. The run summary embeds the helper result and its
outer host timing. Any stderr diagnostic makes the command unverified, including
a command that returns exit code zero. A nonempty archive and confirmed stop are
required for a completed profile.

The attached PID defines the CPU sampling target. This does not establish that
the collector's raw ETW artifact contains no broader machine metadata. CPU
sampling is distinct from native-dispatch counts, reported emulation speed,
wall-clock waits or hardware audio-underrun measurements. Collector overhead can
perturb the measured workload.

The tested optimized package has no adjacent EXE or generated-module PDB. Merely
finding a PDB would not establish matching GUID/age. Precise generated-function
attribution needs matching symbols and the exact corresponding binaries and
generated source; no resolved native CPU attribution has been verified here.

## Verified failure and correction

The 2026-09-13 attempt in ignored
`work/recomp-startup/20260913-081913-ea87e191/` used a 70-second runtime budget
and requested a 15-second profile at 40 seconds. Its executable SHA256 was
`797948B87AD2052CF0F5D702EA32F440C35B36D46A002C46BC710459CC58394C`
and its generated module SHA256 was
`526A56A0BCE6C675CAF1274F1E76F54A455C243DBF84A1235A5721EA97C6DAA9`.

PowerShell's dynamic scope exposed the collector command's five-second
`TimeoutSeconds` parameter to the host polling callback. The harness therefore
queued its own normal stop at 40.430 seconds, well before its intended 70-second
deadline. The runtime exited with code zero. The first screenshot at 35 seconds
was saved; the later screenshot was never reached. The collector reported
`Running`, but the immediate process recheck failed before the lifetime update
and requested observation. Its original stop command timed out after five
seconds. No diagnostic archive was produced.

Host scheduler configuration and state now use explicit script scope. A
regression test invokes the real profiling helper with mocked native commands
whose local five-second timeout and other local variables shadow host names.
It verifies that the runtime retains its 70-second deadline, screenshots and
status remain serviced, and the helper completes its synthetic observation.
The live failure was not repeated after this correction.

`tests/recomp_cpu_profile_tests.ps1` passes 70 focused checks and
`tests/recomp_startup_profile_tests.ps1` passes 49, under both PowerShell 7 and
Windows PowerShell 5.1. These tests launch no runtime, native collector, ETW
recording or service. They validate ownership, timing and failure handling;
they do not validate a successful real capture.

## Residual paused session

The exact locally owned session is
`c6d048d2-b057-4378-917c-0347e8830622`. Subsequent GUID-specific stop attempts
returned exit zero but printed `E_ACCESSDENIED` (`0x80070005`) from
`ICollectionSession.Stop`. A targeted pause succeeded, and a subsequent status
query confirmed `Paused`. One further stop after pausing was also denied; the
last verified state remains **Paused**, not stopped or removed. No further
recording, service stop, global cancellation, elevation or command targeting the
former, reusable game PID was performed during cleanup.

Raw cleanup evidence is under ignored
`work/recomp-profile-analysis/20260913-081913-ea87e191-cleanup-a85de2a7430f466abbdd0ba165cb6fc2/`.
Installed command help exposes `stop <sessionID> /output:<fileName>` with no force
or cancellation option. If an administrator performs cleanup from a PowerShell
already opened at this repository root, the minimal supported targeted attempt
is below. Administrator success has not been tested; inspect stderr and the
following status rather than trusting exit code zero.

```powershell
$collector = 'C:/Program Files/Microsoft Visual Studio/18/Insiders/Team Tools/DiagnosticsHub/Collector/VSDiagnostics.exe'
$cleanupArchive = Join-Path (Get-Location) 'work/recomp-profile-analysis/admin-cpu-cleanup.diagsession'
& $collector stop 'c6d048d2-b057-4378-917c-0347e8830622' "/output:$cleanupArchive"
& $collector status 'c6d048d2-b057-4378-917c-0347e8830622'
```
