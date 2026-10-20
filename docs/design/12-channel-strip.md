# 12. Channel strip

## Purpose and anatomy

Every track and bus has a channel strip: a fixed set of built-in processors that
are always there and switched on in place, the way a console channel (or Cubase's
channel strip) works. It is separate from the insert rack: nothing is added or
removed, and the modules never show up in the inserts menu or the insert slots.

Modules, in their default order:

| Module | Hue (track palette) | What it does |
|---|---|---|
| Gate | Mint | Key-filtered gate: threshold, range, attack, hold, release, key high-pass and low-pass |
| EQ | Sky | Six-band parametric: low cut, four bands (peak, low shelf, high shelf or notch), high cut |
| Compressor | Indigo | Feed-forward, peak or RMS detector, soft knee, make-up (manual and auto), wet/dry |
| De-esser | Violet | Linkwitz-Riley split at the frequency, reduction on the band (split) or everything (wide), listen |
| Saturation | Tangerine | Tape, tube or clip curve with anti-aliasing, tone, output trim, wet/dry |
| Limiter | Coral | Zero-latency peak limiter: input gain, ceiling, release |

Module hues are data colours, never the accent: the accent keeps meaning
selection, focus and drop targets.

The strip sits after the inserts by default (signal flow: inserts, strip, fader);
its caption row switches it to before them. Modules can be dragged into another
order. A module that is off costs almost nothing (the processor's settled bypass
is a no-op) and adds no latency (the limiter has no lookahead for that reason).

## 12.1 In the Editor panel

```
| INSERTS              CHANNEL STRIP  [After inserts ⇄] [All off]
| [EQ card] [+]        |G| |E| [ Compressor ─────────────── -4.2 dB ‹ ] |D| ...
|                      |a| |Q| [ curve ][ history          ][IN GR OUT]
|                      |t| | | [ knobs ...                           ]
```

- The panel shows the selected channel's inserts and strip side by side in signal
  order, each under an uppercase `captionCaps` caption in `textTertiary` (22 px row).
  The strip caption row also holds the position switch (ghost button "After
  inserts ⇄" / "Before inserts ⇄") and "All off" (one undo step).
- A module that is off is shown as a **tab**: 34 px wide, `surface1`, radius `md`,
  the hue as a 2 px top line at 25% alpha, the power button at the top and the name
  running down it in `label`. Clicking the tab expands the card without switching
  the module on; switching a module on (from anywhere) expands it. The expanded or
  collapsed choice is remembered per module type for the session.
- An expanded **card**: `surface1`, radius `md`, 1 px `lineSubtle`, the hue as a
  2 px line along the top (25% when off). Header 28 px: power (`IconButton`, the
  module's hue when on), name in `label`, a live readout on the right in `value`
  (the gain reduction in `meterHot`, the gate's "open"/"closed"), and a collapse
  chevron. Dragging the header reorders the strip with a 2 px `accent` insertion
  line.
- Body: a display well (`canvas`, radius `sm`, 1 px `lineSubtle`) above the
  controls. The display takes what the controls leave (at least 96 px; 140 for the
  EQ). Knobs are 28 px rings in the module's hue with the value under the ring in
  `value` and the name under that in `caption`, on a 60 px grid. Choices are
  segmented controls; two-state options are pills (the hue at 25% when on).
- Off, the body is dimmed (`surface1` at 42% over it) but stays live: the display
  keeps measuring the input, so a threshold can be set against the signal before
  the module is switched on.

## 12.2 Real-time feedback

Every module publishes, from the audio thread, a level history of 200 frames a
second (input peak, output peak, gain reduction, a module-specific level) and the
EQ two signal taps (before and after it). The UI reads them at 60 Hz while the
card is visible. Meters fall at 1.5 × `meterFallDbPerSec`; gain reduction falls
twice as fast. Histories scroll right to left over three seconds.

## 12.3 Gate

- Display: the key level (after the key filters) as a grey area, the output as a
  hue line, the threshold as a dashed line with its value; a 4 px lane along the
  bottom is the hue while the gate is open and `surface3` while it is closed.
- Drag anywhere in the display to set the threshold; double-click resets it.
- Knobs (two rows of four): Threshold, Range, Attack, Hold, Release, Key HP, Key LP.

## 12.4 EQ

- Display (+-24 dB, 20 Hz-20 kHz on a log axis; grid lines at 30, 50, 100, 200,
  300, 500 Hz, 1, 2, 3, 5, 10 kHz with labels below, dB lines every 6 dB): the
  overall response as a 2 px `textPrimary` line, the focused band's own curve
  filled in its hue at 16%, and a node per band (14 px, its hue; hollow when the
  band is off; the selected node with a `textPrimary` ring).
- Band hues: low cut Teal, bands 1-4 Coral, Amber, Lime, Magenta, high cut Violet.
- Nodes: drag for frequency and gain (cuts and notches move in frequency only);
  Shift for fine, Alt for gain only; the wheel sets Q (a cut's slope: 6, 12, 18,
  24, 36 or 48 dB/oct); double-click switches the band; right-click picks the
  shape or slope, switches or resets the band. Dragging a band that is off
  switches it on. A readout above the node shows frequency, gain and Q while it is
  hovered or dragged; elsewhere the pointer's frequency and level show top left.
- Analyser (the "Analyser" pill, on by default, remembered for the session): an
  8192-point Hann FFT of the signal after the EQ as a gradient fill in the hue
  (32% to 6%) and before it as a `textSecondary` line at 45%, on a -96..0 dBFS
  scale tilted +3 dB/octave about 1 kHz so a typical mix reads level; 30 updates a
  second, rising quickly and falling at 40 dB/s. The taps only run while an
  analyser is showing.
- Under the display: a chip per band (cuts wider), filled in the band's hue when
  on; click selects, double-click switches. Under the chips the selected band's
  controls: On, shape (or slope), Frequency, Gain, Q, with Output on the right.

## 12.5 Compressor

- Display: the static curve (a square, input across, output up, -60..0 dB, the
  unity diagonal dashed, the knee shaded, make-up and mix applied) with the live
  detector level riding it as a dot; a three-second history (input grey, output
  in the hue, gain reduction hanging from the top in `meterHot`, the threshold
  dashed); IN, GR and OUT bars (GR on a 0..24 dB scale).
- Drag on the curve left of the knee for the threshold, right of it to bend the
  ratio (the pointer sets the slope above the threshold); drag in the history for
  the threshold.
- Knobs (one row): Threshold, Ratio, Attack, Release, Knee, Makeup, Mix; under
  them the Auto make-up pill and the Peak / RMS detector.

## 12.6 De-esser

- Display: the sibilance band's level as a grey area, gain reduction from the top,
  the threshold dashed and draggable.
- Knobs: Frequency, Threshold, Range; Split / Wide and a Listen pill (hear the band
  alone).

## 12.7 Saturation

- Display: the curve at the current drive, type, trim and mix over -1..1 (unity
  dashed), the part the signal is using right now shaded in the hue with dots where
  its peaks land; IN and OUT bars.
- Type: Tape / Tube / Clip; knobs: Drive, Tone (Dark / Neutral / Bright), Output,
  Mix. Drive is trimmed by half its value on the way out, so more drive is more
  colour rather than only more level.

## 12.8 Limiter

- Display: history (input grey, output in the hue, reduction from the top) on a
  0..-36 dB scale with the ceiling as the draggable line; IN, GR and OUT bars.
- Knobs: Input, Ceiling, Release.

## 12.9 In the mixer

A 16 px row under the input on every strip: a cell per module with its initial
(G E C D S L), the hue at 30% when on (42% on hover) and `surface2` when off; a
click switches the module in place, and modules that reduce gain show it as a
2 px `meterHot` bar along the bottom of their cell (full width at 12 dB).
Right-click offers the same switches and "Edit channel strip", which selects the
channel and shows it in the Editor panel.

## Acceptance

- Editor panel screenshot with a track playing: an expanded EQ with the analyser,
  the compressor showing reduction on its curve, history and GR bar, two modules
  collapsed to tabs.
- Switching a module from a tab, the mixer row and the card's power all land on
  the same state, each as one undo step.
- A project from before the strip opens with every channel's strip present and
  off, without an undo step.
- `--strip demo` with `--master-gain -120` gives every display something to show
  without sound, for unattended screenshots.
