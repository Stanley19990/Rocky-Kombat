@echo off
REM ---------------------------------------------------------------------------
REM rkc_sdk_aligntools.bat - install the exact SDK components UE 5.8.3 expects.
REM UE 5.8.3 declares (Engine/Config/Android/Android_SDK.json):
REM   platforms android-36, build-tools 36.0.0, cmake 3.22.1, ndk 27.2.12479018
REM This script makes sure build-tools 36.0.0 is present in addition to the
REM already-installed 36.1.0, so the engine's version check passes unchanged.
REM ---------------------------------------------------------------------------
setlocal
set JAVA_HOME=C:\AndroidSetup\jdk\jdk-17.0.13+11
set ANDROID_HOME=C:\Users\DELL\AppData\Local\Android\Sdk
set SDKMANAGER=%ANDROID_HOME%\cmdline-tools\latest\bin\sdkmanager.bat

if not exist "%SDKMANAGER%" (
  echo SDKMANAGER_NOT_FOUND: %SDKMANAGER%
  exit /b 3
)

echo === installing build-tools;36.0.0 ===
call "%SDKMANAGER%" --sdk_root="%ANDROID_HOME%" "build-tools;36.0.0"
echo INSTALL_EXITCODE=%errorlevel%

echo === installed components ===
call "%SDKMANAGER%" --sdk_root="%ANDROID_HOME%" --list_installed
echo LIST_EXITCODE=%errorlevel%
endlocal
