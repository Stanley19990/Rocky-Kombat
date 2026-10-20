@echo off
REM ============================================================================
REM  Rocky Kombat - Android APK packaging
REM
REM  Produces a versioned, release-signed, ARM64-only APK with all content
REM  inside it (no .obb, no network dependency).
REM
REM  Signing: the project config ships a throwaway debug key so dev builds
REM  install over each other. For a shareable APK this script temporarily
REM  overlays the private release keystore from .secrets\, builds, then
REM  restores Config\DefaultEngine.ini byte-for-byte - even on failure.
REM
REM  Usage:  package_android.bat            (Shipping, arm64)
REM          package_android.bat Development
REM ============================================================================
setlocal EnableDelayedExpansion
set "RKC_ROOT=C:\RockyKombat"
set "CFG=%RKC_ROOT%\Config\DefaultEngine.ini"
set "CFG_BAK=%RKC_ROOT%\Config\DefaultEngine.ini.presign"
set "ARCH=%RKC_ROOT%\Dist\Android\_archive"
set "OUT=%RKC_ROOT%\Dist\Android"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Shipping"

call "%~dp0android_env.bat" quiet
if errorlevel 1 (
    echo.
    echo ======================================================================
    echo  CANNOT PACKAGE: the Android build environment is incomplete.
    echo  The report above lists every missing piece. The engine component is
    echo  the only one a script cannot supply:
    echo    Epic Games Launcher -^> Library -^> UE 5.8 -^> Options
    echo      -^> Target Platforms -^> Android   ^(~4.5 GB^)
    echo ======================================================================
    exit /b 2
)

REM --- version ----------------------------------------------------------------
set "VERSION_DISPLAY="
set "STORE_VERSION="
for /f "usebackq tokens=1,* delims==" %%a in ("%~dp0version.txt") do (
    if /i "%%a"=="VERSION_DISPLAY" set "VERSION_DISPLAY=%%b"
    if /i "%%a"=="STORE_VERSION" set "STORE_VERSION=%%b"
)
if not defined VERSION_DISPLAY (
    echo [FAIL] version.txt has no VERSION_DISPLAY
    exit /b 3
)

REM --- signing material -------------------------------------------------------
set "SIGDIR=%RKC_ROOT%\.secrets"
set "KS=%SIGDIR%\rkc-release.keystore"
set "KS_PW_FILE=%SIGDIR%\keystore.pw"
if not exist "%KS%" (
    echo [FAIL] release keystore missing: %KS%
    echo        Run Tools\Android\make_keystore.bat once, then keep a backup of .secrets.
    exit /b 4
)
if not exist "%KS_PW_FILE%" (
    echo [FAIL] keystore password file missing: %KS_PW_FILE%
    exit /b 4
)
set /p KS_PW=<"%KS_PW_FILE%"

echo ======================================================================
echo  Rocky Kombat - Android packaging
echo ----------------------------------------------------------------------
echo  Config       : %CONFIG%
echo  Version      : %VERSION_DISPLAY%  ^(code %STORE_VERSION%^)
echo  Architecture : arm64
echo  Signature    : rkc-release ^(private keystore^)
echo ======================================================================
echo.

REM --- overlay release signing + version into the project config ---------------
copy /y "%CFG%" "%CFG_BAK%" >nul
if errorlevel 1 (
    echo [FAIL] could not back up %CFG%
    exit /b 5
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0apply_signing.ps1" -ConfigPath "%CFG%" -KeystorePath "%KS%" -KeystorePassword "%KS_PW%" -VersionDisplay "%VERSION_DISPLAY%" -StoreVersion "%STORE_VERSION%" -KeyAlias rkc-release
if errorlevel 1 (
    echo [FAIL] could not write release signing into %CFG%
    copy /y "%CFG_BAK%" "%CFG%" >nul
    exit /b 6
)

REM --- build, cook, stage, package --------------------------------------------
if not exist "%ARCH%" mkdir "%ARCH%"
echo [1/2] Building ^+ cooking ^+ packaging ^(this takes a long time on first run^)...
call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun ^
    -project="%RKC_ROOT%\RockyKombat.uproject" ^
    -noP4 -utf8output ^
    -platform=Android -targetplatform=Android ^
    -clientconfig=%CONFIG% ^
    -build -cook -stage -pak -package ^
    -map=/Engine/Maps/Entry ^
    -archive -archivedirectory="%ARCH%"
set "BUILD_RC=%ERRORLEVEL%"

REM --- always put the project config back -------------------------------------
copy /y "%CFG_BAK%" "%CFG%" >nul
del /q "%CFG_BAK%" 2>nul

if not "%BUILD_RC%"=="0" (
    echo.
    echo [FAIL] BuildCookRun exit code %BUILD_RC%. The project config was restored.
    echo        Full log: %RKC_ROOT%\Saved\Logs\Log.txt  and  Log_UAT.txt
    exit /b %BUILD_RC%
)

REM --- collect the APK --------------------------------------------------------
echo [2/2] Collecting the APK...
set "APK_SRC="
for /r "%ARCH%" %%f in (*.apk) do if not defined APK_SRC set "APK_SRC=%%f"
if not defined APK_SRC (
    echo [FAIL] BuildCookRun reported success but produced no .apk under "%ARCH%".
    exit /b 7
)

set "APK_NAME=RockyKombat-%VERSION_DISPLAY%-arm64.apk"
copy /y "%APK_SRC%" "%OUT%\%APK_NAME%" >nul
if errorlevel 1 (
    echo [FAIL] could not copy the APK to %OUT%
    exit /b 8
)

for %%A in ("%OUT%\%APK_NAME%") do set "APK_BYTES=%%~zA"
set /a APK_MB=%APK_BYTES%/1048576

set "APK_HASH="
for /f "tokens=*" %%h in ('powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 -LiteralPath '%OUT%\%APK_NAME%').Hash.ToLower()"') do set "APK_HASH=%%h"

(
    echo Rocky Kombat - Android build
    echo ---------------------------------------------
    echo Version        : %VERSION_DISPLAY%
    echo Store version  : %STORE_VERSION%
    echo Config         : %CONFIG%
    echo Architecture   : arm64
    echo Package name   : com.rockycat.rockykombat
    echo Signed by      : rkc-release
    echo APK file       : %APK_NAME%
    echo APK size       : %APK_BYTES% bytes ^(%APK_MB% MB^)
    echo SHA-256        : %APK_HASH%
    echo Built          : %DATE% %TIME%
    echo.
    echo Install:  adb install -r "%APK_NAME%"
    echo           or copy to the phone and open it ^(allow install from this source^).
) > "%OUT%\BUILD_INFO.txt"

echo.
echo ======================================================================
echo  APK READY
echo    File    : %OUT%\%APK_NAME%
echo    Size    : %APK_MB% MB
echo    SHA-256 : %APK_HASH%
echo    Details : %OUT%\BUILD_INFO.txt
echo ======================================================================
endlocal
exit /b 0
