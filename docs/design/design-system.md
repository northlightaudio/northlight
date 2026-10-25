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

The base is Machair dusk's peat green (see [brand.md](brand.md)): close to neutral,
with enough green that the window reads as evening land rather than a black box.
Steps are 6 to 9 points of lightness apart: enough to read as planes, not boxes.

### Lines

Two line tokens only. `lineSubtle` (moon cream at 6%) separates planes that touch;
`lineStrong` (moon cream at 12%) outlines popups and fields with focus-less emphasis.
Borders are the exception, not the rule: a panel edge is normally a change of
surface, not a line.

### Text

`textPrimary` for values and names, `textSecondary` for labels and units,
`textTertiary` for hints and inactive tabs, `textDisabled` for disabled. On an
accent or track-colour fill use `textOnAccent` / `textOnColour`; never white on a
light track colour.

### Accent

One interaction colour, `accent` (Overcast blue), with hover and pressed steps and
a 12% `accentSubtle` fill. It means exactly four things: selected, focused, active
toggle, drop target. It is never used for data (meters, clips, notes use their own
tokens).

### Semantic

`record` is a warm red, distinct from `danger` only by use: `record` is the arm
button, the record transport button, the recording playhead and punch regions;
`danger` is destructive confirmation and hard errors. `warning` (amber) is for
dropouts, latency mismatch, missing plugins. `success` (green) is for completed
bounces and healthy status. The track-control trio is `arm` (red), `solo` (amber),
`mute` (slate), matching DAW convention.

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

- **UI face: Inter** (variable). Humanist, clear at 11 to 13 px, open licence.
  Bundled in `assets/fonts`.
- **Numeric face: JetBrains Mono** (variable) for every time display, counter, dB
  value, tempo and sample-rate readout. Monospace guarantees stable widths while
  values change; Inter's tabular figures are not accessible through JUCE 8.

Scale (px at 100%), from `font.size`. A size is the em square, as in CSS and
design tools (JUCE's `withPointHeight`), not JUCE's font height, which is the
ascent plus the descent and about 1.2 em for Inter. Size a text box with
`Theme::lineHeight(size)`, one line of either face in whole px, never the size
plus a guess. The exception is the `legend` face printed on hardware faces: its
sizes are JUCE heights in the face's design pixels, so legends keep the size the
rendered art was drawn for. A number with a fixed count of decimals goes through
`ui::fixedText`: `juce::String(value, 0)` prints every decimal, not none.

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

Paragraph line spacing 1.3. Uppercase labels (section headers in panels) use `caption` at
weight 600 with `letterSpacingCaps` tracking and `textTertiary` colour.

## 2.3 Spacing, radius, grid

A 4 px base grid: `space.xs` 4, `sm` 8, `md` 12, `lg` 16, `xl` 24, `xxl` 32,
`xxxl` 48, plus `xxs` 2 for hairline gaps. Control heights are 24 (compact: track
header rows, mixer buttons), 28 (default), 32 (large: transport, first run).

Radii: `sm` 3 for buttons, fields and clips; `md` 6 for menus, cards and popups;
`lg` 10 for floating windows' content area; `pill` for badges and the segmented
control's thumb.

## 2.4 Elevation and material

**Panels are flat; controls are tactile.** Planes (canvas, surfaces, panels, cards,
strips, lanes, clips) are expressed by surface steps alone and carry no shadows,
bevels, textures or gradients. Controls, the things a hand would touch, get a
consistent lighting model from the `material` tokens:

| Element | Treatment (tokens in `material.*`) |
|---|---|
| Button, toggle, key, M/S/R square, combo | Vertical gradient `controlTop` to `controlBottom`, 1 px `controlHighlight` along the top edge, `controlShadow` below. Pressed: `pressedInset` and the gradient reversed. |
| Active toggle | As above plus the semantic colour's glow: `glowAccent`, `glowRecord`, `glowSolo`, `glowMonitor`. Hard-on arm and record buttons use a solid vertical gradient of the colour (lighter at the top) with `capHighlight` on top. |
| Fader track | A groove: `groove` fill, `grooveInset` shadow, 1 px `grooveLip` under it. |
| Fader cap | `capTop` to `capBottom` gradient, `capHighlight` top edge, `capShadow`; the track-colour line gets a 4 px glow of its own colour. |
| Knob | Rim `knobRim` 3 px with the value arc on it; body a radial gradient `knobBodyLight` (offset to the upper left) to `knobBodyDark`; a faint `knobHighlight` arc at the top; the indicator line `textPrimary`. |
| VU needle | The needle in `textPrimary`, with `needleShadow` cast on the face beneath it. |
| Meter | A well: `meterWell` with `meterWellInset`; the signal is LED-segmented at `ledPitchPx` with `ledGapPx` dark gaps, zone colours unchanged; peak hold and clip as in Meter (2.7). |
| Numeric readouts (time, dB, tempo) | A readout well `readoutWell` with `grooveInset`; text in `readoutText` (no glow); `readoutRecord` while recording. Values in fields that are typed into keep the plain Text field look. |
| Scrollbars, tabs, menus, tooltips, sheets, toasts, badges, checkboxes, sliders' tracks | Unchanged: flat per their component spec. |

Rules: no textures or noise, no bevels on panels, no screw heads, no engraved text, no gradients wider than a control, no glow on anything
that is not an active state or a readout, shadows never longer than 3 px inside the
window. Reduce-motion and high-contrast variants keep the geometry and drop the glows.

Popups (menus, tooltips, combo lists, colour swatches) float with
`elevation.popup`: an 8 px blurred shadow at 50% and a `lineStrong` border.
Secondary windows (plugin, audio setup, browser) are native windows with the same
surface tokens and no inner chrome.

Rendering: blurred glows are expensive, so they are pre-rendered once per scale and
colour and drawn as 9-slice images, never computed per frame. The LED meter is a
single repeating fill, not per-segment rectangles.

## 2.5 Iconography

- 16 px grid (`size.icon`), 1.5 px stroke, round caps and joins, 1 px inner padding.
  20 px variant for the transport.
- Monochrome. `textSecondary` at rest, `textPrimary` on hover, `accent` when the
  action is active, `record` for record, `textDisabled` when disabled.
- **Icons first for actions and modes.** Toolbar actions and mode toggles (loop,
  punch, metronome, count-in, snap, ripple, zoom, view toggles, bypass, pin) are
  icon buttons with a tooltip that gives the name, state and shortcut. A toggle
  that carries a small value (count-in bars, snap grid) overlays it as a 9 px
  figure in the corner or adds a chevron. A toggle whose chevron opens a menu
  (snap) is a split button: a square for the icon as wide as the button is tall,
  then a 16 px (`lg`) slot for the chevron, each centred in its part; the slot opens
  the menu on press, the square toggles; hover and press light only the part under the pointer,
  with a `lineSubtle` hairline between the parts. Text is used where it carries a value or
  a name (time, tempo, device, track, plugin), for navigation (tabs, menu items),
  for the conventional M/S/R letters, and for actions whose icon would be
  ambiguous (Calibrate, Scan, Export) where a short label is clearer.
- Icons never appear without a tooltip. An icon button is 28×28 (20 px icon in the
  32 px transport buttons); related icons sit 8 px apart, groups 24 px apart.

Required set (drawn as vector paths, exported to `assets/icons`): play, stop,
record, loop, punch, metronome, count-in, go-to-start, go-to-end, undo, redo, snap,
ripple, zoom-in, zoom-out, zoom-fit, mixer, browser, inspector, settings, plugin,
power (bypass), pin, close, chevron-down, chevron-right, check, search, filter,
add, remove, drag-handle, mute, solo, arm, monitor, phase, link (stereo), folder,
file-audio, file-midi, marker, region, warning, info, error, success, loudness,
cpu, disk, midi-port, and the track kinds: track-audio (a waveform), track-midi (a
note), track-aux (an output jack); instrument channels use instrument and folder
tracks folder. Preferences adds keyboard (its Shortcuts page). The plugin menus add
the effect types: fx-dynamics (a compressor's bent transfer line), fx-eq (a bell),
fx-space (a decaying train of echoes: reverb and delay), fx-modulation (a sine),
fx-drive (a vacuum tube: drive and saturation), fx-guitar (a plectrum: Corona and the pedals) and fx-stereo (two overlapping circles: stereo and
utility); other effects use plugin.

## 2.6 Motion

Durations from `motion.*`; one easing (`cubic-bezier(0.2, 0, 0, 1)`, decelerating).

| What | Duration | Notes |
|---|---|---|
| Hover tint | 100 ms | Surface and text colour only |
| Press | 60 ms | Immediate on down, eased on up |
| Toggle | 120 ms | Segmented thumb slide, switch |
| Panel open/close | 180 ms | The panel slides over the content at its open size; the content is laid out once, not every frame |
| Menu, popup | 120 ms | Fade plus 2 px translate from the anchor |
| Toast | 200 ms | Slide up 8 px and fade in; the others glide to their places; a dismissed one fades and drops 8 px |
| Sheet | 180 ms in, 120 ms out | Fade with the panel rising 8 px into place; fades out before it goes |
| Track row added / removed | 180 ms | A new row grows from nothing; a removed row's space closes so the rows below glide up |
| Editor panel cards | 180 ms | When the chain changes (an insert or send added, removed, moved) cards glide to their new places and new ones fade in; switching channels is instant |
| Tooltip | 500 ms delay, 80 ms fade | No delay when moving between adjacent tooltipped controls |
| Progress sheet | 400 ms delay, then as Sheet | `motion.progressDelayMs`: work that finishes sooner shows nothing. A delay, so reduce-motion keeps it |
| Meters, playhead, counters | 0 | Ballistics are in `motion.meterRiseMs`, `meterFallDbPerSec` and `peakHoldMs`, not eased |
| Clip snap during drag | 0 | Snap is instantaneous; a guide line appears instead |

Honour the OS reduce-motion setting: all durations become 0 except meter ballistics.

## 2.7 Components

Every component lists its states in the order: rest, hover, pressed, active (on),
focused, disabled. "Focused" always adds a 2 px `lineFocus` ring outside the
control; it is only drawn for keyboard focus, never for pointer clicks.

### Button

- Sizes: `control` 28 default, 24 compact, 32 large. Horizontal padding `md` 12;
  icon-only buttons are square.
- **Secondary** (default): the control material (section 2.4: `controlTop` to
  `controlBottom` gradient, top highlight, 1 px shadow), `textPrimary` label, radius
  `sm`. Hover: gradient one step lighter. Pressed: `pressedInset`, gradient reversed.
  Disabled: flat `surface1` fill, `textDisabled`, no highlight or shadow.
- **Primary**: moonlight, the one bright thing in a view: `primary` fill (a faint
  vertical shade and the control shadow below), `textOnPrimary` label. Hover
  `primaryHover`, pressed `primaryPressed`. At most one per view. It is not the
  accent: Overcast marks what is active, and a filled Overcast button reads as
  washed out against the peat.
- **Ghost**: no fill, `textSecondary` label; hover `surface2` fill and `textPrimary`.
  Toolbar and panel headers use ghost buttons.
- **Danger**: ghost at rest with `danger` text; hover `danger` fill with
  `textOnColour`.
- Label `label` 13/500. A trailing chevron marks buttons that open a menu.
- A label is centred by its letters, not its line box (`ui/OpticalCentre.h`): the
  box's middle is half way between the baseline and the mean of the cap height and
  the x-height, or the cap height for a label of capitals alone ("M", "S"). The same
  for a segmented control's segments and a row of pills.

### Toggle button

Same geometry and material as Button. On state: `accentSubtle` fill, `accent` text,
1 px `accent` inset outline and `glowAccent`. Semantic toggles (M, S, R, monitor)
use their own colour in the same pattern with their own glow; "hard on" for arm
and solo is a solid vertical gradient of the colour (lighter at the top) with
`capHighlight` and the colour's glow, label in `textOnColour`, so an armed track
is unmistakable.

### Segmented control

Pill track `surface1` (`surface0` on a `surface1` card, so it reads as a well),
thumb `surface3` with radius `pill`, 120 ms slide; segment labels `label`; selected
label `textPrimary`, others `textSecondary`. A disabled segment is `textDisabled`,
cannot be chosen and is skipped by the arrow keys. Used for driver type, snap grid,
piano roll tool, and the Preferences choices with two to four options.

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
Readout variant (time displays, fader dB, tempo, loudness): a readout well
(`readoutWell`, `grooveInset`) with the value in `readoutText`,
`readoutRecord` while recording; while being typed into it reverts to the plain
Text field look so the caret and selection are legible.

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

Track: a 4 px groove (`groove`, `grooveInset`, `grooveLip`), `faderTravel` 200 px,
with a 0 dB tick (`lineStrong`, 8 px wide) and a scale (`caption`, `textTertiary`)
at +6, 0, -6, -12, -24, -48, -inf on the left. Cap `faderCap` 36×14 in the cap
material (`capTop` to `capBottom`, `capHighlight`, `capShadow`), radius `sm`, a
1 px track-colour line across its centre with a 4 px glow of the same colour.
Hover: highlight brightens; drag shows the dB value in a tooltip. Law: 0 dB at 75%
travel, +6 dB max. Double-click to 0 dB. Shift drag = fine. Keyboard as Slider.

### Knob (pan, send level, plugin parameters)

28 px (`size.knob`): a 3 px rim `knobRim` carrying the value arc in `accent` (pan:
arc from centre, label L/R; level: from the start); inside it a body with the radial
gradient `knobBodyLight` (offset upper-left) to `knobBodyDark`, a 1 px dark edge and
a faint `knobHighlight` arc at the top; indicator line `textPrimary`. Value shown as
text below in `value` 12 mono when space allows (6 px under the ring), else
in a tooltip while dragging. In a card the label follows the value directly and
`space.sm` separates it from the next row's knob, so each label reads as its own
knob's.
Vertical drag; double-click resets. Keyboard as Slider.

### Bud knob

For a face that wants its knobs light: a ring with no body (Bloom). The track is a
270 degree arc in `surface2` (`surface3` on hover or drag), the value arc in the
module's hue over it (from the centre for a bipolar range), both
`max(3, 8.5% of the size)` thick with round caps; at the value, the bud: a
`textPrimary` dot 1.3 strokes across on a hue halo 2.6 strokes across at 25% (40% on
hover). The value sits under it in `value` `textPrimary`. Sizes, behaviour, keyboard
and focus ring as the knob (`Knob::Style::bud`). Disabled: everything
`textDisabled`.

### Rotary selector (stepped switch)

For a parameter with a few fixed positions that a console would put on a rotary
switch (a bus compressor's ratio, attack and release), where a segmented control
would be too wide and a knob would hide that the values are steps. A 34 px body in
the knob material (rim `knobRim`, body `knobBodyLight` to `knobBodyDark`,
`knobHighlight` arc), with a `textPrimary` pointer and no value arc. The positions
sit on an arc centred at the top, 45° apart (closer when there are more than six,
never past 270° in all); each has a 2 px dot just outside the rim and its short
label (up to four characters, in a 34 px box) in `caption` 18 px further out: the
selected position's dot in the module's hue and its label in `textPrimary`, the
others `textTertiary`, the label under the pointer `textSecondary`. The control is
as wide as its labels need (104 px), so neighbours never share a label's room.
Each label's near edge sits 5 px past its dot whatever its width, so a wide side
label reaches farther out instead of touching its dot. Its
name sits under it as a knob's label does (`caption`, `textSecondary`), with the
unit when the labels leave it out ("Attack (ms)"), on the same baseline as the
knob labels beside it.

- Click a label (or its dot) to select it. Vertical drag steps one position per
  16 px (one undo step for the drag); the wheel steps one position per notch;
  arrows step; Home and End go to the ends; double-click returns to the default.
- The pointer turns to a new position over `motion.toggleMs` (none with reduce
  motion).
- Hover: the body lightens a little (the knob's hover). Focus: the focus ring
  around the body. Disabled: pointer, dots and labels `textDisabled`.
- The tooltip names the control and the full value ("Attack: 10 ms").

### Preset bar

For choosing a built-in effect's preset where its name is shown (a card's header,
a window's title row). A pill `controlCompact` high (`surface0` at 80%, a 1 px
`lineSubtle` outline, fully rounded) holding three ghost buttons: previous (‹) and
next (›), each as wide as the bar is high, round the current preset's name in
`label` with the menu chevron, truncated with an ellipsis when long. The name
reads "Custom" when no preset matches. States: the buttons' ghost hover and
pressed; disabled arrows when the effect has no presets; focus rings on each
button. A click on the name opens the presets menu (screens/plugins.md 7.2b).
Tooltips: "Previous preset", "Next preset", "Presets". The bar narrows
with its header and hides below 112 px.

### Stage tile

For an effect made of several stages that each want a live readout and their
own controls (Patina). A tile is a stage's readout and its tab at once: a 26 px
header (the stage's power `IconButton`, its name in `label`, its current choice
in `caption` `textTertiary` at the right), a display well, and a 4 px amount
bar (`surface0` track, the module's hue filled to the live amount, a 1 px
`textPrimary` tick for the set amount). Tiles sit in a row in signal order,
16 px apart with a `textTertiary` chevron in each gap. States: rest (no fill),
hover (`surface2` at 50%), selected (`surface2`, 1.5 px hue outline, radius
`md`), off (well dimmed by `surface1` at 42%, name `textSecondary`, outline
`textSecondary` when selected), focused (the focus ring round the tile).
Keyboard: Left/Right move, Enter selects, Space switches the stage. The selected
tile's controls show in the row below under the stage's name in `captionCaps`.
No effect uses the pattern now: Patina shows every stage's controls at once,
each stage a module on its rendered face (plugins.md 7.2a).

### Scene face

For a built-in that is its own product but no piece of studio gear (the
instruments, Vesper): the card's body is a scene, and the controls sit on glass
over it. It stays inside the system: every colour is a token or a track hue mixed
with one, every control is the system's own.

- **The scene** fills the body (radius `md`): a sky graded from `canvas`, drawn
  once per paint, with the instrument's or effect's own motif (ribbons, a pulsar,
  clouds, a dusk horizon). A motif that moves repaints only round itself; nothing
  that moves may repaint the whole face (ui-performance.md), so a sky that would
  brighten as a whole stays still. Moving parts are still with reduced motion.
- **The wordmark**: the product's name drawn as an outline, made once per layout,
  filled with a gradient between two of its hues, and what it is under or beside
  it in `captionCaps` `textTertiary` ("POLYSYNTH · NORTHLIGHT"). Instruments set
  it in `font.ui` capitals tracked wide; Vesper in the display serif
  (`Theme::displayFont`).
- **Glass**: `surface1` at 72% over the scene, radius `md` (`lg` for a tile), a
  1 px `textPrimary` highlight at 7% along the top and a `lineSubtle` rim. A
  panel's caption is `captionCaps` `textSecondary` after a 6 px dot in its hue.
  A display inside glass is a well of `canvas` at 82%.

### Light button

A round light that is also a latch (Bloom's Freeze seed, Vesper's Catch star): a
glow in the hue round a body or a motif, a `captionCaps` word saying what it does
(FREEZE, CATCH) that changes to the held state's (FROZEN, HELD). Off, the glow is
dim (22%) and brightens on hover (+15%); held, it changes colour (ice for Bloom,
the gold of Straw for Vesper) and the motif stops moving. Pressed darkens the
body. Click, Enter or Space toggles it as one undo step; focus draws the ring
round the light; the hit area is the circle. It may react to the sound (Vesper's
glow follows the voice) but repaints only itself.

### Hardware face

For an effect whose character is a piece of studio equipment (Patina's rack
unit), the card's body may leave the house surfaces, as the instruments do, and
be drawn as that equipment. It stays inside the system: every colour comes from
the `hardware` token group, every state is designed, every control keeps its
keyboard, focus ring and tooltip, and its text pairs are in contrast.md.

- **Textures** are images under `assets/<effect>/`, made for Northlight (their
  provenance in that folder's README), colour-graded once offline. Each has a
  flat token fallback (`panel`, `plate`, `lamp`) so the face works without them.
  A static layer (panel, nameplate, recesses, engraved rules) is drawn into an
  image once per size and scale.
- **Legends** printed on a panel are `legend` with a `legendShadow` copy 1 px
  below (and a fainter one 1 px right), so they read over a worn finish.
- **Pointer knob** (`Knob::Style::pointer`): a printed scale of 11 ticks over
  the knob's 270 degrees (every fifth longer, `legend` at 95% and 70%, each
  with a shade under it) at the ring size, and a black chicken head at 72% of
  it pointing at the value: `knobBody` lit from the upper left, a 1 px
  `highlight` edge at 8%, a `knobPointer` line along the pointer and a
  `knobCap` cap over the base, with a soft `shadow` below. The value is printed
  under it in `value` `legend`. Hover and drag lighten the body; disabled, the
  scale and value drop to 35-45% and the line to 40%. Same sizes, behaviour and
  keyboard as the knob.
- **Rotary selector, hardware** (`RotarySelector::setHardware`): the chicken
  head at 105% of the 34 px body as the pointer, each position a printed dot
  (`legend` at 70%; the chosen one a 4 px `led`) and its label as a legend (62%,
  90% under the pointer, 100% chosen). Otherwise the rotary selector.
- **Recessed parts** (a preset selector, a meter window): a darker slot with a
  shade under its top lip and a faint `highlight` line along its lower edge;
  pressed keys drop 1 px. Lit glass (a display, a meter window) gets a
  `highlight` reflection across its top left.
- **Hardware key** (Corona's strip and cabinet plate): a bakelite key in a dark
  `bezel` slot, the gradient `keyTop` to `keyBottom` with a faint `highlight`
  along its top, its label printed in `keyGlyph` (`font.legend`, 14 px bold), a
  5 px LED at its left (`ledOff`, `led` with a soft halo while chosen or on). A
  chosen or latched key sits pressed in by 1 px. Keys in a row act as a radio
  group (one lit), or alone as a latch (ON, Ø) or a plain button (LOAD IR, no
  LED). Hover 7%, pressed 10%, disabled at 50%; Space or Enter presses it; a
  focus ring round the slot. A square key (the page keys) puts its icon over its
  label (13 px) and its LED in the top left corner. A rendered key (Orbit's,
  `HardwareKey::setRendered`) is drawn from patches of its face's renders instead:
  ivory with a faint standby glow from behind when unlit (so its label reads in a
  dark face), amber and glowing brightly when lit, no LED; its label is
  printed over it (`orbitKeyInk`, `orbitKeyInkLit`; 14 px, 19 px on a compact
  face). While held it is drawn from a third and fourth render with every key
  pushed into its well (unlit and lit): its edge and shadow shrink, as a real key's
  do, with no tint over it.
- **Two-page face** (Corona): a face too rich for one panel shows one page at a
  time, as a real unit with a lid or a removable grille would. Both pages share
  the face's size, its background and a strip along the foot that holds the page
  keys (a radio pair of square hardware keys, each an icon of its page over its
  name, the current page lit) and whatever must be
  reachable from either page; nothing else is on both. Changing page is
  immediate (no animation), is not a parameter (it is never undone or saved in
  the project) and is remembered per insert while the app runs, so a card made
  again opens where it was left. Each page caches its own static layer.
- **Rendered parts** (the tape echo's console): a face whose parts are 3D renders
  (Blender, the scripts and their sources in `scripts/render/<effect>/`) rather
  than drawn. One lighting for every part: a key from the top of the unit, the same
  room in every reflection, shadows falling down the panel. The rules that keep it
  honest:
  - **Drawn at one size, scaled to fit.** The face has a design size; the card
    scales it uniformly to its body (never above 100%) and reports its width at a
    given height (`ModuleCard::preferredWidthAt`), so the dock narrows the card
    rather than leaving panel beside it. Images are kept at twice the design size.
  - **What turns is a separate layer.** A knob is its body (with its shadow and
    highlights, drawn once, never rotated) and a pointer layer turned with the
    value (`Knob::Style::rendered`, which takes the mouse only on the skirt). A
    roller turns only its screw slot and drillings. Rotating a lit part would turn
    its light with it.
  - **States are renders, not tints:** a lever in each position, a lamp off, lit
    and alarmed.
  - **A lit state can be a patch of a second render.** When a part's light falls
    on the panel round it (Orbit's keys glowing amber), the face is rendered twice,
    unlit and with every such part lit, and a lit part draws its own patch of the
    lit render over the still one: its glow, and its light on its neighbours, are
    exact.
  - **A part that turns whole is a run of frames.** When a part's light cannot be
    parted from its turning (a reel under a spotlight: its windows show the pack and
    throw shadows), it is rendered at even steps over its symmetry (Orbit's reels:
    30 frames over 120°) and the nearest two are blended. At rest the still render
    shows it, so the frames can be at the design size; a part that stops coasts on
    to its next rest angle rather than stopping between frames.
  - **Light is a layer of its own.** A lamp's glow is rendered with every other
    light off and laid over the base as colour with alpha, so its brightness is its
    opacity and its spill comes with it. Glass and perspex are their reflections
    alone, over what is under them.
  - **Text stays live.** Legends, scales and values are drawn in tokens over the
    renders (`font.legend`), so they stay sharp, translatable and in contrast.md.
    A face that caches its static layer draws its printed words in a second pass
    straight to the screen, not into the cache, so the window's renderer sets them
    at their real size.
  - **Drawn at the size they land.** A render is shrunk to the pixels it covers
    (halving in steps, then one high-quality pass; `hardware::drawScaled`) before
    it is drawn, never stretched down in one bilinear step, which turns small
    highlights into sparkle. Chrome is toned to the panel's light when packaged.
  - **Compact below two thirds.** Shown below 65% of its size (the Editor dock),
    a face drops its small print (values under the knobs, printed scale numbers,
    small position legends, the model line) and prints its names 1.45x larger
    with tighter tracking, set smaller only to fit their slot. Each name hangs
    from its top edge under its control with a clear gap (about 20 design px
    under a knob's skirt), names in a row share one line, and names in a tight
    band (under a window, over a lever) grow less and keep clear of what bounds
    them. A stepped
    selector, its positions no longer printed, shows its setting in its name's
    place, and the knob being hovered or turned shows its value there. All text
    on a face is set crisply at its device size on a whole-pixel baseline
    (`hardware::drawCrispText`), and the face itself sits on a whole pixel.
  - **One implementation.** A new rendered face derives `RenderedFace` (design
    size, scaling, cached art, live words) and its card `RenderedCard` (sizing,
    rendered knobs and levers), and draws with `HardwareParts`; it does not copy
    another face's helpers.
  - **Markers, not renders, for what the user places.** A part that is dragged to
    a position (Corona's mics) is a clean marker in a token colour, not a render
    that must also read from every angle.
  - **Every part has a flat fallback** in the `hardware` tokens, as every texture
    does.

### Control groups on a card

A card with more than about eight controls splits them into groups by purpose:

- Each group has a caption in `captionCaps` `textTertiary`, left-aligned to the
  group's first cell, on a `space.lg` (16 px) row above the controls.
- A 1 px `lineSubtle` rule with `space.sm` on each side separates the groups and
  runs from 2 px under the caption row's top to 4 px above the controls' foot.
- Labels keep one baseline across all groups.

A toggle that sits in a row of knobs takes a knob cell:

- the pill (20 px tall, the cell's width less `space.sm`) where the ring sits;
- a live value or "Off" where a knob shows its value;
- its name on the labels' line.

### Frequency handle (on a spectrum)

A vertical line through a plot marking a frequency that drags sideways:

- **Line.** 1 px in the module's hue at 70% while its section is active, dashed
  `textTertiary` at 60% while it is not; `textPrimary` at 85% and 1.5 px on
  hover and drag.
- **Disc.** The shared handle disc sits near the plot's foot.
- **Value.** Its value is in `value` beside the disc on a `meterWell` plate at
  80%.
- **Mouse.** Within 6 px the cursor is a left-right resize. A press elsewhere
  takes the nearer handle, and double-click resets the nearer one.

### Latch (large)

For the one defining on/off state of a module that sits among its large knobs
(Bloom's Freeze), where a pill would be too slight. The size of the large knob's
ring (46 px): the knob body (`knobBodyLight` to `knobBodyDark`, `knobHighlight` arc)
inside a 4 px rim, `knobRim` when off; a glyph in the middle (Freeze: a six-armed
snowflake, 1.5 px strokes); its state under it where a knob shows its value ("Off",
"Held") in `value`, and its name under that on the row's label baseline.

- On: the rim and the glyph in the module's hue, a halo of the hue at 18% just
  outside the ring, the state text in the hue.
- Hover: the body lightens (`surface3` at 22%) and an off glyph turns `textPrimary`.
  Pressed: `surfacePressed` at 45% over the body. Focus: the focus ring around the
  ring. Disabled: rim, glyph and text `textDisabled`.
- Click, Space or Enter toggles (one undo step). Tooltip names what it does.

### Line grip

A line on a display that drags sideways (a crossover, an edge) has a
  grip, not a disc: a 7 x 16 px upright pill, radius 3.5, `textSecondary` (dim:
  `textTertiary`) with two 1 px `surface0` notches across its middle; hover and drag
  turn it `textPrimary` with a `textPrimary` 18% halo 3 px out. Round handles stay
  for points that drag up and down or freely.

### Gap add button

A round "+" that adds an item between two others in a row (the Editor panel's
insert cards), so nothing has to be added at the end and dragged back. It sits
centred in the gap, level with the items' middle, `iconLarge` 20 px across: a
disc in the control material (2.4: gradient, top highlight, shadow) with a 1 px
`lineStrong` outline and the `add` icon's cross (`iconSmall`) in `textSecondary`,
drawn as two bars snapped to device pixels so it stays crisp at every scale, so it
reads as a raised key over the cards' edges. It overlaps the items' edges by a few pixels, so it is out of
sight and takes no clicks until wanted.

- Rest: not drawn.
- Shown: while the pointer is in the gap's column (the gap, `xs` into each item, and
  the button), at any height along the items; it appears and goes at once, with no
  fade. It stays up while its menu is open.
- Hover: the face a step lighter, icon `textPrimary`. Pressed: the face pressed in
  (`pressedInset`, gradient reversed).
- Focused: Tab reaches it even when not shown; it draws itself with the focus ring,
  and Enter or Space opens its menu.
- Never during a drag: pressing anywhere else in the row, or dragging an item or a
  plugin over it, puts it away so the 2 px `accent` insertion line is the one
  thing that marks a position.
- Tooltip and accessible title name the neighbours: "Add an insert between EQ and
  Glue".

### Meter

Vertical bar `meterWidth` 6 px per channel, `meterGap` 2 px between channels, in a
well (`meterWell`, `meterWellInset`); the signal is LED-segmented at `ledPitchPx`
with `ledGapPx` dark gaps, zones per `meter.zones` painted as solid colour runs (no
blur between zones). Peak-hold line 1 px `meterPeakHold` with `peakHoldMs`. Clip
indicator: a 6×6 square above the bar that turns `meterClip` and stays until clicked
(`clipHoldMs` for the auto variant on the master). Scale ticks `caption`
`textTertiary` at `meter.scaleTicksDb` on wide strips only. The bar rises over
`meterRiseMs` (so a lone spike does not jump it) and falls at `meterFallDbPerSec`;
the peak line and the clip light see the raw peaks. The horizontal variant (track
header) is the same bar rotated, without scale.

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
items show a check icon in `accent`. Disabled `textDisabled`. An item's icon sits in
the icon column at 16 px, centred. A hint about an item that is not a shortcut (a
plugin's maker) comes after a tab in its text and is drawn right aligned in `small`
`textTertiary`, dropped when the name needs the room.

### Tooltip

`surface3` fill, radius `sm`, `small` 12 text `textPrimary`, max width
`tooltipMaxWidth`, `sm` padding, shadow. Shows the control name and, for valued
controls, the current value and the shortcut.

On an effect's card a tooltip is a few words (28 characters at most), never a
sentence: the card's labels, values and displays already say what each part is,
and long tooltips cover the design. A display that only shows (a meter, a history,
a scope, a spectrum) has none; one you can drag says what a drag sets ("Drag:
threshold"); a button, toggle or chip names what it does ("Freeze", "Tempo sync",
"Kill dry"). A knob has none while it draws its value under it; one too small to
draw it shows the value. The shortcut (written after two
spaces, keys joined by "+": "Redo  Ctrl+Shift+Z") shows as key caps at the right of
the first line, one per key: `caption` `textSecondary` on `surface1` with a
`lineStrong` border, radius `sm`. Delay `tooltipDelayMs`.

### Dialog (sheet)

Never a modal loop. A sheet: `overlay` behind, `surface0` panel radius `lg`, title
`title`, body `body`, buttons right aligned with the primary last. Esc cancels, Enter
confirms the primary when it is not destructive. Destructive primaries use Danger
styling and no Enter shortcut.

A **progress sheet** (opening a project, screens/shell.md) is blocking: every key
stops at it, a click outside the panel does nothing, and Esc cancels only while its
ghost Cancel is enabled. It holds the input at once but appears after
`motion.progressDelayMs`. Its content is a Progress row (below).

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
In a sheet the label sits above the bar in `body` `textSecondary`, one line with an
ellipsis (`ui::ProgressRow`).

## 2.8 Density and scale

Designed at 100%; verified at 125, 150, 200. All sizes scale uniformly; nothing
reflows except panels, which can be hidden. Minimum window 1280×720 at 100%. Track
row heights are user-adjustable between `trackRowMin` and `trackRowLarge` for all
tracks (the corner stepper), and per track between `trackRowMin` and `trackRowMax`
(drag the row's bottom edge). Expanded take lanes add `takeLane` (40) per lane
under a track's row (screens/comping.md); the row's own height is unchanged.
Open automation lanes follow them, each `automationLane` (48) by default and sized
per lane between `trackRowMin` and `trackRowMax`, `automationLaneCollapsed` (20)
when collapsed, with a 20 px footer row (screens/arrange.md "Automation").

**Sub-rows under a track** (take lanes, automation lanes) share one pattern: a
header in the header column on `surface0`, a step below the track header, with the
track's stripe carried down at 38%; the row's own controls in its header's top
line (24 px), icon buttons as 14 px icons in `textTertiary` that turn `textPrimary`
on a `surface2` rounded square when hovered; a `lineSubtle` line under each. A
sub-row never draws over the track's own row.

## 2.9 Light theme

**Decision**: a light theme is deferred. The token file supports it
(a second file with the same keys), and nothing in the spec assumes dark, but no
light values are tuned yet.
