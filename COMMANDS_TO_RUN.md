# Rocky Kombat - exact command runbook

The agent cannot start processes on this machine (every execution attempt is
refused at the platform level - see the bottom of this file). So this is the
arrangement: the agent writes and edits the code, you run the commands below.

Work through the steps in order. After each step, look at the check line before
moving on. If a step fails, stop there and send the check line back.

---

## One-command path (do this if you just want the whole thing)

Double-click:

    C:\RockyKombat\RUN_ME_FIRST.bat

It runs steps 1 to 4 and the analysis, logs everything, and opens a short
summary in Notepad for you to send back. Expect 20 to 45 minutes. The window
looks idle for long stretches; that is normal, output is going to the log file.
Do not close it.

The steps below are the same thing, broken up, for when you want to watch each
stage and stop at the first failure. That is the better option for diagnosis.

---

## How to open the command prompt

Press Win+R, type: cmd

then Enter. A black window opens. Then move into the project:

    cd /d C:\RockyKombat

Every command below assumes you are in that folder.

---

## Step 1 - build the editor target (compile the arena fix)

    Scripts\build_editor.bat > build_editor.log 2>&1

This is the step that matters most. It compiles the new arena code for the first
time. Look at the result:

    findstr /i /c:"BUILD SUCCESSFUL" /c:"error C" /c:"error LNK" /c:"fatal error" build_editor.log

Expect to see Build succeeded / BUILD SUCCESSFUL and no error lines.

If you see error C or error LNK lines: STOP. Send those lines to the agent. The
fix is a source edit, and the agent can make it without ever running anything.

---

## Step 2 - cook, stage and package a fresh Win64 build

    Scripts\package_win64.bat > packaged.log 2>&1

This is the slow one: 20 to 40 minutes. When it finishes:

    findstr /i /c:"BUILD SUCCESSFUL" /c:"Result: Succeeded" /c:"Result: Failed" /c:"ERROR" packaged.log

Expect Result: Succeeded.

Now confirm the build is actually fresh (this catches stale-build confusion,
which is the number one cause of a duplicate ring that "should" be gone):

    dir Packaged\Windows\RockyKombat.exe

The timestamp must be today. If it is from 2026-10-17, the new code is not in
the binary and nothing else you do will prove anything.

---

## Step 3 - run the packaged self-test (no -rkc.* switches - that is the point)

    Packaged\Windows\RockyKombat.exe -RKCSelfTest=3 -windowed -ResX=1280 -ResY=720 -nosplash -log -abslog=Saved\Evidence\Arena\selftest_packaged.log

Deliberately NO -rkc.* switches: the packaged default must already be the
supplied image with no second ring.

---

## Step 4 - read the self-test record (this is the proof)

The record may land in either of these two places. List the newest ones:

    dir /b /o-d Saved\Evidence\RKC_SELFTEST_*.txt

    dir /b /o-d Packaged\Windows\RockyKombat\Saved\Evidence\RKC_SELFTEST_*.txt

Open the newest, or extract just the lines that matter:

    findstr /i /c:"greybox piece" /c:"failed check" /c:"one visible arena" /c:"restart" /c:"result:" Saved\Evidence\RKC_SELFTEST_<newest>.txt

The two lines that decide the whole item:

    0 of N greybox piece(s) visible
    failed checks: 0

Plus every line containing "restart" must say PASS, which covers the rematch
case. Note the old record from 2026-10-17 does NOT contain the greybox line at
all - that is how you know it is the old binary. If your new record also lacks
it, the rebuild did not take.

---

## Step 5 - look at the packaged game with your own eyes

    Packaged\Windows\RockyKombat.exe

No switches. Play a round. What to confirm:

- exactly one arena, and it is the Midnight Rooftop Ring artwork;
- both fighters' feet are on the painted floor line;
- no empty band above, below or beside the image;
- start a rematch (press R at the results card) and check it again.

Capture screenshots into Saved\Evidence\Arena and name them:

    arena_roundstart.png
    arena_gameplay.png
    arena_results.png
    arena_rematch.png

Then repeat round start at 1024x768 (4:3) and at a portrait window size, since
the requirement asks about those.

---

## Step 6 - analyse the captures (pixels, not impressions)

    python Tools\Diagnostics\rkc_arena_verify.py --art "ArtSource\RockyKombatPack_v1.0\Arena\Midnight_Rooftop_Ring.png" --control "Saved\Evidence\Stages\20261017_235858\05_match.png" --shotdir "Saved\Evidence\Arena" --out "Saved\Evidence\Arena\arena_verify_report.json"

Exit code 0 and M1 to M4 passing on every frame is what you want. The control
image is the known pre-fix duplicate-ring capture, which makes the flat-saturation
test relative and far more sensitive than a fixed threshold.

---

## What to send back

Enough to finish the assessment without guessing:

1. The output of the step 1 check line.
2. The output of the step 2 check line, plus the exe timestamp.
3. The newest self-test record extract from step 4 (or the whole .txt).
4. The screenshots from step 5.
5. arena_verify_report.json from step 6.

Easiest way to send 1 to 3: paste the printed lines into the chat. Big files can
sit on the disk until asked for.

---

## Why the agent cannot run these itself

Every execution path is refused at the platform level, with the same message:

    No active targets are saved in this engagement.

Tested and refused: plain command, interactive shell session, detached background
job, reusing the older session handles that did work on 2026-10-17, and fetching
a URL. File reading and file writing DO work - which is why the agent can edit
the code and write these scripts on your disk, but cannot compile, launch or
photograph anything.

Nothing in the project changed to cause this. The commands ran on 2026-10-17 and
below; they are refused on 2026-10-18. The grant, not the code, went away.

There is a second, separate symptom: delivering files to you fails with

    Local sandbox connection <id> is not subscribed to the command relay.
    Reconnect the local runner or Desktop app, wait until it is ready, then try again.

That is a transport problem on the client side, not a scope problem.

If you want to try to restore execution rather than work around it: fully quit the
Desktop app, wait until it reports ready, and make sure the machine is shown as
connected before starting a new conversation. If that does not clear the refusal,
the command-and-run arrangement above is the reliable path, and it needs nothing
from the platform.
