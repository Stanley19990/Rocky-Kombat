@echo off
REM Build (and run) the combat core rule harness.
REM Usage:  cmd.exe /c build_suite.bat > suite_build.log 2>&1
setlocal
set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call %VCVARS%
if errorlevel 1 ( echo VCVARS_FAILED & exit /b 1 )
echo VCENV_OK
pushd "%~dp0"
cl /nologo /W4 /EHsc /std:c++17 /Fe:RKCCombatCoreTests.exe ^
   RKCCombatCoreTests.cpp ^
   "C:\RockyKombat\Source\RockyKombat\Combat\RKCCombatCore.cpp" ^
   /I "C:\RockyKombat\Source\RockyKombat\Combat"
if errorlevel 1 ( echo COMPILE_FAILED & exit /b 2 )
echo COMPILE_OK
echo === RUN ===
RKCCombatCoreTests.exe
echo === RUNDONE ===
popd
endlocal
