@echo off
REM capture_stages.bat
REM
REM Runs the real game, windowed, and lets the GAME take the screenshots: one per
REM presentation stage (startup, loading, menu, versus, match, touch pad, results).
REM Each picture is requested on the frame the named screen is up, and the manifest
REM written beside it records what the HUD had drawn at that moment, so "the HUD
REM draws" is a measurement rather than an impression. An external window grab
REM cannot do that and can just as easily photograph a stale or black frame.
REM
REM rkc.FrontEndAuto answers the screens a person would answer, rkc.TouchForced
REM puts the on-screen pad up so it can be photographed on a desktop build, and
REM rkc.QuickRules keeps the evidence match to one short round so the run reaches
REM the result card unattended. rkc.CaptureStages=1 makes the run leave when the
REM set is complete.
REM
REM Usage: Scripts\capture_stages.bat [secondsPerScreen] [logfile]

setlocal
set SECONDS=%~1
if "%SECONDS%"=="" set SECONDS=3
set LOG=%~2
if "%LOG%"=="" set LOG=C:\RockyKombat\capture_stages.log

set ENGINE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJECT=C:\RockyKombat\RockyKombat.uproject

cd /d C:\RockyKombat
echo running: "%ENGINE%" "%PROJECT%" -game -windowed -ResX=1280 -ResY=720 -rkc.CaptureStages=1 -rkc.FrontEndAuto=%SECONDS% -rkc.TouchForced=1 -rkc.QuickRules=1
"%ENGINE%" "%PROJECT%" -game -windowed -ResX=1280 -ResY=720 -nosplash -unattended -stdout -rkc.CaptureStages=1 -rkc.FrontEndAuto=%SECONDS% -rkc.TouchForced=1 -rkc.QuickRules=1 > "%LOG%" 2>&1
echo EXIT=%ERRORLEVEL%
endlocal
