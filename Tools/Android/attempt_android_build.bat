@echo off
REM Rocky Kombat - Android build attempt / capture harness
REM Purpose: produce the engine's exact refusal or a real APK. Logs to Saved\Evidence\Android.
setlocal
set UE=C:\Program Files\Epic Games\UE_5.8
set PROJ=C:\RockyKombat\RockyKombat.uproject
set OUTDIR=C:\RockyKombat\Saved\Evidence\Android
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
for /f "tokens=1-3 delims=/ " %%a in ("%DATE%") do set D=%%c%%a%%b
set T=%TIME: =0%
set T=%T::=%
set T=%T:~0,6%
set LOG=%OUTDIR%\ubt_android_%D%_%T%.txt

echo === UE 5.8.3 Android build attempt === > "%LOG%"
echo Project: %PROJ% >> "%LOG%"
echo Target: RockyKombat / Android / Development / arm64 >> "%LOG%"
echo Started: %DATE% %TIME% >> "%LOG%"
echo. >> "%LOG%"

call "%UE%\Engine\Build\BatchFiles\Build.bat" RockyKombat Android Development -Project="%PROJ%" -WaitMutex -architecture=arm64 >> "%LOG%" 2>&1
set RC=%ERRORLEVEL%
echo. >> "%LOG%"
echo === Build.bat exit code: %RC% === >> "%LOG%"

echo ---- tail of %LOG% ----
powershell -NoProfile -Command "Get-Content -LiteralPath '%LOG%' -Tail 40"
echo ---- exit code %RC% ----
exit /b %RC%
