param([string]$Data)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$appPath = Join-Path $projectRoot 'dist\DW4_PC\dw4_pc.exe'
if (-not (Test-Path -LiteralPath $appPath)) { $appPath = Join-Path $projectRoot 'build\Release\dw4_pc.exe' }
if (-not (Test-Path -LiteralPath $appPath)) { throw 'Build first using scripts/build.ps1 -Package.' }
$appArguments = @('--config', (Join-Path $projectRoot 'local\settings.ini'))
if ($Data) { $appArguments += @('--data', $Data) }
& $appPath @appArguments
exit $LASTEXITCODE
