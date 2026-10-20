@echo off
REM build_editor.bat - compile the Rocky Kombat editor target.
REM Usage:  cmd.exe /c Scripts\build_editor.bat > build_editor.log 2>&1
setlocal
set ENGINE=C:\Program Files\Epic Games\UE_5.8
set PROJECT=C:\RockyKombat\RockyKombat.uproject

if not exist "%ENGINE%\Engine\Build\BatchFiles\Build.bat" (
  echo ENGINE_NOT_FOUND: %ENGINE%
  exit /b 3
)

call "%ENGINE%\Engine\Build\BatchFiles\Build.bat" RockyKombatEditor Win64 Development -Project="%PROJECT%" -WaitMutex -NoHotReload
exit /b %errorlevel%
