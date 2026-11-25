@echo off
rem FemboyEngine 2.0 - one-click Windows 10/11 dependency setup.
rem
rem The actual work lives in SetupWindows.ps1 next to this launcher so it can be
rem tested and read as PowerShell rather than escaped batch. This launcher only
rem locates the repository root (two levels up) and forwards the flags.
setlocal
for %%I in ("%~dp0..\..") do set "ROOT=%%~fI\"
if not exist "%ROOT%SetupScripts\Windows\SetupWindows.ps1" (
    echo [Setup] SetupScripts\Windows\SetupWindows.ps1 is missing next to Setup.bat.
    exit /b 1
)
set "FBY_POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if defined PROCESSOR_ARCHITEW6432 set "FBY_POWERSHELL=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%FBY_POWERSHELL%" set "FBY_POWERSHELL=powershell"
set "ERRORLEVEL="
"%FBY_POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%ROOT%SetupScripts\Windows\SetupWindows.ps1" %*
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" (
    echo [Setup] failed with exit code %RESULT%.
) else (
    echo [Setup] Windows dependencies are ready. Run: cmake -S "%ROOT%." -B "%ROOT%build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFBY_BUILD_TESTS=ON
)
exit /b %RESULT%
