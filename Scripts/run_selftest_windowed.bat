@echo off
REM run_selftest_windowed.bat
REM
REM Runs the in-engine self-test WITH rendering, so the HUD draw counts are
REM asserted instead of skipped. The -nullrhi run reports "drawn frames 0" and
REM deliberately does not assert them; only this run proves the HUD really
REM emitted its layout into a canvas.
REM
REM Usage: Scripts\run_selftest_windowed.bat [matches] [logfile]

setlocal
set MATCHES=%~1
if "%MATCHES%"=="" set MATCHES=3
set LOG=%~2
if "%LOG%"=="" set LOG=C:\RockyKombat\selftest_windowed3.log

set ENGINE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJECT=C:\RockyKombat\RockyKombat.uproject

cd /d C:\RockyKombat
echo running: "%ENGINE%" "%PROJECT%" -game -windowed -ResX=1280 -ResY=720 -RKCSelfTest=%MATCHES%
"%ENGINE%" "%PROJECT%" -game -windowed -ResX=1280 -ResY=720 -nosplash -unattended -stdout -RKCSelfTest=%MATCHES% > "%LOG%" 2>&1
echo EXIT=%ERRORLEVEL%
endlocal
