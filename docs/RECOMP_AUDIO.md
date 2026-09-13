# Recompiled runtime audio diagnostics

The owner reported severe audio stutter and low frame rate through the main
menu. The tested executable was not identified by hash. The earlier optimized
30-second startup comparisons used NullSound and therefore did not validate
real mixing, device callbacks or audible quality.

## Silent device test

`scripts/test-recomp-startup.ps1` keeps its default 30-second, 1x EFB,
NullSound test. `-Audio Cubeb` explicitly requests `--headless-audio --audio
Cubeb --mute`: the real device receives mixed audio at zero stream volume.
The ordinary headless default still uses NullSound. The strict diagnostic
mode rejects initialization fallback, start failure and device errors. A
successful zero-volume operation is required before strict silent playback
can start.

The harness accepts up to 90 seconds and optional screenshot times. It creates
a fresh isolated profile, records binary/patch/module identities, bounds its
own process and sends no controller input by default. For example, after a local build:

```powershell
$record = Get-Content work/recomp-build-record.json -Raw | ConvertFrom-Json
./scripts/test-recomp-startup.ps1 -Module $record.module_path `
    -ExtractedRoot 'E:/Games/Emulator/Dolphin-x64/Games/Digimon World 4 - Extracted' `
    -Graphics Vulkan -Resolution 1920x1080 -Audio Cubeb `
    -TimeoutSeconds 90 -Screenshot -ScreenshotAtSeconds 35,50,70
```

`1920x1080` is the existing frontend label for a 3x integer EFB scale; it does
not imply an exact screenshot size or verified widescreen rendering. The
headless Vulkan log identifies the actual initialized adapter. Offscreen
frame-counter rates are not monitor presentation measurements.

`-PressStartAtSeconds 50` optionally publishes one nonblocking Start press on
port 0, followed by an explicit release after approximately half a second.
The harness records and requires both command acknowledgments. Empty physical
bindings remain in place. Acknowledgment establishes controller publication;
only captured game images can establish an intro skip or menu arrival. These
input-bearing probes must be identified separately from neutral movie runs.

## Reading the evidence

Raw automation snapshots are retained in each run's `status-history.jsonl`.
The summary separates process/frame observations from audio observations.
Audio counters are cumulative within one sound-stream generation. They are
host diagnostics, not serialized game state, and do not alter the resampler,
buffer size, gap-fill policy or simulation speed.

- DMA and DTK record source frames, nonzero source frames, queue activity,
  dropped/trimmed granules and active-source empty/replay/fade observations.
  DTK normally receives zero-filled blocks even when disc streaming is off.
  Activity therefore means nonzero accepted source data within a documented
  250 ms window while execution is running, not the game's logical playback
  state. Silence is not automatically an underrun.
  Accepted frames mean input handed to the FIFO, not guaranteed queue storage;
  drop/trim observations are also limited to recent signal while running.
- Cubeb records successful initialization/start, volume application, device
  format, callback frames, nonzero mixed output before device mute, callback
  work time, callback intervals and errors. A callback gap exceeding twice
  the preceding callback's nominal duration is an observation, not proof of a
  hardware underrun. Start/stop epochs exclude deliberate restart intervals.
- `max_speed` is the runtime's estimated speed without its throttle wait.
  It measures estimated headroom, not an unlocked frame rate or correct
  high-FPS gameplay.
- Final five- and ten-second windows use actual sample timestamps and avoid
  subtraction across stream replacement, counter reset or inactive segments.
  Missing observations remain unavailable rather than becoming zero errors.

The snapshot reader retains shared counter ownership across audio shutdown;
it never borrows a raw sound-stream pointer. Audio callbacks do not take the
snapshot registry lock. Individual relaxed counter reads are nearby
observations, not a single atomic audio transaction.

These diagnostics can locate producer starvation, callback delays and runtime
slowdown. They do not establish what a listener hears, complete main-menu
coverage or gameplay correctness without the corresponding scene evidence.

## Initial extended startup comparison

Two 90-second runs used the same optimized game module and instrumented runtime,
fresh profiles, Cubeb at volume zero and Vulkan on the **NVIDIA GeForce RTX 5070
Ti**, confirmed by the initialized-device log. The CPU is the Ryzen 7 7800X3D.
Images at 50 and 70 seconds show the original opening movie; neither run proves
main-menu arrival. Its frame counter represents distinct movie images, while
the present counter advances three times as often in this section.

| Final approximately ten-second window | 3x EFB | 1x EFB |
| --- | ---: | ---: |
| Frame counter advances per second | 9.13 | 9.29 |
| Present counter advances per second | 27.40 | 27.87 |
| Final reported simulation speed | 0.2913 | 0.2974 |
| Process CPU milliseconds per frame advance | 112.06 | 107.21 |
| DMA active empty/replay events | 197 | 196 |
| Mean Cubeb callback interval | 10.0001 ms | 10.0004 ms |
| Mean callback work | 0.1792 ms | 0.1787 ms |
| Callback gaps over two periods / work overruns / device errors | 0 / 0 / 0 | 0 / 0 / 0 |

The DMA source supplies approximately 9,765 and 9,874 frames per wall-clock
second, compared with its configured rate of approximately 32,028 Hz. The
device continues consuming approximately 48,000 frames per second. Repeated
FIFO replay therefore accompanies insufficient fresh source production.
Reducing the render scale did not resolve the slowdown. These observations
implicate slow execution upstream of audio output; they do not prove which
CPU function accounts for the elapsed work.

The previous 30-second logo tests missed this later slowdown. They remain
valid within their documented scope, and must not be generalized to the
opening movie, main menu or gameplay. In particular, the new startup harness
passing means that execution, capture, real muted audio and controlled shutdown
were observed; it does not mark this slow scene as a performance pass.

Local evidence:

- 3x: `work/recomp-startup/20260913-064357-e7086117/summary.json`
- 1x: `work/recomp-startup/20260913-064622-3d7bf438/summary.json`
- Paired timeline and matching hashes: `work/recomp-audio-timeline-comparison.json`

The 3x run had a four-second standalone checker compilation overlapping seconds
20–24. That overlap is outside the later comparison windows. The runs use
wall-clock windows, not exact movie-frame alignment. Original images and raw
game-dependent traces remain ignored local evidence.

A later input audit found another limitation in these initial runs: their
empty pad configuration could load Dolphin's default keyboard bindings,
including Return for Start. Sending no automation input did not exclude that
physical input path. No physical keypress was recorded or proven. These runs
retain their observed scene/audio evidence but do not establish controlled
neutral input.

## Neutral-input cache batching comparison

Two subsequent 70-second runs compared guarded cache batching disabled and
enabled using the same runner, game module and patch hashes, 3x EFB, Vulkan,
enabled scheduler optimization and muted Cubeb. Both profiles explicitly
configured an emulated GameCube controller on port 0, always connected with
empty device/control bindings; ports 1–3 were disconnected. The recorded
initial pad hashes match the harness's configuration. Post-run INI serialization
changed whitespace and therefore file hashes, while preserving the parsed
neutral settings. The processed automation commands were three captures and
one stop in each run.

| Final approximately ten-second window | Cache disabled | Cache enabled |
| --- | ---: | ---: |
| Frame counter advances per second | 9.22 | 9.35 |
| Present counter advances per second | 27.67 | 28.04 |
| Final reported simulation speed | 0.3123 | 0.2694 |
| Process CPU milliseconds per frame advance | 114.20 | 101.48 |
| DMA accepted source frames per wall-clock second | 9,905 | 9,993 |
| DMA active empty/replay events | 195 | 195 |
| DMA active fade events | 0 | 0 |
| Mean Cubeb callback interval | 10.0000 ms | 10.0002 ms |
| Mean callback work | 0.1793 ms | 0.1796 ms |
| Callback gaps over two periods / work overruns / device errors | 0 / 0 / 0 | 0 / 0 / 0 |

The enabled run batched **18,900,599 cache lines** in 81,192 accepted batches.
Its separate accounting records 37,801,198 native cycles and 94,502,995 hook
cycles, exactly two and five per line. Disabled counters were zero. These are
whole-run shutdown totals, not deltas from the final measurement window.
The isolated checker compared original instructions, emitted C and batching
across 1,536 cases and 9,203 timing boundaries without CPU, split-cycle or
cache-line journal differences. It does not exercise live JIT metadata.

This pair shows no clear frame-rate gain. Although the enabled final window
used less process CPU time per frame advance, startup and scene positions were
not exactly aligned: the first running snapshots occurred at 16.26 and
23.31 seconds. A causal percentage improvement cannot be assigned from this
single pair. Slow source production and repeated FIFO replay remain. Earlier
in the enabled run, one active fade event affected 32 output frames; the final
window's zero must not be read as a whole-run zero. Device callbacks remained
active without observed callback gaps, work overruns or device errors. Audible
quality remains untested because the device was muted.

An earlier 90-second cache-disabled run at
`work/recomp-startup/20260913-070310-f2b2a33e/` also lacked explicit neutral input
and changed scene. Its late CPU measurements are excluded from this cache
comparison; a possible default keyboard path does not prove a keypress caused
the scene change.

Local evidence:

- Disabled: `work/recomp-startup/20260913-071234-68f00103/summary.json`
- Enabled: `work/recomp-startup/20260913-071901-d3c750d9/summary.json`
- Matching identities, neutral semantics and timeline:
  `work/recomp-cache-neutral-comparison.json`
- Isolated equivalence: `work/recomp-cache/latest.json` and `invocation.json`

## Controlled title-screen arrival

A subsequent 90-second probe ran the packaged `DW4_PC.exe` directly with its
portable profile. The 45-second image shows the opening movie; the 65- and
85-second images show the original title screen with **PRESS START**. One
controlled Start press was acknowledged at 50.1409 seconds. Its normal release
was queued 0.5462 seconds later and acknowledged at 50.7912 seconds, before
controlled shutdown near 90 seconds. No second Start or menu selection was sent.

In the final ten-second window the title-screen counter advanced 57.35 frames
per second, with a final reported speed of 0.99874 and 8.15 process CPU
milliseconds per frame advance. DMA still recorded seven active empty/replay
events and one fade event. Cubeb showed no callback gaps, work overruns or
device errors in that window. Near-normal title-screen speed does not establish
smooth audio or a sustained 60 FPS acceptance result.

This run used the earlier harness stop-file ordering. Its normal release
finished well before shutdown and did not use the two-second release fallback;
it does not validate the later fixes for delayed-release or stop-order edge
cases. The movie and title screen are different workloads, so their measured
rates do not demonstrate a fix for the movie slowdown. Title-screen arrival
does not establish main-menu progression or gameplay. Cubeb remained muted,
leaving audible quality untested.

Evidence: `work/recomp-startup/20260913-074432-035d0acd/` and
`work/recomp-controlled-start-analysis.json`. The run's portable profile was
archived under its `portable-savedata/` directory.

## Two controlled Start presses

The hidden harness now accepts `-SecondStartPressAtSeconds` alongside
`-PressStartAtSeconds`. The second request requires the first, at least five
seconds of scheduled and actual queue separation, a completed first release,
and a five-second shutdown margin. It has separate command names and records
both press/release acknowledgments. Normal and exceptional shutdown release
an active command. Default runs still send no input.

The actual extracted scheduler, publishers, outcome checks and cleanup pass
60 synthetic checks under PowerShell 7 and 5.1. The existing profile (49),
in-process probe (15) and reference-build (11) tests also pass on both shells.
`start_probes` records command-cycle outcomes; neither acknowledgment nor a
counter rate proves that the original game reached a particular menu.

The rebuilt package's first two-press run, at 40 and 75 seconds, is
`work/recomp-startup/20260913-101047-8d8ade4a/`. Both command cycles completed,
but screenshots at 60 and 73 seconds still show the movie; the 85-second image
shows the **PRESS START** title screen. Its final ten-second window measured
60.0045 unique counter advances/s, reported speed 1.0024 and 9.6751 process CPU
ms/advance. Fresh DMA supply was 32,066.2 frames/s, with zero replay events in
that window and no device errors. Cubeb remained muted. This is title-screen
evidence, not main-menu progression, audible quality or gameplay acceptance.

A later schedule at 60 and 80 seconds reached the original **Game Start /
Option** main menu: `work/recomp-startup/20260913-101436-7f7f6bc1/`. The
73-second image shows PRESS START; both the 85- and 88-second images show the
main-menu choices. Both command cycles completed and the process exited
normally, with no failed SMC chunks. The final 5.0900-second window, after
the first main-menu screenshot, measured 60.7070 unique counter advances/s,
final simulation speed 1.00291 and 10.1133 process CPU ms/advance. DMA supplied
32,435.2 frames/s with zero replay events, callback gaps or device errors in
that window. The ten-second window includes the title-to-menu transition and
is kept separately in the report.

This establishes bounded arrival at the first main menu on 1x Vulkan, not menu
navigation, player selection, gameplay, sustained performance at the frontend's
higher resolution, or audible output. Both runs use the `1104466C…` launcher
and `4772F734…` module described in [RECOMP_PERFORMANCE.md](RECOMP_PERFORMANCE.md).
