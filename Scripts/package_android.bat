@echo off
REM Internal alpha: a self-contained, versioned ARM64 APK, verified after packaging.
REM Usage: Scripts\package_android.bat [Development|Shipping]
setlocal
set "RKC_PROJECT_ROOT=%~dp0.."
set "RKC_BUILD_CONFIG=%~1"
if "%RKC_BUILD_CONFIG%"=="" set "RKC_BUILD_CONFIG=Development"
call "%~dp0..\Tools\Android\android_env.bat" quiet
if errorlevel 1 (
    echo [FAIL] Android environment incomplete. Add Android under Epic Games Launcher engine Options.
    exit /b 2
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\Tools\Android\package_test_android.ps1" -Configuration "%RKC_BUILD_CONFIG%"
exit /b %errorlevel%