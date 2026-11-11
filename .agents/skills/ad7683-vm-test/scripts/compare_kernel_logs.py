#!/usr/bin/env python3
"""Compare same-boot dmesg --raw snapshots. Exit: 0 clean, 1 alert, 2 incomplete."""

import argparse
from pathlib import Path
import re


ALERT = re.compile(
    r"\b(?:WARNING|WARN(?:_ON(?:_ONCE)?)?|Oops|BUG|KASAN|KCSAN|UBSAN)\b"
    r"|kernel panic|general protection fault|unable to handle kernel|Call Trace:",
    re.IGNORECASE,
)
RECORD = re.compile(r"^<\d+>\[\s*\d+(?:\.\d+)?\]")


def compare(before, after):
    """Return (status, detail, new_lines); never certify a missing baseline."""
    if not before or not after:
        return 2, "NOT RUN: empty snapshot; coverage cannot be established", after
    if not RECORD.match(before[0]) or not RECORD.match(after[0]):
        return 2, "NOT RUN: expected dmesg --raw snapshots with timestamps", after
    if after[:len(before)] != before:
        return 2, ("NOT RUN: baseline is not an exact prefix; possible ring-buffer "
                   "loss, reboot, or changed capture format. Inspect AFTER manually."), after
    added = after[len(before):]
    if any(ALERT.search(line) for line in added):
        return 1, "FAIL: new WARN/Oops or other diagnostic markers found", added
    return 0, "PASS: baseline retained; no new WARN/Oops markers found", added


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    args = parser.parse_args(argv)
    try:
        before = args.before.read_text(encoding="utf-8").splitlines()
        after = args.after.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as exc:
        print(f"NOT RUN: cannot read snapshots: {exc}")
        return 2
    status, detail, added = compare(before, after)
    print(detail)
    print("NEW LINES:" if status != 2 else "AFTER SNAPSHOT (coverage unknown):")
    for line in added:
        print(line)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
