@REM
@REM Regenerates system-docs\md\*.md from the PDFs in system-docs\.
@REM Requires uv (https://docs.astral.sh/uv/) - markitdown is fetched on demand.
@REM
@echo off
setlocal

pushd "%~dp0"

where uvx >nul 2>&1
if errorlevel 1 (
  echo ERROR: uvx was not found on PATH.
  echo        Install uv from https://docs.astral.sh/uv/ and re-run.
  popd
  exit /b 1
)

if not exist "system-docs\md" mkdir "system-docs\md"

echo Converting LibPSn00b Reference.pdf ...
uvx markitdown "system-docs\LibPSn00b Reference.pdf" -o "system-docs\md\psn00b.md" || goto :failed

echo Converting LibOver47.pdf ...
uvx markitdown "system-docs\LibOver47.pdf" -o "system-docs\md\libover47.md" || goto :failed

echo Converting Libref.pdf ...
uvx markitdown "system-docs\Libref.pdf" -o "system-docs\md\libref.md" || goto :failed

echo Done. See system-docs\md\README.md
popd
exit /b 0

:failed
echo ERROR: conversion failed.
popd
exit /b 1
