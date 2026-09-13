# Package an already verified local build. This script never imports or launches a game.
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'start-recomp.ps1') -ValidateOnly
$record = Get-Content -LiteralPath (Join-Path $repoRoot 'work/recomp-build-record.json') -Raw | ConvertFrom-Json
$runtimeBuild = Join-Path $repoRoot 'work/moderngekko-build'
$runtimeSource = Join-Path $repoRoot 'work/moderngekko'
$launcher = Join-Path $runtimeBuild 'DW4_PC.exe'
if (-not $record.PSObject.Properties['launcher_binary_sha256'] -or
    (Get-FileHash -LiteralPath $launcher).Hash -ne $record.launcher_binary_sha256) {
    throw 'Build the verified DW4 launcher before packaging.'
}
$compilerInstall = $record.compiler_installation
$redistVersion = (Get-Content -LiteralPath (Join-Path $compilerInstall 'VC/Auxiliary/Build/Microsoft.VCRedistVersion.default.txt') -Raw).Trim()
$redistRoot = Join-Path $compilerInstall ("VC/Redist/MSVC/$redistVersion/x64")
$crtFolders = @(Get-ChildItem -LiteralPath $redistRoot -Directory | Where-Object { $_.Name -match '^Microsoft\.VC[0-9]+\.CRT$' })
if ($crtFolders.Count -ne 1) { throw 'Cannot identify the compiler-matched x64 redistributable CRT.' }
$crtFiles = @(Get-ChildItem -LiteralPath $crtFolders[0].FullName -File -Filter '*.dll')
foreach ($required in @('msvcp140.dll','msvcp140_atomic_wait.dll','vcruntime140.dll','vcruntime140_1.dll')) {
    if ($required -notin $crtFiles.Name) { throw "The x64 CRT is missing $required." }
}
$redistNotice = Join-Path $compilerInstall 'Licenses/1033/Redist.txt'
if (-not (Test-Path -LiteralPath $redistNotice -PathType Leaf)) { throw 'The Visual C++ redistribution notice is missing.' }
$destination = Join-Path $repoRoot ('dist/DW4_Recomp/PC-Preview-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))
if (Test-Path -LiteralPath $destination) { throw 'The package destination already exists; no files were replaced.' }
New-Item -ItemType Directory -Path $destination | Out-Null
Copy-Item -LiteralPath $launcher -Destination (Join-Path $destination 'DW4_PC.exe')
Copy-Item -LiteralPath (Join-Path $runtimeBuild 'moderngekko-run.exe') -Destination (Join-Path $destination 'moderngekko-run.exe')
Copy-Item -LiteralPath $record.module_path -Destination (Join-Path $destination 'gGDJEB2_recomp.dll')
Copy-Item -LiteralPath (Join-Path $runtimeBuild 'Sys') -Destination (Join-Path $destination 'Sys') -Recurse
Copy-Item -LiteralPath (Join-Path $repoRoot 'licenses') -Destination (Join-Path $destination 'licenses') -Recurse
Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md') -Destination (Join-Path $destination 'THIRD_PARTY_NOTICES.md')
Copy-Item -LiteralPath (Join-Path $runtimeSource 'vendor/dolphin/LICENSES') -Destination (Join-Path $destination 'licenses/Dolphin-SPDX') -Recurse
Copy-Item -LiteralPath (Join-Path $runtimeSource 'vendor/dolphin/Externals/SDL/SDL/LICENSE.txt') -Destination (Join-Path $destination 'licenses/SDL-runtime.txt')
Copy-Item -LiteralPath (Join-Path $runtimeSource 'vendor/dolphin/Externals/imgui/imgui/LICENSE.txt') -Destination (Join-Path $destination 'licenses/ImGui-runtime.txt')
Copy-Item -LiteralPath $redistNotice -Destination (Join-Path $destination 'licenses/Microsoft-Visual-Cpp-Redist.txt')
$crtManifest = @($crtFiles | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $destination $_.Name)
    [ordered]@{name=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash;file_version=$_.VersionInfo.FileVersion}
})
Copy-Item -LiteralPath (Join-Path $repoRoot 'patches') -Destination (Join-Path $destination 'patches') -Recurse
$record.dependency_pins | Set-Content -LiteralPath (Join-Path $destination 'source-pins.txt')
@'
Digimon World 4 PC - experimental local preview

Open DW4_PC.exe and select your Digimon World 4 USA GameCube ROM.
The launcher validates and imports the game files automatically. No compiler
or manual disc extraction is used by this packaged launcher. Keep the EXE,
runtime DLL and Sys folder together.
The matching x64 Microsoft Visual C++ runtime DLLs are included beside the EXE.

Settings, saves and imported data use %LOCALAPPDATA%\DW4_PC. The separate
developer-script preview uses its own profile under the repository's work/.
Existing invalid import folders are preserved rather than replaced.

Original game assets are not included. This is an experimental local build;
complete gameplay, audio, controller, mod and online compatibility are still
being validated. The launcher settings are separate from the future in-game
ImGui menu. Higher presentation rates and widescreen remain in development.

Source and build instructions: https://github.com/Dekirai/DW4_PC
Runtime source: https://github.com/ExpansionPak/ModernGekko
Dependency revisions and local runtime patches are included in this folder.
'@ | Set-Content -LiteralPath (Join-Path $destination 'README.txt') -Encoding utf8
$manifest = [ordered]@{
    game_id='GDJEB2'; entrypoint='DW4_PC.exe'; original_asset_files_included=$false;
    main_dol_sha256=$record.main_dol_sha256; required_assets_sha256=$record.required_assets_sha256;
    module_sha256=$record.module_sha256; runner_sha256=$record.runner_binary_sha256;
    launcher_sha256=$record.launcher_binary_sha256;
    visual_cpp_redist_version=$redistVersion; visual_cpp_runtime_files=$crtManifest;
    runtime_revision=$record.runtime_revision; chassis_revision=$record.chassis_revision;
    runtime_patch_sha256=$record.runtime_patch_sha256; chassis_patch_sha256=$record.chassis_patch_sha256;
    packaged_at_utc=[DateTime]::UtcNow.ToString('o')
}
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $destination 'package-record.json')
Write-Host "Local package: $destination"
Write-Host 'No game assets were copied and no application was launched.'
