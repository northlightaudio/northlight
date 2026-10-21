#!/usr/bin/env python3
"""Writes assets/icons/<name>.svg from the icon table in src/ui/Icons.cpp, so the
design canvas and the app draw the same paths. Run after editing the table."""
import pathlib
import re

root = pathlib.Path(__file__).resolve().parent.parent
source = (root / "src" / "ui" / "Icons.cpp").read_text(encoding="utf-8")
out = root / "assets" / "icons"
out.mkdir(parents=True, exist_ok=True)
pattern = re.compile(r'\{"([A-Za-z]+)",\s*"([^"]+)",\s*(true|false)(?:,\s*"([^"]+)")?\}')
count = 0
for name, d, filled, solid in pattern.findall(source):
    style = 'fill="currentColor"' if filled == "true" else \
        'fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"'
    # A stroked icon may have a filled part (the mark's moon), drawn under it.
    under = f'<path d="{solid}" fill="currentColor"/>' if solid else ""
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">'
           f'{under}<path d="{d}" {style}/></svg>\n')
    kebab = re.sub(r"([A-Z])", lambda m: "-" + m.group(1).lower(), name)
    (out / f"{kebab}.svg").write_text(svg, encoding="utf-8")
    count += 1
print(f"wrote {count} icons to {out}")
