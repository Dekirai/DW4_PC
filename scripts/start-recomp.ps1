# Run explicitly to open the experimental recompiled game. Builds never call this script.
param([switch]$ValidateOnly)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
$recordPath = Join-Path $repoRoot 'work/recomp-build-record.json'
if (-not (Test-Path -LiteralPath $recordPath -PathType Leaf)) {
    throw 'Build the game first with scripts/build-recomp.ps1.'
}
$build = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
foreach ($field in @('module_built','module_path','module_sha256','extracted_root',
    'runner_binary_sha256','runtime_patch_sha256','chassis_patch_sha256',
    'abi_report_path','abi_report_sha256','main_dol_sha256')) {
    if (-not $build.PSObject.Properties[$field]) { throw "Incomplete build record: $field. Rebuild before starting." }
}
if ($build.module_built -isnot [bool] -or -not $build.module_built) {
    throw 'The latest module build is incomplete. Rebuild before starting.'
}
$runner = Join-Path $repoRoot 'work/moderngekko-build/moderngekko-run.exe'
$patch = Join-Path $repoRoot 'patches/moderngekko-local-runtime.patch'
$checks = @(
    @($runner,$build.runner_binary_sha256),
    @($patch,$build.runtime_patch_sha256),
    @((Join-Path $repoRoot 'patches/recompcore-headless-shader-progress.patch'),$build.chassis_patch_sha256),
    @($build.module_path,$build.module_sha256),
    @($build.abi_report_path,$build.abi_report_sha256),
    @((Join-Path $build.extracted_root 'sys/main.dol'),$build.main_dol_sha256)
)
foreach ($check in $checks) {
    if (-not (Test-Path -LiteralPath $check[0] -PathType Leaf) -or
        (Get-FileHash -LiteralPath $check[0] -Algorithm SHA256).Hash -ne $check[1]) {
        throw "A verified build input changed or is missing: $($check[0]). Rebuild before starting."
    }
}
$abi = Get-Content -LiteralPath $build.abi_report_path -Raw | ConvertFrom-Json
if ($abi.passed -isnot [bool] -or -not $abi.passed) { throw 'The module has not passed its ABI check.' }
if ($ValidateOnly) {
    Write-Host 'Manual launcher inputs verified. No game launched.'
    return
}
$profile = Join-Path $repoRoot 'work/recomp-user'
New-Item -ItemType Directory -Path $profile -Force | Out-Null
Write-Host 'Starting the experimental DW4 recompilation. Full gameplay, audio, saves and local multiplayer are still under validation.'
Write-Host "Settings and saves: $profile"
$previousStatic = [Environment]::GetEnvironmentVariable('MODERNGEKKO_STATICRECOMP','Process')
try {
    [Environment]::SetEnvironmentVariable('MODERNGEKKO_STATICRECOMP','1','Process')
    & $runner --game $build.extracted_root --module $build.module_path --user-dir $profile --no-mods --title 'Digimon World 4 PC - Experimental Recompilation'
    if ($LASTEXITCODE -ne 0) { throw "The runtime exited with code $LASTEXITCODE." }
} finally {
    [Environment]::SetEnvironmentVariable('MODERNGEKKO_STATICRECOMP',$previousStatic,'Process')
}
