# Local EXE package and ROM import

The experimental launcher is `DW4_PC.exe`. It accepts a user-selected Digimon
World 4 USA GameCube image, checks disc ID `GDJEB2`, exports into a new staging
directory, validates the executable and game-file identities, and publishes
the prepared folder only after validation passes. No compiler or manual disc
extraction is part of this packaged launcher workflow.

The application hosts the original-game session in its own process. No
`moderngekko-run.exe` is included or required by the package. The compiled
game module, `Sys` resources and matching x64 Microsoft Visual C++ runtime
DLLs remain adjacent dependencies; keep the whole package together. Static
module/resource integration is still required for the final single-file
binary distribution. Original disc content is supplied locally and is never
included in the package or repository.

## Building and packaging

Run `scripts/build-recomp.ps1` as documented in
[RECOMPILATION.md](RECOMPILATION.md), then run:

```powershell
./scripts/package-recomp.ps1
```

The script verifies the completed build record, module ABI evidence and current
patch hashes before creating a new dated directory under `dist/DW4_Recomp/`.
The package includes dependency notices, source pins, local runtime patches,
and a manifest of binary identities. Existing packages are preserved. Packaging
does not open the launcher or import any game content.

The default user profile is `savedata` beside the actual executable, independent
of the working directory. Settings, memory cards, states and imported game data
live there. Earlier AppData/Documents profiles are preserved; automatic save
migration and a revised internal storage layout remain pending. The
developer-script profile under `work/recomp-user` is separate. A valid existing
import is reused. An invalid
existing destination is preserved and reported; the importer does not silently
replace it. Diagnostic runs use an explicit isolated `--user-dir`.

## Validation boundaries

The integrated package passed the import checks and a 70-second hidden original
startup with its adjacent game DLL and no runner EXE present. The run used the
new default profile without `--user-dir`, from a different working directory;
the runtime wrote its graphics configuration in the expected adjacent
`savedata`. Its input profile was neutral and its real Cubeb device was verified
at zero volume. The temporary test profile was archived with the evidence so
the packaged application retains its ordinary first-run defaults.

This establishes the integrated command-line session and default path, not an
interactive Play-button or netplay acceptance test. Separate hidden launcher
images verify the import UI before and after the actual ROM import. The
opening movie still slows to approximately 30% simulation speed. Save/load
roundtrips, full gameplay and single-file distribution remain
pending. Evidence stays local under `work/recomp-import/20260913-073647-debed570`
and `work/recomp-startup/20260913-073751-6e84201c`.

The later integrated F1 menu has separate native-surface rendering and input
checks; see [RECOMP_INGAME_MENU.md](RECOMP_INGAME_MENU.md). Those checks do not
extend the ROM import or audible-performance acceptance scope.

The current package additionally passes a 90-second integrated session with a
controlled intro skip and controller-state synchronization. Both input commands
are acknowledged before shutdown, and captures show the original "PRESS START"
title screen. The game remains in the single EXE process with its adjacent game
module, using the portable default profile. This profile was also archived after
the test. See `work/recomp-startup/20260913-074432-035d0acd` and
[RECOMP_AUDIO.md](RECOMP_AUDIO.md) for the remaining audio/performance limits.

The baseline DOL SHA256 is
`ec61c010477c844781763a3041922e38719b3ced3b74be5ce71d778ce458e471`.
The required `files/` tree manifest SHA256 is
`8b415a50951a40dac2ae1945fed8377dd0218617eb2703aef5a8b75f4503e5fe`.
The manifest hashes sorted relative paths, lengths and each file's content
hash; it is not an ISO hash. Baseline validation is separate from future mod
overlays, which must not silently redefine the known original data.

The parser bounds FST nesting before recursively constructing the disc tree
and uses 64-bit arithmetic for its table-size check. The importer separately
limits nesting, entry count and total exported bytes. It rejects path traversal,
Windows-reserved names and case-insensitive duplicate destinations before
export. Export failures propagate; failed newly owned staging data is cleaned
up only after its resolved location is checked. Earlier imports and unrelated
directories are not cleanup targets.

Run the bounded local checks against a built or packaged launcher:

```powershell
./scripts/test-recomp-import.ps1 -Launcher '<package>/DW4_PC.exe' -IncludeOriginalRom
```

Without `-IncludeOriginalRom`, checks use only synthetic malformed discs, an
actual-header filename policy test, and a hidden two-frame launcher image.
The optional original-data check imports the owner's ISO, compares every
exported file with the independent extracted reference, checks repeat-import
reuse and verifies preservation of an invalid existing folder. It uses a
Unicode profile path and excludes development tools from the child process's
`PATH`. Each child has a 60-second bound and redirected diagnostic output.
Inspect the saved `launcher.png` before accepting the visual result.

These checks do not execute the game. Passing them establishes this importer
and local package on the tested Windows host; gameplay, audible output, four
independent controllers, save states and online compatibility need their own
acceptance checks. The supplied frontend exposes upstream controls whose
presence alone does not establish DW4 feature support.

## Verified local result

On 2026-09-13 the packaged launcher passed 47 actual-header filename-policy
checks and all 18 bounded CLI/frontend cases, plus complete file equivalence
for the original-data import. All nine exported files matched their independent
reference hashes. The native validator independently returned both required
SHA256 identities above. Reuse and preservation checks passed. The hidden
820x700 frontend images before and after import were decoded and inspected.

These checks ran on Windows 11 with a fresh `profile-ü日本` directory, a dummy
SDL video driver and software renderer, and only Windows system directories
on the child's `PATH`. Both packaged EXEs declare the Windows GUI subsystem;
their non-system MSVC DLL imports are included beside them. The redistribution
folder is version 14.51.36231; its DLL file versions are 14.51.36247.0. This is
local package evidence, not a substitute for a clean-machine installation test.
