"""Repair RKCGameModePresentationSelfTest.cpp.

Diagnosed corruption (verified by inspection before this script was run):

  * line 733 is a mangled splice: 'FString::Printf(TEXT("leaving the results
    card retMusic::Arena,' - the correct line was cut after '...card ret' and
    the tail of a later line was glued on.
  * lines 734..1077 are intact but shifted up by three lines relative to the
    correct layout (the three lines that the splice swallowed).
  * lines 1078..1424 are a redundant, byte-identical copy of the tail
    (start of restored line 2 onward), appended at the end of the file.
  * lines 319..328 hold a dead 'PlanFor' lambda that calls an undeclared
    PlanOnAudio(), which is a compile error.

Reconstruction: keep 1..731, restore the three swallowed lines, keep 734..1077,
drop 1078..1424, then drop the dead PlanFor block.

The reconstruction is self-validating: the correct text of the mangled line is
rebuilt by joining the surviving prefix with the first line of the redundant
copy, and the whole redundant copy must equal the reconstructed tail exactly.
"""
import sys

PATH = r"C:/RockyKombat/Source/RockyKombat/Match/RKCGameModePresentationSelfTest.cpp"
BACKUP = PATH + ".corrupt.bak"

with open(PATH, "r", encoding="utf-8", newline="") as handle:
    lines = handle.readlines()

def at(n):
    """1-based line accessor."""
    return lines[n - 1]

fails = []
def check(name, condition, detail=""):
    print("  %-46s %s%s" % (name, "OK" if condition else "FAIL",
                             "" if condition else "  :: " + detail))
    if not condition:
        fails.append(name)

print("== corruption signature ==")
check("total lines == 1424", len(lines) == 1424, "got %d" % len(lines))
check("line 731 is TickBack(Machine);", at(731).strip() == "TickBack(Machine);", repr(at(731)))
check("line 732 opens the results->menu check",
      at(732).strip() == "SelfTestCheck(Machine.GetScreen() == EScreen::Menu,", repr(at(732)))
mangled = at(733)
check("line 733 is the mangled splice",
      mangled.lstrip().startswith('FString::Printf(TEXT("leaving the results card ret')
      and mangled.rstrip("\r\n").endswith("Music::Arena,"), repr(mangled))
check("line 734 is the arena-bed TEXT line",
      at(734).strip().startswith('TEXT("the arena bed does not keep playing behind the main menu")'),
      repr(at(734)))
check("line 1077 closes RunSelfTestPresentationSystems", at(1077).strip() == "}", repr(at(1077)))
check("line 1078 starts the redundant copy",
      at(1078).startswith("urns to the main menu (on %s)"), repr(at(1078)[:60]))
check("last line of the file is a closing brace", at(len(lines)).strip() == "}", repr(at(len(lines))))

# ---- rebuild the three swallowed lines -------------------------------------
mangled_body = mangled.rstrip("\r\n")
head_frag = mangled_body[:len(mangled_body) - len("Music::Arena,")]
check("mangled prefix ends where we expect", head_frag.endswith("card ret"), repr(head_frag[-24:]))

# First line of the redundant copy is the remainder of the correct line 2.
remainder = at(1078)
restored_printf = head_frag + remainder
expected_printf = ('FString::Printf(TEXT("leaving the results card returns to the main menu (on %s)"),\n')
check("prefix + duplicate remainder reconstructs the correct line",
      restored_printf.strip() == expected_printf.strip(), repr(restored_printf))

restore = [
    "        SelfTestCheck(Machine.GetScreen() == EScreen::Menu,\n",
    restored_printf,
    "                                      *ScreenText(Machine)));\n",
    "        SelfTestCheck(Audio == nullptr || Audio->GetCurrentMusic() != ERKCMusic::Arena,\n",
]

head = lines[0:731]      # 1-based 1..731
tail = lines[733:1077]   # 1-based 734..1077

# ---- the redundant copy must be exactly redundant --------------------------
dup = lines[1077:len(lines)]           # 1-based 1078..end
rebuilt_tail = restore[2:] + tail
check("redundant copy == reconstructed tail (no unique content lost)",
      "".join(dup[1:]) == "".join(rebuilt_tail),
      "dup lines %d vs rebuilt %d" % (len(dup) - 1, len(rebuilt_tail)))

content = head + restore + tail
print("\n== after dropping the redundant copy ==")
print("  lines: %d" % len(content))

# ---- drop the dead PlanFor lambda ------------------------------------------
start = next((i for i, t in enumerate(content) if "auto PlanFor = [&MakeContext]" in t), None)
check("found the PlanFor lambda", start is not None)
end = next((i for i in range(start or 0, len(content)) if content[i].strip() == "(void)PlanFor;"), None)
check("found the end of the PlanFor block", end is not None,
      "start=%s" % start)

if start is not None and end is not None:
    print("  PlanFor block occupies 1-based lines %d..%d" % (start + 1, end + 1))
    replacement = [
        "    // The planner is pure and only reachable through the audio system, so no\n",
        "    // stand-in system is needed here (the system is asserted to exist above).\n",
    ]
    content = content[:start] + replacement + content[end + 1:]

# ---- write out -------------------------------------------------------------
if fails:
    print("\nABORTING: corruption signature did not match, file left untouched.")
    print("failed checks: %s" % ", ".join(fails))
    sys.exit(1)

with open(BACKUP, "w", encoding="utf-8", newline="") as handle:
    handle.writelines(lines)
print("\ncorrupt original backed up to: %s" % BACKUP)

with open(PATH, "w", encoding="utf-8", newline="") as handle:
    handle.writelines(content)

blob = "".join(content)
print("\n== result checks ==")
check("PlanOnAudio gone", "PlanOnAudio" not in blob)
check("PlanFor gone", "PlanFor" not in blob)
check("VscreenPlaceholder gone", "VscreenPlaceholder" not in blob)
check("results-card line present exactly once",
      blob.count("leaving the results card returns to the main menu") == 1,
      "count=%d" % blob.count("leaving the results card returns to the main menu"))
check("'--- D. TOUCH CONTROLS ---' once", blob.count("--- D. TOUCH CONTROLS ---") == 1,
      "count=%d" % blob.count("--- D. TOUCH CONTROLS ---"))
check("'--- A. AUDIO CATALOG ---' once", blob.count("--- A. AUDIO CATALOG ---") == 1,
      "count=%d" % blob.count("--- A. AUDIO CATALOG ---"))
check("'--- B. EVENT PLANNING ---' once", blob.count("--- B. EVENT PLANNING ---") == 1,
      "count=%d" % blob.count("--- B. EVENT PLANNING ---"))
check("'--- C. FRONT END ---' once", blob.count("--- C. FRONT END ---") == 1,
      "count=%d" % blob.count("--- C. FRONT END ---"))
check("'first-run drive:' once", blob.count("first-run drive:") == 1)
check("braces balanced", blob.count("{") == blob.count("}"),
      "%d vs %d" % (blob.count("{"), blob.count("}")))
check("parens balanced", blob.count("(") == blob.count(")"))
check("function closes at end of file", content[-1].strip() == "}", repr(content[-1]))
check("no NUL bytes", "\x00" not in blob)
check("ends with newline", blob.endswith("\n"))

print("\nfinal line count: %d" % len(content))
print("RESULT: %s" % ("PASS" if not fails else "FAIL: " + ", ".join(fails)))
sys.exit(0 if not fails else 1)
