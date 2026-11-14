@REM
@REM Build script for Windows,
@REM should be setting CMAKE and stuff properly.
@REM
@echo off
setlocal

pushd "%~dp0"

CALL "%~dp0setvars.bat"

REM Configure CMake (includes fileslist generation as custom target)
cmake --preset default .
cmake --build ./run-tree

cmake -S source-tests -B tests -G Ninja
cmake --build tests
ctest --test-dir tests --output-on-failure

popd
