@echo off
setlocal enabledelayedexpansion
rem ---------------------------------------------------------------------------
rem Rocky Kombat - offline opponent regression: build + run.
rem
rem Compiles the SHIPPED AI brain and the SHIPPED combat core with plain MSVC and
rem plays the bot against a completely passive opponent at real game rule
rem values. A green run is evidence about production code, not a test-only path.
rem
rem Usage:  cmd.exe /c build_opponent_regression.bat > opponent_regression.log 2>&1
rem ---------------------------------------------------------------------------

set "SRC=C:\RockyKombat\Source\RockyKombat"
set "OUT=%~dp0build"
set "BIN=RKCOpponentRegression.exe"

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 ( echo VCVARS_FAILED & exit /b 1 )
echo VCENV_OK

if not exist "%OUT%" mkdir "%OUT%"
pushd "%OUT%" || ( echo MKDIR_FAILED & exit /b 1 )

set COMMON=/nologo /EHsc /std:c++17 /W4 /WX /I "%SRC%"

cl %COMMON% /c "%SRC%\Combat\RKCCombatCore.cpp" /FoRKCCombatCore_opp.obj
if errorlevel 1 ( echo CORE_COMPILE_FAILED & popd & exit /b 2 )

cl %COMMON% /c "%SRC%\AI\RKCAI.cpp" /FoRKCAI_opp.obj
if errorlevel 1 ( echo AI_COMPILE_FAILED & popd & exit /b 2 )

cl %COMMON% /c "%~dp0RKCOpponentRegression.cpp" /FoRKCOpponentRegression.obj
if errorlevel 1 ( echo HARNESS_COMPILE_FAILED & popd & exit /b 3 )

echo COMPILE_OK

link /nologo /SUBSYSTEM:CONSOLE /OUT:%BIN% ^
   RKCCombatCore_opp.obj RKCAI_opp.obj RKCOpponentRegression.obj
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
