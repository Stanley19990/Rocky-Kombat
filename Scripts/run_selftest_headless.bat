@echo off
REM run_selftest_headless.bat
REM
REM Runs the in-engine self-test with rendering disabled. Absolute paths are used
REM deliberately: a relative .uproject argument is resolved against the process
REM working directory, and when the engine cannot find the project it exits 1
REM without writing any log at all, which looks exactly like a test failure.
REM
REM Usage: Scripts\run_selftest_headless.bat [matches] [logfile]

setlocal
set MATCHES=%~1
if "%MATCHES%"=="" set MATCHES=3
set LOG=%~2
if "%LOG%"=="" set LOG=C:\RockyKombat\selftest_headless_new.log

set ENGINE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJECT=C:\RockyKombat\RockyKombat.uproject

cd /d C:\RockyKombat
echo running: "%ENGINE%" "%PROJECT%" -game -nullrhi -RKCSelfTest=%MATCHES%
"%ENGINE%" "%PROJECT%" -game -nullrhi -nosplash -unattended -stdout -RKCSelfTest=%MATCHES% > "%LOG%" 2>&1
echo EXIT=%ERRORLEVEL%
endlocal
