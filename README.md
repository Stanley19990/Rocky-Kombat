# Rocky Kombat

## Android test build

[Download the Android APK — 1.0.2-alpha.2, build 3](https://github.com/Stanley19990/Rocky-Kombat/releases/download/v1.0.2-alpha.2/RockyKombat-1.0.2-alpha.2-build3-arm64.apk)

[Release notes, build information and checksum](https://github.com/Stanley19990/Rocky-Kombat/releases/tag/v1.0.2-alpha.2)

Internal alpha for ARM64 Android 8.0/API 26 or newer. Download size: about 175 MB.
Game content is included; no separate OBB or Unreal installation is needed on the phone.

Open the downloaded APK, allow installation from your browser/file manager if
prompted, then install and launch Rocky Kombat.

First match: **Play > Rocky Boxing Arena > Play Against Computer > choose both
fighters > Champion > Fight**. Move with the left stick. Use Jab, Heavy,
Block (hold), and Dodge on the right. Choose Rematch after the result.

The APK is debug-signed for testing. Phone rendering, performance, sound and touch
still need device testing. Four-pose animations remain provisional. Online Versus
awaits a hosted multiplayer service; offline play against the CPU is available.

## iPhone and iPad

The APK cannot install on iOS. There is no IPA or TestFlight invitation yet.
A Mac with Xcode and an Apple Developer/App Store Connect account are needed to
complete that separately signed build. See [mobile setup](docs/15-mobile-testing.md).

## Source and build

Built with Unreal Engine 5.8.4. The APK's exact source revision, signature status
and SHA-256 are recorded in the release's `BUILD_INFO.json` and `SHA256SUMS.txt`.
See [build notes](docs/02-build-and-run.md) and [multiplayer deployment](Services/multiplayer/README.md).
