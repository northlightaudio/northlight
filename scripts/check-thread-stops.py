#!/usr/bin/env python3
"""Fails when code in src can kill a thread.

juce::Thread::stopThread with a time limit kills the thread when it misses it,
and juce::ThreadPool's destructor gives each thread 500 ms. A thread killed while
it holds a lock leaves it held for ever; killed while it starts or ends, it holds
Windows' loader lock, and every later DLL load or unload (quitting included)
waits for ever. Stop threads with stopThread(-1) and use engine::BackgroundQueue
for background jobs (docs/decisions.md).

    python scripts/check-thread-stops.py [--root <repository>]
"""

import argparse
import pathlib
import re
import sys

PATTERNS = [
    re.compile(r"stopThread\s*\(\s*[0-9]"),
    re.compile(r"juce::ThreadPool\b"),
    re.compile(r"TerminateThread\s*\("),
    re.compile(r"killThread\s*\("),
]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=pathlib.Path(__file__).resolve().parent.parent, type=pathlib.Path)
    args = parser.parse_args()

    failures = []
    for path in sorted((args.root / "src").rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            code = line.split("//", 1)[0]  # comments may name what they avoid
            if any(p.search(code) for p in PATTERNS):
                failures.append(f"{path}:{number}: {line.strip()}")

    if failures:
        print("Code that can kill a thread (use stopThread(-1) or engine::BackgroundQueue):")
        for failure in failures:
            print(f"  {failure}")
        return 1
    print("Nothing in src kills a thread.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
