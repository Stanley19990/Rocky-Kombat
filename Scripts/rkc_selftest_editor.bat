@echo off
REM rkc_selftest_editor.bat - run the in-engine self-test from the EDITOR binary.
REM Fast check after an editor rebuild: exercises the new arena assertions before
REM paying for a full cook/package. Headless (-nullrhi) so no window is needed.
REM Usage:  Scripts\rkc_selftest_editor.bat 3
setlocal
set ENGINE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJECT=C:\RockyKombat\RockyKombat.uproject
set MATCHES=%~1
if "%MATCHES%"=="" set MATCHES=3

if not exist "%ENGINE%" (
  echo ENGINE_NOT_FOUND: %ENGINE%
  exit /b 3
)

cd /d C:\RockyKombat
"%ENGINE%" "%PROJECT%" -game -nullrhi -unattended -nosplash -stdout -RKCSelfTest=%MATCHES%
exit /b %errorlevel%
