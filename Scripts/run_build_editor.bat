@echo off
REM run_build_editor.bat - wrapper so the editor build can be launched from a POSIX shell.
REM Writes everything to Saved\build_safearea.log and appends the exit code.
setlocal
cd /d C:\RockyKombat
call Scripts\build_editor.bat > Saved\build_safearea.log 2>&1
echo EXITCODE=%errorlevel%>> Saved\build_safearea.log
endlocal
