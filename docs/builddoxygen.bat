@echo off
setlocal

set "ROOT_DIR=%~dp0.."
for %%I in ("%ROOT_DIR%") do set "ROOT_DIR=%%~fI"
set "DOXYFILE=%ROOT_DIR%\docs\Doxyfile"

if not exist "%DOXYFILE%" (
  echo Doxyfile not found at: %DOXYFILE%
  exit /b 1
)

doxygen "%DOXYFILE%"
endlocal
