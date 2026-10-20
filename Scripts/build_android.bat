@echo off
REM build_android.bat - compile the RockyKombat game module for Android ARM64.
REM This exercises the NDK/Clang toolchain without needing the APK Java templates.
setlocal
cd /d C:\RockyKombat
set "ENGINE=C:\Program Files\Epic Games\UE_5.8\Engine"
set "JAVA_HOME=C:\AndroidSetup\jdk\jdk-17.0.13+11"
set "ANDROID_HOME=C:\Users\DELL\AppData\Local\Android\Sdk"
set "ANDROID_SDK_ROOT=%ANDROID_HOME%"
set "NDKROOT=%ANDROID_HOME%\ndk\27.2.12479018"
set "NDK_ROOT=%NDKROOT%"
set "PATH=%JAVA_HOME%\bin;%PATH%"
call "%ENGINE%\Build\BatchFiles\Build.bat" RockyKombat Android Development -Project="C:\RockyKombat\RockyKombat.uproject" -WaitMutex -NoHotReload
exit /b %errorlevel%
