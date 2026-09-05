@REM
@REM Run script for Windows,
@REM make sure the game is built first.
@REM
@echo off
setlocal

CALL setvars.bat

if not exist "run-tree\game.cue" (
  echo Please run build.bat to build the game first.
  exit /b 1
)

pcsx-redux -fastboot -stdout -run -iso run-tree\game.cue
