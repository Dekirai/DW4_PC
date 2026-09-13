# Portable configuration migration checks

Run `scripts/test-recomp-portable-profile.ps1` on Windows with the Visual Studio
C++ workload. The checker compiles the actual `portable_profile.hpp` with
optimized MSVC and active failure checks. It does not build or launch the game.

The migration copies legacy `Config/Dolphin.ini` bytes to `Config/Runtime.ini`
only when no current file exists. Existing current settings, the legacy file
and saves are preserved. A locked source, exclusive temporary file, bounded
copy and no-replace publication of the open file handle prevent partial output
and replacement of a concurrent destination. Failed temporary files are removed
through their retained handles. The automatic migration is Windows-specific;
other hosts receive manual-copy guidance when a legacy file needs migration.

Fixtures cover missing and existing files, empty and malformed bytes, Unicode
paths, directory inputs, concurrent writers, the exact 4 MiB bound and overflow,
stale temporary files, and a destination created during copying. The losing
copy must preserve the winning destination and remove its own temporary file.
Symlink fixtures are reported as skipped when Windows denies their creation.
Results and source/compiler hashes are recorded under ignored
`build/recomp-portable-profile-check/`.
