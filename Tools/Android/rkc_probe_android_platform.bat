@echo off
REM ---------------------------------------------------------------------------
REM rkc_probe_android_platform.bat
REM One cheap UBT invocation whose only purpose is to report whether the
REM INSTALLED UE 5.8.3 engine can target Android at all. UBT validates the target
REM platform before it builds, so this prints the exact engine-side error.
REM ---------------------------------------------------------------------------
setlocal
set ENGINE=C:\Program Files\Epic Games\UE_5.8
set PROJECT=C:\RockyKombat\RockyKombat.uproject
set LOG=C:\RockyKombat\Saved\android_platform_probe.log
set JAVA_HOME=C:\AndroidSetup\jdk\jdk-17.0.13+11
set ANDROID_HOME=C:\Users\DELL\AppData\Local\Android\Sdk
set ANDROID_SDK_ROOT=%ANDROID_HOME%
set NDKROOT=%ANDROID_HOME%\ndk\27.2.12479018
set ANDROID_NDK_ROOT=%NDKROOT%

if exist "%LOG%" del /q "%LOG%"
echo === UBT probe: RockyKombat Android Development ===
call "%ENGINE%\Engine\Build\BatchFiles\Build.bat" RockyKombat Android Development -Project="%PROJECT%" -WaitMutex -NoHotReloadFromIDE > "%LOG%" 2>&1
echo PROBE_EXITCODE=%errorlevel%
echo === engine Android support paths ===
if exist "%ENGINE%\Engine\Build\Android\Java" (echo PRESENT: Engine\Build\Android\Java) else (echo MISSING: Engine\Build\Android\Java)
if exist "%ENGINE%\Engine\Build\Android\Prebuilt" (echo PRESENT: Engine\Build\Android\Prebuilt) else (echo MISSING: Engine\Build\Android\Prebuilt)
if exist "%ENGINE%\Engine\Binaries\Android" (echo PRESENT: Engine\Binaries\Android) else (echo MISSING: Engine\Binaries\Android)
echo === last 40 log lines ===
endlocal
