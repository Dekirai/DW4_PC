# DW4 static recompilation

The owner selected DolRecomp and its recommended ModernGekko template on
2026-09-13. This is now the next implementation path toward the complete
original game. The existing native core remains available for reference,
asset investigation and regression comparisons.
The recompiled game does not call that manually recovered engine. Retain its
useful analysis and tests, but prioritize observed failures in the recompiled
game instead of extending a separate full-engine reconstruction.

The owner's updated PC target includes aggressive optimization, a measured
60 FPS baseline and presentation options up to 300 FPS, 16:9, higher rendering
and texture resolution, ImGui graphics/cheat/save-state controls, modding and
online four-player co-op. The final frontend should accept a user-provided ROM
and prepare the required assets automatically through a simple EXE entry point.
These are future acceptance gates in [ROADMAP.md](ROADMAP.md), not capabilities
established by translation or the current developer launcher.
The supplied USA data's actual language content and remaining selector/font
checks are documented in [LOCALIZATION.md](LOCALIZATION.md).

## Local source inputs

- DolRecomp: `C:/Users/shinz/Downloads/DolRecomp-main`.
- Template: `C:/Users/shinz/Downloads/ModernGekko-Template-main`.
- Extracted game: `E:/Games/Emulator/Dolphin-x64/Games/Digimon World 4 - Extracted`.
- Original executable: `sys/main.dol` inside that extracted root.
- Original executable SHA-1: `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.
- Game ID: `GDJEB2`.

The template names DolRecomp and ModernGekko as its two subprojects. Its
`recompile` target builds a module, while `run` also launches it. Use the build
path for the owner's manual previews. All generated code, game data, downloaded
dependencies and build output stay in ignored directories under this repository.
GitHub Actions remains disabled; validation runs locally.

## Local build

Run from the repository in PowerShell with a Visual Studio C++ workload, CMake,
Ninja, Git and Python 3.8 or newer available. The script discovers the compiler
and Python locally and follows the supplied template's C-backend build recipe.

```powershell
./scripts/build-recomp.ps1 `
    -DolRecompSource 'C:/Users/shinz/Downloads/DolRecomp-main' `
    -TemplateSource 'C:/Users/shinz/Downloads/ModernGekko-Template-main' `
    -ExtractedRoot 'E:/Games/Emulator/Dolphin-x64/Games/Digimon World 4 - Extracted'
```

The wrapper verifies the USA disc ID and DOL hash, obtains the pinned runtime
and recursive dependencies, applies the documented local source override and
builds the runtime and game module. It rejects unexpected checkout changes
instead of resetting them. `-Jobs 4` is the default parallelism. `-OptLevel 2`
builds optimized game code for manual testing; `-OptLevel 0` is available only
as a slower diagnostic profile. Use `-RuntimeOnly` to build just the runtime.
The build also produces the branded `DW4_PC.exe` frontend. Local packaging and
automatic ROM-import checks are documented in [ROM_IMPORT.md](ROM_IMPORT.md).
Compiler optimization does not replace behavior/performance validation.
No build option launches the game.

Before publishing a successful build record, `scripts/test-recomp-abi.ps1`
compiles separate public-header and chassis-header probes. It compares all
49 CPU fields, the 16 module-descriptor fields and three nested ABI records,
including types, offsets, sizes and alignment. It then loads the actual DLL's
descriptor and checks the ABI versions, CPU size, game ID, entry point and code
coverage without calling game dispatch. The verified Windows layout is CPU
ABI 3, 3,528 bytes. The checker builds cleanly with `/W4 /WX`.

To open the experimental game yourself after a verified build, run
`./scripts/start-recomp.ps1`. The current local shortcut is
`dist/DW4_Recomp/Play-Recompiled.cmd`. The launcher checks the build's hashes
and ABI report before starting and keeps its persistent settings/saves in
`work/recomp-user`. `-ValidateOnly` checks the launch inputs without opening
the game. This is an experimental local build, not a portable release package
or a claim of complete gameplay validation.

The build record is `work/recomp-build-record.json`; logs and dependency pins
are also in `work/`. Runtime output is in `work/moderngekko-build`, and generated
modules are cached in `work/recomp-modules`. These locations are ignored by Git.
See [the runtime patch notes](../patches/README.md) for the local adaptations.

## Bounded startup check

After a successful module build, run the explicit diagnostic separately:

```powershell
$build = Get-Content work/recomp-build-record.json -Raw | ConvertFrom-Json
./scripts/test-recomp-startup.ps1 `
    -Module $build.module_path -ExtractedRoot $build.extracted_root
```

It uses a fresh profile under `work/recomp-startup/`, the Null graphics backend,
muted audio and no mods. The runner must match the recorded build and current
headless-alert patch. The script observes a bounded run, requests shutdown and
uses a watchdog on its own child process if needed. Runtime logs, frame status,
sampled dispatch addresses and a JSON classification remain in that run's
directory. A hung process or absent frame progression is not a passing result.

`frame_timing` in new summaries reports the final approximately five- and
ten-second counter windows using their actual observation durations. It uses
the last contiguous running segment before stop, reports short windows and
unavailable values explicitly, and never treats duplicate presents as unique
frames. The calculation is separate from startup pass criteria. Its synthetic
checks run without game data or a runtime launch:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/recomp_startup_metrics_tests.ps1
```

Each frame window also records process CPU time using its own observation
duration, average busy cores and CPU milliseconds per unique-frame advance.
Busy cores are total process CPU seconds divided by elapsed seconds, not a
machine-wide utilization percentage. Invalid or nonmonotonic samples produce
an unavailable result. `-IdleMode Enabled` and `-IdleMode Disabled` select the
guarded DW4 scheduler optimization in a fresh profile for a same-binary A/B
comparison; the generic `StaticRecompIdlePC` remains zero.

For package acceptance, `-UseBundledModule` requires `-Module` to identify the
hash-matched `gGDJEB2_recomp.dll` beside the selected `-Runner`. It omits the
runner's explicit module argument so the real executable-relative discovery
path is exercised. All build, ABI and content checks remain active; the child
receives no inherited module override. The imported game folder may be supplied
as `-ExtractedRoot`. This verifies package startup without opening a game window.

This check exercises original CPU execution and runtime startup. Null graphics
does not validate rendered pixels; audio, controls, saves and playable game
behavior require separate acceptance. The runtime's reported interpreter
fallback counter excludes its fallback JIT branch, so zero in that counter
does not establish wholly native execution.

Use `-Graphics Vulkan -Screenshot` for the separate offscreen rendering check.
It queues a late screenshot after frame progression, requires the processed
command and a PNG header, and records the image path/hash. Decode and inspect
that image before claiming visual acceptance. Vulkan headless mode creates
real GPU resources without a render window, surface or swapchain.

The hidden test deliberately uses 640x528 at 1x EFB and NullSound. NullSound
sets output sample rate to zero, so these measurements exclude host audio
queuing, resampling and device callbacks. The current separate manual profile
uses the 1920x1080 option, which maps to 3x EFB, and ordinarily selects Cubeb
audio. Normal runtime speed defaults to 1.0. These differences limit comparisons
with the owner's main-menu report: its exact executable identity was not
recorded, and no hidden-test result establishes that audible stutter is fixed.

## Responsibilities

[DolRecomp](https://github.com/ExpansionPak/DolRecomp) statically translates
the PowerPC CPU program to C or native LLVM objects. The selected initial
backend is C, built with the available Windows compiler.

[ModernGekko](https://github.com/ExpansionPak/ModernGekko) supplies the runtime.
The recommended [template](https://github.com/ExpansionPak/ModernGekko-Template)
uses a Dolphin-derived core for video, audio and other console services. The
resulting architecture combines compiled original CPU logic with those runtime
services. This differs from the existing manually recovered engine operations
and must be validated on its own behavior.

The runtime dependency was obtained in `work/moderngekko` at revision
`5417826c31187d4dadf8588c7aa25bf107782936`. Its nested dependencies and any
local build adaptations must be recorded before distributing a build. In
particular, the runtime's port helper must use the owner's selected DolRecomp
source instead of silently substituting a different bundled compiler.

## Evidence so far

The supplied DolRecomp source builds locally with MSVC 19.51 and CMake 4.3.1.
All 19 upstream tool tests pass. The build has seven upstream CRT deprecation
warnings; it is not warning-free. A source manifest confirms its 161 compared
files match upstream revision `40637c4683bd2820ac5b23607ee344720beb26df`
after normalizing line endings.

The actual DW4 executable translates with the Gekko CPU profile and C backend.
Output contains 254 generated C chunks. The tool reports 1,037,440 decoded
instruction slots: 1,035,788 recognized instructions, 1,652 embedded-data slots
and zero unknown slots. These are decoder counts, not a proof that every live
instruction path or indirect call behaves correctly.

All 254 chunks also compile with the supplied DolRecomp CPU implementation and
link into a standalone CPU library. A link checker resolves the original entry
point and a researched collision function without executing either. This
compilation check uses `/Od /bigobj /W3 /fp:strict` and completes without compiler
warnings. It is not the runtime game module and supplies no performance result.

ModernGekko's runtime and port helper also build locally with MSVC. Its pinned
RecompCore dependency is `55c7b023fa0f4eba1cf3fdbbb25b1c5ec468d5ac`.
The local configuration enables GameCube controller support, and the port
helper uses the supplied DolRecomp source. Vendored dependency builds emit
upstream warnings; this runtime build is not warning-free.

The C module adapter compiles generated chunks against the runtime's GXRuntime
CPU header and implementation, then exports the required module ABI. The
standalone CPU library above must not be substituted for that adapter: the
supplied tool and runtime CPU structures are not identical. Successful module
compilation must be followed by behavior checks.

The pinned ModernGekko public header also disagrees with its own pinned
RecompCore dependency: it advertises CPU ABI 4 and adds `cycle_budget`, while
the actual chassis and module use GXRuntime ABI 3. The first hidden startup
correctly rejected this combination before game execution. The local patch
aligns the public version and complete structure with the actual pinned
execution layout. It preserves the loader's version and size checks and
does not modify the supplied DolRecomp sources. Neither the generated C nor
the selected host/runtime implementation uses that extra field.

The actual `gGDJEB2_recomp.dll` now builds successfully through that adapter,
including all 254 chunks, the GXRuntime CPU implementation and module exports.
The first `/Od` module build reports 261 MSVC D9025 warnings because the upstream
Release configuration sets `/O2` before the explicit `/Od` option. These are
option-override warnings, not a warning-free build. The selected diagnostic
profile has no measured gameplay performance beyond the bounded boot counters
reported below. A repeated wrapper build checks
the input and source records before using the module cache.

The generated report identifies 166 possible self-modifying-code ranges.
Those warnings require examination against reached runtime behavior; they are
not evidence that all those locations modify code during play. Do not patch
them out solely to silence the report.

## Original startup result

The corrected build passes a 30-second hidden Null-backend startup check on
2026-09-13. The runtime loads the actual generated module at original entry
`0x80005240`, reports 46,442,407 native dispatches and reaches frame count 744
with present count 1,495. Ninety pre-stop observations record continuing frame
progress. It exits normally with code 0 after the first stop command, without
watchdog termination or headless alerts. DOL, module and runner hashes remain
unchanged across the run.

The trace contains 45 sampled native dispatch rows. Runtime counters report
zero interpreter-fallback steps, 4,883,412 `hook_fb`, 17,317 `native_exc`,
192 chunk verifications, five reverify events and zero `smc_failed`. These are
the runtime's own counters; the interpreter-fallback value excludes its
fallback-JIT branch. They do not prove wholly native execution or that every
suspected SMC site is correct.

Evidence remains in
`work/recomp-startup/20260912-231156-adcec009/summary.json`, its dispatch trace
and status history. The earlier ABI rejection is retained separately at
`work/recomp-startup/20260912-225859-5c9ae006/summary.json`.

This establishes bounded original startup and sustained CPU/frame progress.
Rendered output, audio, controls, saves and original gameplay remain separate
acceptance gates. The previous walking preview remains a separate program.

## Offscreen rendering and initial performance

A separate 30-second Vulkan run passes after correcting a headless-only
shader-progress callback in the pinned RecompCore. That callback previously
accessed an ImGui context which headless mode deliberately does not create.
The dependency patch skips only its visual progress UI; shader compilation,
waiting and result retrieval still run, and ordinary windowed behavior is
unchanged.

The run reaches 494 frames and 995 presents, exits normally, and produces an
actual 640-by-527 PNG. Decoding and visual inspection show the original
"Licensed by Nintendo" boot screen. This is a rendered original boot image,
not main-menu or gameplay acceptance. Evidence is in
`work/recomp-startup/20260912-232452-98da1237/`.

This initial module used `/Od`. Its final approximately ten seconds advance
407 frame-counter steps in 10.21035 seconds (39.86 per second); the last reported
simulation speed is 0.685052. The runtime's `fps`/`vps` status fields remain zero
in this mode and were not used for the measurement. Counter deltas are not
monitor presentation FPS. The owner separately reports low FPS and strongly
stuttering audio at the main menu. The optimized module now builds with
`/O2 /fp:strict`, with a separate cache entry that preserves the `/Od` baseline.
Its full ABI check passes, and the manual launcher accepts its verified inputs
under Windows PowerShell 5.1 without opening the game. The optimized DLL SHA256
is `526A56A0BCE6C675CAF1274F1E76F54A455C243DBF84A1235A5721EA97C6DAA9`.
Audio quality must be checked separately from muted startup/frame measurements.

The optimized module passes the equivalent 30-second hidden Vulkan check at
`work/recomp-startup/20260912-233931-c132eb66/`. It exits normally without alerts
or watchdog intervention; module, runner and original DOL hashes are unchanged.
The decoded 640-by-527 screenshot now shows the original memory-card check.
This is additional original boot progress, not gameplay or successful save/load
acceptance. All 254 generated chunks compile under `/O2` without native compiler
warnings, and the ABI probe also remains warning-free. The upstream compiler
version probe's `cl --version` diagnostics remain in the module manifest and do
not describe the actual module compilation result.

| Same bounded startup conditions | Diagnostic `/Od` | Optimized `/O2` |
| --- | --- | --- |
| Final approximately 10-second frame-counter rate | 39.86/s | 60.22/s |
| Final approximately 5-second frame-counter rate | 39.18/s | 60.44/s |
| Last pre-stop reported simulation speed | 0.685052 | 0.999073 |
| Screenshot at approximately 21 seconds | Nintendo license screen | Memory-card check |

The optimized final ten-second window advances 608 frame-counter steps over
10.09555 seconds; its final five-second speed range is 0.998478 to 1.001610.
Both runs use the same runner, game input, backend and fresh-profile procedure.
They reach different boot phases because the optimized game advances faster,
so this establishes original-speed startup throughput, not a controlled
identical-scene gameplay speedup or a universal performance percentage.
The observation timer excludes the subsequent orderly stop/grace interval.

Both bounded graphics runs use a fresh profile and the headless Null sound
backend. In this runtime, `--mute` changes volume, while headless mode selects
Null sound independently. Null sound skips normal host mixer/output work, so
these runs can compare the same headless workload but cannot establish audible
playback quality or live device underruns. Optional DSP/DTK WAV dumps contain
source samples before host output/resampling; a clean dump alone would not
establish that the user's reported playback stutter is fixed.

The test host has an AMD Ryzen 7 7800X3D, 32 GB RAM and Windows 11 Pro build
26200, with NVIDIA GeForce RTX 5070 Ti and AMD integrated graphics enumerated.
These hardware names do not identify the active Vulkan adapter by themselves.
The current image check uses the original-size render output, not 4K or MSAA
acceptance. Preserve these conditions when comparing compiler profiles.

## Next PC integration boundaries

The packaged build now also passes a same-runner comparison of the guarded
DW4 scheduler optimization and automatic bundled-module discovery. It lowers
measured process CPU cost per boot frame-counter advance by about 46% while
retaining original speed and the same visible boot image. This is separate
from the historical compiler-profile comparison above. See
[RECOMP_PERFORMANCE.md](RECOMP_PERFORMANCE.md) for exact conditions and limits.

Keep `EmulationSpeed=1.0` during performance work. The runtime's zero/unlimited
setting removes guest-clock throttling; CPU/VI overclock settings also change
console timing. Neither implements 300 FPS presentation at unchanged game speed.
The retained DW4 timer research establishes nominal 60 Hz time units for known
consumers, not frame independence across the entire game. Measure the actual
update/render/VI scheduling and verify rendering separation or interpolation.

The pinned runtime already contains useful implementation foundations:

- Dolphin's graphics configuration supports aspect ratio, internal EFB scale,
  VSync/MSAA and hashed PNG/DDS replacement textures. Camera, culling, HUD,
  texture-pack behavior and backend sample support still need DW4 validation.
- ModernGekko provides state-save/load commands and native mod ABI hooks. State
  compatibility and a real restore roundtrip remain required before claiming
  reliable save states or modded gameplay.
- Its separate SDL/ImGui launcher contains a DiscIO importer and NetPlay tools.
  A DW4 in-game menu requires video-thread drawing, input capture and safe CPU
  commands; four GameCube player mappings and multi-machine behavior require
  their own integration. The existing tools do not establish working DW4 online
  co-op. The local automatic-ROM package now passes its import/startup checks;
  complete release acceptance remains separate.

Use those runtime foundations selectively instead of reconstructing equivalent
services in the manual core. Preserve native-code verification, original timing,
state restoration and mod/controller ownership while optimizing measured hot
paths. No arbitrary idle-loop address, instruction-cost change or fast-math
substitution is enabled by the current compiler optimization.
