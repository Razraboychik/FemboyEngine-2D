@echo off
rem FemboyEngine 2.0 - 7-Zip archiver setup for Windows.
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI\"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%SetupScripts\SetupWindows.ps1" -Component Archiver %*
exit /b %ERRORLEVEL%
