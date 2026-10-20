@echo off
REM validate_android.bat - ask UBT what it thinks of the Android platform with our toolchain.
REM Uses the engine's bundled dotnet runtime (the standalone UBT.exe apphost has no runtime).
setlocal
cd /d C:\RockyKombat
set "ENGINE=C:\Program Files\Epic Games\UE_5.8\Engine"
set "JAVA_HOME=C:\AndroidSetup\jdk\jdk-17.0.13+11"
set "ANDROID_HOME=C:\Users\DELL\AppData\Local\Android\Sdk"
set "ANDROID_SDK_ROOT=%ANDROID_HOME%"
set "NDKROOT=%ANDROID_HOME%\ndk\27.2.12479018"
set "NDK_ROOT=%NDKROOT%"
set "PATH=%JAVA_HOME%\bin;%PATH%"
"%ENGINE%\Binaries\ThirdParty\DotNet\10.0\win-x64\dotnet.exe" "%ENGINE%\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.dll" -Mode=ValidatePlatforms -OutputSDKs -AllPlatforms -project="C:\RockyKombat\RockyKombat.uproject" -log="C:\RockyKombat\Saved\Evidence\android_validate.log" -verbose > Saved\Evidence\validate_android_stdout.txt 2>&1
echo EXITCODE=%errorlevel%>> Saved\Evidence\validate_android_stdout.txt
endlocal
