@echo off
rem Developer launch with build identity validation and a separate work/recomp-user profile.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start-recomp.ps1"
exit /b %errorlevel%
