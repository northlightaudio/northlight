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
delay `Amber`. The instruments (7.4b-7.4e) have faces of their own.

- **Gain**: the input and output history with IN and OUT meters; Gain.
- **EQ**: the strip's EQ graph with the built-in's four bands (a high-pass,
  off at 0 Hz, sitting at the left edge; low shelf; mid with Q on the wheel; high
  shelf) and the analyser. The selected band's knobs below under its name.
- **Compressor**: the channel strip compressor's card (the insert and the strip
  module are one compressor): the transfer curve, the history with the threshold
  line, the IN threshold meter, GR and OUT; Ratio, Attack, Release, Knee, Makeup,
  Mix; Auto make-up and Peak / RMS. The insert also takes a sidechain key (its
  menu), tapped before or after the source's fader.
- **Limiter**: the history with the ceiling, the ceiling on the IN meter, GR and
  OUT; Release, Lookahead.
- **Reverb**: the decay: the dry sound, the pre-delay, early reflections (more and
  longer for a bigger room), the tail falling 60 dB over the decay and the highs'
  faster fall (dashed) for the damping. Drag the tail's start for the pre-delay, its
  end for the decay. Size, Damping, Low cut, Width, Mix.
- **Delay**: the echoes over time as bars as loud as the feedback leaves them,
  left up and right down when ping-pong alternates them, darker as the tone falls.
  Drag the first echo for the time, the second for the feedback. Time, Feedback,
  Tone, Mix, a Ping-pong pill and a Sync chip: off (the time in ms) or a note
  value from 1/64 to 1/1, straight, triplet or dotted, at the project tempo.
  Synced, the Time knob dims and shows the note value, and dragging the first
  echo snaps to the nearest one.

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

## 7.4a Instruments: channels, MIDI tracks and the rack

An instrument is not an effect, and its sound is not its MIDI's. An instrument
lives on an **instrument channel**: an audio channel whose **instrument slot**,
ahead of its inserts, takes only instruments (built-in, VST3 or CLAP), and whose
inserts, channel strip, sends, fader and automation process the instrument's main
output. **MIDI tracks** hold the notes (clips, an input, arm and monitoring, MIDI
out) and make no audio: each aims at an instrument channel on a MIDI channel
(1-16). Several MIDI tracks may play one instrument (a multitimbral plugin tells
them apart by channel); a MIDI track's volume scales its notes' velocities, its
mute stops new notes. Each further stereo output of an instrument is an **output
track** with its own processing.

The "+" menu's **MIDI track** submenu offers: **With a new instrument** (any
built-in or scanned instrument), which makes both in one step, the MIDI track and,
just under it, its instrument channel (named for the instrument); **Playing** any
instrument channel already in the project (the MIDI track goes just above it); or
**Playing nothing yet**. Ctrl+Shift+T and the empty project's "Add MIDI track" make
a MIDI track with a new Aurora channel. The insert menus list
effects only; an instrument chosen from the browser goes to the selected
instrument channel, the one the selected MIDI track plays (or a new channel for
it, when it plays none), or a new MIDI track and instrument channel.

- **Arrange**: a MIDI track's header has no meter, solo or automation; in their
  place a chip says what it plays ("→ Drums: Pulsar  ch 2"); a click (or "Plays..."
  in its menu) offers every instrument channel, Nothing, the MIDI channel and New
  instrument. It has no Send to. An instrument channel's header has no input or
  arm; its lane reads "Instrument channel: Pulsar ← Drums, Fills". An output
  track's lane reads "Output 3/4 of Drums: Pulsar".
- **Editor panel sends**: a SENDS section ends the strip, after the inserts and
  the channel strip: a card per send with the bus's colour and name, a vertical
  fader (the mixer's law) with the level under it, Pre/Post and a remove button,
  then a "+ Send" slot listing the FX tracks and groups the channel can feed (not
  ones that would loop back into it; ones it already feeds are ticked). A MIDI
  track has no sends; its instrument channel's show instead.
- **Editor panel**: selecting a MIDI track shows the instrument channel it plays,
  titled "MIDI 1 → Drums: Pulsar"; a MIDI track that plays nothing shows a
  "+ Instrument" slot that makes a channel for it. An instrument channel shows an
  INSTRUMENT section first, before the strip and the inserts: the instrument's
  card (its own editor for a built-in, Open for a plugin with a GUI) with Replace
  and Remove in its menu and no wet/dry footer. The card is not reordered with the
  inserts.
- **Mixer**: strips for the channels that make audio (MIDI tracks have none). An
  instrument row under the module row (reserved on every strip when any has one,
  so the sections stay aligned): the slot with an INST / VST3 / CLAP badge, or
  "+ Instrument". Its menu: Open, Bypass, Outputs, Replace instrument, Remove
  instrument.
- **Instruments panel** (dock tab, F8): the rack. A unit per instrument channel,
  64 px, a `surface1` panel with the channel's hue down its left edge: power (in
  the hue), the instrument's name with a BUILT-IN / VST3 / CLAP badge, the channel
  and who plays it ("Drums: Pulsar ← Drums, Fills (ch 2)"), OUTPUTS as chips, Edit,
  and a menu (open, select track, replace, remove). Each output pair is a chip:
  "1/2 Drums: Pulsar" for the main pair (the channel itself) and each pair with an
  output track (click selects it), a dashed "+ Track for 3/4" for a pair without
  one (click makes it). "+ Instrument" at the end makes an instrument channel with
  the instrument chosen and a MIDI track to play it.

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

## 7.4c Pulsar

The drum synth bundled with Aural, Aurora's sibling for percussion: eight
synthesised pads on the General MIDI drum notes (kick C1, rim C#1, snare D1,
clap D#1, closed hat F#1, low tom A1, open hat A#1, high tom C2) and on C3 to G3
in that order, played out whatever the note's length; the closed hat chokes the
open one. Each pad: Tune, Decay, two shaping controls named for the sound (kick
Click and Punch, rim Ring and Click, snare Snappy and Crack, clap Bright and
Spread, hats Bright and Noise, toms Noise and Punch), Level, Pan and Output: the
main pair or an extra one (3/4 to 15/16). Pulsar has as many extra outputs as its
pads use, each named for the pads on it ("Kick", "Closed hat + Open hat"), and
each becomes a track of its own from the rack's chip or the mixer's Outputs menu,
named that way. The master: Drive, Velocity, Level (applied to every output). Kits: Pulsar, Boom Room, Tight Electro, Dusty, Minimal
Clicks, Industrial.

- Deep space (the canvas darkening around the star) with stars, and the pulsar in
  the top-left corner: a white core, a `Sky` halo and two beams that turn slowly
  while it plays and flare on every hit, which also sends a ring in the pad's hue
  out across the sky behind the glass. Still when silent or with reduce motion on.
- The wordmark PULSAR tracked wide in a Sky-to-Coral gradient, "DRUM SYNTH ·
  AURAL", and the kit bar (previous, the name as a menu, next; "Custom" off-kit).
- Eight pad tiles (4×2) of glass, each in its hue (Coral, Amber, Tangerine, Lime,
  Mint, Teal, Sky, Indigo): its short name, its note, and its sound, rendered and
  drawn as a waveform; a hit lights the tile from within and its rim. Clicking a
  tile selects it (a stronger rim).
- The selected pad's panel: its name and note, its output in a chip at the
  caption's right, its sound drawn large over a time grid (drag across for the
  decay, up and down for the tune, Shift for fine), and its six knobs. A tile whose
  pad has its own output shows the pair ("3/4") in a chip at its bottom right.
- "Outputs" beside the kit bar: all to the main output, one output per pad, or
  four groups (kick; snare, clap and rim; the hats; the toms).
- MASTER: Drive, Velocity, Level and a scope of the output.

## 7.4d Nebula

The pad synth bundled with Aural, for slow, airy, ethereal sounds. Each note is a
cloud of partials (4 to 24), each a pair of sines detuned by the spread and
wandering on its own with the drift, panned alternately left and right (wider the
higher), shaped by Brightness (the spectrum's tilt), Odd/even (hollow to full),
Stretch (towards a bell) and a vowel Formant; a Halo of partials an octave up, a
Sub and Breath (noise around the note); a slow envelope; Motion, an LFO sweeping
the brightness, the formant and the partials' pans; a gentle Tone low-pass; and
Space, a large eight-line reverb. Eight voices. Presets: Nebula, Choir of Dust,
Glass Halo, Hollow Moon, Solar Breath, Deep Field, Ice Bells, Drifting Veil.

- Gas clouds in Violet, Magenta, Teal and Indigo drifting behind the panels and
  breathing with the sound, dust rising through them; still when silent or with
  reduce motion on. The wordmark NEBULA, light and tracked wide in a Magenta-to-Teal
  gradient on a soft glow; "PAD SYNTH · AURAL"; the preset bar; eight voice lights.
- SPECTRUM: the partials as columns of light on a log axis (55 Hz to 12 kHz, for
  A3 and the octave), the halo's in Mint, under the formant's cloud, swaying with
  the motion; drag across for the vowel, up and down for the brightness; the octave
  in a corner chip. Partials, Bright, Odd/even, Stretch, Vowel, Formant.
- CLOUD: where the partials sit in the stereo field (left to right, low to high),
  each a pair of dots as far apart as the spread, wandering with the drift. Drift,
  Spread, Width, Halo, Sub, Breath.
- ENVELOPE (draggable), MOTION (the wave with the live value), SPACE (the tail as
  long as the size and as bright as the mix; drag for both) with their knobs.

## 7.4e Starbit

The chiptune synth bundled with Aural, after 8-bit consoles' sound chips. A
pulse (12.5, 25, 50 or 75% duty, with a duty sweep that cycles them), a 4-bit
stepped triangle and saw, LFSR noise (long, or short and metallic, pitched with
the note) and a 32-step 4-bit Wave you draw; volume in 16 steps; arpeggios in
tracker style (Chord cycles the held notes on one voice; Octave, Major, Minor,
Power, Maj7, Min7 cycle a shape over each note) at a rate in steps a second;
vibrato with a delay, a pitch sweep into each note and glide; Mono (newest note,
legato) or four voices; Bits, Rate (sample and hold) and a chip Echo. Presets:
Starbit, Hero Lead, Chord Arp, Major Sparkle, Tri Bass, Wave Pad, Laser Zap,
Noise Snare, Metal Hat, Crunchy Saw.

- Pixel art: a 5×7 pixel-font wordmark STARBIT, each letter a palette colour
  with a drop shadow, "CHIP SYNTH · AURAL" in mono, square stars that blink in
  whole steps a few times a second while it plays, windows and screens with
  notched corners and 2 px borders, scanlines over the screens, four square voice
  lights. The knobs are the app's own.
- WAVE: the cycle as 32 columns of 16 cells; the shape, the duty (for the pulse)
  and the noise mode (for the noise) in chips; with Wave chosen, click and drag to
  draw the steps. Duty sweep, Octave, Coarse, Velocity.
- VOLUME: the envelope as 16-step bars. ARP: the steps as blocks as high as their
  offsets, the one playing lit; the mode and Mono/Poly in chips; a Sync chip under
  Rate makes each step a note value at the project tempo, on the song's grid
  while it plays (the Rate knob dims and shows the value). PITCH: the sweep
  and the vibrato over a note's first second. LO-FI: a sine as the bits and rate
  leave it.

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
