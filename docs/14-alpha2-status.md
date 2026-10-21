# Alpha 2 — 2026-10-21

Version `1.0.2-alpha.2`, Android build number `3`. Work continues from `05bd380`.
Checkpoint: `Saved/Checkpoints/online-presentation-20261021`.

Faster shared fighter tuning: Rocky walks 240/180 cm/s, Shadow 265/200 cm/s.
Jab startup/active/recovery: Rocky 5/3/9 ticks, Shadow 4/4/8 ticks at 60 Hz.
Heavy recovery and dodge duration are shorter. Champion reacts, closes, defends
and punishes more frequently without extra health, stamina or damage. Punches
still use the existing hitboxes and animation phase sampling. Contact rays follow
actual hit stun. The limited four-pose art remains provisional.

The supplied logo, RBA belt, arena poster and portraits are imported unchanged.
The poster is confined to presentation screens. Character selection and versus
cards show the chosen roster and ownership. Menus draw and hit-test the same
rectangles, including a touch scroll strip for long lists. Normal match HUDs no
longer show raw input or AI telemetry; F1 retains diagnostics.

Standalone guest multiplayer is in `Services/multiplayer`. See its README for
deployment. Real player discovery, invitations, acceptance, fighter assignment,
ready agreement, ordered simulation input, rematch agreement and disconnect
handling are connected to the game. It is unranked delay-based lockstep, with
checksum mismatch termination. No public host was provisioned: the owner chose
to connect hosting later. Android's packaged endpoint is deliberately empty.

Verification records are local in `Saved/Evidence`:

- `shader_gate_editor_build_20261021.log` and
  `alpha2_game_build_final_20261021.log`: editor and normal game targets built
  successfully with Unreal 5.8.3 on Windows 10 x64 (10.0.19045).
- `alpha2_selftest_verified_20261021.log`: three-match engine integration suite
  passes with zero failures, including touch hit tests at three viewport sizes.
- `combat_after_tuning_20261021.log`: 364 checks, zero failures.
- `champion_ai_tests_final_20261021.log`: 224 checks, zero failures.
- `playable_balance_20261021.log`: Champion Counter won 98/100 against Rookie
  Balanced with identical fighter statistics; all completed, with hits, blocks
  and dodges. Automated evidence, not a human difficulty assessment.
- `online_engine_test_20261021.log`: two real Unreal clients completed a match
  and rematch over local WebSockets. Both peers had identical end-state
  checksums (`511e2717`, `3e8dd5aa`) and each threw punches and landed hits.
- `Services/multiplayer`: three socket integration tests pass, including invalid
  input order and intentional state mismatch. Run `npm test` to reproduce.
- `presentation_import_20261021.log`: all five presentation textures imported,
  zero errors. Desktop visual review found a transient editor shader fallback;
  the loading gate now waits for compilation before gameplay. Android remains
  unverified.
- `presentation_render_verified_20261021.log`: 1280x720 offscreen desktop
  frames confirm logo, RBA belt, title, portrait selection, versus and live
  punches. The inspected gameplay frames after the compilation wait show no
  fallback rectangle. An earlier complete scripted playthrough and rematch are
  in `presentation_render_20261021.log`. Neither run used audible playback.

Android packaging still fails preflight with **UE Android engine component
MISSING** (`android_alpha2_attempt_20261021.log`). Neither
`Engine/Binaries/Android/UnrealGame.target` nor `Engine/Build/Android/Java` exists.
Finish the installed engine download and enable **Epic Games Launcher > Library
> UE 5.8 > Options > Target Platforms > Android**, then run
`Scripts/package_android.bat`. No APK, checksum or GitHub release asset exists
for this version. No authorized Android device is connected. Audible output,
phone controls, performance and cross-platform online play are untested.

Signing remains local: the old tracked keystore was a disposable debug identity,
removed from the index in `b731252`; its local backup remains. Private release
keys and passwords remain ignored and are not included in these changes.
