@echo off
setlocal enabledelayedexpansion

rem Usage: buildhdocs.bat [OS_NAME]
rem Default OS_NAME: Windows
set "OS_NAME=%~1"
if "%OS_NAME%"=="" set "OS_NAME=Windows"

set "ROOT_DIR=%~dp0.."
for %%I in ("%ROOT_DIR%") do set "ROOT_DIR=%%~fI"

set "BUILD_ROOT=%ROOT_DIR%\build\%OS_NAME%"
set "BUILD_DIR=%BUILD_ROOT%\Release"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

pushd "%BUILD_DIR%"
cmake .. -G "Ninja" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
popd

if not exist "%ROOT_DIR%\build" mkdir "%ROOT_DIR%\build"
copy /Y "%BUILD_DIR%\compile_commands.json" "%ROOT_DIR%\build\compile_commands.json" >nul

echo compile_commands.json: %ROOT_DIR%\build\compile_commands.json

pushd "%ROOT_DIR%"
hdoc
popd
endlocal
