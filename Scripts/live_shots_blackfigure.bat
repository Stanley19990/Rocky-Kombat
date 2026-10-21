@echo off
REM live_shots_blackfigure.bat
REM
REM CONTROL RUN for the black duplicate figure.
REM
REM An external window grab (PrintWindow) of this game shows a pure-black copy of
REM each fighter standing beside the real one. This run asks the GAME ITSELF for
REM screenshots while an ordinary match is played, using -rkc.LiveShots. The
REM engine's own screenshot is the rendered frame, so:
REM   * black figure present in BOTH  -> it is really rendered on screen
REM   * black figure only in the grab  -> the grab is what creates it
REM
REM The run is driven by the built-in 'playthrough' timeline so the match is
REM reached without a person at the keyboard, and the log records, per shot, the
REM simulation X and the resolved flipbook of both fighters.
setlocal
set EXE=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe
set PROJ=C:\RockyKombat\RockyKombat.uproject
set LOG=C:\RockyKombat\Saved\Evidence\live_shots_blackfigure.log
set SHOTS=%~1
if "%SHOTS%"=="" set SHOTS=40

if not exist "%EXE%" (
    echo EDITOR_NOT_FOUND
    exit /b 3
)
if not exist "%PROJ%" (
    echo PROJECT_NOT_FOUND
    exit /b 3
)

cd /d C:\RockyKombat
echo running: UnrealEditor -game -windowed -rkc.Script=playthrough -rkc.LiveShots=%SHOTS%
"%EXE%" "%PROJ%" -game -windowed -ResX=1280 -ResY=720 -nosplash -unattended -stdout ^
  -rkc.Script=playthrough -rkc.LiveShots=%SHOTS% -rkc.QuickRules=1 -rkc.FrontEnd=1 ^
  -log -abslog="%LOG%"
echo EXIT=%ERRORLEVEL%
endlocal
