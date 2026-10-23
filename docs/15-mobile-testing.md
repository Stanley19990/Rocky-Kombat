# Mobile test distribution

Android and iOS are separate builds. An APK installs on Android phones; it does
not install on an iPhone. Installing the Unreal iOS platform files on Windows
does not supply Xcode or an Apple signing identity.

## Android

Current download: [1.0.3-alpha.3, build 4](https://github.com/Stanley19990/Rocky-Kombat/releases/tag/v1.0.3-alpha.3).
This OpenGL ES 3.2 candidate adds Rockycat launch branding after a reported
startup exit in build 3. Its public download and checksum passed verification;
phone startup remains unverified. See [candidate checks](16-android-startup-candidate.md).

The first APK is published as
[1.0.2-alpha.2, build 3](https://github.com/Stanley19990/Rocky-Kombat/releases/tag/v1.0.2-alpha.2)
on 2026-10-23. Size: 175,204,803 bytes. Source:
`20954a30ba6ffe04796f90f0466f34947453f798`.
SHA-256: `187802321f3fec79247ce6a8857c883d06a3afb56c9ee81b8bd41271a50d9637`.
The signature, ARM64 manifest and embedded `assets/main.obb.png` passed checks.
The public APK was downloaded without authentication and matched the full SHA-256.
The existing three-match engine integration suite also passed on Unreal 5.8.4.
These checks do not constitute a phone playtest; no authorized phone was connected.

Run `Scripts/package_android.bat` to build a self-contained ARM64 APK. Successful
packaging writes the APK, `BUILD_INFO.json` and `SHA256SUMS.txt` in `Dist/Android`.
The build requires Unreal's Android optional component, SDK platform 36,
build-tools 36.0.0, NDK 27.2.12479018 and Java 17 or newer. The installed
Unreal 5.8.4 SDK manifest and Java templates were checked on 2026-10-23.

Download the `.apk` asset from the repository's pre-release on the Android
phone. Open it and, if prompted, allow that browser or file manager to install
unknown apps. Install and open Rocky Kombat. If updating fails due to a signing
identity mismatch, remove the previous test installation first (this erases its
local settings). This alpha uses a disposable Android debug signing identity.

First match: **Play > Rocky Boxing Arena > Play Against Computer > choose your
fighter > choose the CPU fighter > Champion > Fight**. Use the left thumb stick
to move and the right-side Jab, Heavy, Block (hold) and Dodge controls. Results
offers Rematch. The APK includes its game content; no separate OBB is needed.

Minimum: ARM64 Android 8.0/API 26 and OpenGL ES 3.2. Device rendering, performance, sound, touch
and completion/rematch need a real phone test before wider release. Four-pose
animations remain provisional. Online Versus awaits a hosted service endpoint;
offline human-versus-CPU play is the intended test in this build.

## iPhone and iPad

Project identity and landscape orientation are prepared under
`IOSRuntimeSettings` in `Config/DefaultEngine.ini`. The owner confirmed on
2026-10-23 that no Mac or Apple Developer account is available yet, so no IPA
or TestFlight invitation exists.

To complete iOS distribution:

1. Provide a Mac with Xcode and the matching Unreal 5.8 installation. This C++
   project can build on that Mac or use Unreal's remote Mac build from Windows.
2. Enroll in the Apple Developer Program and create an App Store Connect app
   for `com.rockycat.rockykombat`. Configure signing locally with that team.
3. Build an iOS distribution archive and upload it to App Store Connect.
4. Configure the TestFlight test information and external tester group, submit
   the build for the required beta review, then enable its public invitation.
5. Share that TestFlight invitation with iPhone/iPad testers. A GitHub source
   ZIP or Android APK is not an iOS installation route.

Never commit Apple signing keys, certificate bundles, provisioning profiles or
App Store Connect API secrets. Keep remote-Mac credentials in local settings.

References: [Epic iOS setup](https://dev.epicgames.com/documentation/unreal-engine/setting-up-an-unreal-engine-project-for-ios),
[Apple external TestFlight testers](https://developer.apple.com/help/app-store-connect/test-a-beta-version/invite-external-testers/).
