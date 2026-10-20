#!/usr/bin/env bash
# bootstrap_android.sh
#
# Installs the Android toolchain that Unreal Engine 5.8 requires, into standard
# locations, without touching anything else on the machine.
#
# Versions are taken from the installed engine, not from memory:
#   Engine/Extras/Android/SetupAndroid.bat  -> platforms android-36,
#                                              build-tools 36.1.0,
#                                              cmake 3.22.1,
#                                              ndk 27.2.12479018
#   UEDeployAndroid.cs MinimumSDKLevel = 36
#   AndroidPlatformSDK.cs requires Java release >= 17
#
# Everything is logged. Re-running is safe: existing downloads and packages are
# skipped or re-verified rather than duplicated.

set -u

LOG=/c/RockyKombat/Saved/android_setup.log
DL=/c/AndroidSetup
SDK="/c/Users/DELL/AppData/Local/Android/Sdk"
JDK_ZIP_URL="https://aka.ms/download-jdk/microsoft-jdk-17-windows-x64.zip"
CT_ZIP_URL="https://dl.google.com/android/repository/commandlinetools-win-13114758_latest.zip"
NDK_VER="27.2.12479018"

mkdir -p "$DL" "$SDK" "$(dirname "$LOG")"
exec > >(tee -a "$LOG") 2>&1

step() { echo; echo "===== [$(date +%H:%M:%S)] $* ====="; }

step "ENVIRONMENT"
echo "dl=$DL"
echo "sdk=$SDK"

step "1/6 DOWNLOAD JDK 17 (Microsoft OpenJDK)"
if [ ! -s "$DL/jdk17.zip" ]; then
  curl -L --fail --retry 3 --retry-delay 5 --max-time 1800 -o "$DL/jdk17.zip" "$JDK_ZIP_URL" \
    && echo "downloaded $(stat -c %s "$DL/jdk17.zip") bytes"
else
  echo "already present: $(stat -c %s "$DL/jdk17.zip") bytes"
fi

step "2/6 DOWNLOAD ANDROID COMMAND-LINE TOOLS"
if [ ! -s "$DL/cmdline-tools.zip" ]; then
  curl -L --fail --retry 3 --retry-delay 5 --max-time 1800 -o "$DL/cmdline-tools.zip" "$CT_ZIP_URL" \
    && echo "downloaded $(stat -c %s "$DL/cmdline-tools.zip") bytes"
else
  echo "already present: $(stat -c %s "$DL/cmdline-tools.zip") bytes"
fi

step "3/6 EXTRACT JDK"
if [ ! -d "$DL/jdk" ]; then
  mkdir -p "$DL/jdk"
  unzip -q -o "$DL/jdk17.zip" -d "$DL/jdk" && echo "extracted"
else
  echo "already extracted"
fi
JAVA_HOME_DIR="$(find "$DL/jdk" -maxdepth 2 -type d -name 'jdk-17*' | head -1)"
echo "JAVA_HOME_DIR=$JAVA_HOME_DIR"
if [ -z "$JAVA_HOME_DIR" ]; then echo "FATAL: no jdk-17 directory found"; exit 1; fi
export JAVA_HOME="$JAVA_HOME_DIR"

step "4/6 EXTRACT COMMAND-LINE TOOLS INTO THE SDK"
if [ ! -d "$SDK/cmdline-tools/latest/bin" ]; then
  rm -rf "$DL/ct"; mkdir -p "$DL/ct"
  unzip -q -o "$DL/cmdline-tools.zip" -d "$DL/ct" || { echo "FATAL: unzip failed"; exit 1; }
  mkdir -p "$SDK/cmdline-tools"
  rm -rf "$SDK/cmdline-tools/latest"
  # The zip contains a top-level 'cmdline-tools' folder; sdkmanager wants
  # cmdline-tools/latest/ with bin/ directly inside it.
  if [ -d "$DL/ct/cmdline-tools/bin" ]; then
    mv "$DL/ct/cmdline-tools" "$SDK/cmdline-tools/latest"
  else
    find "$DL/ct" -maxdepth 2 -type d -name bin -printf '%h\n' | head -1 | xargs -I{} sh -c 'mkdir -p "'"$SDK"'/cmdline-tools"; cp -r "{}" "'"$SDK"'/cmdline-tools/latest"'
  fi
else
  echo "sdkmanager already installed"
fi
ls -la "$SDK/cmdline-tools/latest/bin" 2>/dev/null | head

step "5/6 ACCEPT LICENCES"
SDKMANAGER="$SDK/cmdline-tools/latest/bin/sdkmanager.bat"
if [ ! -f "$SDKMANAGER" ]; then echo "FATAL: sdkmanager.bat not found at $SDKMANAGER"; exit 1; fi
yes | "$SDKMANAGER" --sdk_root="$(cygpath -w "$SDK")" --licenses >"$DL/licenses.log" 2>&1
echo "licence step exit=$? (see $DL/licenses.log)"
tail -5 "$DL/licenses.log"

step "6/6 INSTALL SDK PACKAGES (this is the large one)"
"$SDKMANAGER" --sdk_root="$(cygpath -w "$SDK")" \
  "platform-tools" \
  "platforms;android-36" \
  "build-tools;36.1.0" \
  "cmake;3.22.1" \
  "ndk;$NDK_VER" 2>&1 | tail -40
echo "sdkmanager exit=${PIPESTATUS[0]}"

step "RESULT"
ls -d "$SDK/platforms/android-36" "$SDK/build-tools/36.1.0" "$SDK/ndk/$NDK_VER" "$SDK/platform-tools" 2>&1
echo "JAVA_HOME=$JAVA_HOME"
"$JAVA_HOME/bin/java" -version 2>&1 | head -3
echo
echo "DONE $(date)"
