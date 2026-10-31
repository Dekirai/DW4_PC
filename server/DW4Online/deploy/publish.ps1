<#
.SYNOPSIS
  Builds a self-contained Release of DW4Online for the Windows Server (win-x64) and zips it.

.DESCRIPTION
  Runs the unit tests (and the integration tests when DW4ONLINE_TEST_MYSQL is set), publishes DW4Online.Server
  self-contained for win-x64 into dist\DW4Online-<version> at the repository root (gitignored), copies the deployment
  kit and the SQL migrations next to it and writes dist\DW4Online-<version>.zip. Nothing is uploaded or installed.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File deploy\publish.ps1
#>
param(
    [string]$Configuration = 'Release',
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$serverRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Resolve-Path (Join-Path $serverRoot '..\..')
$project = Join-Path $serverRoot 'src\DW4Online.Server\DW4Online.Server.csproj'
$version = ([xml](Get-Content (Join-Path $serverRoot 'Directory.Build.props'))).Project.PropertyGroup.Version
$name = "DW4Online-$version"
$out = Join-Path $repoRoot "dist\$name"
$zip = Join-Path $repoRoot "dist\$name.zip"

Write-Output "dotnet SDK: $(dotnet --version)"
# No shared compiler server: with the 11.0 RC SDK it generates broken Razor code when DW4Online.Web is built a second
# time in a row (unit tests, integration tests and the win-x64 publish each build it; see the README). Separate
# compiler processes per build are slower but always compile the unchanged pages.
$noServer = '--disable-build-servers'
if (-not $SkipTests) {
    dotnet test (Join-Path $serverRoot 'tests\DW4Online.Tests') -c $Configuration $noServer
    if ($LASTEXITCODE -ne 0) { throw 'Unit tests failed.' }
    if ($env:DW4ONLINE_TEST_MYSQL) {
        dotnet test (Join-Path $serverRoot 'tests\DW4Online.Integration.Tests') -c $Configuration $noServer
        if ($LASTEXITCODE -ne 0) { throw 'Integration tests failed.' }
    } else {
        Write-Output 'DW4ONLINE_TEST_MYSQL is not set: integration tests skipped (scripts\local-db.ps1 start / env).'
    }
}

if (Test-Path $out) { Remove-Item -Recurse -Force $out }
dotnet publish $project -c $Configuration -r win-x64 --self-contained true -o $out $noServer
if ($LASTEXITCODE -ne 0) { throw 'Publish failed.' }

foreach ($leak in @('appsettings.Development.json', 'appsettings.Local.json', 'appsettings.Production.json')) {
    if (Test-Path (Join-Path $out $leak)) { throw "$leak must not be part of a release." }
}
# Keys and certificates never travel with a release (the TLS keys stay offline, DEPLOY.md 6.1).
$keys = @(Get-ChildItem (Join-Path $serverRoot 'deploy') -Recurse -File -Include *.pem, *.pfx, *.p12, *.key)
if ($keys.Count -gt 0) { throw "Key or certificate files in deploy\ must not be part of a release: $($keys.Name -join ', ')" }
Copy-Item -Recurse (Join-Path $serverRoot 'deploy') (Join-Path $out 'deploy')
Copy-Item -Recurse (Join-Path $serverRoot 'sql') (Join-Path $out 'sql')
Remove-Item (Join-Path $out 'deploy\publish.ps1')

if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path (Join-Path $out '*') -DestinationPath $zip
Write-Output "Release: $zip"
