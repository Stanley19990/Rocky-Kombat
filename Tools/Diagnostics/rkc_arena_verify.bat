@echo off
setlocal
title Rocky Kombat - arena single-ring verification

REM ===========================================================================
REM  rkc_arena_verify.bat
REM
REM  Rebuilds, re-packages and then PROVES the packaged game, instead of trusting
REM  a source change. Steps:
REM    1 locate engine + project
REM    2 build RockyKombatEditor Win64 Development
REM    3 cook / stage / package a fresh Win64 Development build
REM    4 run the NEWEST packaged exe with -RKCSelfTest=3 and extract the record
REM    5 print the manual screenshot instructions
REM    6 analyse every capture in Saved\Evidence\Arena with rkc_arena_verify.py
REM
REM  Place this file next to rkc_arena_verify.py (e.g. Tools\Diagnostics\).
REM
REM  Optional environment overrides:
REM    RKC_UE_ROOT      engine install              (default: C:\Program Files\Epic Games\UE_5.8)
REM    RKC_PROJECT      project root                (auto-detected)
REM    RKC_SKIP_BUILD   1 = skip step 2
REM    RKC_SKIP_PACKAGE 1 = skip step 3
REM    RKC_SKIP_SELFTEST 1 = skip step 4
REM ===========================================================================

set "RKC_UE_ROOT=%RKC_UE_ROOT%"
if not defined RKC_UE_ROOT set "RKC_UE_ROOT=C:\Program Files\Epic Games\UE_5.8"

set "RKC_PROJECT=%RKC_PROJECT%"
if not defined RKC_PROJECT call :find_project
if not defined RKC_PROJECT goto :no_project

set "RKC_EVID=%RKC_PROJECT%\Saved\Evidence\Arena"
if not exist "%RKC_EVID%" mkdir "%RKC_EVID%" 2>nul

echo ===========================================================================
echo  Rocky Kombat - arena single-ring verification
echo  engine  : %RKC_UE_ROOT%
echo  project : %RKC_PROJECT%
echo  evidence: %RKC_EVID%
echo ===========================================================================
echo.

if not exist "%RKC_UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" goto :no_ue
if not exist "%RKC_PROJECT%\RockyKombat.uproject" goto :no_project

REM ---------------------------------------------------------------- 2. build
if "%RKC_SKIP_BUILD%"=="1" goto :skip_build
echo [2/6] Building RockyKombatEditor Win64 Development ...
call "%RKC_UE_ROOT%\Engine\Build\BatchFiles\Build.bat" RockyKombatEditor Win64 Development -project="%RKC_PROJECT%\RockyKombat.uproject" -waitmutex
if errorlevel 1 goto :fail
goto :built
:skip_build
echo [2/6] build skipped (RKC_SKIP_BUILD=1)
:built

REM -------------------------------------------------------------- 3. package
if "%RKC_SKIP_PACKAGE%"=="1" goto :skip_pack
echo.
echo [3/6] Cooking / staging / packaging Win64 Development ...
echo        (this is the slow step; expect many minutes)
call "%RKC_UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%RKC_PROJECT%\RockyKombat.uproject" -noP4 -platform=Win64 -clientconfig=Development -serverconfig=Development -build -cook -allmaps -stage -pak -compress -archive -archivedirectory="%RKC_PROJECT%\Packaged" -utf8output
if errorlevel 1 goto :fail
goto :packed
:skip_pack
echo [3/6] package skipped (RKC_SKIP_PACKAGE=1)
:packed

REM -------------------------------------------------------------- 4. selftest
set "RKC_EXE=%RKC_PROJECT%\Packaged\Windows\RockyKombat.exe"
if not exist "%RKC_EXE%" set "RKC_EXE=%RKC_PROJECT%\Packaged\WindowsNoEditor\RockyKombat.exe"

if "%RKC_SKIP_SELFTEST%"=="1" goto :skip_selftest
echo.
echo [4/6] Running the packaged self-test (windowed, 1280x720) ...
if not exist "%RKC_EXE%" goto :no_exe
echo        exe: %RKC_EXE%
"%RKC_EXE%" -RKCSelfTest=3 -windowed -ResX=1280 -ResY=720 -nosplash -log -abslog="%RKC_EVID%\selftest_packaged.log"
echo        self-test exit code: %errorlevel%
findstr /i /c:"greybox piece" /c:"failed check" /c:"checks" /c:"FAILED" "%RKC_EVID%\selftest_packaged.log" > "%RKC_EVID%\selftest_extract.txt" 2>nul
echo.
echo        ---- self-test record extract ----
type "%RKC_EVID%\selftest_extract.txt" 2>nul
echo        ----------------------------------
echo.
echo        Required in that record before you continue:
echo          * 0 failed checks
echo          * "0 of N greybox piece(s) visible"
echo        If the packaged build still reports a non-zero visible count, STOP:
echo        see the triage section of docs\09-arena-single-ring.md.
goto :selftest_done
:skip_selftest
echo [4/6] packaged self-test skipped (RKC_SKIP_SELFTEST=1)
:selftest_done

REM --------------------------------------------------------- 5. human capture
echo.
echo [5/6] Now look at the packaged game yourself and capture PNGs into:
echo        %RKC_EVID%
echo.
echo        Launch:  "%RKC_EXE%"
echo        Do NOT pass any -rkc.* switch: the packaged default must already be
echo        the supplied Midnight Rooftop Ring with no duplicate ring.
echo.
echo        Capture these three stages at minimum (use the in-game capture key
echo        from docs\08-playtest-checklist.md, then copy the PNGs here and name
echo        them so the report is readable):
echo          arena_roundstart.png   countdown / round just started
echo          arena_gameplay.png     mid-round, both fighters visible
echo          arena_results.png      results card
echo        Repeat arena_roundstart.png for a rematch, and once at 1024x768 (4:3)
echo        and once at 720x1280 (portrait) via the in-game resolution options.
echo.
echo        A source-code visibility change does not establish the fix; these
echo        captures are the evidence.
echo.

REM ------------------------------------------------------------ 6. analyse
echo [6/6] Analysing captures ...
where python >nul 2>nul
if errorlevel 1 goto :no_python

set "RKC_ART=%RKC_PROJECT%\ArtSource\RockyKombatPack_v1.0\Arena\Midnight_Rooftop_Ring.png"
set "RKC_CTL=%RKC_PROJECT%\Saved\Evidence\Stages\20261017_235858\05_match.png"
set "RKC_TOOL=%~dp0rkc_arena_verify.py"
if not exist "%RKC_TOOL%" goto :no_tool

if not exist "%RKC_ART%" goto :art_only
if not exist "%RKC_CTL%" goto :art_only
python "%RKC_TOOL%" "--art=%RKC_ART%" "--control=%RKC_CTL%" "--shotdir=%RKC_EVID%" "--out=%RKC_EVID%\arena_verify_report.json"
goto :analysed

:art_only
echo        note: reference artwork and/or pre-fix control not found at the
echo              documented paths - M2 falls back to the weaker absolute test.
python "%RKC_TOOL%" "--shotdir=%RKC_EVID%" "--out=%RKC_EVID%\arena_verify_report.json"

:analysed
echo.
echo ===========================================================================
echo  Sign-off requires ALL of:
echo    1. packaged self-test record: 0 failed checks and 0 visible greybox pieces
echo    2. analyzer: M1..M4 pass on every capture (round start, gameplay, results,
echo       a rematch, plus a 4:3 and a portrait run)
echo    3. YOUR eyes on the packaged build: exactly one arena - the supplied
echo       Midnight Rooftop Ring - with both fighters' feet on the painted floor
echo       and no empty space below or around the image
echo  The self-test record, the captures and arena_verify_report.json are the
echo  evidence packet. Anything not captured is not verified.
echo ===========================================================================
endlocal
exit /b 0

REM ------------------------------------------------------------------ helpers
:find_project
for %%D in (
  "%USERPROFILE%\Documents\Unreal Projects\RockyKombat"
  "%USERPROFILE%\Documents\RockyKombat"
  "%USERPROFILE%\Desktop\RockyKombat"
  "%USERPROFILE%\RockyKombat"
  "%USERPROFILE%\Source\RockyKombat"
  "%USERPROFILE%\Projects\RockyKombat"
  "C:\RockyKombat"
  "C:\Projects\RockyKombat"
  "C:\Dev\RockyKombat"
  "D:\RockyKombat"
) do (
  if not defined RKC_PROJECT if exist "%%~D\RockyKombat.uproject" set "RKC_PROJECT=%%~D"
)
if defined RKC_PROJECT goto :eof
for /d %%D in ("%USERPROFILE%\*") do (
  if not defined RKC_PROJECT if exist "%%D\RockyKombat.uproject" set "RKC_PROJECT=%%D"
)
if defined RKC_PROJECT goto :eof
for /d %%D in ("%USERPROFILE%\Documents\*") do (
  if not defined RKC_PROJECT if exist "%%D\RockyKombat.uproject" set "RKC_PROJECT=%%D"
)
goto :eof

:no_ue
echo [FAIL] Unreal Engine not found at "%RKC_UE_ROOT%".
echo        Set RKC_UE_ROOT to the engine install directory and re-run.
endlocal
exit /b 1

:no_project
echo [FAIL] Could not locate RockyKombat.uproject.
echo        Set RKC_PROJECT to the project root directory and re-run.
endlocal
exit /b 1

:no_exe
echo [FAIL] Packaged exe not found. Expected one of:
echo          %RKC_PROJECT%\Packaged\Windows\RockyKombat.exe
echo          %RKC_PROJECT%\Packaged\WindowsNoEditor\RockyKombat.exe
echo        Run the packaging step (or check the BuildCookRun output) first.
endlocal
exit /b 1

:no_python
echo [FAIL] python is not on PATH. Install Python 3 and re-run, or run the
echo        analyzer manually:
echo          python rkc_arena_verify.py --art "...Midnight_Rooftop_Ring.png" --control "...05_match.png" --shotdir "%RKC_EVID%"
endlocal
exit /b 1

:no_tool
echo [FAIL] rkc_arena_verify.py not found next to this script (%~dp0).
echo        Keep the two files in the same folder.
endlocal
exit /b 1

:fail
echo.
echo [FAIL] the previous step returned an error. Nothing was verified.
echo        Fix the error, then re-run. To resume from a later step use
echo        RKC_SKIP_BUILD=1 / RKC_SKIP_PACKAGE=1 / RKC_SKIP_SELFTEST=1.
endlocal
exit /b 1
