# 08 — Human acceptance checklist

Use this with the **new** packaged build only. Confirm you have the right binary before starting:
the run prints its build stamp to the log and writes `Packaged\Windows\RockyKombat\Saved\BuildStamp.txt`
(the same folder as the game's `Saved` directory).

## Launch (normal interactive mode — nothing automatic)

```bat
cd /d C:\RockyKombat
Packaged\Windows\RockyKombat.exe -windowed -ResX=1280 -ResY=720
```

No `-RKCSelfTest`, no `-rkc.FrontEndAuto`, no `-rkc.CaptureStages`, no `-rkc.QuickRules`,
no `-rkc.TouchForced`. You should land on the branded startup card and drive everything yourself.
If touch controls appear on startup, loading, menus, versus or results, that is a defect (item 6).

## Controls (from `Config\DefaultInput.ini`)

Both rosters are selectable by either player, so the columns are hands, not characters.

| Action | Player 1 — left hand | Player 2 — right hand |
| --- | --- | --- |
| Move left / right | `A` / `D` | `Left` / `Right` |
| Block (hold) | `S` | `Down` |
| Jab (light) | `Z` | `NumPad 1` (no numpad: `,`) |
| Heavy | `X` | `NumPad 2` (no numpad: `.`) |
| Dodge | `C` | `NumPad 3` (no numpad: `/`) |

Shared: `R` rematch · `P` pause · `F1` frame-data overlay · `F10` leave the match
(packaged builds).
Menus: arrow keys / `WASD` to move, `Enter` or `Space` to accept, `Escape` to go back.

`Escape` during a match opens the pause card; `F10` is what quits the process. This table is the
same list the Controls screen prints and the in-engine self-test asserts against
(`RKCFrontEnd::FMachine::ControlReference` vs `Config\DefaultInput.ini`), so if the game and this
table ever disagree, the run is the authority.

## The eight acceptance checks

| # | Check | Pass condition | Result |
| --- | --- | --- | --- |
| 1 | Choose ROCKY and control ROCKY | Rocky is selected, moves only when you press a key | |
| 2 | Choose SHADOW and control SHADOW | Shadow is selected and behaves as the player character | |
| 3 | Jab / heavy respond and hit only in range | Damage lands at contact distance, whiffs at distance | |
| 4 | Block and dodge respond correctly | Held block reduces/negates damage; dodge moves clearly and has a cost | |
| 5 | No combat before FIGHT | Neither fighter moves or attacks during introductions/countdown | |
| 6 | Correct arena, stable fighters, audible feedback | One ring only (the supplied Midnight Rooftop Ring); no black silhouettes; sounds heard | |
| 7 | Complete match, rematch, change fighters | Result screen, `R` restarts, back-to-menu and re-pick both work, and each fighter is **drawn as the character you just picked** | |
| 8 | Local two-player has no AI | Both fighters idle without input; CPU acts only after FIGHT | |

## Defects to watch for specifically

- **Arena, item 4.** The match should show **one** ring — the painted one from the supplied image.
  If a second ring (black posts, red/white/blue ropes, pale slab) is visible, note it. `rkc.ArenaGreybox=1`
  in the console draws the procedural ring back on top of the image for A/B comparison without a rebuild;
  `rkc.ArenaGreybox=0` returns to the image. Also check the bottom of the frame: if the world visibly
  ends (a void band) below the painted canvas, say so — the procedural canvas and ringside floor are
  currently hidden along with the ring, and that is the one framing question this change leaves open.
- **Stale artwork on a rematch, item 7.** Found and fixed while preparing this build: a rematch
  reuses the same two fighter actors, and the artwork was only ever resolved once per actor, so a
  player who picked a different character for the rematch kept being **drawn as the previous
  character** while the name banner and the combat data had already switched. The self-test now
  performs that swap on purpose and reads back the flipbook that is on screen; it failed on the old
  code (`FB_Rocky_Knockdown` for a fighter rostered as Shadow) and passes now. Worth watching anyway:
  after a rematch, the artwork, the name banner and the division on both sides must all name the same
  character. A mismatch is this defect returning.
- **Black fighters, item 5.** Watch the first load after launch, every animation change
  (idle → walk → jab → heavy → block → dodge → knockdown → victory) and each rematch. A fighter that
  renders solid black is the known intermittent defect.
- **Audio, item 5.** Menu/fight music, whiffs, clean hits, blocked hits, character reactions, and the
  volume settings in Options. A log line saying a cue fired is **not** evidence of audible output —
  only your ears are.
- **Lifecycle.** Pause (background the window), resume, alt-tab, and quit mid-round: no stuck inputs,
  no music left playing after exit.
- **Touch UI, item 6.** In this ordinary desktop launch the touch pad must be off everywhere.

## What to send back

Per check: pass/fail, and for anything unexpected the screen, the fighter, and what you pressed.
For defects, `Saved\Logs\RockyKombat.log` plus the `BuildStamp.txt` from the same run is enough to
line the report up with the exact binary.
