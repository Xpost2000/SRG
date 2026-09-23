@REM
@REM Run script for Windows,
@REM make sure the game is built first.
@REM
@echo off
setlocal

pushd "%~dp0"

CALL "%~dp0setvars.bat"

if not exist "run-tree\game.cue" (
  echo Please run build.bat to build the game first.
  popd
  exit /b 1
)

pcsx-redux -fastboot -stdout -run -iso run-tree\game.cue

popd
