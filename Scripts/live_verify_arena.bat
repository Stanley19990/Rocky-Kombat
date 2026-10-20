@echo off
REM ---------------------------------------------------------------------------
REM live_verify_arena.bat
REM
REM Runs the real game so the offline opponent can be observed while it plays:
REM   -rkc.FrontEnd=0     skip the menu, start a match straight away
REM   -rkc.QuickRules=1   one short round, so the run finishes unattended
REM   -rkc.BotSide=1      the CPU drives side B, side A gets no input at all
REM                       (the player who never touches the controls)
REM   -rkc.LogPositions=1 a 1 Hz trace of both fighters: x, facing, health,
REM                       stamina, state, action, separation, pair midpoint
REM   -rkc.Evidence=1     the run's own evidence record
REM
REM Usage: Scripts\live_verify_arena.bat [logfile]
REM ---------------------------------------------------------------------------

setlocal
set ENGINE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set PROJECT=C:\RockyKombat\RockyKombat.uproject
set LOG=%~1
if "%LOG%"=="" set LOG=C:\RockyKombat\live_verify_arena.log

cd /d C:\RockyKombat
echo running: "%ENGINE%" "%PROJECT%" -game -windowed -ResX=1280 -ResY=720 -nosplash -unattended -stdout -rkc.FrontEnd=0 -rkc.QuickRules=1 -rkc.BotSide=1 -rkc.LogPositions=1 -rkc.Evidence=1
"%ENGINE%" "%PROJECT%" -game -windowed -ResX=1280 -ResY=720 -nosplash -unattended -stdout -rkc.FrontEnd=0 -rkc.QuickRules=1 -rkc.BotSide=1 -rkc.LogPositions=1 -rkc.Evidence=1 > "%LOG%" 2>&1
echo EXIT=%ERRORLEVEL%
endlocal
