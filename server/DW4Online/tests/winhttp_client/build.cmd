@echo off
rem Builds dw4online_client.exe (the WinHTTP end-to-end test tool) into tests\winhttp_client\out with MSVC.
rem Finds Visual Studio with vswhere; set VCVARS to a vcvars64.bat to override.
setlocal
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if defined VCVARS goto have_vcvars
if not exist "%VSWHERE%" goto no_vcvars
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
:have_vcvars
if not exist "%VCVARS%" goto no_vcvars
rem vcvars64.bat itself looks for vswhere.exe on PATH.
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VCVARS%" >nul || exit /b 1
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE dw4online_client.cpp /Foout\ /Fe:out\dw4online_client.exe winhttp.lib bcrypt.lib crypt32.lib ws2_32.lib
exit /b %ERRORLEVEL%
:no_vcvars
echo vcvars64.bat not found; set VCVARS to its path.
exit /b 1
