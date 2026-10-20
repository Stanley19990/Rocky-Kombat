@echo off
REM run_selftest_safearea.bat - headless self-test for the safe-area increment.
setlocal
cd /d C:\RockyKombat
set EDITOR=C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
if not exist "%EDITOR%" echo EDITOR_NOT_FOUND & exit /b 3
"%EDITOR%" "C:\RockyKombat\RockyKombat.uproject" -game -nullrhi -nosplash -RKCSelfTest=3 -log -abslog="C:\RockyKombat\Saved\Evidence\SELFTEST_SAFEAREA.log" > Saved\Evidence\selftest_safearea_stdout.log 2>&1
echo EXITCODE=%errorlevel%>> Saved\Evidence\selftest_safearea_stdout.log
endlocal
