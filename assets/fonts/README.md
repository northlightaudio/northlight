# Fonts

The build copies this folder beside the executable, and `ui::Theme` loads the fonts
from there, falling back to system fonts when one is missing.

- **Newsreader** (committed, the upright and italic variable cuts), the display face
  for wordmarks and headings (the italic for Vesper's), under the SIL Open Font
  License in `Newsreader-OFL.txt`. The italic is `Newsreader-Italic[opsz,wght].ttf`
  from the google/fonts repository (`ofl/newsreader`), renamed.
- **Barlow Condensed** (committed, the SemiBold and Bold cuts), the legend face printed
  on a hardware face (the tape echo's console), under the SIL Open Font License in
  `BarlowCondensed-OFL.txt`.
- **Inter** and **JetBrains Mono**, the interface and number faces, are not committed.
  Add `Inter-Variable.ttf` and `JetBrainsMono-Variable.ttf` here to use them.
