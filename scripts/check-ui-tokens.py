#!/usr/bin/env python3
"""Fails when src/ui carries a literal colour.

The design system (docs/design/design-system.md) requires every colour to come
from the token file through ui::Theme; this is the CI guard. Theme.cpp (the
loader) is the one place allowed to build colours. Sizes are checked screen by
screen as each is restyled (see docs/design/interaction.md).

    python scripts/check-ui-tokens.py [--root <repository>]
"""

import argparse
import pathlib
import re
import sys

PATTERNS = [
    re.compile(r"fromRGB\s*\("),
    re.compile(r"Colour\s*\(\s*0x"),
    re.compile(r"Colours::(?!transparentBlack)"),
    re.compile(r'fromString\s*\(\s*"#?[0-9a-fA-F]{6}'),
    re.compile(r"fromFloatRGBA\s*\("),
]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=pathlib.Path(__file__).resolve().parent.parent, type=pathlib.Path)
    args = parser.parse_args()

    failures = []
    for path in sorted((args.root / "src" / "ui").rglob("*")):
        if path.suffix not in (".cpp", ".h") or path.name == "Theme.cpp":
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if any(p.search(line) for p in PATTERNS):
                failures.append(f"{path}:{number}: {line.strip()}")

    if failures:
        print("Literal colours in src/ui (use ui::Theme tokens):")
        for failure in failures:
            print(f"  {failure}")
        return 1
    print("No literal colours in src/ui.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
