@echo off
setlocal enabledelayedexpansion
rem ---------------------------------------------------------------------------
rem Rocky Kombat - standalone AI harness build + run.
rem
rem Compiles the SHIPPED AI brain and the SHIPPED combat core with plain MSVC,
rem links them with no Unreal libraries, and runs bot-vs-bot matches at the real
rem fixed timestep. A green run is evidence about production code, not a
rem test-only path. RKCRoster.cpp is excluded on purpose: it pulls CoreMinimal.h.
rem
rem Usage:  cmd.exe /c build_ai_tests.bat > ai_tests.log 2>&1
rem ---------------------------------------------------------------------------

set "SRC=C:\RockyKombat\Source\RockyKombat"
set "OUT=%~dp0build"
set "BIN=RKCAITests.exe"

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 ( echo VCVARS_FAILED & exit /b 1 )
echo VCENV_OK

if not exist "%OUT%" mkdir "%OUT%"
pushd "%OUT%" || ( echo MKDIR_FAILED & exit /b 1 )

set COMMON=/nologo /EHsc /std:c++17 /W4 /WX /I "%SRC%"

rem --- 1. Shipped units, strict: warnings are build failures ------------------
cl %COMMON% /c "%SRC%\Combat\RKCCombatCore.cpp" /FoRKCCombatCore.obj
if errorlevel 1 ( echo CORE_COMPILE_FAILED & popd & exit /b 2 )

cl %COMMON% /c "%SRC%\AI\RKCAI.cpp" /FoRKCAI.obj
if errorlevel 1 ( echo AI_COMPILE_FAILED & popd & exit /b 2 )

rem --- 2. Harness itself ------------------------------------------------------
cl %COMMON% /c "%~dp0RKCAITests.cpp" /FoRKCAITests.obj
if errorlevel 1 ( echo HARNESS_COMPILE_FAILED & popd & exit /b 3 )

echo COMPILE_OK

rem --- 3. Link: only the C runtime, no engine, no linker 3rd party libs -------
link /nologo /SUBSYSTEM:CONSOLE /OUT:%BIN% ^
   RKCCombatCore.obj RKCAI.obj RKCAITests.obj
if errorlevel 1 ( echo LINK_FAILED & popd & exit /b 4 )

echo LINK_OK
echo.
echo ==== RUN ====
%BIN%
set RC=%errorlevel%
echo.
echo ==== EXIT CODE %RC% ====
popd
endlocal & exit /b %RC%
