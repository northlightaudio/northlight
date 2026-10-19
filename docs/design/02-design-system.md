# 2. Design system

The system is defined by [tokens.json](tokens.json). This document explains the
decisions behind the tokens and specifies every component and its states. Token
names are written `color.accent`, `size.control`, and so on.

## 2.1 Colour

### Base and surfaces

Five neutral steps, each used for exactly one plane:

| Token | Use |
|---|---|
| `canvas` | The lowest plane: the arrange lanes background, the area behind panels |
| `surface0` | Panels: header bar, track header column, mixer background, dock, dialogs |
| `surface1` | Raised groups inside a panel: mixer strips, cards, list rows on hover, clip headers' base |
| `surface2` | Controls at rest: buttons, fields, combos, fader tracks |
| `surface3` | Controls on hover; the lightest neutral that is not text |

`surfacePressed` is darker than `surface2` so pressing reads as "pushed in".
`overlay` dims the window behind a sheet.

The base is near-black with a faint warm cast (`#0F0F10` rather than a blue grey) so
track colours and meters sit on something neutral. Steps are 6 to 9 points of
lightness apart: enough to read as planes, not enough to read as boxes.

### Lines

Two line tokens only. `lineSubtle` (white at 6%) separates planes that touch;
`lineStrong` (white at 12%) outlines popups and fields with focus-less emphasis.
Borders are the exception, not the rule: a panel edge is normally a change of
surface, not a line.

### Text

`textPrimary` for values and names, `textSecondary` for labels and units,
`textTertiary` for hints and inactive tabs, `textDisabled` for disabled. On an
accent or track-colour fill use `textOnAccent` / `textOnColour`; never white on a
light track colour.

### Accent

One interaction colour, `accent` (a cool, airy blue), with hover and pressed steps and
a 12% `accentSubtle` fill. It means exactly four things: selected, focused, active
toggle, drop target. It is never used for data (meters, clips, notes use their own
tokens), so when something is accent-coloured the user knows it is about their
current action.

### Semantic

`record` is a warm red, distinct from `danger` only by use: `record` is the arm
button, the record transport button, the recording playhead and punch regions;
`danger` is destructive confirmation and hard errors. `warning` (amber) is for
dropouts, latency mismatch, missing plugins. `success` (green) is for completed
bounces and healthy status. The track-control trio is `arm` (red), `solo` (amber),
`mute` (slate), matching long-standing DAW convention so nobody has to learn it.

### Meters

Three zones: `meterLow` to -18 dBFS, `meterMid` to -6, `meterHot` to 0, then
`meterClip`. Zone boundaries are in `meter.zones`. The peak-hold line is
`meterPeakHold`. The loudness meter shows `loudnessTarget` as a thin marker at the
target LUFS (default -14, user-settable).

### Track palette

Twelve hues tuned to read on the dark base: saturation around 65%, lightness around
62%, so a clip body at 22% alpha still shows its hue and a waveform at 90% alpha
is vivid without glowing. New tracks cycle through the first eleven; buses default
to Slate; the master has no colour. Users can pick any of the twelve from a swatch
grid or enter a hex.

Derived uses, all by alpha over the base (see `track.*Alpha`):

| Use | Alpha of track colour |
|---|---|
| Clip body | 0.22 (selected 0.32) |
| Clip header | 0.55 |
| Waveform | 0.90 (muted 0.35) |
| Track header stripe (4 px left edge) | 1.0 |
| Mixer strip cap (name background) | 0.55 |
| Fader cap accent line | 1.0 |
| Piano roll notes on that track | 0.90 |

## 2.2 Typography

- **UI face: Inter** (variable). Humanist, excellent at 11 to 13 px, open licence.
  Bundled in `assets/fonts`.
- **Numeric face: JetBrains Mono** (variable) for every time display, counter, dB
  value, tempo and sample-rate readout. Monospace guarantees stable widths while
  values change; Inter's tabular figures are not accessible through JUCE 8.

Scale (px at 100%), from `font.size`:

| Token | Size | Weight | Use |
|---|---|---|---|
| caption | 11 | 500 | Clip names, ruler labels, meter scale, badges |
| small | 12 | 400 | Secondary labels, table cells, tooltips |
| body | 13 | 400 | Default labels, menu items, fields |
| label | 13 | 500 | Control captions, strip names |
| title | 16 | 600 | Panel and dialog titles |
| heading | 20 | 600 | First-run and about screens |
| timeLarge | 22 (mono) | 500 | Transport position |
| timeSmall | 13 (mono) | 500 | Secondary counters, status bar |
| value | 12 (mono) | 500 | dB, pan, tempo, latency values |

Line height 1.3. Uppercase labels (section headers in panels) use `caption` at
weight 600 with `letterSpacingCaps` tracking and `textTertiary` colour.

## 2.3 Spacing, radius, grid

A 4 px base grid: `space.xs` 4, `sm` 8, `md` 12, `lg` 16, `xl` 24, `xxl` 32,
`xxxl` 48, plus `xxs` 2 for hairline gaps. Control heights are 24 (compact: track
header rows, mixer buttons), 28 (default), 32 (large: transport, first run).

Radii: `sm` 3 for buttons, fields and clips; `md` 6 for menus, cards and popups;
`lg` 10 for floating windows' content area; `pill` for badges and the segmented
control's thumb.

## 2.4 Elevation

Inside the main window there are no shadows; planes are expressed by surface steps.
Popups (menus, tooltips, combo lists, colour swatches) float with
`elevation.popup`: an 8 px blurred shadow at 50% and a `lineStrong` border.
Secondary windows (plugin, audio setup, browser) are native windows with the same
surface tokens and no inner chrome.

## 2.5 Iconography

- 16 px grid (`size.icon`), 1.5 px stroke, round caps and joins, 1 px inner padding.
  20 px variant for the transport.
- Monochrome. `textSecondary` at rest, `textPrimary` on hover, `accent` when the
  action is active, `record` for record, `textDisabled` when disabled.
- Icons never appear without a tooltip; text buttons are preferred where space allows.

Required set (Phase 7 draws these as vector paths in `assets/icons`): play, stop,
record, loop, punch, metronome, count-in, go-to-start, go-to-end, undo, redo, snap,
ripple, zoom-in, zoom-out, zoom-fit, mixer, browser, inspector, settings, plugin,
power (bypass), pin, close, chevron-down, chevron-right, check, search, filter,
add, remove, drag-handle, mute, solo, arm, monitor, phase, link (stereo), folder,
file-audio, file-midi, marker, region, warning, info, error, success, loudness,
cpu, disk, midi-port.

## 2.6 Motion

Durations from `motion.*`; one easing (`cubic-bezier(0.2, 0, 0, 1)`, decelerating).

| What | Duration | Notes |
|---|---|---|
| Hover tint | 100 ms | Surface and text colour only |
| Press | 60 ms | Immediate on down, eased on up |
| Toggle | 120 ms | Segmented thumb slide, switch |
| Panel open/close | 180 ms | Width or height, content fades last 60 ms |
| Menu, popup | 120 ms | Fade plus 2 px translate from the anchor |
| Toast | 200 ms | Slide up 8 px and fade |
| Tooltip | 500 ms delay, 80 ms fade | No delay when moving between adjacent tooltipped controls |
| Meters, playhead, counters | 0 | Ballistics are in `motion.meterFallDbPerSec` and `peakHoldMs`, not eased |
| Clip snap during drag | 0 | Snap is instantaneous; a guide line appears instead |

Honour the OS reduce-motion setting: all durations become 0 except meter ballistics.

## 2.7 Components

Every component lists its states in the order: rest, hover, pressed, active (on),
focused, disabled. "Focused" always adds a 2 px `lineFocus` ring outside the
control; it is only drawn for keyboard focus, never for pointer clicks.

### Button

- Sizes: `control` 28 default, 24 compact, 32 large. Horizontal padding `md` 12;
  icon-only buttons are square.
- **Secondary** (default): `surface2` fill, `textPrimary` label, radius `sm`. Hover
  `surface3`. Pressed `surfacePressed`. Disabled: `surface1` fill, `textDisabled`.
- **Primary**: `accent` fill, `textOnAccent` label. Hover `accentHover`, pressed
  `accentPressed`. At most one per view.
- **Ghost**: no fill, `textSecondary` label; hover `surface2` fill and `textPrimary`.
  Toolbar and panel headers use ghost buttons.
- **Danger**: ghost at rest with `danger` text; hover `danger` fill with
  `textOnColour`.
- Label `label` 13/500. A trailing chevron marks buttons that open a menu.

### Toggle button

Same geometry as Button. On state: `accentSubtle` fill, `accent` text and 1 px
`accent` inset outline. Semantic toggles (M, S, R, monitor) use their own colour in
the same pattern: on = colour at 20% fill, colour text, colour outline; "hard on"
for arm is a solid `arm` fill with `textOnColour` so an armed track is unmistakable.

### Segmented control

Pill track `surface1`, thumb `surface3` with radius `pill`, 120 ms slide; segment
labels `label`; selected label `textPrimary`, others `textSecondary`. Used for
driver type, snap grid, piano roll tool.

### Combo / select

`surface2` fill, radius `sm`, value in `body` 13, chevron-down `textSecondary` right
aligned with `sm` padding. Hover `surface3`. Open: outline `accent` 1 px; the popup
uses Menu. Disabled as Button.

### Text field

`surface2` fill, `lineStrong` 1 px border, radius `sm`, `body` text, caret `accent`.
Focus: `accent` border. Invalid: `danger` border and a `caption` message below in
`danger`. Placeholder `textTertiary`.

### Numeric field

A text field that also drags: pointer down and vertical drag changes the value
(1 px = 1 step; Shift = fine, 0.1 step; Ctrl = coarse, 10 steps), double-click to
type, Enter commits, Esc reverts, scroll wheel over a focused field steps. Value in
`value` (mono) 12/500, unit in `textSecondary` after a thin space. Right aligned.

### Checkbox

16 px box, radius `sm`, `lineStrong` border; checked: `accent` fill with a
`textOnAccent` check icon. Label `body` to the right with `sm` gap; the whole row is
the hit target.

### Slider (horizontal)

Track 4 px `surface2` with radius `pill`; filled portion `accent`; thumb 14 px
circle `textPrimary` with 1 px `canvas` edge. Hover: thumb grows to 16 px. Pressed:
value tooltip appears above the thumb. Bipolar sliders fill from the centre. Keyboard:
arrows step, Shift+arrows fine, Home/End. Double-click resets to default.

### Fader (vertical, mixer)

Track 4 px `surface2`, `faderTravel` 200 px, with a 0 dB tick (`lineStrong`, 8 px
wide) and a scale (`caption`, `textTertiary`) at +6, 0, -6, -12, -24, -48, -inf on
the left. Cap `faderCap` 36×14, `surface3` fill, radius `sm`, a 1 px track-colour
line across its centre, hover `textSecondary` outline, drag shows the dB value in a
tooltip. Law: Logic-style, 0 dB at 75% travel, +6 dB max. Double-click to 0 dB.
Shift drag = fine. Keyboard as Slider.

### Knob (pan, send level, plugin parameters)

28 px (`size.knob`) ring: track `surface2` 3 px, value arc `accent` (pan: arc from
centre, label L/R; level: from the start), indicator line `textPrimary`. Value shown
as text below in `value` 12 mono when space allows, else in a tooltip while dragging.
Vertical drag; double-click resets. Keyboard as Slider.

### Meter

Vertical bar `meterWidth` 6 px per channel, `meterGap` 2 px between channels, track
`meterTrack`, zones per `meter.zones` painted as solid segments (no gradient blur),
peak-hold line 1 px `meterPeakHold` with `peakHoldMs`, clip indicator: a 6×6 square
above the bar that turns `meterClip` and stays until clicked (`clipHoldMs` for the
auto variant on the master). Scale ticks `caption` `textTertiary` at
`meter.scaleTicksDb` on wide strips only. Rise is instant; fall
`meterFallDbPerSec`. Horizontal variant (track header) is the same bar rotated,
without scale.

### Scrollbar

10 px (`size.scrollbar`) gutter, thumb `surface3` radius `pill`, hover `textTertiary`,
no arrows. Overlay style on the arrange view: appears on hover or scroll, fades after
800 ms.

### Tabs

Text tabs in `label`; selected `textPrimary` with a 2 px `accent` underline, others
`textSecondary`; hover `textPrimary`. Used in the dock (Mixer, Editor, Browser) and
the plugin browser (Effects, Instruments, Blocked).

### Menu

`surface1` fill, radius `md`, `elevation.popup`, 4 px inset; items 28 px, `body`
text, `sm` left padding plus 20 px icon column; hover `surface3`; shortcut hint right
aligned in `textTertiary` mono; separators `lineSubtle`; submenu chevron. Checked
items show a check icon in `accent`. Disabled `textDisabled`.

### Tooltip

`surface3` fill, radius `sm`, `small` 12 text `textPrimary`, max width
`tooltipMaxWidth`, `sm` padding, shadow. Shows the control name and, for valued
controls, the current value and the shortcut. Delay `tooltipDelayMs`.

### Dialog (sheet)

Never a modal loop. A sheet: `overlay` behind, `surface0` panel radius `lg`, title
`title`, body `body`, buttons right aligned with the primary last. Esc cancels, Enter
confirms the primary when it is not destructive. Destructive primaries use Danger
styling and no Enter shortcut.

### Toast / notification

Bottom-left of the main window, above the status bar; `surface1` with a 3 px left
stripe in the semantic colour; icon, `body` message, optional action link in
`accent`; auto-dismiss after 6 s for info and success, persistent for warning and
error until dismissed. Stack up to 3. Replaces status-bar messages for anything the
user should act on.

### Badge

`pill` radius, `caption` 600 uppercase, 2×6 padding; colour fills at 20% with
colour text. Used for plugin format (VST3, CLAP), take numbers, "REC", "LIVE".

### Table / list

Rows 28 px, `body`, zebra `laneAlt`, hover `surface1`, selected `selectionFill` with
`textPrimary`; header `caption` 600 uppercase `textTertiary`, sortable with a
chevron; column resize handles. Keyboard: arrows, Home/End, type-ahead.

### Splitter

5 px invisible hit area over a 1 px `lineSubtle`; hover shows `accent` 1 px; drag
cursor; double-click resets to default size. Panels snap closed below `panelMin`.

### Progress

4 px bar, `surface2` track, `accent` fill; indeterminate: a 30% segment sweeping at
1.2 s. Always paired with a label and a Cancel ghost button where cancel is possible.

## 2.8 Density and scale

Designed at 100%; verified at 125, 150, 200. All sizes scale uniformly; nothing
reflows except panels, which can be hidden. Minimum window 1280×720 at 100%. Track
row heights are user-adjustable between `trackRowMin` and `trackRowLarge`.

## 2.9 Light theme

**Decision**: light theme is deferred past Phase 7. The token file supports it
(a second file with the same keys), and nothing in the spec assumes dark, but no
light values are tuned yet.
