@REM
@REM Build script for Windows,
@REM should be setting CMAKE and stuff properly.
@REM
@echo off
setlocal

CALL setvars.bat

cmake --preset default .
cmake --build ./run-tree
