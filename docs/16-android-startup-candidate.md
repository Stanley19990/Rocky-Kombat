# Android startup compatibility candidate

The owner reported that Android alpha 1.0.2-alpha.2, build 3, exits after showing
the Unreal splash. No phone is connected and the phone model, Android version
and crash log are unavailable. The cause has **not** been established.

Build 4 (1.0.3-alpha.3) switches this 2D game from automatic Vulkan selection to
OpenGL ES 3.2 only and disables Android File Server in both engine configurations.
These are compatibility mitigations, not a verified resolution of the report.
The package remains ARM64, Android 8.0/API 26+, target API 36, ETC2, and includes
its game content. Phones also need an OpenGL ES 3.2 driver.

The original owner's Rockycat logo is used for the landscape loading card.
The launcher icon is a text-free adaptation made with built-in image_gen;
[generation.json](../ArtSource/Presentation/Android/generation.json) records its
exact prompt and source. Android density exports are made by
`Tools/Android/prepare_branding.ps1`. Android 12+ system splash resources use the
same new icon. The in-game Rockycat, RBA, title and versus presentation is retained.

The previous APK's ZIP alignment and every native library's ELF LOAD segment
alignment passed 16 KB checks. This does not constitute a device launch test.
Startup breadcrumbs now identify entry into game mode, world construction and
front-end/audio initialization in logs to narrow a subsequent crash report.

Published on 2026-10-23: [1.0.3-alpha.3, build 4](https://github.com/Stanley19990/Rocky-Kombat/releases/tag/v1.0.3-alpha.3),
160,689,730 bytes, source `a887fdfeae7841dbbb33e81ca15e206e5d14afbd`.
SHA-256: `dad9c5ad14da80cc8b1c3b6dd895cff267c96f334fc7af72245a9bb8a4f14962`.
Build/cook/package succeeded. Signature and 16 KB ZIP/ELF checks passed; compiled
manifest confirms Vulkan support is disabled and file-server entries are absent.
Rockycat resources, OpenGL
startup shaders and embedded game containers were inspected in the actual APK.
The public APK was downloaded without credentials and matched the full checksum.
The existing three-match desktop engine suite passed with zero failed checks
using `-nullrhi`; it did not verify rendering, touch or audible output on a phone.

Install build 4 over build 3 and launch **Rocky Kombat**. Test reaching the menu,
selecting each fighter, entering a human-versus-CPU match, touch movement and
attacks, audible output, finishing a match and choosing Rematch. None of those
phone checks can be marked passed without a device result. If it still exits,
record the phone model/Android version and an Android bug report or ADB logcat;
do not keep changing graphics settings without that evidence.

Sources for the supported graphics path and alignment checks:
- [Epic Android quick start](https://dev.epicgames.com/documentation/unreal-engine/android-quick-start)
- [Android engine renderer selection](https://developer.android.com/games/develop/vulkan/game-engine-support)
- [Android 16 KB alignment verification](https://developer.android.com/guide/practices/page-sizes)
