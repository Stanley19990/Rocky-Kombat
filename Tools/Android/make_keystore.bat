@echo off
REM ============================================================================
REM  Rocky Kombat - create the private Android release signing identity
REM
REM  Run this ONCE, then back up .secrets\ and never regenerate it: every APK
REM  must be signed by the same key or Android refuses to install an update
REM  over an existing one.
REM
REM  Output (all under C:\RockyKombat\.secrets\, gitignored):
REM    rkc-release.keystore   - PKCS12, RSA 4096, valid 30 years
REM    keystore.pw            - the generated store/key password
REM    rkc-signing-backup.zip - both of the above, for off-machine backup
REM    SHA256SUMS.txt         - hashes to verify the backup later
REM ============================================================================
setlocal EnableDelayedExpansion
set "SIGDIR=C:\RockyKombat\.secrets"
call "%~dp0android_env.bat" quiet

if not exist "%JAVA_HOME%\bin\keytool.exe" (
    echo [FAIL] keytool not found. Install the JDK first.
    exit /b 1
)
if not exist "%SIGDIR%" mkdir "%SIGDIR%"

if exist "%SIGDIR%\rkc-release.keystore" (
    echo.
    echo  A release keystore already exists:
    echo    %SIGDIR%\rkc-release.keystore
    echo  NOT overwriting. Replacing it would break updates for every installed APK.
    echo  If you truly need a new key, move the old one aside yourself.
    exit /b 9
)

REM 28 characters from a strong random source, alphanumeric only so it is safe
REM to pass through a .bat and an .ini without quoting surprises.
set "PW="
for /f "usebackq tokens=*" %%p in (`powershell -NoProfile -Command "$b=[byte[]]::new(32); [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($b); -join ($b | ForEach-Object { 'ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789'[$_ %% 56] }) | Select-Object -First 1"`) do set "PW=%%p"
if not defined PW (
    echo [FAIL] could not generate a password
    exit /b 2
)
> "%SIGDIR%\keystore.pw" echo !PW!

"%JAVA_HOME%\bin\keytool.exe" -genkeypair -v ^
    -keystore "%SIGDIR%\rkc-release.keystore" -storetype PKCS12 ^
    -alias rkc-release -keyalg RSA -keysize 4096 -validity 10950 ^
    -storepass "!PW!" -keypass "!PW!" ^
    -dname "CN=Rockycat Ecosystem, OU=Rocky Kombat, O=Rockycat Ecosystem, L=Internet, ST=Internet, C=US"
if errorlevel 1 (
    echo [FAIL] keytool could not create the keystore
    exit /b 3
)

powershell -NoProfile -Command "Compress-Archive -Path '%SIGDIR%\rkc-release.keystore','%SIGDIR%\keystore.pw' -DestinationPath '%SIGDIR%\rkc-signing-backup.zip' -Force"
powershell -NoProfile -Command "Get-FileHash -Algorithm SHA256 -Path '%SIGDIR%\rkc-release.keystore','%SIGDIR%\rkc-signing-backup.zip' | ForEach-Object { $_.Hash.ToLower() + '  ' + (Split-Path $_.Path -Leaf) }" > "%SIGDIR%\SHA256SUMS.txt"

echo.
echo ======================================================================
echo  Release signing identity created
echo    Keystore : %SIGDIR%\rkc-release.keystore
echo    Backup   : %SIGDIR%\rkc-signing-backup.zip
echo    Hashes   : %SIGDIR%\SHA256SUMS.txt
echo.
echo  NOW: copy rkc-signing-backup.zip somewhere off this machine.
echo       If it is lost, no future APK can update an installed one.
echo ======================================================================
endlocal
exit /b 0
