@echo off
rem Native engine runtime (dw4_game). Optional first argument: stage name,
rem e.g. "Play-DW4-Game.cmd stagml11" for the main lobby. Default: boot logos.
cd /d "%~dp0"
if not exist "build\Game\dw4_game.exe" (
  echo build\Game\dw4_game.exe was not found. Build it first.
  pause
  exit /b 1
)
set STAGE=%1
if "%STAGE%"=="" set STAGE=stagis08
"build\Game\dw4_game.exe" --stage %STAGE%
