@echo off
rem FemboyEngine 2.0 - C++ toolchain and source dependencies for Windows.
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI\"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%SetupScripts\SetupWindows.ps1" -Component Cpp %*
exit /b %ERRORLEVEL%
