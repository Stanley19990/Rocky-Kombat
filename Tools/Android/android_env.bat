@echo off
REM ============================================================================
REM  Rocky Kombat - Android build environment
REM  Sets the variables UE 5.8's Android toolchain requires, then verifies each
REM  one. Nothing here writes to the registry or to a system-wide location: all
REM  paths are project-local except the Android SDK, which the UE docs expect at
REM  the per-user location.
REM  Call this from other scripts:  call "%~dp0android_env.bat" quiet
REM ============================================================================
setlocal EnableDelayedExpansion

set "RKC_ROOT=C:\RockyKombat"
set "UE_ROOT=C:\Program Files\Epic Games\UE_5.8"

REM --- Java: project-local portable JDK 17 (matches what UE 5.8 expects) ------
if not defined JAVA_HOME set "JAVA_HOME=%RKC_ROOT%\Tools\jdk\jdk-17.0.13+11"
REM Shells such as Git Bash may export /c/... paths that cmd and Java cannot use.
if not exist "%JAVA_HOME%\bin\javac.exe" set "JAVA_HOME=%RKC_ROOT%\Tools\jdk\jdk-17.0.13+11"

REM --- Android SDK ------------------------------------------------------------
if not defined ANDROID_HOME set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
if not exist "%ANDROID_HOME%\platform-tools\adb.exe" set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
if not defined ANDROID_SDK_ROOT set "ANDROID_SDK_ROOT=%ANDROID_HOME%"
if not exist "%ANDROID_SDK_ROOT%\platform-tools\adb.exe" set "ANDROID_SDK_ROOT=%ANDROID_HOME%"

REM --- NDK --------------------------------------------------------------------
set "NDK_VERSION=27.2.12479018"
if not defined NDKROOT set "NDKROOT=%ANDROID_HOME%\ndk\%NDK_VERSION%"
if not exist "%NDKROOT%\source.properties" set "NDKROOT=%ANDROID_HOME%\ndk\%NDK_VERSION%"
set "NDK_ROOT=%NDKROOT%"
set "NDK_PATH=%NDKROOT%"

REM --- UE's own toolchain expectations ---------------------------------------
set "UE_ANDROID_API=android-26"
set "UE_SDK_PLATFORM=android-36"
set "UE_BUILD_TOOLS=36.0.0"

REM --- PATH: put Java, platform-tools and build-tools in front -----------------
set "PATH=%JAVA_HOME%\bin;%ANDROID_HOME%\platform-tools;%ANDROID_HOME%\build-tools\%UE_BUILD_TOOLS%;%PATH%"

if /i "%~1"=="quiet" goto :verify_quiet

echo ======================================================================
echo  Rocky Kombat - Android environment
echo ======================================================================
echo  JAVA_HOME   = %JAVA_HOME%
echo  ANDROID_HOME= %ANDROID_HOME%
echo  NDKROOT     = %NDKROOT%
echo  UE root     = %UE_ROOT%
echo.

:verify_quiet
set "ENV_RC=0"

if exist "%JAVA_HOME%\bin\javac.exe" (
    for /f "tokens=*" %%v in ('"%JAVA_HOME%\bin\javac.exe" -version 2^>^&1') do set "JAVAV=%%v"
    if /i not "%~1"=="quiet" echo  [ OK ] Java: !JAVAV!
) else (
    echo  [FAIL] Java: no javac at "%JAVA_HOME%\bin"
    set "ENV_RC=1"
)

if exist "%ANDROID_HOME%\platforms\%UE_SDK_PLATFORM%" (
    if /i not "%~1"=="quiet" echo  [ OK ] SDK platform: %UE_SDK_PLATFORM%
) else (
    echo  [FAIL] SDK platform %UE_SDK_PLATFORM% missing
    set "ENV_RC=1"
)

if exist "%ANDROID_HOME%\build-tools\%UE_BUILD_TOOLS%" (
    if /i not "%~1"=="quiet" echo  [ OK ] build-tools: %UE_BUILD_TOOLS%
) else (
    echo  [FAIL] build-tools %UE_BUILD_TOOLS% missing
    set "ENV_RC=1"
)

if exist "%NDKROOT%\source.properties" (
    if /i not "%~1"=="quiet" echo  [ OK ] NDK: %NDK_VERSION%
) else (
    echo  [FAIL] NDK %NDK_VERSION% missing at "%NDKROOT%"
    set "ENV_RC=1"
)

if exist "%ANDROID_HOME%\platform-tools\adb.exe" (
    if /i not "%~1"=="quiet" echo  [ OK ] adb present
) else (
    echo  [FAIL] adb missing at "%ANDROID_HOME%\platform-tools"
    set "ENV_RC=1"
)

REM The engine component is the one thing that cannot be provisioned by script.
if exist "%UE_ROOT%\Engine\Binaries\Android\UnrealGame.target" (
    if /i not "%~1"=="quiet" echo  [ OK ] UE Android engine component installed
) else (
    echo  [FAIL] UE Android engine component MISSING
    echo         Fix: Epic Games Launcher -^> Library -^> UE 5.8 -^> Options -^> Target Platforms -^> Android
    set "ENV_RC=1"
)

if /i not "%~1"=="quiet" (
    echo.
    if "%ENV_RC%"=="0" (
        echo  Environment READY.
    ) else (
        echo  Environment NOT ready ^(see FAIL lines above^).
    )
    echo ======================================================================
)

REM setlocal hides these on exit, so re-export for the calling script
endlocal & set "JAVA_HOME=%JAVA_HOME%" & set "ANDROID_HOME=%ANDROID_HOME%" & set "ANDROID_SDK_ROOT=%ANDROID_SDK_ROOT%" & set "NDKROOT=%NDKROOT%" & set "NDK_ROOT=%NDKROOT%" & set "NDK_PATH=%NDKROOT%" & set "PATH=%PATH%" & exit /b %ENV_RC%
