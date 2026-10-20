@echo off
REM run_selftest_packaged.bat - exercise the SHIPPED (packaged) binary with the in-engine self-test.
REM Absolute paths deliberately: a relative path is resolved against the process working directory
REM and the engine exits 1 without writing a log, which looks exactly like a test failure.
REM
REM Usage: Scripts\run_selftest_packaged.bat [matches] [logfile]
setlocal
set MATCHES=%~1
if "%MATCHES%"=="" set MATCHES=3
set LOG=%~2
if "%LOG%"=="" set LOG=C:\RockyKombat\Saved\Evidence\PKG_SELFTEST_LIVE.log

set EXE=C:\RockyKombat\Packaged\Windows\RockyKombat.exe
if not exist "%EXE%" echo PACKAGED_EXE_NOT_FOUND & exit /b 3

cd /d C:\RockyKombat
echo running: "%EXE%" -RKCSelfTest=%MATCHES% -nullrhi -nosplash -unattended -log -abslog="%LOG%"
"%EXE%" -RKCSelfTest=%MATCHES% -nullrhi -nosplash -unattended -log -abslog="%LOG%"
echo EXIT=%ERRORLEVEL%
endlocal
