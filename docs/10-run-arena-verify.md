# 10 - Running the arena verification by hand (double-click path)

Companion to `docs/09-arena-single-ring.md`. Section 5 of that document is the only
thing that closes the single-arena item. This document is about making that run
survivable when nobody can drive a terminal for you.

## Why a wrapper exists

`Tools\Diagnostics\rkc_arena_verify.bat` prints to the console and exits. Double-clicked
from Explorer it therefore closes its own window the instant it finishes, and every line
is lost - including the compiler result and the self-test record extract. If the run
takes forty minutes and then fails, you have nothing to show.

`RUN_ME_FIRST.bat` in the project root wraps it:

| | |
| --- | --- |
| Entry point | `C:\RockyKombat\RUN_ME_FIRST.bat` |
| Full output | `Tools\Diagnostics\arena_run_<stamp>.log` |
| Short report | `Tools\Diagnostics\arena_summary_<stamp>.txt` |

Double-click the entry point. It performs the same six steps (rebuild editor, cook /
stage / package Win64, run the packaged self-test, print the capture instructions,
analyse the captures), but:

- it redirects the whole run to a timestamped log, so a compile failure is recoverable
  rather than scrolled away;
- it extracts the decisive lines into a short summary: build result, `error C` and
  `error LNK` hits, `greybox piece`, `failed check`, analyzer verdict, and the last 25
  lines of the log;
- it opens that summary in Notepad and waits for a keypress, so the window stays up.

The summary is a few dozen lines on purpose, so it can be pasted into a message whole
and nothing important is truncated. **When reporting a failed run, send the summary
file, not the raw log.**

## Running it

    Double-click:  C:\RockyKombat\RUN_ME_FIRST.bat

Expect twenty to forty-five minutes. The window will look idle for long stretches; that
is intentional, because the output is going to the log file. Do not close the window,
and do not type into it while the packaged self-test is running.

## Re-running only the analysis

After you have captured your own screenshots into `Saved\Evidence\Arena`, you can score
them in seconds without rebuilding. Open a cmd window and run these lines in order:

    cd /d C:\RockyKombat
    set RKC_SKIP_BUILD=1
    set RKC_SKIP_PACKAGE=1
    set RKC_SKIP_SELFTEST=1
    RUN_ME_FIRST.bat

The same three variables can be used to resume after a late failure: skip the build that
already succeeded and let the later steps run.

## Other overrides

| Variable | Meaning | Default |
| --- | --- | --- |
| `RKC_UE_ROOT` | engine install directory | `C:\Program Files\Epic Games\UE_5.8` |
| `RKC_PROJECT` | project root | the folder holding `RUN_ME_FIRST.bat` |
| `RKC_SKIP_BUILD` | `1` skips the editor build | unset |
| `RKC_SKIP_PACKAGE` | `1` skips cook / stage / package | unset |
| `RKC_SKIP_SELFTEST` | `1` skips the packaged self-test | unset |

If the engine lives somewhere else, run from cmd:

    set RKC_UE_ROOT=D:\Your\Engine\Path
    C:\RockyKombat\RUN_ME_FIRST.bat

## What the summary has to contain before this item can be signed off

1. The build and package steps report success, and the packaged exe plus its `.pak`
   both carry the timestamp of this run. A new exe with an old pak still runs old code.
2. The self-test record shows `failed checks: 0`.
3. The same record shows `0 of N greybox piece(s) visible`, and a cover margin greater
   than zero.
4. Every `match N restart:` line passes, which is the rematch case the request calls out.
5. `arena_verify_report.json` passes M1 to M4 for every capture supplied.
6. Your own eyes on the packaged build: exactly one arena, the supplied Midnight
   Rooftop Ring, both fighters standing on the painted floor line, no gap or filler
   band above, below or beside the image.

Items 1 to 5 are mechanical. Item 6 is the acceptance criterion quoted at the top of
document 09 and cannot be replaced by any amount of automation: a source-code
visibility change does not establish that the fix holds on screen.

## Note on this wrapper

It is a convenience layer around the existing harness. It does not change what counts
as evidence, and it does not make the arena change verified. Until the steps above have
actually run against a freshly packaged build, the arena item remains implemented in
source and unproven.
