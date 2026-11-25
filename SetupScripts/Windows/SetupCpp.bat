@echo off
rem FemboyEngine 2.0 - C++ toolchain and source dependencies for Windows.
setlocal
if not exist "%~dp0SetupWindows.ps1" (
    echo [Setup] SetupWindows.ps1 is missing next to SetupCpp.bat.
    exit /b 1
)
set "FBY_POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if defined PROCESSOR_ARCHITEW6432 set "FBY_POWERSHELL=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%FBY_POWERSHELL%" set "FBY_POWERSHELL=powershell"
set "ERRORLEVEL="
"%FBY_POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0SetupWindows.ps1" -Component Cpp %*
exit /b %ERRORLEVEL%
