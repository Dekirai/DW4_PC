# Local EXE package and ROM import

The experimental launcher is `DW4_PC.exe`. It accepts a user-selected Digimon
World 4 USA GameCube image, checks disc ID `GDJEB2`, exports into a new staging
directory, validates the executable and game-file identities, and publishes
the prepared folder only after validation passes. No compiler or manual disc
extraction is part of this packaged launcher workflow.

The entry point still requires its adjacent runtime EXE, compiled module,
`Sys` resources and matching x64 Microsoft Visual C++ runtime DLLs. Keep the
whole package together. This is one application entry point, not a single-file
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

The default user profile is `%LOCALAPPDATA%/DW4_PC`; settings, memory cards,
states and imported game data live there. The developer-script profile under
`work/recomp-user` is separate. A valid existing import is reused. An invalid
existing destination is preserved and reported; the importer does not silently
replace it. Diagnostic runs use an explicit isolated `--user-dir`.

## Validation boundaries

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
