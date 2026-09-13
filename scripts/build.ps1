param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')][string]$Configuration = 'Release',
    [switch]$CoreOnly,
    [switch]$Package,
    [ValidateRange(1,64)][int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswherePath)) { throw 'Install Visual Studio with Desktop development with C++, or use CMake in a configured developer shell.' }
$vsRoot = & $vswherePath -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ tools were not found.' }
$vcvarsPath = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
# Import the compiler environment into this process only. Do not change global PATH.
$environmentDirectory = Join-Path $projectRoot 'build'
New-Item -ItemType Directory -Force $environmentDirectory | Out-Null
$environmentScript = Join-Path $environmentDirectory 'compiler-env.cmd'
@('@echo off', "call `"$vcvarsPath`" >nul", 'if errorlevel 1 exit /b %errorlevel%', 'set') |
    Set-Content -LiteralPath $environmentScript -Encoding ascii
$compilerEnvironment = & $env:ComSpec /d /c $environmentScript
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize the Visual Studio compiler environment.' }
foreach ($line in $compilerEnvironment) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}
$cmakeExe = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (-not (Test-Path -LiteralPath $cmakeExe)) { $cmakeExe = (Get-Command cmake -ErrorAction Stop).Source }
$ctestExe = Join-Path (Split-Path $cmakeExe) 'ctest.exe'
$buildDirectory = Join-Path $projectRoot "build\$Configuration"
$buildApp = if ($CoreOnly) { 'OFF' } else { 'ON' }
& $cmakeExe -S $projectRoot -B $buildDirectory -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" "-DDW4_BUILD_APP=$buildApp"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmakeExe --build $buildDirectory --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
& $ctestExe --test-dir $buildDirectory --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
if ($Package) {
    & $cmakeExe --install $buildDirectory --prefix (Join-Path $projectRoot 'dist\DW4_PC')
    if ($LASTEXITCODE -ne 0) { throw 'Packaging failed.' }
}
Write-Host "Build and tests complete: $buildDirectory"
