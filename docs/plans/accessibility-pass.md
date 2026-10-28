# Accessibility pass

An audit of the app against [design/accessibility-performance.md](../design/accessibility-performance.md)
(section 11), done 2026-10-27 by reading the code. Nothing here has been checked with
Narrator yet.

The rule for every fix: the default look does not change. A fix is either invisible
(screen reader names, focus that only shows for the keyboard, key paths) or sits
behind an option that is off by default (High contrast, Reduce motion).

## What already works

- Shared focus ring (`NorthlightLookAndFeel::drawFocusRing`), shown only for keyboard
  focus, drawn by most controls and every strip card part.
- `IconButton` takes its accessible title from its tooltip, so icon buttons have names.
- Fader, Knob, Meter, NumericField, SegmentedControl, the hardware key and lever, and
  ModuleCard knobs expose the right role and value.
- Status bar messages and toasts are announced.
- Preferences and Export title every control from its label.
- High contrast is a real, saved, live-switching setting. UI scale goes to 200%.
- The OS "show animations" setting is read; `Tween` and the animated cards honour it.
- Nothing flashes. Every action in the keyboard map (interaction.md 10.4) is registered.

## Gaps

### Bugs

1. **High contrast compounds.** `Theme::highContrastTokens()` reads the current colours.
   Changing the accent while High is on rewrites `textTertiary` as `textPrimary`.
   Build it from the token file's colours instead (`Theme.cpp:738`, `PreferencesView.cpp:999`).
2. **Focus ring only comes back in the main window.** The flag is set only by Tab in
   `MainWindow::keyPressed` (`MainWindow.cpp:349`). After any click, Tab in Preferences,
   Audio Setup, floating panels or plugin windows shows no ring. Arrow keys never set
   it (the comment says they do), and setting it does not repaint.
3. **Blocking sheets swallow Tab** (`Sheet.cpp:335`), so their buttons cannot be reached.

### Screen reader (11.3) — all invisible

| Area | Gap |
|---|---|
| Transport toggles (play, loop, click, snap…) | Named, but on/off state is not exposed: `IconButton` is never toggleable |
| Time counters (`TimeDisplay`) | Not exposed at all |
| Tempo slider, signature box | Tooltip only, no title |
| Track header M/S/R/A | Named "M", "S", "R"; no toggle state; no track name |
| Track header pan and meter | No title |
| Mixer fader, pan, meter | Right role and value, no title |
| Mixer M/S/R/Ø | Single letters, no track name |
| Mixer name cap, insert slots, sends | Painted, not exposed |
| Editor panel plugin knobs and pan | No title |
| Card face parts (Corona, TapeEcho, Bloom, Vesper, VU) | Title and description but no handler: generic role, value only in the description |
| Audio Setup | Combo boxes untitled; labels not attached |
| Media and plugin browsers | Rows read as "Row N" (no `getNameForRow`); search fields untitled |
| Segmented choice in Export | Inner control has a tooltip but no title |
| Arrange clips, take/tempo/automation lanes, piano roll notes | Painted, not exposed. The largest piece of work |

Most of the table falls to five shared changes: a toggleable `IconButton`; Fader, Knob,
Meter and NumericField falling back to their tooltip for a title; a helper that names
M/S/R/A as "Mute *track*"; `getNameForRow` on both browsers; a read-only handler for
`TimeDisplay`.

### Keyboard and focus (11.2) — all invisible

- **Tab order** is JUCE's default. The documented cycle (header → headers → lanes →
  dock → side → status, interaction.md:153) is not built: no focus containers or
  traversers anywhere.
- **Track headers and the arrange view cannot take focus.** Esc's
  `tracks_.grabKeyboardFocus()` has nowhere to land.
- **Clips have no keyboard selection.** Left/Right nudges instead of moving the
  selection, against interaction.md:155. Only Ctrl+A or the mouse selects a clip.
- **Header controls** (mute, solo, arm, colour, name, menu) are reachable only through
  the global Shift+M/S/R keys.
- **No context-menu key** (Shift+F10 / Menu) for arrange, tempo or automation menus.
- **Piano roll:** no key adds a note or changes velocity; the controller lane refuses focus.
- **Pointer-only edits:** automation points, clip gain, stretch, fades, insert reorder,
  sends, dragging from the browsers.
- **Focus rings missing** on the browser lists, Preferences folder list, piano roll,
  audio editor, tempo lane and shortcut editor.
- **Focus is not returned** to the opener when a sheet, Preferences, Audio Setup or a
  plugin window closes. Sheets do not trap focus. Plugin and editor windows ignore Esc.

### Visual (11.1, 11.4, 11.5) — opt-in only

- **Contrast table is incomplete.** `textTertiary` on surface1/2/3 is 4.31, 3.76 and
  3.27 and is missing from `contrast.md`. The spec's "3.6:1 on surface0" disagrees
  with the table's 4.72.
- **`textTertiary` carries essential caption-size text** (11 px, spec says 13 px or
  larger): sends summary, field units, meter scale, browser status, sheet footnotes,
  audio setup hints, about 25 spots in the strip cards. The default look stays; High
  contrast should lift these.
- **High contrast changes only four tokens.** It leaves `textDisabled`, the raised
  surfaces and the `hardware.*` inks alone.
- **Windows High Contrast is ignored** (no `SPI_GETHIGHCONTRAST`).
- **Reduce motion has no in-app setting** and is read once at start (not on
  `WM_SETTINGCHANGE`). ModuleCard, EditorPanel and InsertEditor ticks are not gated
  (mostly meters, which is right, but card animations that ride on them are not).
- **Colour alone:** the meter clip square, the pitch editor's flagged note outline,
  and the Diagnostics load thresholds.
- **No text size option** apart from whole-UI scale. Probably fine; noted.

Not a gap: glows turning off under reduce motion is the documented behaviour
(design-system.md: "Reduce-motion and high-contrast variants keep the geometry and
drop the glows").

## Proposed order

1. **Invisible quick wins.** Done 2026-10-27. One loose end: an already-open
   Preferences or Audio Setup window brought back keeps its first opener. The three bugs, the five shared screen-reader changes,
   Audio Setup titles, card face handlers, missing focus rings, focus return and Esc
   on windows.
2. **Opt-in visual.** Done 2026-10-28. Left for later: the Diagnostics load
   thresholds still use colour alone, and the ModuleCard, EditorPanel and
   InsertEditor ticks are not gated by reduce-motion. Contrast table rows; a fuller High contrast (raised-surface
   text, `textDisabled`, hardware inks, a shape on the meter clip and flagged note);
   follow Windows High Contrast; a Reduce motion setting (System / On / Off) re-read
   on settings change.
3. **Keyboard model.** Focus containers for the documented Tab cycle; focusable track
   headers and arrange view; clip selection by keys; Shift+F10 menus; note entry and
   velocity keys in the piano roll.
4. **Screen reader for painted content.** Accessibility handlers (or invisible proxies)
   for clips, lanes, notes, insert slots and sends. Then the Narrator walkthrough that
   11.3's acceptance asks for.
