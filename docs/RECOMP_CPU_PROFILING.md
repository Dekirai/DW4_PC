# Bounded CPU sampling

`scripts/test-recomp-startup.ps1` supports an opt-in in-process probe and a separate
Visual Studio collector. Both are disabled by default and cannot run together.

The current local collector session described below is paused but not removed.
Resolve that session before another recording. The first live attempt produced
no usable external CPU profile. That failed recording establishes no hotspot or
performance improvement. The in-process probe does not use that session or ETW.

## In-process probe

Use `-InProcessCpuProbeSeconds 15 -InProcessCpuProbeAtSeconds 40 -TimeoutSeconds 70`
with the ordinary hidden startup command below, replacing its two external
`CpuProfile*` arguments. The executable, module, patches and ABI must match the
completed local build. The harness explicitly injects the probe's two variables
into its otherwise sanitized child environment. The runtime accepts only a
1–30-second observation and a 0–85-second delay. Normal launches perform no probe
clock reads or report I/O. The probe stores its bounded table in the core object.
The runtime chooses a `RunImpl<true>` or `RunImpl<false>` instantiation once per
`Run` entry. Compile-time gates remove probe calls from ordinary dispatches.
Inspection of the actual MSVC Release assembly confirms no direct probe or
clock API calls in the ordinary body; evidence is in ignored
`work/recomp-probe-codegen/verification.json`. This later isolation change was
not present in the first completed probe package documented below.

The window is relative to the emulation thread's first `Run`, not executable
launch. State survives temporary `Run` reentry, including screenshots. The
scheduled cutoff is fixed before collection; reported start/end times show
polling and calibration latency. A sample crossing the cutoff is excluded from
site totals. An incomplete or missed window is not successful profiling.

Only the owning emulation thread calls `GetThreadTimes` and
`QueryThreadCycleTime`. The former measures whole-window user plus kernel CPU
time in 100 ns units; that unit is not the clock's effective resolution. The
latter supplies raw per-sample thread CPU cycles, which are never converted to
milliseconds. Steady-clock elapsed nanoseconds include preemption and waits.
Sixteen empty sample pairs record measurement overhead; their raw cost is
reported separately and is not silently subtracted. Small samples near that
overhead cannot establish expensive code. See Microsoft's
[GetThreadTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadtimes)
and [QueryThreadCycleTime](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime)
contracts.

Samples use deterministic, irregular gaps averaging 4096 opportunities, with
the seed and stride retained. A fixed 1024-entry table bounds storage; failures,
counter resets, thread changes or overflow invalidate the parsed diagnostic.
The report is serialized only at quiesced core shutdown. The parser keeps raw
lines and rejects malformed, duplicate, incomplete or inconsistent reports.

| Site key | Timed region |
| --- | --- |
| Original runtime PC | Native dispatch, including any host hooks called inside it |
| `00000001` | CoreTiming advance, including event callbacks and throttle waits |
| `00000002` | Guarded bulk cache invalidation |
| `00000003` | Idle-loop GPU synchronization |
| `00000004` | Top-level mod host call, including cached-address retry |
| `00000005` | Fallback JIT or interpreter execution |

These sequential regions do not double-count nested native hooks. They do not
cover all work: verification, register synchronization, dispatch guards and
other bookkeeping remain unattributed. Raw sampled totals are not whole-window
CPU percentages. Keep site counts and overhead visible when ranking candidates,
and compare an instrumented run with an uninstrumented run of the same binary
before drawing performance conclusions. Scene alignment remains necessary.

The actual-header checker passes 414 checks for disabled operation, ownership,
reentry, timing bounds, resets/failures, quantized counters, sample cutoff,
bounded storage and irregular sampling. It also reads its own Windows thread's
counters, without starting a workload or external recorder. The strict report
parser passes 272 checks and the harness opt-in/environment integration passes
15, under PowerShell 7 and 5.1. These checks validate measurement mechanics;
they are not a CPU hotspot or gameplay-performance result.

### First completed local probe

The 2026-09-13 run `work/recomp-startup/20260913-091311-19329903/` completed
a 15-second observation, scheduled 25 seconds after the first core `Run`.
Actual bounds were 25.0000825–40.0000720 seconds on that relative clock.
The owner-thread CPU total was 13.984375 seconds. It accepted 2,500 samples
from 10,325,932 opportunities across 428 sites, with no failed counters or
table overflow. The seed was `3494363358` and mean stride was 4096.

The largest raw sampled site was `8040154c`: 271 samples, 6,109,841 thread CPU
cycles and 1,772,300 elapsed nanoseconds. Original-code inspection identifies
this block as a 4x4 planar YUV-to-tiled-RGBA8 conversion step. Its averaged
22,545.5 cycles and 6,539.9 ns per sample exceeded the empty-pair calibration
averages of 1,572 cycles and 1,881.25 ns. This makes the conversion a concrete
optimization candidate. It does not attribute all of that cost to quantization,
or establish an exclusive whole-thread CPU percentage. The bulk cache region
had only eight samples, too few for a precise comparison.

The 90-second hidden Vulkan run used the NVIDIA GeForce RTX 5070 Ti on the
Ryzen 7 7800X3D host, 1x EFB, neutral bindings and verified Cubeb volume zero.
It exited normally, with zero failed SMC chunks and no measured device callback
gaps. Audio-source replay remained. Screenshots showed original opening-movie
content; the first was a dark transition. This is no audible-quality proof.

The executable SHA256 was
`128E85A80081625864714BAF94F50C20A98CC96ABEE85930280D2880E07F6341`;
the module remained
`526A56A0BCE6C675CAF1274F1E76F54A455C243DBF84A1235A5721EA97C6DAA9`.
The uninstrumented run of the same binary is
`work/recomp-startup/20260913-091018-20188f4e/`. Startup and subsequent counter
intervals varied substantially between these runs and the earlier build.
Neither this pair nor the cache metadata guard demonstrates a speedup. See
[RECOMP_PERFORMANCE.md](RECOMP_PERFORMANCE.md#empty-cache-metadata-guard).

### Rebuilt probe verification

The later `1104466C…` EXE / `4772F734…` module pair also completed the opt-in
probe after compile-time dispatch isolation. Run
`work/recomp-startup/20260913-101239-ed75d6d3/` observed
25.0006402–40.0005398 seconds after first `Run`, with 10.671875 seconds of
owner-thread CPU time and 1,863 accepted samples from 7,647,670 opportunities.
It had no counter failures or table overflow and exited normally. The largest
raw sampled site remained `8040154c`: 239 samples, 5,822,262 CPU cycles and
1,823,000 elapsed ns. Its raw average must not be interpreted as an isolated
quantizer measurement or compared causally without accounting for scene and
sampling variation. Calibration recorded 16 pairs, 16,347 cycles and 10,800 ns.

The same rebuilt binary's uninstrumented run is
`20260913-100635-cc822ac6`. It reached 1,413 unique counter advances versus
1,363 with the probe; their final ten-second rates were about 4.49 and 6.73/s
in different phases. That pair verifies opt-in integration and its disabled
counterpart, not a precise overhead percentage. All runs used muted Cubeb,
neutral bindings and 1x Vulkan; the movie slowdown remains unresolved.

### Preserved reference builds

For comparisons after a rebuild, `-ReferenceBuildDirectory` can select a
preserved `build-record.json`, `abi.json` and both corresponding source patches.
It requires an explicit packaged unified EXE and its bundled module. Existing
EXE, module, DOL, patch and ABI hash gates still apply; the selected record and
its hash are retained in the invocation. This option does not silently accept
an older binary against a newer build record. Ordinary runs still use the
current repository build. The preserved pre-quantizer reference is
`work/recomp-reference-builds/20260913-090202/`, paired with package
`dist/DW4_Recomp/PC-Preview-20260913-090536/`.

### Cache guard snapshots

An enabled in-process probe can also record up to 64 cache-guard snapshots
per CPU-core lifetime, at every 65,536th candidate while the probe window is
active. These bounded stderr records show the live guest/PPC MSR, registers,
exceptions, dispatch charge, hook count and prepared line count. They are
compiled only into `RunImpl<true>`; ordinary execution has no snapshot sampling
or logging. They use the existing probe option and introduce no external
collector. Diagnostic I/O can perturb that diagnostic run, so its timing is
not a performance comparison.

The `flags` mask records module-active (1), running (2), lockstep (4), verified
chunk (8), host hooks (16), forced fallback (32) and enabled data cache (64).
`lines=0` means preparation rejected the candidate; `mapped_bat=0` in that case
does not establish a translation failure because the mapping test was skipped.

The 65-second local diagnostic at
`work/recomp-startup/20260913-121947-52c5c74c/` completed the requested 25–40-second
probe window and exited normally. Its 37 guard snapshots include 33 with guest
EE clear, 17 with guest/PPC MSR differences confined to EE/RI, and one with a
pending exception. None prepared a batch. The source used the earlier guard
that required EE and exact guest/PPC MSR equality. These are sampled guard
facts, not exhaustive percentages of all rejected calls. The derived records
remain in `work/recomp-cache-probe-guards.json`.

That diagnostic EXE SHA256 was
`4195B4DA1F727C89EFBF3B3FDC7538DDF03E6E2807CE0A64EE85F2E997AEE0C2`;
the unchanged module was `0F7C3DA7…`. Actual Cubeb initialization/start and zero
volume were observed. The CPU probe recorded 4,015 samples without failures or
table overflow; `8040154c` remained the largest raw accumulated sampled-cycle
site. See [RECOMP_PERFORMANCE.md](RECOMP_PERFORMANCE.md#additional-guarded-cache-loops)
for the subsequent policy change and its validation.

## Usage

After resolving the residual session, an example from the repository root is:

```powershell
$package = (Get-Content 'work/recomp-unified-package-path.txt' -Raw).Trim()
./scripts/test-recomp-startup.ps1 `
  -Runner (Join-Path $package 'DW4_PC.exe') `
  -UnifiedExecutable -UseBundledModule `
  -Module (Join-Path $package 'data/game.dll') `
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
