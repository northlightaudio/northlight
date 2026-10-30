# 5. Mixer

## Purpose and anatomy

A console: one strip per track and bus, the master on the right, all signal flow
visible. It lives in the dock (default) or can be popped out to its own window.

```
+--+--------+--------+--------+-----+ ... +--------+ | +----------+
|  |  strip |  strip |  strip | bus |     |  bus   | | |  master  |
+--+--------+--------+--------+-----+ ... +--------+ | +----------+
```

Strip anatomy, top to bottom (`mixerStripWidth` 92 px; wide mode 112 px adds the
scale and a second column for sends):

```
| ▌ 01 Kick        |  name cap (track colour 55%), 24 px
| In 1          ▾  |  input (tracks only), 24 px
| ┌ inserts ──────┐|
| │ EQ            │|  insert slots, 20 px each, up to 6 visible, scroll beyond
| │ DC1A3     VST3│|
| │ +             │|
| └───────────────┘|
| ┌ sends ────────┐|
| │ Reverb   ─o── │|  send rows, 20 px: name + mini level slider, pre/post dot
| │ +             │|
| └───────────────┘|
| ( pan knob )     |  28 px knob, L/R text below in value font
| [M][S][R][Ø]     |  24 px toggles
|  ‖  █            |  fader 200 px travel  +  stereo meter beside it
|  ‖  █            |
| -6.2 dB          |  value field (numeric, drag/type), 24 px
| Master        ▾  |  output select, 24 px
|     +2.7 ms      |  latency line, 14 px (only while a strip's plugins delay it)
```

## Layout

- Strip width 84; gap `space.xxs` 2 between strips; `surface1` strip on `surface0`
  background with radius `sm` at top and bottom.
- The fader section is the flexible part: dock height sets fader travel between
  120 and 200 px; below 120 the inserts and sends collapse to one-line summaries
  ("3 inserts", "2 sends"; design-system.md "Section summary"). A click lists the
  section: each slot with its whole menu, then "Add insert" or "Add send" (a
  menu rather than a hover pop-over, which would cover the fader the pointer is
  on its way to).
- Wide strips (the background menu or a name cap's menu, remembered): 112 px, the
  dB scale between fader and meter, and a pan column on each send row ("C",
  "L35": drag up and down, double-click centres).
- Hidden strips: "Hide strip" in a name cap's menu leaves the strip out (it still
  plays). A ghost "2 hidden" button under "+ Bus", and the background menu's
  "Hidden strips", show them again, one or all. With every strip hidden the
  mixer says "Every strip is hidden".
- The background (a right-click between or after the strips): Wide strips,
  Hidden strips, Close all plugin windows.
- Master strip: 112 px, separated by `space.lg` and a `lineSubtle`, pinned right; its
  meter is 10 px per channel with a scale, and under it the loudness readout
  (M / S / I in `value` mono with a target marker; lined up with the channels,
  it sits over the meter, where they have pan and toggles); under its gain field the
  project's plugin delay compensation ("PDC 12 ms", `caption` `textTertiary`, the
  samples in its tooltip) while there is any. Master has inserts, no input, no
  sends, no pan. Its insert slots sit under the name cap like a channel's (the same
  slot, hover power and handle, drag to reorder, the same menu without Sidechain
  and Outputs), filling the room above the fader band when it lines up with the
  channels and up to six slots otherwise; "+ Insert" adds an effect. They run on
  the whole mix before the master fader, so the loudness readout and the export
  hear them.
- Buses sit after tracks in a group with a slate header line; a vertical `lineSubtle`
  separates tracks from buses.
- Horizontal scroll with an overlay scrollbar; the master never scrolls.
- Selected strip (following the arrange selection and vice versa): name cap at 100%
  track colour alpha and a 1 px `accent` outline around the strip.

## Visual detail

- Name cap: track colour at 55% with the 4 px full-colour stripe on the left; name in
  `label` `textPrimary`, truncated; double-click renames; click selects the track.
  At its right end an edit button (the Editor panel's plug icon, `iconSmall` in
  `textPrimary`, a square the cap's height, the group number left of it) selects
  the channel and shows it in the Editor panel. The master's cap has none.
- Insert slot: `surface2`, 20 px, name in `small`, format badge right (VST3/CLAP) in
  `caption`; bypassed: name `textTertiary` with a strike; power icon on hover at the
  left; drag handle on hover at the right; dragging reorders with a 2 px `accent`
  insertion line; the "+" slot is a ghost button opening the plugin menu. The
  drag goes to other strips too (the master's included), and Ctrl at the drop
  copies the insert there instead (a hosted plugin with its state); a plugin
  dragged from the browser drops at the line the same way (an instrument only on
  an instrument channel, where it takes the slot). Right-click: the menu every
  slot shares with the Editor panel's cards (ui/InsertMenu.h): open, bypass,
  presets, replace, move up/down, wet/dry, sidechain, outputs, copy, copy chain,
  paste after, show in plugin browser, the latency, remove. A slot whose plugin
  is missing, failed or crashed is drawn per design-system.md "Slot in trouble".
- Send row: name `small`, a 40 px mini slider (drag for level, double-click resets,
  tooltip shows dB), a pre/post dot (filled `accent` = pre-fader) that toggles on
  click. A muted send's name is struck in `textTertiary` and its slider at 40%.
  Right-click: Pre-fader, Mute send, Pan (L100 to R100), Remove send. A send pans
  what reaches the bus on top of the channel's pan.
- "+ Send" lists the buses and FX tracks the channel can feed, then "New FX
  track…": an FX track with that effect and the send to it, one undo step. With
  none yet it says "No FX tracks or buses yet" over that item.
- Pan: knob per section 2; centre detent; value "C", "L35", "R12".
- Toggles: M `mute`, S `solo`, R `arm`, Ø phase (`textSecondary` on, with the on
  pattern). Ctrl+click on S solos the channel alone (every other solo cleared,
  one undo step); Ctrl+click again on the only soloed one clears it. Solo safe
  is in the name cap's right-click menu and shows as design-system.md's "Solo
  safe mark" (solo is always in place: there is no other mode to choose).
- Fader and meter: fader per section 2 with the track-colour cap line; the stereo
  meter sits to its right, 6 px per channel, 2 px gap, peak-hold, clip square above.
  Wide strips add the dB scale between fader and meter.
- Value field: numeric field (section 2) in `value` mono; drag changes the gain.
- Output select: compact combo listing Master and buses (cyclic targets greyed),
  then under "Hardware outputs" the device's other output pairs ("Out 3/4"): the
  channel plays straight there, past the master's inserts, fader and meters,
  lined up with the master by delay compensation. A pair the device lacks stays
  listed greyed ("(not on this device)") and the channel plays to its bus or the
  master meanwhile; an export, which renders the master, mixes it in the same way.
- Latency line: a channel whose plugins delay it shows "+2.7 ms" in `caption`
  `textTertiary` under its output; the row is reserved on every strip while any
  has one, so the rows line up.
- Fader groups: a coloured 3 px bar across the name caps of grouped strips with the
  group number; grouped faders move together; Alt breaks the link for one gesture.

## Behaviour

- Fader: 0 dB at 75% travel; Shift fine; double-click resets; wheel over the fader
  steps 0.5 dB; keyboard arrows when focused.
- Clicking an insert name opens its editor (plugin window or generic); clicking the
  badge (it stays while the slot is hovered, left of the handle, `accent` under
  the pointer) opens the plugin browser searching for that plugin, its row
  selected.
- Solo: soloing a bus gives the strips that feed it a dim `solo` outline, showing
  why they are audible. A solo-safe channel is heard whatever is soloed (mute
  still silences it).
- Several strips at once: not yet. The selection is one channel, shared with the
  arrange view (ui/Selection.h); fader groups link strips that move together.
- Dropouts and overloads: a clip indicator stays lit until clicked; the master's
  clip indicator also appears in the header bar's transport cluster.
- Metering is never eased; ballistics from `motion.meterFallDbPerSec`.

## Pop-out window

The mixer follows the panel model (section 3): ⤢ in its header, dragging its tab
out, or Shift+F2 opens it in its own window with the same layout and a 240 px
minimum height. Fader travel grows with the window up to 200 px.

## Empty state

No tracks: the dock shows only the master strip and the arrange empty-state hint
"Add a track to start mixing" in `textTertiary`.

## Acceptance

- At 100% a strip matches the anatomy and listed heights; fader travel is 200 px
  at the default dock height.
- Fader law: 0 dB at 75% travel, +6 at top, -inf at bottom; the value field and
  tooltip agree with the engine's gain within 0.1 dB.
- Meter zones change at -18 and -6 dBFS; peak hold 1.5 s; the clip indicator
  latches.
- 32 strips plus 4 buses plus master render and scroll at 60 fps at 150% scale with
  meters live.
- Keyboard: Tab moves between strips, arrows adjust the focused fader, M/S/R toggle
  the focused strip.
