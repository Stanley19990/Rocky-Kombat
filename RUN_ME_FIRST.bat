@echo off
setlocal EnableExtensions
title Rocky Kombat - rebuild, repackage and prove the single-arena fix

REM ===========================================================================
REM  RUN_ME_FIRST.bat   --   DOUBLE-CLICK THIS FILE.
REM
REM  Why it exists: the interactive harness (Tools\Diagnostics\rkc_arena_verify.bat)
REM  prints everything to the console and exits immediately, so double-clicking it
REM  closes the window and loses the output. This wrapper captures all of it and
REM  writes a SHORT summary at the end that is small enough to copy and send back.
REM
REM  What it does:
REM    1. rebuilds RockyKombatEditor (Win64 Development)
REM    2. cooks / stages / packages a fresh Win64 Development build
REM    3. runs the NEWEST packaged exe with -RKCSelfTest=3
REM    4. analyses any captures in Saved\Evidence\Arena
REM
REM  Outputs (both in Tools\Diagnostics\):
REM    arena_run_<stamp>.log       full, possibly enormous, raw output
REM    arena_summary_<stamp>.txt   the ~40 lines that actually matter (auto-opens)
REM
REM  Takes 20-45 minutes. The window WILL look idle: that is intentional, all
REM  output is being written to the .log file. Do not close the window, and do
REM  not press keys in it while the game self-test is running.
REM
REM  Optional overrides (set them in a cmd window before launching, or pre-set
REM  them when calling this file):
REM    RKC_UE_ROOT       engine install   (default: C:\Program Files\Epic Games\UE_5.8)
REM    RKC_PROJECT       project root     (default: the folder holding this file)
REM    RKC_SKIP_BUILD    1 = skip step 1
REM    RKC_SKIP_PACKAGE  1 = skip step 2
REM    RKC_SKIP_SELFTEST 1 = skip step 3
REM
REM  Typical SECOND run, after you have captured your own screenshots into
REM  Saved\Evidence\Arena (does step 4 only, seconds not minutes):
REM    cmd.exe /c "cd /d C:\RockyKombat && set RKC_SKIP_BUILD=1 && set RKC_SKIP_PACKAGE=1 && set RKC_SKIP_SELFTEST=1 && RUN_ME_FIRST.bat"
REM ===========================================================================

set "HERE=%~dp0"
if "%HERE:~-1%"=="\" set "HERE=%HERE:~0,-1%"
set "DIAG=%HERE%\Tools\Diagnostics"

if not defined RKC_PROJECT set "RKC_PROJECT=%HERE%"
if not defined RKC_UE_ROOT set "RKC_UE_ROOT=C:\Program Files\Epic Games\UE_5.8"
set "RKC_PROJECT=%RKC_PROJECT%"

REM --------------------------------------------------------------- sanity gates
if not exist "%HERE%\RockyKombat.uproject" goto :wrong_folder
if not exist "%DIAG%\rkc_arena_verify.bat" goto :missing_harness

if not exist "%DIAG%" mkdir "%DIAG%" 2>nul
if not exist "%DIAG%\" goto :no_write

set "STAMP=run"
for /f "usebackq delims=" %%i in (`powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss" 2^>nul`) do set "STAMP=%%i"

set "LOG=%DIAG%\arena_run_%STAMP%.log"
set "SUM=%DIAG%\arena_summary_%STAMP%.txt"

echo ===========================================================================
echo  Rocky Kombat - rebuild, repackage and prove the single-arena fix
echo ---------------------------------------------------------------------------
echo   engine   : %RKC_UE_ROOT%
echo   project  : %RKC_PROJECT%
echo   full log : %LOG%
echo   summary  : %SUM%
echo ---------------------------------------------------------------------------
echo   Expect 20-45 minutes. The window will look idle; that is normal, the
echo   output is going to the full log file. Do NOT close this window.
echo ===========================================================================
echo.
echo   Started: %STAMP%
echo.

if not exist "%RKC_UE_ROOT%\Engine\Build\BatchFiles\Build.bat" (
  echo   [WARN] No engine at "%RKC_UE_ROOT%".
  echo          If your engine lives elsewhere, close this, open a cmd window and run:
  echo            set RKC_UE_ROOT=D:\Your\Engine\Path
  echo            C:\RockyKombat\RUN_ME_FIRST.bat
  echo.
)

call "%DIAG%\rkc_arena_verify.bat" > "%LOG%" 2>&1
set "RC=%errorlevel%"
set "ERRLEV=%RC%"

echo.
echo   Harness finished with exit code %RC%.
echo   Writing the short summary ...

> "%SUM%" echo ROCKY KOMBAT - ARENA SINGLE-RING VERIFICATION SUMMARY
>>"%SUM%" echo run started: %STAMP%
>>"%SUM%" echo harness exit code: %RC%
>>"%SUM%" echo full log: %LOG%
>>"%SUM%" echo.
>>"%SUM%" echo ================= BUILD / PACKAGE =================
findstr /i /c:"BUILD SUCCESSFUL" /c:"BUILD FAILED" /c:"Result: Succeeded" /c:"Result: Failed" /c:"AutomationTool exiting" /c:"error C" /c:"error LNK" /c:"fatal error" /c:"[FAIL]" "%LOG%" >>"%SUM%" 2>nul
>>"%SUM%" echo.
>>"%SUM%" echo ================= SELF-TEST / ARENA CHECKS =================
findstr /i /c:"greybox piece" /c:"failed check" /c:"SelfTest" /c:"self-test exit code" /c:"one visible arena" /c:"the imported image is still the arena" "%LOG%" >>"%SUM%" 2>nul
>>"%SUM%" echo.
>>"%SUM%" echo ================= ANALYSIS OF CAPTURES =================
findstr /i /c:"arena_verify_report" /c:"flat_sat_arena" /c:"bottom_band" /c:"M1" /c:"M2" /c:"M3" /c:"M4" /c:"PASS" /c:"FAIL" "%LOG%" >>"%SUM%" 2>nul
>>"%SUM%" echo.
>>"%SUM%" echo ================= LAST 25 LINES OF FULL LOG =================
powershell -NoProfile -Command "Get-Content -LiteralPath '%LOG%' -Tail 25" >>"%SUM%" 2>nul

>>"%SUM%" echo.
>>"%SUM%" echo ================= WHAT TO DO NEXT =================
>>"%SUM%" echo 1. Look for "0 failed checks" and "0 of N greybox piece(s) visible".
>>"%SUM%" echo 2. Launch the packaged game yourself and LOOK at it:
>>"%SUM%" echo      Packaged\Windows\RockyKombat.exe
>>"%SUM%" echo    No -rkc.* switches. Exactly one arena, both fighters' feet on the
>>"%SUM%" echo    painted floor, no empty band under or beside the image.
>>"%SUM%" echo 3. Capture round start / mid-round / results into Saved\Evidence\Arena
>>"%SUM%" echo    then re-run this file with the three RKC_SKIP_* set to 1 (see the
>>"%SUM%" echo    header comment) to score the pixels.

echo.
echo ===========================================================================
echo   SEND THIS FILE BACK:  %SUM%
echo ===========================================================================
echo.
type "%SUM%" 2>nul
echo.
 echo   Opening it in Notepad so you can copy the whole thing ...
start "" notepad.exe "%SUM%"
echo.
echo   Press any key to close this window.
pause >nul
endlocal & exit /b %ERRLEV%

REM ------------------------------------------------------------------- helpers
:wrong_folder
echo [FAIL] RockyKombat.uproject is not next to this file.
echo        This wrapper must sit in the project root, i.e. %HERE%
echo        Move RUN_ME_FIRST.bat into the folder that contains
echo        RockyKombat.uproject, then double-click it again.
echo.
pause
endlocal & exit /b 1

:missing_harness
echo [FAIL] Tools\Diagnostics\rkc_arena_verify.bat was not found under:
echo          %DIAG%
echo        This wrapper is only an output-capturing shell around that harness.
echo        Restore the diagnostics folder, or run the harness directly.
echo.
pause
endlocal & exit /b 1

:no_write
echo [FAIL] Cannot create the diagnostics folder:
echo          %DIAG%
echo        Check permissions / free disk space, then try again.
echo.
pause
endlocal & exit /b 1
