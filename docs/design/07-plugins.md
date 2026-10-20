# 7. Inserts, plugin windows, browser

## 7.1 Insert chain panel (Editor tab, dock)

Selecting a track or bus and opening the Editor tab shows its chain as a horizontal
strip of device cards, Bitwig-style, so the whole signal path reads left to right.

```
| 01 Kick ▸  [ EQ ─────────── ] [ DC1A3 ──── VST3 ] [ Limiter ───── ] [ + ]
|             ┌─────────────┐   ┌──────────────┐   ┌──────────────┐
|             │ ⏻  EQ     ⋮ │   │ ⏻ DC1A3   ⋮  │   │ ⏻ Limiter  ⋮ │
|             │ [ knobs … ] │   │ [ open GUI ] │   │ [ knobs … ]  │
|             │ mix ──o──   │   │ mix ──o──    │   │ mix ──o──    │
|             └─────────────┘   └──────────────┘   └──────────────┘
```

- Card: `surface1`, radius `md`, 1 px `lineSubtle`, min width 160, height fills the
  dock; header 28 px with power (bypass) toggle, name in `label`, format badge,
  and a "⋮" menu (replace, remove, presets, sidechain, latency "32 smp").
- Body: for built-ins, the built-in's own editor (7.2a); for
  hosted plugins with a GUI, a thumbnail placeholder with an "Open" primary button
  (the native editor opens in a plugin window) and the three most important
  parameters as knobs if the plugin exposes them; for hosted plugins without a GUI,
  the generic editor in the card.
- Footer 24 px: wet/dry mini slider with "Mix 100%".
- Bypassed card: body at 50% alpha, power icon `textTertiary`, header name struck.
- Drag the header to reorder; a 2 px `accent` insertion line between cards. Drag a
  plugin from the browser onto the strip to insert at the line.
- The "+" card is a ghost card that opens the plugin menu (recent, built-ins,
  favourites, then by manufacturer).
- Sidechain: a small "key: 02 Hats" chip under the header when set, `accentSubtle`
  fill, click to change.

## 7.2 Plugin window

Native window with a 32 px Aural bar above the plugin's own editor:

```
| ⏻  DC1A3 · 01 Kick     VST3   mix ──o── 100%   presets ▾   ⟲   📌   ⤢ |
```

- Bar: `surface0`; power toggle, "Plugin · Track" title in `label` (track name in
  `textSecondary`), format badge, wet/dry slider (80 px) with value, presets combo
  (factory programs and user presets, with Save… at the bottom), an "A/B" toggle
  (**Decision**: deferred, keep the slot), pin (always on top) toggle, and a
  generic-editor toggle for plugins that have both.
- The plugin's editor sits below at its native size; the window follows plugin
  resize requests; DPI scaling passed through.
- Position and size remembered per plugin type; pin state remembered.
- Closing the window never unloads the plugin; the card in the chain shows "open" as
  an `accent` dot on its header while the window is open.

## 7.2a Built-in editors

Every built-in has an editor in the channel strip's language (12-channel-strip.md
12.1-12.2): a display in a well that reads the processor live and drags, then
knobs in the module's hue. The same editor is the card's body in the Editor panel
and the body of its window (under the title, bypass and mix). The hues follow the
strip: EQ `Sky`, compressor `Indigo`, limiter `Coral`; gain `Lime`, reverb `Teal`,
delay `Amber`, synth `Magenta`.

- **Gain**: the input and output history with IN and OUT meters; Gain.
- **EQ**: the strip's EQ graph with the built-in's four bands (a high-pass,
  off at 0 Hz, sitting at the left edge; low shelf; mid with Q on the wheel; high
  shelf) and the analyser. The selected band's knobs below under its name.
- **Compressor**: the transfer curve, the history with the threshold line, the IN
  threshold meter, GR and OUT; Ratio, Attack, Release, Knee, Makeup.
- **Limiter**: the history with the ceiling, the ceiling on the IN meter, GR and
  OUT; Release, Lookahead.
- **Reverb**: the decay: the dry sound, the pre-delay, early reflections (more and
  longer for a bigger room), the tail falling 60 dB over the decay and the highs'
  faster fall (dashed) for the damping. Drag the tail's start for the pre-delay, its
  end for the decay. Size, Damping, Low cut, Width, Mix.
- **Delay**: the echoes over time as bars as loud as the feedback leaves them,
  left up and right down when ping-pong alternates them, darker as the tone falls.
  Drag the first echo for the time, the second for the feedback. Time, Feedback,
  Tone, Mix and a Ping-pong pill.
- **Synth**: the oscillator's cycle (saw into square), the filter's curve (drag
  across for the cutoff, up for the resonance) and the envelope (drag the peak, the
  sustain point and the release end); the eight knobs under them in the same order.

Draggable points are one handle everywhere: a 9 px `textPrimary` disc with a 1.5 px
ring in the hue. A display's readout is `value` at its top right.

## 7.3 Generic parameter editor

For GUI-less hosted plugins. A scrollable grid of parameter rows:

- Row 28 px: name `body` left (160 px), control centre (knob for continuous, segmented
  for choices under 5 options, combo above, checkbox for booleans), value `value`
  mono right (80 px, editable numeric field).
- Sections (from the plugin's group info or the built-in's layout) as uppercase
  `caption` headers with `space.lg` above.
- Drag on a knob begins a gesture (one undo step); the value field commits on Enter.
- Sidechain and latency shown in a footer line.

## 7.4 Plugin browser (side panel tab and window)

```
| 🔍 Search plugins                         [All ▾] [Scan] |
| ☆ Favourites (3)                                       |
|   ★ DC1A3                  Klanghelm        VST3       |
| Recent                                                 |
| Effects                                                |
|   Aural EQ                 Built-in                    |
|   DC1A3                    Klanghelm        VST3       |
|   Aural Test Gain          Leafshade        CLAP       |
| Instruments                                            |
|   …                                                    |
| Blocked (1)                                             |
|   ⚠ BadPlugin.vst3         crashed on scan   Retry     |
```

- Search field at the top with a format filter segmented control (All, VST3, CLAP,
  Built-in) and a category toggle (Effects, Instruments). Type-ahead filters as you
  type; Enter inserts the highlighted plugin on the selected track.
- Rows 28 px per the Table component; name `body`, manufacturer `small`
  `textSecondary`, format badge; star toggles favourite; drag a row to a chain or a
  track header to insert.
- Blocked section lists blocklisted files with the reason in `small` `warning` and a
  Retry ghost button per row; "Clear blocklist" in the overflow menu.
- Scan: a ghost button "Scan for changes" with a chevron menu for "Rescan all";
  while scanning, a progress bar replaces the button with "Scanning 12 of 48 ·
  Surge XT.vst3" and a Cancel; results appear as they arrive.
- The window version (from the app menu) is the same panel at 720×560.

## 7.4a Instruments: the slot and the rack

An instrument is not an effect: a MIDI track has an **instrument slot**, ahead of
its inserts, that takes only instruments (built-in, VST3 or CLAP). The insert
menus list effects only; an instrument dropped or chosen from the browser goes to
the selected MIDI track's slot, or makes a new instrument track when the
selection is not a MIDI track.

- **Editor panel**: an INSTRUMENT section first, before the strip and the inserts:
  the instrument's card (its own editor for a built-in, Open for a plugin with a
  GUI) with Replace and Remove in its menu and no wet/dry footer, or a dashed
  "+ Instrument" slot. The card is not reordered with the inserts.
- **Mixer**: an instrument row under the module row on every strip (reserved on
  all strips when any has one, so the sections stay aligned): the slot with an
  INST / VST3 / CLAP badge, or "+ Instrument". Its menu: Open, Bypass, Outputs,
  Replace instrument, Remove instrument.
- **Instruments panel** (dock tab, F8): the rack. A unit per MIDI track, 64 px, a
  `surface1` panel with the track's hue down its left edge: power (in the hue),
  the instrument's name with a BUILT-IN / VST3 / CLAP badge and the track it plays
  on, OUTPUTS as chips, Edit, and a menu (open, select track, replace, remove).
  Each output pair is a chip: "1/2 Keys" for the main pair (the track itself) and
  each pair with an output track (click selects the track), a dashed
  "+ Track for 3/4" for a pair without one (click makes it: an aux track named
  after the track and the channels, in its colour, with its own inserts, strip,
  sends and fader). "+ Instrument track" at the end makes a MIDI track with the
  instrument chosen. A track without an instrument shows a "+ Instrument" slot.

## 7.4b Aurora

The synth bundled with Aural reads as its own product, so its editor is bolder
than the built-in effects' while keeping to the theme's palette (Mint, Teal,
Sky, Violet, Magenta, Amber) and type:

- A night sky (the canvas darkening to the top) with twinkling stars and three
  aurora ribbons in Mint, Teal and Violet drifting across the top; they move and
  brighten while Aurora sounds and hold still when it is silent or with reduce
  motion on.
- The wordmark AURORA, tracked wide with a Mint-to-Violet gradient, "POLYSYNTH ·
  AURAL" beside it; the preset bar (previous, the name as a menu, next; "Custom"
  when the values match no preset); twelve voice lights that light as voices sound.
- Six glass panels (`surface1` at 72% over the sky, a rim and a top highlight),
  each a hue dot and caption, a display and its knobs in its hue: OSCILLATORS (both
  cycles, drag across to morph sine, triangle, saw, square), UNISON (the voices as
  a fan, spread by the detune and coloured by the pan; drag for the spread),
  FILTER (the response; drag for cutoff and resonance; the mode in a corner chip),
  ENVELOPES (the amp envelope filled and the filter envelope dashed; the corner chip
  picks which the handles edit), LFO (its shape, the live value as a marker; the
  shape in a corner chip) and OUTPUT (a scope of the output).
- Factory presets: Northern Lights, Solar Wind, Glacier Keys, Polar Bass, Ice
  Pluck, Starfield, Comet Lead, Aurora Drift.

## 7.5 States and errors

- Plugin missing on this machine: the chain card shows the name in `textTertiary`
  with a `warning` icon and "Not installed"; state is preserved; the card can be
  removed or replaced.
- Plugin failed to load (layout refused, crash on instantiate): a toast with the
  reason and a "Blocklist" action; the card shows "Failed" in `danger`.
- Latency change (plugin reports new latency): no UI beyond the card's latency label
  updating; the graph re-solves silently.

## Acceptance

- Chain strip screenshot with three cards matches sizes and states, including a
  bypassed card and a drag insertion line mid-drag.
- Plugin window bar screenshot at 100% and 150% with DC1A3 shows the 32 px bar and
  the plugin at its native size.
- Browser screenshot with favourites, both formats and a blocked row.
- Scan progress and cancel verified with the stub scanner tests' hanging plugin.
