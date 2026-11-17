@echo off
rem The Android port's compile check (docs/ANDROID.md): the portable libraries
rem dw4_core, dw4_engine and dw4_online from the root CMakeLists.txt, built
rem with the NDK's clang for arm64-v8a (the AYN Thor) and x86_64 (the
rem emulator). Every compile error is listed (ninja -k 0); no network.
rem Usage: android\ndk-core-check.cmd [build directory, default build\ndk-check]
setlocal
if not defined ANDROID_HOME set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
set "NDK=%ANDROID_HOME%\ndk\30.0.16248370"
set "CMAKE=%ANDROID_HOME%\cmake\3.31.6\bin\cmake.exe"
set "NINJA=%ANDROID_HOME%\cmake\3.31.6\bin\ninja.exe"
if not exist "%NDK%\build\cmake\android.toolchain.cmake" (echo Missing NDK %NDK% & exit /b 1)
if not exist "%CMAKE%" (echo Missing CMake %CMAKE% & exit /b 1)
cd /d "%~dp0.."
set "OUT=%~1"
if "%OUT%"=="" set "OUT=build\ndk-check"
set FAILED=0
for %%A in (arm64-v8a x86_64) do (
  "%CMAKE%" -S . -B "%OUT%\%%A" -G Ninja "-DCMAKE_MAKE_PROGRAM=%NINJA%" "-DCMAKE_TOOLCHAIN_FILE=%NDK%\build\cmake\android.toolchain.cmake" -DANDROID_ABI=%%A -DANDROID_PLATFORM=android-33 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DDW4_ANDROID_APP=OFF >nul || exit /b 1
  echo === %%A
  rem Below normal priority. "|| ..." would only see whether start itself ran;
  rem start /wait leaves cmake's exit code in errorlevel, which "if errorlevel"
  rem reads when the line runs (inside this block, too).
  start "" /b /wait /belownormal "%CMAKE%" --build "%OUT%\%%A" --target dw4_core dw4_engine dw4_online -- -k 0
  if errorlevel 1 (
    echo === %%A: compile errors
    set FAILED=1
  )
)
if %FAILED%==1 (echo NDK compile check: errors & exit /b 1)
echo NDK compile check: dw4_core, dw4_engine, dw4_online compile for arm64-v8a and x86_64
