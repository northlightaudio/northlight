# 7. Inserts, plugin windows, browser

## 7.1 Insert chain panel (Editor tab, dock)

Selecting a track or bus and opening the Editor tab shows its chain as a horizontal
strip of device cards, so the whole signal path reads left to right.

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
- Body: for built-ins, the built-in's own editor (7.2a); for hosted plugins with a
  GUI, a thumbnail placeholder with an "Open" primary button (opens the plugin
  window) and the three most important parameters as knobs if the plugin exposes
  them; for hosted plugins without a GUI, the generic editor.
- Footer 24 px: wet/dry mini slider with "Mix 100%".
- Bypassed card: body at 50% alpha, power icon `textTertiary`, header name struck.
- Drag the header to reorder; a 2 px `accent` insertion line between cards. Drag a
  plugin from the browser onto the strip to insert at the line.
- The "+" card is a ghost card that opens the plugin menu (recent, built-ins,
  favourites, then by manufacturer).
- Sidechain: a small "key: 02 Hats" chip under the header when set, `accentSubtle`
  fill, click to change.

## 7.2 Plugin window

Native window with a 32 px Northlight bar above the plugin's own editor:

```
| ⏻  DC1A3 · 01 Kick     VST3   mix ──o── 100%   presets ▾   ⟲   📌   ⤢ |
```

- Bar: `surface0`; power toggle, "Plugin · Track" title in `label` (track name in
  `textSecondary`), format badge, wet/dry slider (80 px) with value, presets combo
  (factory programs and user presets, with Save… at the bottom), an "A/B" toggle
  (**Decision**: deferred, keep the slot), pin (always on top) toggle, and a
  generic-editor toggle for plugins that have both.
- The plugin's editor sits below at its native size; the window follows plugin
  resize requests; DPI scaling is passed through.
- Position and size remembered per plugin type; pin state remembered.
- Closing the window never unloads the plugin; the card in the chain shows "open" as
  an `accent` dot on its header while the window is open.

## 7.2a Built-in editors

Every built-in has an editor in the channel strip's language
([channel-strip.md](channel-strip.md) 12.1-12.2): a live, draggable display in a
well, then knobs in the module's hue. The same editor is the card's body in the Editor panel
and the body of its window (under the title, bypass and mix). The hues follow the
strip: EQ `Overcast`, compressor `Loch`, limiter `Rowan`; gain `Machair`, reverb `Lichen`,
delay `Gorse`, glue compressor `Thrift`, Patina `Kelp`, exciter `Machair`, Bloom `Heather`, tape echo `Rowan`, stereo widener `Sea grass`. The instruments (7.4b-7.4e) have faces of their own, and so does Patina, a hardware face (design-system.md "Hardware face").

- **Gain**: the input and output history with IN and OUT meters; Gain.
- **EQ**: the strip's EQ graph with the built-in's four bands (a high-pass,
  off at 0 Hz, sitting at the left edge; low shelf; mid with Q on the wheel; high
  shelf) and the analyser. The selected band's knobs below under its name.
- **Compressor**: the channel strip compressor's card (they are one compressor):
  the transfer curve, the history with the threshold
  line, the IN threshold meter, GR and OUT; Ratio, Attack, Release, Knee, Makeup,
  Mix; Auto make-up and Peak / RMS. The insert also takes a sidechain key, tapped
  before or after the source's fader: a Sidechain button beside Peak / RMS picks
  it (the same items as the slot menu) and, while a key is set, lights in the
  module's hue as "Key: Kick (post)"; the threshold meter's caption reads KEY
  and it, the history's grey trace and the threshold all follow the key.
- **Limiter**: the history with the ceiling, the ceiling on the IN meter, GR and
  OUT; Release, Lookahead.
- **Glue Compressor** (a console-style VCA bus compressor, for a mix or a group):
  feed-forward and stereo linked with a 4 dB soft knee; Ratio 1.5, 2, 4 or 10:1;
  Attack 0.1, 0.3, 1, 3, 10 or 30 ms; Release 0.1, 0.3, 0.6, 1.2 s or Auto (a fast
  stage releasing over 0.1 s and a slow one that charges from it over 0.4 s while the
  compression is held and
  releases over 1.2 s: quick back from a hit, slow back from held compression);
  Key HP on the detector (off at 20 Hz, up to 400 Hz) so the low end does not pump
  it; Makeup 0 to 20 dB; Mix for parallel compression. No latency. Its editor shows
  what glue does rather than a transfer curve:
  - **Dynamics ribbon** (the display, flexible width): the last three seconds,
    each column a band from the 50 ms RMS (the body) to the peak, the input
    `textTertiary` at 32% under the output in the hue at 42%, with the output's
    RMS as a 1.5 px line in the hue; -48..0 dB with the dB grid every 12 dB; peaks
    fall 30 dB a second across the columns, as the other histories. The two bands
    show what the settings do to the distance between body and peaks: a fast
    attack holds the peaks and the coloured band sits thinner; a slow attack lets
    the transients through over a lowered body, and it sits wider. The threshold is the
    dashed line with its value (drag anywhere in the ribbon, double-click resets).
    At the top left, **CREST** in `captionCaps` and the crest factor (peak over
    RMS) of the last second going in and coming out, "14.2 → 9.6 dB", the input
    figure `textSecondary`, the arrow `textTertiary`, the output in the hue; "–"
    in silence.
  - **GR arc** (to its right, up to 120 px, centred in its well with room for its
    labels): ten LED segments over 240°, 7 px thick, for 0.5, 1,
    2, 3, 4, 6, 8, 10, 14 and 20 dB, lit in the hue (`meterHot` past 10 dB),
    `surface2` when dark; the deepest reduction of the last `motion.peakHoldMs`
    holds as an outlined segment, then falls at 20 dB a second; 1, 4, 10 and 20
    labelled outside in `caption`; the reading in `value` in the middle (`meterHot`
    while reducing) over GR in `captionCaps`.
  - Controls, in three groups split by `lineSubtle` rules: Threshold and Makeup as
    46 px knobs with 0-10 scale dots; Ratio, Attack (ms) and Release (s) as rotary
    selectors (design-system.md); Key HP ("Off" at the bottom) and Mix as knobs.
    Every control's label sits on one baseline along the bottom of the row.
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
- **Patina** (a vintage colour processor: six stages that age a sound in
  different ways, under one Age control): in signal order **Heat** (drive into
  a Tube, Tape, Iron or Fuzz curve, anti-aliased by ADAA and level-held, with a
  tilt), **Chamber** (a Room, Spring or Plate, mixed over the sound), **Drift**
  (wow and flutter of a Reel, a cassette Deck or a Disc, with a rate and a
  stereo spread), **Grain** (Vinyl crackle, Tape hiss, 50 or 60 Hz Hum, or room
  Air, which can duck under the music or ride with it), **Wear** (lost highs,
  dropouts, azimuth skew) and **Crush** (bits and rate, Soft, Hard, Mu-law or
  Jitter). Each stage has a switch, a main amount and a Breath that lets the
  amount wander at the Pace beside Age. **Age** scales every stage's amount:
  half way plays them as set, down to nothing makes the sound new, up to full
  takes each toward ruin in proportion to how much of it is used. An output
  section follows: Tone (a tilt about 900 Hz), Low cut and High cut ("Off" at
  their ends), Width and Output. Every switch and choice fades over 10 ms. No
  latency. Twelve factory presets (Gentle Age, the defaults, first). Its editor is
  a hardware face (design-system.md "Hardware face"), a worn 1960s studio rack
  unit, and shows what each stage is doing, live:
  - **The panel** fills the body (radius `md`): worn hammertone enamel
    (`assets/patina/panel-aged.jpg`, cover), the same at every Age; its edges
    rubbed back toward bare metal in `panelWear`, a 20 px band at 70% fading
    inward; a 1 px `highlight` at 12% along the top. Without the image, flat
    `panel`. It is drawn once per size and scale.
  - **The top row** (40 px). At the left the **nameplate**, nine-sliced from
    `nameplate.jpg`: its screw ends drawn 14 px square, the edges and middle
    repeated at that scale, so the screws stay round and the wear even at any
    width (`plate` without the image). Lettered in `ink`: PATINA at 21 px
    semibold with 0.3 tracking at 85%, a 1 px rule at 35%, then VINTAGE COLOUR
    / PROCESSOR at 9 px semibold with 0.18 tracking at 80%; a 2 px `shadow` at
    60% under the plate. At the right the **pilot lamp**: 26 px, `lamp.jpg` in
    a circle with a `lamp` glow at 50% out to its 40 px box while the insert is
    on; bypassed, shaded `shadow` at 60% with no glow. Left of the lamp, 12 px
    away, the **preset selector**: a 280 x 32 px recess (`bezel`, radius 5, a
    4 px `shadow` shade under its top lip, a 1 px `highlight` at 14% below it)
    holding two 28 px bakelite keys round an amber display. A key: radial
    `keyTop` to `keyBottom`, radius 3, a `highlight` line at 14% along its top
    and a `shadow` line at 50% along its foot, a `keyGlyph` triangle at 75%
    (95% on hover); pressed, it drops 1 px and loses its lines. The display:
    radial `displayCentre` to `displayEdge`, radius 2; the preset's number
    ("07", "--" for Custom) in `value` at 85% size in `displayDim` at 75%, then
    its name in capitals in `value` at 105% size, 0.06 tracking, `displayText`
    over a `displayGlow` halo at 22% (brighter on hover); scan lines every 3 px
    (`shadow` at 12%), a `highlight` reflection at 13% across its top left and
    a 4 px shade under the bezel's lip. The keys step through the same list as
    the header's preset bar (7.2b), the user's presets too (wrapping; from Custom,
    Next goes to the first); a click on the display shows the preset menu. A preset is one undo step ("Preset
    Worn Cassette"); any change of a value makes it Custom. Tooltips:
    "Previous preset", "Next preset", "Presets: <name>"; the lamp "On" or "Off:
    Patina is bypassed". The keys and display take focus and draw the focus
    ring.
  - **Stage windows** (flexible height, six across, 12 px apart). A window has
    a 20 px header with its name in `captionCaps` `legend` (70% when the stage
    is off) and, just after the name, the stage's **slide switch**: 22 x 12 px,
    a `switchDark` slot with a `switchLight` lever in its right half when on and
    its left when off, a 1 px `shadow` at 60% round it. Under the header the
    window: a 4 px bevelled frame (`glassFrameTop` to `glassFrameBottom`,
    radius 4, a `highlight` line at 12% at its foot) round the glass. Lit (the
    stage on), the glass is a radial `glassCentre` through `glassMid` to
    `glassEdge` and the readout draws in `glassInk`; unlit, `glassOffCentre` to
    `glassOffEdge` with the readout in `glassMid`. Where the readouts below say
    the hue, read the ink; `textSecondary` and `textTertiary` are the ink at
    55% and 50%, the grid the ink at 16%, and the ring of Heat's dot the
    glass's centre colour. The figure sits at the top right; the stage's choice
    is printed at the glass's foot in `captionCaps`, the ink at 70% ("TUBE",
    "SPRING"). Over the glass, a 6 px `shadow` shade at 45% under the frame's
    lip and a `highlight` reflection (22% lit, 10% unlit) across its top left.
    Under the window, a row of eight 9 x 5 px lamps, 3 px apart: lit `led` (the
    last two `ledHot`) with a 35% halo up to the amount Age and Breath leave
    right now, dim (the colour at 40%) on to where Age alone puts it, the rest
    `ledOff`. The window is the stage's tab: hover rings the frame in
    `highlight` at 30%; selected, a 1.5 px `led` ring with a 3 px `led` halo at
    25%. Click selects; Left and Right move between windows, Enter selects,
    Space switches the stage; focus draws the focus ring round the frame. The
    readouts:
    - Heat: the static curve at the live amount (input across -1..1, output
      up, in a square), the unity diagonal dashed, the loudest input of the
      last 50 ms as a dot riding the curve (a 7 px `textPrimary` disc round a
      4 px hue one); the figure: how much the curve took off the peaks over the
      last 200 ms ("−2.7 dB", the hue while above 0.1 dB, else `textTertiary`).
    - Chamber: the wet level over three seconds (-60..0 dB, a grid line every
      20 dB) as a hue area at 32% under a 1.5 px hue line; the figure: the decay
      time ("2.1 s").
    - Drift: the speed error over three seconds in cents, the left side a
      1.5 px hue line and (with Stereo above zero) the right a 1 px
      `textSecondary` line at 70%, about a centre line with dashed limits; the
      scale grows to fit what plays (±5, 10, 20, 40 cents, its label at the top
      left in `caption` `textTertiary`); the figure: the widest swing of the
      last second ("±8.7 c").
    - Grain: the noise's RMS as a hue area at 36% with a 1 px hue edge and the
      music's peak as a 1.5 px `textSecondary` line, -84..0 dB, grid every
      24 dB, so ducking and riding show; the figure: the noise level ("−38 dB",
      "–" when silent).
    - Wear: the surviving bandwidth (the high cut on a log axis, 1 to 20 kHz,
      grid at 2, 5 and 10 kHz, "20k" at the top left) as a hue area at 26% under
      a hue line; each column keeps its lowest value so dropouts show as
      notches; the figure: the cutoff ("11 kHz"), or the depth of a dropout
      while one plays ("−14.2 dB", in the hue).
    - Crush: a scope of about six of the converter's samples from a rising zero
      crossing: what goes in as a 1 px `textSecondary` line at 60%, what comes
      out as 1.5 px hue steps; the figure: the bits and rate in effect
      ("6 bit · 6.0 kHz").
  - Controls, one row under the windows in three groups split by engraved rules
    (a 1 px `shadow` line at 55% beside a 1 px `highlight` at 10%), every label
    on one baseline. Every knob is the hardware pointer knob and every choice
    the hardware rotary selector, their labels in `legend`: **Age** as a 56 px
    knob and **Pace** ("0.16 Hz"); the selected stage's controls under its name
    printed in `captionCaps` `legend` at 85% (left aligned, in a group as wide
    as the widest stage's); the output knobs. Each stage: a rotary selector for its
    choice where it has one, its main amount as a 46 px knob, then 28 px knobs:
    Heat: Character (Tube, Tape, Iron, Fuzz), Drive, Tone, Breath. Chamber:
    Space (Room, Spring, Plate), Mix, Decay (in seconds for the space), Tone,
    Breath. Drift: Machine (Reel, Deck, Disc), Wow, Flutter, Rate ("1.00×"),
    Stereo, Breath. Grain: Source (Vinyl, Tape, 50Hz, 60Hz, Air), Amount, a
    texture knob named for the source (Density in crackles a second for Vinyl,
    Colour for Tape, Buzz for Hum, Tone for Air), Follow ("Duck 40", "Steady",
    "Ride 40"), Breath. Wear: Loss, Dropouts, Skew, Breath. Crush: Flavour
    (Soft, Hard, μ-law, Jitter), Bits ("12 bit"), Rate ("22 kHz"), Breath.
    While the selected stage is off its controls fade to 50% and stay usable. The selected stage is remembered
    for the session.
- **Exciter** (adds harmonics to flat sources; psychoacoustic bass):
  - **How it works.** The band above Frequency (700 Hz-16 kHz, 24 dB/oct) is
    normalised to a fixed level and driven into one of four curves:
    - Tube: the octave and the twelfth, warm.
    - Tape: odd orders, an edge.
    - Air: mostly the octave, placed an octave above the crossover.
    - Crisp: a dense even series, bright.

    Only what the curve adds is mixed back: its linear part is taken out, at a
    level calibrated per curve and drive. So Drive sets the character alone,
    and Amount sets the harmonics against the band: 50% is 6 dB under it, 100%
    is 6 dB over it.
  - **Anti-aliasing.** The curves run at twice the rate (polyphase IIR) with
    ADAA, on the harmonics only. No latency.
  - **Gate.** Below -84 dBFS of band level nothing is excited.
  - **Focus** tilts what is added: Dark to Edge, ±4 dB about three times the
    crossover.
  - **Dynamics** runs from Fill (more in the quiet moments and tails) through
    Static to Push (more on the loud moments), against the band's 600 ms
    average. **Transients** adds up to 15 dB more on onsets.
  - **Low end.** Harmonics of the mono band under Bass Freq (30-250 Hz), kept
    between it and eight times it. Amount 100% matches the low band.
  - **Output stage.**
    - Width spreads the harmonics into a decorrelated side copy; the mono sum
      never changes.
    - Mix scales everything added.
    - Output.
    - Auto gain (on by default) turns the output down by the K-weighted
      loudness the exciter adds, never up.

  The editor shows what is added, where and when:

  - **Harmonic spectrum** (the display, flexible width; -96..0 dBFS tilted
    +3 dB/octave about 1 kHz; 20 Hz-20 kHz on the EQ's grid and labels;
    dB lines every 24 dB in `gridBeat`):
    - **Traces.** An 8192-point Hann FFT of the processor's two taps, 30 times a
      second, rising quickly and falling at 40 dB/s:
      - the input as a `textTertiary` fill at 16% under a 1 px `textSecondary`
        line at 45%;
      - what is added as a gradient fill in the hue (45% to 8%) under a 1.5 px
        hue line.

      Where the colour rises out of the grey, the exciter has made energy that
      was not there.
    - **Regions.** The region above the crossover and the region below the bass
      frequency are tinted with the hue at 7% while their section adds anything.
    - **Handles.** Each frequency is a vertical handle:
      - Active: a 1 px hue line at 70%. While its section is at 0%: dashed
        `textTertiary` at 60%.
      - Hover or drag: `textPrimary` at 85%, 1.5 px.
      - The shared 9 px handle disc sits 12 px above the plot's bottom
        (`textPrimary`, or `textSecondary` while inactive) with a 1.5 px hue
        ring (2 px on hover).
      - Its frequency is in `value` beside it, on the side with room, on a
        `meterWell` plate at 80% (radius `sm`) so traces never cross the figures.
    - **Mouse.** A press moves the nearer handle to the pointer and drags it (one
      gesture), and double-click resets the nearer one. The cursor is a
      left-right resize within 6 px of a handle.
    - **Legend.** Top left: 8 px rounded swatches with "Input" and "Added" in
      `caption`.
    - **Empty state.** When the analyser has nothing: "Play something to see
      what is added" in `caption` `textTertiary`, centred.
  - **Excitation history** (to its right, 208 px or a third of the display):
    - The last three seconds:
      - the input's 50 ms RMS as a `textTertiary` area at 32%;
      - the added RMS as a hue area at 42% with a 1.5 px hue line.
    - Range -72..0 dB, the grid every 12 dB with labels at the right as in the
      Glue's ribbon.
    - Static dynamics keep the two a set distance apart. Push widens the gap on
      loud moments, Fill on quiet ones, and Transients on each onset.
    - Top left: **ADDED** in `captionCaps`, then the added level against the
      input over the last 300 ms in `value` in the hue ("-26.8 dB"); "–" in
      `textTertiary` in silence.
    - The card header's live readout is the same figure in the hue.
  - **Controls,** in four groups. Each group has a `captionCaps` caption in
    `textTertiary` on a 16 px row above it (HARMONICS, LOW END, DYNAMICS,
    OUTPUT), and `lineSubtle` rules between the groups run from the caption row
    to the controls' foot:
    - HARMONICS: Character, a rotary selector (Tube, Tape, Air, Crisp); Amount, a
      46 px knob with 0-10 scale dots; Drive; Frequency; Focus ("Dark 40",
      "Flat", "Edge 40").
    - LOW END: Amount, a 46 px knob with scale dots; Frequency.
    - DYNAMICS: Dynamics ("Fill 35", "Static", "Push 60"); Transients.
    - OUTPUT: Width; Mix; Output; Auto gain. Auto gain is a "Match" pill where
      a knob's ring sits; under it, where a knob shows its value, the live trim
      ("-1.8 dB", or "Off" in `textTertiary`), then its label.

    Every label sits on one baseline along the bottom of the row. Every control
    has a tooltip that says what it does to the sound.
  - **Size.** The card is 939 px wide at 100% and its body is 300 px tall.
- **Bloom** (an extreme reverb for use as an effect: huge, frozen, shimmering,
  swelling, gated or ducked tails): a feedback delay network of eight modulated
  lines with an allpass in each, mixed by a Householder matrix. Decay 0.4 s to 60 s
  (RT60); Size from a large room to an absurd space (5 to 88 m), gliding like tape;
  Freeze holds the tail for good, the input kept out, the shimmer and damping out of
  the loop and a little modulation always on so it keeps moving. Shimmer pitch-shifts
  part of each pass inside the loop (Pitch -12, +5, +7, +12, +19 or ±12 semitones),
  so the tail climbs over seconds; Swell feeds the input in through a rising cloud
  of taps so the tail builds like a reversed reverb (off, or 10 ms to 4 s); Gate cuts
  the tail a set time after the input stops (off, or 40 ms to 2 s; never while
  frozen); Duck lowers it under the dry input (up to 24 dB, Release 50 ms to 3 s).
  Low cut and Damping act inside the loop as shelves: lows and highs past them decay
  four times faster than the decay at any decay time. Tilt ±6 dB about 900 Hz after
  the loop, Pre-delay 0-500 ms or a note value (Sync), Diffusion, Mod depth and Rate,
  Width, Mix. A governor holds the network's energy under a ceiling, so no setting
  runs away. No latency; bypassed, the network keeps running so the tail is current
  when it comes back. Its editor shows the tail as a blossom:
  - **The face**: the card's body filled `surface0` (radius `md`); the controls'
    tiles `surface1`, radius `lg`.
  - **Blossom** (the display, at the left, as tall as the body; its field 1.32 times
    as wide as it is tall so the corner readouts clear the flower). The tail alone
    (tap 1, before the mix) over the last five seconds as a flower: time runs
    clockwise over 330 degrees to now at the top (a 15 degree gap either side, a
    1.5 px `textPrimary` line and a 6 px dot at 12 o'clock), frequency rises
    outward, 50 Hz to 16 kHz on a log scale, from a quarter of the radius to 0.885
    of it. 56 slices by 14 bands of petals, each separated by a hairline (0.7
    degrees, narrowing outward; 1.2 px between bands), each band the mean power of
    its bins (2048-point FFT, Hann), -96 to -24 dB, coloured as the hue fading in
    (alpha `v^1.4 x 0.95`, at most 92%) and warming toward `textPrimary` at the top
    20%. A slice heard while frozen is coloured in ice (`accent` 40% toward
    `textPrimary`), so a held tail turns to ice slice by slice. The petals are drawn
    through a per-pixel lookup into an image held at twice the flower's size, redrawn
    only when a slice arrives. Ground: a radial gradient from `canvas` 13% toward the
    hue (ice while frozen) at the heart through 4% at 70% to `canvas` at the rim.
    Dotted rings (`textPrimary` at 7%) at 100 Hz, 1 kHz and 10 kHz, labelled in the
    gap at the top in `caption - 1` mono `textTertiary`. **Rim**: the tail's RMS per
    slice (-60..0 dB) as a band in the hue at 85% on a 0.906 radius track
    (`textPrimary` at 6%), up to 0.047 of the radius deep; the dry input's RMS as a
    1.5 px `textSecondary` tick outside it, for slices above a fifth of the scale.
    **Shimmer** (above 2%, not while frozen): three dotted spirals (1 px dots every
    5 px, 1.2 px) in the hue at 20% + 45% of the amount, climbing outward (falling
    inward for -12; one of three inward for ±12). **Corners**: top left DECAY
    (`captionCaps` `textTertiary`), the RT60 in mono `title + 1` `textPrimary` and the
    highs' ("2.7 s at 8 kHz", `value` `textSecondary`); frozen, "∞" over "held". Top
    right SPACE, the size in metres and the pre-delay ("20 ms before", or the note
    value). Bottom left the legend: a 9 px hue dot TAIL, a 9 x 2 px `textSecondary`
    bar DRY; bottom right "5 S · NOW AT THE TOP"; captions `captionCaps`
    `textTertiary`.
  - **The seed** (Freeze), at the flower's heart, 84% of the inner radius's
    diameter plus 16 px: a round button. Off: a `canvas` moat at 90% with a hue glow
    (22% fading out) round it, a body from `canvas` 14% toward the hue to 5%, a
    1 px hue rim at 40% (70% on hover), a 1.5 px snowflake and FREEZE (`captionCaps`)
    in `textPrimary` at 85% (full on hover). Held: an ice glow at 45%, an ice body
    (brighter at the top), a 1 px rim of ice brightened, the snowflake and FROZEN in
    `canvas`. Pressed darkens the body. Click, Enter or Space toggles it (one undo
    step); focus draws the focus ring round it; its hit area is the circle.
  - **Large controls** (right, 644 px wide in all): Decay ("6.0 s"), Size ("27 m"),
    Shimmer (%) and Swell ("Off" at the bottom) as 52 px bud knobs (design-system.md
    "Bud knob") in four equal tiles, their names in `label` `textSecondary`. Frozen,
    Shimmer and the interval pills fade to 50% (the shimmer has left the loop).
  - **Shimmer interval** (a tile, the row's width less the next tile): SHIMMER
    INTERVAL in `captionCaps` `textTertiary`, then six 26 px pills 6 px apart: -12,
    +5, +7, +12, +19, ±12 in `value`. The chosen pill is filled in the hue with its
    text in `canvas`; the others have a 1 px `textPrimary` outline at 15% and
    `textSecondary` text, filled `surface2` with `textPrimary` text on hover. Click a
    pill, or Left, Right, Home and End; focus rings the chosen pill. Tooltip
    "Shimmer interval".
  - **One hit** (a 196 px tile beside it): ONE HIT, then the envelope: what one hit
    becomes, -60..0 dB: the dry hit (a 2 px `textTertiary` bar at 0), the pre-delay,
    the swell rising, the decay falling 60 dB over the RT60 (a 1.5 px hue line over a
    hue gradient from 32% to 4%), the highs' faster fall dashed (hue brightened,
    80%), the gate's vertical cut; frozen, a flat line to the edge with ∞ in the
    hue. Grid at -20 and -40 dB (`gridBeat`). In a well under 56 px tall it is
    compact: the curve fills the well (5 x 4 px in) and only the span shows, at the
    top right, hidden while frozen; taller, 0 and the span sit under it.
  - **Secondary controls** (28 px bud knobs in 50 px cells) in one row of four
    groups 14 px apart, each under a `captionCaps` `textTertiary` caption and a 1 px
    `textPrimary` hairline at 8% running to the group's end: SPACE (Pre-delay,
    Diffusion, Motion, Rate; the Sync chip, a CornerChoice, at the caption's right,
    the hairline stopping 8 px short of it: "Sync off" or the note value, and synced
    the Pre-delay knob dims and shows the note), TONE (Low cut, Damping "6.0 kHz",
    Tilt), DYNAMICS (Duck, Release, Gate, "Off" at the bottom), OUTPUT (Width, Mix).
  - Header readout: "Frozen" in ice while frozen, else the ducking ("Duck -4.2 dB",
    `textSecondary`) while it ducks.
  - Body 316 px; width the flower's field (1.32 x the body's height less 8 px), a
    20 px gap and the 644 px of controls.
- **Tape Echo** (a tape loop machine, the Delay's characterful sibling):
  - A record head and four playback heads at 1, 2, 3 and 4 times head 1's time. Eight
    head combinations: 1, 2, 3, 4, 1+3, 2+4, 1+3+4, all.
  - Feedback is the active heads' mean, so 100% is the edge of oscillation; it goes
    to 120%.
  - Time is the motor speed (head 1, 20-1000 ms). Synced, it is head 1's note value.
    A change glides like a motor with a flywheel: the echoes' pitch bends while it
    settles, with no crossfade.
  - Wow and Flutter wobble the tape speed, with randomness. Flutter's rate follows
    the motor.
  - Every pass round the loop goes through the record path again: the Bass and
    Treble shelves, the tape's saturation with pre- and de-emphasis (anti-aliased),
    and the tape's high loss (darker with Age and at slow speeds). Hiss breathes
    under the echoes and dropouts travel round the loop, both growing with Age. Each
    repeat therefore darkens and wears.
  - Past the edge, the loop sustains into the saturation, bounded.
  - Drive sets the record level, and the echoes are compensated by half.
  - Stereo is Pan (the active heads alternate left and right; a lone head is
    centred), Ping (ping-pong) or L/R. Spread sets the width.
  - Spring is a send to a short dispersive spring (its chirp and drip) on the echoes
    only.
  - Mix, Output, and a Kill dry pill for use on a send. No latency.
  - **The machine** (the display, the card's full width; about 154 px tall at the
    card's 250 px body):
    - **The tape.** The loop's centre line runs from R right along the bottom past
      the heads, round four 13 px rollers (the bottom-right is the capstan, with its
      pinch roller), and back along the top. The top run is slack and sags 1-6 px
      with Wow, at the wow rate, plus a 1 px flutter shimmer.
    - The tape is 3 px `surface3` (`textTertiary` while hovered or dragged).
    - What the record head wrote (the frames' `extra`) is drawn on the tape at its
      age. Its distance from R is age ÷ the motor's head-1 time, in head units. It
      shows as a band 3-12 px wide over the bottom 42 dB, in the hue mixed
      opaquely into the well at 30-100%, fading by up to 45% round the loop.
    - Each echo can be watched travelling from R to the heads.
    - 1.5 px `textTertiary` marks every 14 px move with the tape at the motor's
      speed, and are hidden when they would move more than half their pitch in a
      frame. The rollers' three spokes turn with the tape.
    - **The heads.** 14 × 10 px blocks under the bottom run with a 1.5 px gap line.
      - R is `surface3` with a `textSecondary` rim, its gap lit in the hue by what
        it writes.
      - Active heads are filled in the hue, with a `textOnColour` gap line and a
        22 px halo in the hue that flares as an echo passes.
      - Inactive heads are `surface2` with a `lineStrong` rim.
      - Labels R, 1-4 are in `caption`: `textPrimary` for active heads,
        `textTertiary` for inactive ones, `textSecondary` for R.
      - Each active head's time sits above it inside the loop, in `value`
        `textSecondary`.
    - **Feedback path.** A line from under each active head's label to a bus that
      runs back to R, ending in an arrow.
      - Below a loop gain of 0.85 it is the hue at 40-100% with the feedback, 1 px.
      - From 0.85 it is `warning`, 1.5 px.
      - At 1 and above it is `meterHot`.
      - At no feedback it is dashed `textTertiary` at 50%.
    - **Inside the loop, left.** MOTOR in `captionCaps`, then the Sync chip (the
      corner chip: "Sync off" or the note value ▾). Below them is head 1's time as
      the motor runs, in mono `title` `textPrimary`, so it glides after a change.
      When synced, the note value shows with its ms after it in `value`. A clamped
      note reads "max 1000 ms" in `warning`.
    - **Inside the loop, right.** Two 150 px meters, each a `captionCaps` name with a
      `value` reading on one row and a bar of twelve 6 px segments under it
      (`surface2` when dark):
      - **LOOP** is the loop gain over 0-1.2. Segments light in the hue below 0.85,
        `warning` to 1 and `meterHot` past it. A 1 px `textSecondary` tick marks the
        edge at 1. The reading turns `warning`/`meterHot` with it.
      - **SATURATION** is the tape's compression over 0-12 dB, lit in the hue, from
        the readout ballistics. It reads "–" when there is none.
    - The machine is a readout only (the Time knob sets the motor), so it has no
      hover or drag state.
  - **Header status:** "Self-oscillating" in `meterHot` while the loop gain is at
    1 or more. The Feedback knob's arc takes the same hue / `warning` / `meterHot`
    colour.
  - **Controls.** One row in five groups split by `lineSubtle` rules:
    - Time and Feedback are 46 px knobs with 0-10 scale dots. Synced, Time dims to
      50% and shows the note value.
    - Between them, Heads is a rotary selector with eight positions (1, 2, 3, 4,
      13, 24, 134, 1234), 38.6° apart over 270°. Its cell is 11 px taller than the
      selector's preferred height, for the lowest labels.
    - Bass and Treble.
    - Drive, Wow, Flutter and Age.
    - Stereo as a three-position rotary selector (Pan, Ping, L/R; tooltip "Heads
      panned / Ping-pong / Left and right"), then Spread and Spring.
    - Mix, Output, and a 48 × 22 px "Kill" pill where a knob's ring would be, named
      "Dry" on the baseline.
    - Every label sits on one baseline along the bottom of the row.
- **Stereo Widener** (width per band, a mono-safe spread, and the image on screen):
  the side (L-R) is split into Low, Mid and High by Linkwitz-Riley crossovers (Low
  split 40 Hz-1 kHz, default 250 Hz; High split 1-16 kHz, default 4 kHz) and each band
  scaled from 0 (mono) through 100% (as it came) to 200%; the mid goes through the
  matching allpasses, so at 100% the sound is unchanged and at any width the mono
  sum is the mid. Mono below (Off at 20 Hz, up to 500 Hz) takes the side out of the
  bass with an LR4 high-pass. Spread (0-100%) widens a mono source by adding a copy
  of the mid to the side only, so it cancels in mono: Diffuse through an allpass
  network (each frequency leans a different way: no echo, no regular comb), or Haas
  through a 1-30 ms delay (Time); both high-passed at 120 Hz. Rotation (±45°) turns
  the image keeping its width; Balance (±100%) turns one side down. The Guard narrows
  the image (quick in, slow out, to no less than 25% of its side) while its
  correlation would fall below Guard at (default 0). The level is compensated for the
  width (the power in over the power out, 300 ms, -6 to +3 dB). Output -24..+12 dB.
  No latency. Its editor:
  - **Vectorscope** (left, square, the display's height less the meter's): the
    output as a goniometer, mid up and side across; L and R on the diagonals in
    `gridBar` with a circle at full scale, M and S in `gridBeat`; L, M, R in
    `captionCaps` `textTertiary`. The newest 2048 samples as 1.6 px dots in the hue in
    four ages, 18% to 84% alpha, newest brightest; the scale follows the level (shrinks
    at once, grows over 600 ms) so a quiet source still fills it. A rotation other than
    0 draws a dashed `textSecondary` line from the centre to where a centred sound now
    sits. Silent for a quarter second: a 3 px dot at the centre and "No signal" in
    `caption` `textTertiary`.
  - **Correlation meter** (under the scope, 38 px, its own well): -1 to +1 on an 8 px
    track tinted by zone at 20%: below 0 `danger`, 0 to 0.3 `warning`, 0.3 up `success`;
    a `lineStrong` tick at 0; -1, 0, +1 under it in mono `caption` - 1. The reading
    (300 ms) is a bar from 0 in its zone's colour at 75% with a 2 px `textPrimary` end;
    the lowest of the last `motion.peakHoldMs` holds as a 1.5 px tick in its zone's
    colour; the input's correlation is a small `textSecondary` triangle under the
    track. The guard's threshold is a line with a notch above the track: solid in the
    hue while the guard is on, dashed `textTertiary` while off; drag it (steps of
    0.05, double-click resets; hover brightens it and shows a left-right cursor). The
    reading in `value` at the right in its zone's colour, "–" in silence.
  - **Width bands** (the rest of the display): 20 Hz-20 kHz across (grid at 50, 100,
    200, 500, 1k, 2k, 5k, 10k; decades `gridBar`, the rest `gridBeat`; labels in mono
    `caption` - 1 under the plot), width 0-200% up (100% `lineStrong`, 50 and 150%
    `gridBeat`; 0%, 100%, 200% labelled at the left). A 20 px row above the plot
    holds each band's name in `captionCaps` `textTertiary` and its width in `value`
    `textSecondary` ("Mono" at 0; only the value when the band is narrow, nothing when
    it is narrower still). Each band is a block in the hue at 14% up to its width
    with a 2 px line in the hue and a handle (the built-ins' 9 px disc) at its middle;
    hover or drag raises the fill to 22% and the value to `textPrimary`. Drag anywhere
    in a band for its width (it settles on 100% and on mono within 3%); double-click
    resets. The crossovers are 1 px `lineStrong` lines from the top of the well (they
    divide the caption row too), each with a grip at its foot: a 7 x 16 px upright
    pill in `textSecondary` with two `surface0` notches, so it reads as "drag sideways"
    beside the round "drag up and down" handles; hover or drag: line 1.5 px and grip
    `textPrimary` with a 18% halo, and the frequency in a `surface3` chip near the top.
    Mono below is a dashed line with its grip at the top; the region under it is
    `canvas` at 80% (no side there) with MONO in `captionCaps`; Off, the grip sits dim
    (`textTertiary`) at the left edge. Over everything, the width the output measures
    at each frequency (side over mid, from a 4096-point FFT of the two taps, 56 log
    bands each averaged with its neighbours, about 150 ms of averaging) as a 1.25 px
    `textPrimary` line at 60%, broken where a band is below -80 dBFS or 60 dB under the
    loudest; it shows what the settings actually did to the sound. While the guard
    narrows, the width it leaves is a dashed `warning` line in each band.
  - Controls, in four groups split by `lineSubtle` rules, every label on one baseline:
    Low, Mid, High ("Mono" at 0) and Mono below ("Off") as knobs; Mode as a segmented
    control (Diffuse | Haas, centred on the knobs' rings, captioned Mode), Spread and
    Time (Haas time: disabled, ring and label `textDisabled`, in Diffuse); Rotation
    ("+12°") and Balance ("C", "L 40", "R 40"); the Guard pill (in the hue when on,
    captioned "Mono safe") and Output.
  - Header status while the guard narrows: "Guard −35%" in `warning`.

Draggable points are one handle everywhere: a 9 px `textPrimary` disc with a 1.5 px
ring in the hue. A display's readout is `value` at its top right.

## 7.2b Built-in presets

Every built-in effect has presets: a factory list (8 to 13 for most, six levels for
Gain), the first of them its defaults ("Default", or Patina's "Gentle Age"), and the
user's own. The instruments keep their own preset bars in their faces (7.4b-7.4e).

- **Where.** A preset bar (design-system.md "Preset bar") in the effect card's
  header in the Editor panel, right of the name and before the menu, and in the
  effect's window, right of the title and before Bypass. It is 208 px wide, or what
  the header can spare while leaving the name 72 px; below 112 px it hides (the
  menu's items stay). A face may also have its own preset control (Patina's amber
  display); it uses the same list, so the two always show the same preset.
- **The name** is the preset whose values the effect has right now, or "Custom".
  Previous and next step through the whole list (factory, then the user's),
  wrapping; from Custom, next goes to the first and previous to the last.
- **The menu** (a click on the name): the factory presets, then the user's under
  a "Yours" section header in name order, the current one ticked; a separator;
  "Save preset...", "Delete "<name>"" (for a preset of the user's, dimmed
  otherwise) and "Show presets folder".
- **Save preset...** opens a sheet asking for a name (the current preset's, or "My
  preset"); a preset of the user's with the same name is replaced. Every bar for
  that effect updates at once.
- **Files.** One JSON file per preset in Documents/Northlight/Effect Presets/<effect
  name>: the name, the type and each parameter's value by its name. A file from an
  older version loads with the defaults for parameters it lacks; one for another
  effect is ignored. Delete moves the file to the recycle bin.
- **Undo.** Applying a preset sets every parameter it changes in one undo step
  ("Preset Vocal Plate").

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
|   Northlight EQ            Built-in                    |
|   DC1A3                    Klanghelm        VST3       |
| Instruments                                            |
|   …                                                    |
| Blocked (1)                                            |
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

## 7.4g Files (the browser's second tab)

```
| [ Plugins | Files ]                                    |
| 🔍 Search files                                        |
| ( Music › Drums                         ▾ )  [↑] [+]   |
|   📁 Loops                                             |
|   ▶ Kick 01.wav                       0.60 s   48 kHz  |
|   ♪ Hat Closed.wav                    0.25 s   48 kHz  |
| ( ■  ━━━━━━━━━━━━●━━━━━━━━━━              -6.2 dB )    |
```

- A segmented control at the top of the Browser panel switches Plugins and
  Files (remembered). Leaving Files stops a preview.
- Places: the saved project's folder, Music, and folders the user adds (the
  "+" button or the place menu's "Add a folder…"; "Remove this folder from the
  list" and "Show in Explorer" there too). The place button (ghost, chevron)
  shows the place and the path below it; ↑ (and Backspace) goes up, never above
  the place. Place, ↑ and + sit in one tray.
- Rows 28 px: folders first (folder icon, `textSecondary`), then audio files
  (wav, aif, aiff, flac, mp3, ogg) with the length in `value` `textSecondary` and
  the rate in `textTertiary` at the right. A click on a folder opens it.
- Search looks under the whole place for names holding every word typed; a
  result shows its folder in `small` `textTertiary` after the name. Escape in
  the field stops the preview and clears it.
- Click a file to audition it: its icon becomes a play mark in `accent`, the
  name turns `accent` and `accentSubtle` fills the row as it plays. Another
  click, another file, Escape or Stop ends it with a 5 ms fade; the preview
  plays over the mix at the footer's volume, outside the faders and meters.
- Double-click or Enter puts the file on a new track at the playhead; drag a row
  onto the arrange view for a new track where it is dropped (the drop preview a
  file from Explorer shows), or onto a Sampler's waveform to load it.
- The footer tray: Stop and the preview volume (a slider with its dB readout).
- Empty folder: "No audio files here"; no match: "No files match"; reading:
  "Reading…" in `small` `textTertiary`.

## 7.4a Instruments: channels, MIDI tracks and the rack

An instrument is not an effect, and its sound is not its MIDI's. An instrument
lives on an **instrument channel**: an audio channel whose **instrument slot**,
ahead of its inserts, takes only instruments (built-in, VST3 or CLAP), and whose
inserts, channel strip, sends, fader and automation process the instrument's main
output. **MIDI tracks** hold the notes (clips, an input, arm and monitoring, MIDI
out) and make no audio: each aims at an instrument channel on a MIDI channel
(1-16). Several MIDI tracks may play one instrument (a multitimbral plugin tells
them apart by channel); a MIDI track's volume scales its notes' velocities, and
its mute stops new notes. Each further stereo output of an instrument is an **output
track** with its own processing.

The "+" menu's **MIDI track** submenu offers: **With a new instrument** (any
built-in or scanned instrument), which makes both in one step, the MIDI track and,
just under it, its instrument channel (named for the instrument); **Playing** any
instrument channel already in the project (the MIDI track goes just above it); or
**Playing nothing yet**. Ctrl+Shift+T and the empty project's "Add MIDI track" make
a MIDI track with a new Aurora channel. The insert menus list effects only; an
instrument chosen from the browser goes to the selected
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

The synth bundled with Northlight reads as its own product, so its editor is bolder
than the built-in effects' while keeping to the theme's palette (Sea grass, Lichen,
Overcast, Heather, Thrift, Gorse) and type:

- A night sky (the canvas darkening to the top) with twinkling stars and three
  aurora ribbons in Sea grass, Lichen and Heather drifting across the top; they move and
  brighten while Aurora sounds and hold still when it is silent or with reduce
  motion on.
- The wordmark AURORA, tracked wide with a Sea grass-to-Heather gradient, "POLYSYNTH ·
  NORTHLIGHT" beside it; the preset bar (previous, the name as a menu, next; "Custom"
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

The drum synth bundled with Northlight, Aurora's sibling for percussion: eight
synthesised pads on the General MIDI drum notes (kick C1, rim C#1, snare D1,
clap D#1, closed hat F#1, low tom A1, open hat A#1, high tom C2) and on C3 to G3
in that order, played out whatever the note's length; the closed hat chokes the
open one. Each pad: Tune, Decay, two shaping controls named for the sound (kick
Click and Punch, rim Ring and Click, snare Snappy and Crack, clap Bright and
Spread, hats Bright and Noise, toms Noise and Punch), Level, Pan and Output: the
main pair or an extra one (3/4 to 15/16). Pulsar has as many extra outputs as its
pads use, each named for the pads on it ("Kick", "Closed hat + Open hat"), and
each becomes a track of its own from the rack's chip or the mixer's Outputs menu,
named that way. The master: Drive, Velocity, Level (applied to every output).
Kits: Pulsar, Boom Room, Tight Electro, Dusty, Minimal Clicks, Industrial.

The voices follow the TR-808's circuits. The kick and toms are a sine that drops
gently onto its note (further and faster with Punch), soft-clipped, the kick with
its trigger's click through a low-pass as bright as Click. The snare is two shell
modes under high-passed noise (the wires, as loud as Snappy) with a band of noise
for Crack. The clap is band-passed noise retriggered: three short bursts and a
longer fourth, then a tail as long as the decay. The hats are six square
oscillators at the 808's ratios, mixed with noise, through a band-pass and a
high-pass that open with Bright. The rim is two short modes (about 1.7 kHz and
460 Hz) clipped together. Velocity drives the kick, toms and snare harder, as the
808's accent does. Filters are set in hertz and noise is scaled to the sample
rate, so a pad sounds the same at 44.1 kHz and at 192 kHz.

- Deep space (the canvas darkening around the star) with stars, and the pulsar in
  the top-left corner: a white core, an `Overcast` halo and two beams that turn slowly
  while it plays and flare on every hit, which also sends a ring in the pad's hue
  out across the sky behind the glass. Still when silent or with reduce motion on.
- The wordmark PULSAR tracked wide in an Overcast-to-Rowan gradient, "DRUM SYNTH ·
  NORTHLIGHT", and the kit bar (previous, the name as a menu, next; "Custom" off-kit).
- Eight pad tiles (4×2) of glass, each in its hue (Rowan, Gorse, Kelp, Machair,
  Sea grass, Lichen, Overcast, Loch): its short name, its note, and its sound, rendered and
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

The pad synth bundled with Northlight, for slow, airy, ethereal sounds. Each note is a
cloud of partials (4 to 24), each a pair of sines detuned by the spread and
wandering on its own with the drift, panned alternately left and right (wider the
higher), shaped by Brightness (the spectrum's tilt), Odd/even (hollow to full),
Stretch (towards a bell) and a vowel Formant; a Halo of partials an octave up, a
Sub and Breath (noise around the note); a slow envelope; Motion, an LFO sweeping
the brightness, the formant and the partials' pans; a gentle Tone low-pass; and
Space, a large eight-line reverb. Eight voices. Presets: Nebula, Choir of Dust,
Glass Halo, Hollow Moon, Solar Breath, Deep Field, Ice Bells, Drifting Veil.

- Gas clouds in Heather, Thrift, Lichen and Loch drifting behind the panels and
  breathing with the sound, dust rising through them; still when silent or with
  reduce motion on. The wordmark NEBULA, light and tracked wide in a Thrift-to-Lichen
  gradient on a soft glow; "PAD SYNTH · NORTHLIGHT"; the preset bar; eight voice lights.
- SPECTRUM: the partials as columns of light on a log axis (55 Hz to 12 kHz, for
  A3 and the octave), the halo's in Sea grass, under the formant's cloud, swaying with
  the motion; drag across for the vowel, up and down for the brightness; the octave
  in a corner chip. Partials, Bright, Odd/even, Stretch, Vowel, Formant.
- CLOUD: where the partials sit in the stereo field (left to right, low to high),
  each a pair of dots as far apart as the spread, wandering with the drift. Drift,
  Spread, Width, Halo, Sub, Breath.
- ENVELOPE (draggable), MOTION (the wave with the live value), SPACE (the tail as
  long as the size and as bright as the mix; drag for both) with their knobs.

## 7.4e Starbit

The chiptune synth bundled with Northlight, after 8-bit consoles' sound chips. A
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
  with a drop shadow, "CHIP SYNTH · NORTHLIGHT" in mono, square stars that blink in
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

## 7.4f Sampler

One sample across the keyboard: root note and key range, Tune and Fine, Glide;
Loop (forward, with a crossfade into the loop's end) or One-shot (to the end
point, note-offs ignored); Voices (1-32; the oldest fades out over the limit),
Velocity, Level; an ADSR on the amplitude; a resonant low-pass whose cutoff the
envelope moves by Env octaves. Hues: the sample `Lichen`, loop `Gorse`, keys
`Sea grass`, envelope `Overcast`, filter `Loch`.

- Header: "Sampler" in the hue over a small decaying wave; the file's name
  (`label`; `warning` when it did not load) with its rate, channels and length
  in `caption` `textTertiary`; the voices sounding with a light; a ghost
  **Load…** button (a file chooser).
- SAMPLE tray, full width: the waveform in a well, bright from the start to the
  end point and at the muted waveform alpha outside; the loop shaded in its hue
  at 12% with the crossfade as a gradient before its end; a 1 px line per voice
  sounding (`textPrimary`, fainter while it releases); the length that plays at
  the top in `value`. Start and end (S, E) are handles on the top edge, the
  loop's (L) on the bottom edge (shown while Loop is on); drag them, each stays
  on its side of its partner. Empty, the well reads "Drop an audio file here, or
  Load one"; a missing file reads its name and the reason in `warning`. Under it
  a keyboard of all 128 notes in a well: the range lit in the keys' hue, the
  root solid, C labels; click a key for the root, drag an end of the range.
- Trays below: PITCH (Root, Low, High as note names; Tune, Fine, Glide), PLAY
  (Loop fade, Voices, Velocity; Level and the Loop and One-shot pills), ENVELOPE
  (the curve in a well, times on a gentle log scale; Attack, Decay, Sustain,
  Release), FILTER (the response in a well, a fainter second curve where the
  envelope takes the cutoff; Cutoff, Reso, Env).
- Dropping an audio file on the waveform (from Explorer or the browser's Files
  tab) loads it as one undo step and resets the start, end and loop points to
  the whole file; the well outlines in `dropTarget` while a file is over it.
- From the arrange view: right-click an audio clip, **Open in Sampler**, for a
  new instrument channel and MIDI track playing that file, its start and end at
  the part the clip uses.

## 7.5 States and errors

- Plugin missing on this machine: the chain card shows the name in `textTertiary`
  with a `warning` icon and "Not installed"; state is preserved; the card can be
  removed or replaced.
- Plugin failed to load (layout refused, crash on instantiate): a toast with the
  reason and a "Blocklist" action; the card shows "Failed" in `danger`.
- Latency change: no UI beyond the card's latency label updating.

## Acceptance

- A chain of three cards matches sizes and states, including a bypassed card and
  the insertion line mid-drag.
- At 100% and 150%, a plugin window shows the 32 px bar and the plugin at its
  native size.
- The browser shows favourites, both formats and a blocked row.
- Scan progress and Cancel work when a plugin hangs during the scan.
