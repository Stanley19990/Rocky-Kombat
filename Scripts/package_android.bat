@echo off
REM package_android.bat - build + cook + stage + package an ARM64 Android APK.
REM
REM Usage:  Scripts\package_android.bat [Development|Shipping]
REM Output: Saved\Android\<Config>\RockyKombat-<arch>.apk  (and a -arm64 build log)
REM
REM The Android toolchain (JDK 17, NDK r27c, platform 36, build-tools 36.1.0) is
REM installed under %ANDROID_HOME%; the paths are exported here so a plain cmd.exe
REM works without the machine-wide environment being set up first.
setlocal enabledelayedexpansion
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Development"

cd /d C:\RockyKombat

set "ENGINE=C:\Program Files\Epic Games\UE_5.8\Engine"
set "JAVA_HOME=C:\AndroidSetup\jdk\jdk-17.0.13+11"
set "ANDROID_HOME=C:\Users\DELL\AppData\Local\Android\Sdk"
set "ANDROID_SDK_ROOT=%ANDROID_HOME%"
set "NDKROOT=%ANDROID_HOME%\ndk\27.2.12479018"
set "NDK_ROOT=%NDKROOT%"
set "PATH=%JAVA_HOME%\bin;%PATH%"

echo [rkc] config      : %CONFIG%
echo [rkc] JAVA_HOME   : %JAVA_HOME%
echo [rkc] ANDROID_HOME: %ANDROID_HOME%
echo [rkc] NDKROOT     : %NDKROOT%
echo [rkc] keystore    : C:\RockyKombat\Build\Android\rkc.keystore

if not exist "%ANDROID_HOME%\ndk\27.2.12479018" (
  echo [rkc] ANDROID TOOLCHAIN MISSING under %ANDROID_HOME%
  exit /b 4
)
if not exist "C:\RockyKombat\Build\Android\rkc.keystore" (
  echo [rkc] KEYSTORE MISSING - generate it with keytool first
  exit /b 5
)

call "%ENGINE%\Build\BatchFiles\RunUAT.bat" BuildCookRun ^
  -project="C:\RockyKombat\RockyKombat.uproject" ^
  -noP4 -platform=Android -clientconfig=%CONFIG% ^
  -cookflavor=Multi ^
  -build -cook -stage -package -pak -iostore -compressed ^
  -archive -archivedirectory="C:\RockyKombat\Saved\Android" ^
  -nocompileeditor -utf8output -nohotreload ^
  -targetplatforms=Android ^
  -map=/Engine/Maps/Entry

exit /b %errorlevel%
