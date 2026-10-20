@echo off
REM capture_stages_clean.bat
REM
REM Same in-engine stage capture as capture_stages.bat, but run against the
REM SHIPPED (packaged) binary and WITHOUT -rkc.TouchForced, so the menus are
REM photographed the way a desktop player actually sees them (no on-screen pad).
REM
REM The earlier set (Stages\20261019_201119) forced the pad on so that the touch
REM stage could be photographed at all; those frames are therefore not a normal
REM run's menus. This run exists to close that gap and to supply the real screens
REM for the first-round guide.
REM
REM The touch-pad stage is expected to be recorded as MISSED here - that is the
REM driver telling the truth rather than photographing an empty stage.
REM
REM Usage: Scripts\capture_stages_clean.bat [secondsPerScreen] [logfile]
setlocal
set SECONDS=%~1
if "%SECONDS%"=="" set SECONDS=3
set LOG=%~2
if "%LOG%"=="" set LOG=C:\RockyKombat\Saved\Evidence\capture_stages_clean.log

set EXE=C:\RockyKombat\Packaged\Windows\RockyKombat.exe
if not exist "%EXE%" echo PACKAGED_EXE_NOT_FOUND & exit /b 3

cd /d C:\RockyKombat
echo running: "%EXE%" -windowed -ResX=1280 -ResY=720 -rkc.CaptureStages=1 -rkc.FrontEndAuto=%SECONDS% -rkc.QuickRules=1
"%EXE%" -windowed -ResX=1280 -ResY=720 -nosplash -unattended -stdout ^
  -rkc.CaptureStages=1 -rkc.FrontEndAuto=%SECONDS% -rkc.QuickRules=1 ^
  -log -abslog="%LOG%"
echo EXIT=%ERRORLEVEL%
endlocal
