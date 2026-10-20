@REM
@REM Build script for Windows,
@REM should be setting CMAKE and stuff properly.
@REM
@echo off
setlocal

pushd "%~dp0"

CALL "%~dp0setvars.bat"

REM Configure CMake
cmake --preset default .

REM Generate fileslist from iso.xml (must run before CMake build)
python "%~dp0tools\gen-fileslist.py" "%~dp0iso.xml"

cmake --build ./run-tree

popd
