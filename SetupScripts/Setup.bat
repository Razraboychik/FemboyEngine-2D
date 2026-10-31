@echo off
rem FemboyEngine 2.0 - one-click Windows 10/11 dependency setup.
rem
rem The actual work lives in SetupWindows.ps1 next to this launcher so it can be
rem tested and read as PowerShell rather than escaped batch. This launcher only
rem locates the repository root (one level up) and forwards the flags.
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI\"
if not exist "%ROOT%SetupScripts\SetupWindows.ps1" (
    echo [Setup] SetupScripts\SetupWindows.ps1 is missing next to Setup.bat.
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%SetupScripts\SetupWindows.ps1" %*
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" (
    echo [Setup] failed with exit code %RESULT%.
) else (
    echo [Setup] Windows dependencies are ready. Run: cmake -S . -B build -G Ninja -DFBY_BUILD_TESTS=ON
)
exit /b %RESULT%
