@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "%~dp0build" mkdir "%~dp0build"
pushd "%~dp0build"
cl /nologo /EHsc /std:c++17 /W4 /WX /I "C:\RockyKombat\Source\RockyKombat" "%~dp0RKCPlayableBalanceTests.cpp" "C:\RockyKombat\Source\RockyKombat\Combat\RKCCombatCore.cpp" "C:\RockyKombat\Source\RockyKombat\AI\RKCAI.cpp" /Fe:RKCPlayableBalanceTests.exe
if errorlevel 1 (popd & exit /b 2)
RKCPlayableBalanceTests.exe
set RESULT=%errorlevel%
popd
exit /b %RESULT%
