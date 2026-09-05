@REM
@REM Build script for Windows,
@REM should be setting CMAKE and stuff properly.
@REM
@echo off
setlocal

set PATH=PATH;toolchain-win64\bin;toolchain-win64\cmake-4.4.3-windows-x86_64\bin;
set PSN00BSDK_LIBS=toolchain-win64\lib\libpsn00b\

cmake --preset default .
cmake --build ./run-tree
