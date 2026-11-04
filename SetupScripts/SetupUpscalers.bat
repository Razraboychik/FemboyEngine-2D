@echo off
rem FemboyEngine 2.0 - optional DLSS/FidelityFX/XeSS SDK setup for Windows.
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI\"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%SetupScripts\SetupUpscalers.ps1" %*
exit /b %ERRORLEVEL%
