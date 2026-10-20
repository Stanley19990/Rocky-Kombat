#!/usr/bin/env bash
# rkc_fetch_android.sh
#
# Downloads and installs the Android toolchain Unreal Engine 5.8 needs for an
# ARM64 APK, on a link that keeps resetting mid-transfer.
#
# WHY THIS EXISTS
# The first attempt used a single curl per file. This host's connection to the
# JDK and Google hosts reset at 91% of the JDK download (curl error 56) and
# immediately on the command-line tools (curl error 35), so nothing was
# installed. A single-shot downloader cannot survive that. Every fetch here is
# resumable (curl -C -), retried until the byte count matches the size the
# server declares, and the archive is integrity-checked before it is unpacked.
#
# VERSIONS (read from the engine, not guessed)
#   Engine/Extras/Android/SetupAndroid.bat pins:
#     platforms;android-36  build-tools;36.1.0  cmake;3.22.1
#     ndk;27.2.12479018
#   and expects JAVA_HOME to be a JDK 17 (Android Studio ships one as jbr).
#   Android Studio itself is NOT installed and is not needed: the command-line
#   tools plus a standalone JDK 17 are enough for sdkmanager.
#
# WHAT IT TOUCHES
#   C:\AndroidSetup                      downloads and the unpacked JDK
#   %LOCALAPPDATA%\Android\Sdk           the SDK (platform-tools, platforms,
#                                        build-tools, cmake, ndk)
#   User environment variables JAVA_HOME, ANDROID_HOME, NDKROOT, NDK_ROOT.
#   The machine PATH is deliberately NOT modified (setx on PATH truncates it).
#   Nothing outside these is changed.
#
# SAFE TO RE-RUN: already-complete files are skipped, a corrupt archive is
# deleted and fetched again, and every step is logged.
#
# Log: C:\AndroidSetup\fetch.log

set -u

DL=/c/AndroidSetup
SDK="/c/Users/DELL/AppData/Local/Android/Sdk"
LOG="$DL/fetch.log"
mkdir -p "$DL" "$SDK" || exit 1

JDK_URL="https://aka.ms/download-jdk/microsoft-jdk-17.0.13-windows-x64.zip"
JDK_BYTES=186360891
CT_URL="https://dl.google.com/android/repository/commandlinetools-win-13114758_latest.zip"
CT_BYTES=143040480

log() { echo "[$(date '+%H:%M:%S')] $*" | tee -a "$LOG"; }

filesize() { [ -f "$1" ] && stat -c %s "$1" 2>/dev/null || echo 0; }

# fetch_resume <url> <out> <expected-bytes> <label>
# Resumes until the file is exactly the size the server declares. Each pass gets
# its own retry budget, and a transfer that stalls below 8 KB/s for 45 s is
# aborted so the loop can resume instead of hanging.
fetch_resume() {
    local url="$1" out="$2" want="$3" label="$4" attempt=0 have
    while :; do
        have=$(filesize "$out")
        if [ "$have" -eq "$want" ]; then
            log "$label: complete, $have bytes"
            return 0
        fi
        attempt=$((attempt + 1))
        if [ "$attempt" -gt 60 ]; then
            log "$label: FAILED after $attempt attempts, $have / $want bytes"
            return 1
        fi
        log "$label: attempt $attempt, resuming at $have / $want bytes"
        curl -L -C - --retry 6 --retry-delay 4 --retry-all-errors \
             --connect-timeout 25 --speed-time 45 --speed-limit 8192 \
             -o "$out" "$url" >>"$LOG" 2>&1
        sleep 3
    done
}

unpack_zip() {
    # unpack_zip <zip> <destdir>
    rm -rf "$2"
    mkdir -p "$2"
    unzip -q -o "$1" -d "$2" >>"$LOG" 2>&1
}

log "===== Rocky Kombat Android toolchain fetch ====="
log "downloads: $DL"
log "sdk:       $SDK"

# ---- 1. JDK 17 ------------------------------------------------------------
JDKZIP="$DL/jdk17.zip"
if ! fetch_resume "$JDK_URL" "$JDKZIP" "$JDK_BYTES" "JDK 17"; then
    log "FATAL: JDK 17 could not be downloaded"
    exit 1
fi

if ! unzip -tq "$JDKZIP" >/dev/null 2>&1; then
    log "JDK archive failed its integrity check; discarding it and fetching again"
    rm -f "$JDKZIP"
    if ! fetch_resume "$JDK_URL" "$JDKZIP" "$JDK_BYTES" "JDK 17 (retry)"; then
        log "FATAL: JDK 17 could not be downloaded cleanly"
        exit 1
    fi
    if ! unzip -tq "$JDKZIP" >/dev/null 2>&1; then
        log "FATAL: JDK archive is still corrupt after a clean re-download"
        exit 1
    fi
fi
log "JDK archive integrity: OK"

rm -rf "$DL/jdk"
unpack_zip "$JDKZIP" "$DL/jdk"
JAVA_HOME_DIR=$(find "$DL/jdk" -maxdepth 1 -type d -name "jdk-*" | head -1)
if [ -z "$JAVA_HOME_DIR" ]; then
    log "FATAL: no jdk-* directory inside the unpacked archive"
    exit 1
fi
export JAVA_HOME="$JAVA_HOME_DIR"
log "JAVA_HOME=$JAVA_HOME"
"$JAVA_HOME/bin/java" -version >>"$LOG" 2>&1 || {
    log "FATAL: the unpacked JDK does not run"
    exit 1
}
log "java: $("$JAVA_HOME/bin/java" -version 2>&1 | head -1)"

# ---- 2. Android command-line tools ---------------------------------------
CTZIP="$DL/cmdline-tools.zip"
if ! fetch_resume "$CT_URL" "$CTZIP" "$CT_BYTES" "Android command-line tools"; then
    log "FATAL: command-line tools could not be downloaded"
    exit 1
fi
if ! unzip -tq "$CTZIP" >/dev/null 2>&1; then
    log "command-line tools archive failed its integrity check; discarding it"
    rm -f "$CTZIP"
    fetch_resume "$CT_URL" "$CTZIP" "$CT_BYTES" "Android command-line tools (retry)" || exit 1
fi

# sdkmanager only counts as installed at <sdk>/cmdline-tools/latest/bin
unpack_zip "$CTZIP" "$DL/cmdline-tools-unpacked"
mkdir -p "$SDK/cmdline-tools"
rm -rf "$SDK/cmdline-tools/latest"
mv "$DL/cmdline-tools-unpacked/cmdline-tools" "$SDK/cmdline-tools/latest" || {
    log "FATAL: could not place cmdline-tools at $SDK/cmdline-tools/latest"
    exit 1
}
SDKMANAGER="$SDK/cmdline-tools/latest/bin/sdkmanager.bat"
[ -f "$SDKMANAGER" ] || { log "FATAL: sdkmanager.bat missing after unpack"; exit 1; }
log "sdkmanager: $SDKMANAGER"

# ---- 3. SDK packages ------------------------------------------------------
export ANDROID_HOME="$SDK"
export PATH="$JAVA_HOME/bin:$SDK/platform-tools:$PATH"

log "---- accepting the SDK licenses ----"
yes 2>/dev/null | "$SDKMANAGER" --sdk_root="$SDK" --licenses >>"$LOG" 2>&1
log "license step exit: $?"

# The exact set the engine pins. One retry pass: Google's mirrors occasionally
# drop a package mid-transfer on this link.
PKGS=( "platform-tools" "platforms;android-36" "build-tools;36.1.0" "cmake;3.22.1" "ndk;27.2.12479018" )
install_pkgs() {
    "$SDKMANAGER" --sdk_root="$SDK" "${PKGS[@]}" >>"$LOG" 2>&1
}
log "---- installing: ${PKGS[*]} ----"
if ! install_pkgs; then
    log "first install pass failed; retrying once"
    sleep 10
    install_pkgs || log "install pass failed again"
fi

# ---- 4. Verify what actually landed --------------------------------------
log "---- verification ----"
FAILED=0
for d in "platform-tools" "platforms/android-36" "build-tools/36.1.0" "cmake/3.22.1" "ndk/27.2.12479018"; do
    if [ -d "$SDK/$d" ]; then
        log "  OK      $d"
    else
        log "  MISSING $d"
        FAILED=1
    fi
done

if [ "$FAILED" -ne 0 ]; then
    log "RESULT: INCOMPLETE - at least one package is missing"
    exit 2
fi

# ---- 5. Persist the environment for the editor, UBT and RunUAT ------------
powershell -NoProfile -Command "[Environment]::SetEnvironmentVariable('JAVA_HOME','$JAVA_HOME','User')" >>"$LOG" 2>&1
powershell -NoProfile -Command "[Environment]::SetEnvironmentVariable('ANDROID_HOME','$SDK','User')" >>"$LOG" 2>&1
powershell -NoProfile -Command "[Environment]::SetEnvironmentVariable('NDKROOT','$SDK/ndk/27.2.12479018','User')" >>"$LOG" 2>&1
powershell -NoProfile -Command "[Environment]::SetEnvironmentVariable('NDK_ROOT','$SDK/ndk/27.2.12479018','User')" >>"$LOG" 2>&1
log "user environment written: JAVA_HOME, ANDROID_HOME, NDKROOT, NDK_ROOT (PATH untouched)"

log "RESULT: COMPLETE"
log "  JAVA_HOME=$JAVA_HOME"
log "  ANDROID_HOME=$SDK"
log "  NDKROOT=$SDK/ndk/27.2.12479018"
exit 0
