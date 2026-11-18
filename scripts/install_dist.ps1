<#
.SYNOPSIS
Installs a configured build folder into a folder that then holds exactly the release layout (build.ps1 -Package).

.DESCRIPTION
Runs "cmake --install" of -BuildDirectory into -Prefix (CMakeLists.txt's install rules: the release layout, docs/DEVELOPMENT.md
"Release packages"). A file an earlier install into the same -Prefix put there (the build folder's install_manifest.txt from
before) that the layout no longer has is removed, and so is a folder that became empty by that: an older release's
dw4_inspect.exe and developer docs no longer stay beside the new layout. Files the install never put there (saves, settings.ini,
mods, a converted ISO in data) stay.
#>
param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$Prefix,
    [string]$CMake = 'cmake'
)
$ErrorActionPreference = 'Stop'
$prefixFull = [System.IO.Path]::GetFullPath($Prefix).TrimEnd('\')
$manifestPath = Join-Path $BuildDirectory 'install_manifest.txt'
function Read-Manifest {
    if (-not (Test-Path -LiteralPath $manifestPath)) { return @() }
    # (Its lines mix '/' and '\' and hold "/./": the full paths compare.)
    return @(Get-Content -LiteralPath $manifestPath | Where-Object { $_ } | ForEach-Object { [System.IO.Path]::GetFullPath($_) })
}
$before = Read-Manifest
& $CMake --install $BuildDirectory --prefix $Prefix
if ($LASTEXITCODE -ne 0) { throw 'cmake --install failed.' }
$now = Read-Manifest
$inPrefix = $prefixFull + '\'
$folders = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)
foreach ($file in $before) {
    if (-not $file.StartsWith($inPrefix, [System.StringComparison]::OrdinalIgnoreCase)) { continue }
    if ($now -contains $file) { continue }
    if (Test-Path -LiteralPath $file -PathType Leaf) {
        Remove-Item -LiteralPath $file -Force
        Write-Host "Removed (no longer part of the release layout): $($file.Substring($inPrefix.Length))"
    }
    for ($folder = Split-Path -Parent $file; $folder.Length -gt $prefixFull.Length; $folder = Split-Path -Parent $folder) { [void]$folders.Add($folder) }
}
# (The deepest first: a folder emptied by the removals above, never one with a file of anyone else.)
foreach ($folder in ($folders | Sort-Object -Property Length -Descending)) {
    if ((Test-Path -LiteralPath $folder -PathType Container) -and -not (Get-ChildItem -LiteralPath $folder -Force | Select-Object -First 1)) {
        Remove-Item -LiteralPath $folder -Force
    }
}
