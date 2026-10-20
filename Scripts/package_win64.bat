@echo off
REM ---------------------------------------------------------------------------
REM package_win64.bat - build, cook, stage, pack and archive the Win64 game.
REM This is the command behind acceptance gate A ("project compiles, runs and
REM packages"). It compiles the shipping RockyKombat target, cooks the project
REM content, stages the runtime and writes the archived build to
REM C:\RockyKombat\Packaged.
REM
REM Usage:  cmd.exe /c Scripts\package_win64.bat > packaged.log 2>&1
REM ---------------------------------------------------------------------------
setlocal
set ENGINE=C:\Program Files\Epic Games\UE_5.8
set PROJECT=C:\RockyKombat\RockyKombat.uproject
set ARCHIVE=C:\RockyKombat\Packaged
set UAT=%ENGINE%\Engine\Build\BatchFiles\RunUAT.bat

if not exist "%UAT%" (
  echo ENGINE_NOT_FOUND: %UAT%
  exit /b 3
)

call "%UAT%" BuildCookRun ^
  -project="%PROJECT%" ^
  -noP4 ^
  -platform=Win64 ^
  -clientconfig=Development ^
  -build ^
  -cook ^
  -stage ^
  -pak ^
  -archive ^
  -archivedirectory="%ARCHIVE%" ^
  -utf8output
exit /b %errorlevel%
