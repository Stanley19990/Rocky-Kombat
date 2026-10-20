# rkc_uasset_strings.py
#
# Prints the printable runs found inside a .uasset, which for a material is
# enough to read its parameter names out of the serialised name table (this
# host has no `strings` binary, hence the local implementation).
#
# Usage: python Tools\Diagnostics\rkc_uasset_strings.py <file.uasset> [min_len]

import re
import sys

KEEP = re.compile(rb"[\x20-\x7e]{%d,}")


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: rkc_uasset_strings.py <file.uasset> [min_len]")
        return 2
    path = sys.argv[1]
    minimum = int(sys.argv[2]) if len(sys.argv) > 2 else 5

    with open(path, "rb") as handle:
        blob = handle.read()

    seen = []
    for match in KEEP.finditer(blob):
        text = match.group().decode("ascii", "replace")
        if text not in seen:
            seen.append(text)

    print("file: %s (%d bytes, %d unique runs)" % (path, len(blob), len(seen)))
    for text in seen:
        print("  %s" % text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
